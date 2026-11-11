// http_server.cpp - REST + SSE server (cpp-httplib), thread-safe, CORS on.
//
// Routes:
//   GET  /            browser chat UI (from web/ folder, embedded fallback)
//   GET  /health      service status (ollama, models, chunk count, ...)
//   GET  /documents   indexed PDF list with per-file stats
//   POST /chat        {"session_id","message"} -> {answer, sources, ...}
//   POST /chat/stream same via Server-Sent Events
//   POST /ingest      rescan the PDF folder (incremental)
//   POST /upload      multipart PDF upload into the pdfs folder
#include "dumy/http_server.h"

#include <httplib.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <process.h>
#define DUMY_GETPID _getpid
#else
#include <unistd.h>
#define DUMY_GETPID getpid
#endif

#include "dumy/common.h"
#include "dumy/prompts.h"

namespace dumy {
namespace {

// Set once in HttpServer::start; used by the CORS helpers below.
const Config* g_cfg = nullptr;

// Reflecting only an allow-listed Origin (and never "*") keeps a website that
// embeds DUMY from being callable by every other page in the browser.
std::string cors_origin(const httplib::Request& req) {
    if (!g_cfg) return "";
    const std::string origin = req.get_header_value("Origin");
    if (origin.empty()) return "";
    for (const auto& allowed : g_cfg->allowed_origins) {
        if (allowed == "*" || allowed == origin) return origin;
    }
    return "";
}

void apply_cors(const httplib::Request& req, httplib::Response& res) {
    const std::string origin = cors_origin(req);
    if (!origin.empty()) {
        res.set_header("Access-Control-Allow-Origin", origin);
        res.set_header("Vary", "Origin");
        res.set_header("Access-Control-Allow-Methods",
                       "GET, POST, PATCH, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers",
                       "Content-Type, X-API-Key, X-Session-Id");
    }
}

// nlohmann::json::dump() throws on invalid UTF-8. Error bodies must never
// escape as a broken response (the browser reports that as "Failed to fetch"),
// so a serialization failure falls back to a plain Azerbaijani message.
std::string safe_dump(const nlohmann::json& body) {
    try {
        return body.dump();
    } catch (const std::exception& e) {
        log_error(std::string("json dump failed: ") + e.what());
        return std::string(
            "{\"error\":\"cavab JSON kimi göstərilə bilmədi\"}");
    }
}

void reply_json(const httplib::Request& req, httplib::Response& res, int status,
                const nlohmann::json& body) {
    apply_cors(req, res);
    res.status = status;
    res.set_header("Cache-Control", "no-store");
    res.set_content(safe_dump(body), "application/json; charset=utf-8");
}

// Parses the request body as JSON; on failure replies 400 and returns false.
bool parse_body(const httplib::Request& req, httplib::Response& res, nlohmann::json& out) {
    try {
        out = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
        return true;
    } catch (const std::exception& e) {
        // e.what() can quote invalid bytes from the request body; sanitize it
        // so the 400 response itself is always serializable.
        reply_json(req, res, 400,
                   {{"error", sanitize_utf8(std::string("sorğu JSON-u səhvdir: ") +
                                                  e.what())}});
        return false;
    }
}

// Minimal embedded chat page shown only when web/index.html cannot be found.
constexpr const char* FALLBACK_HTML =
    "<!DOCTYPE html><html lang=\"az\"><head><meta charset=\"UTF-8\">"
    "<title>DUMY</title></head><body style=\"font-family:sans-serif;max-width:600px;"
    "margin:40px auto\"><h1>DUMY serveri işləyir</h1>"
    "<p>Chat səhifəsi tapılmadı (web/index.html). API aktivdir:</p>"
    "<ul><li>GET /health</li><li>GET /documents</li><li>POST /chat</li>"
    "<li>POST /chat/stream (SSE)</li><li>POST /ingest</li>"
    "<li>POST /upload (multipart PDF)</li></ul></body></html>";

// Keeps the last uploaded-file sanity info; used by /upload.
bool is_loopback(const std::string& addr) {
    return addr == "127.0.0.1" || addr == "::1" || addr == "localhost" ||
           starts_with(addr, "127.");
}

// --- API key, per-IP rate limit and request limits ------------------------
std::mutex& rate_mutex() {
    static std::mutex m;
    return m;
}
struct RateEntry {
    std::chrono::steady_clock::time_point window{};
    int count = 0;
};
std::map<std::string, RateEntry>& rate_map() {
    static std::map<std::string, RateEntry> m;
    return m;
}

// Fixed one-minute window per IP; it only has to stop accidental floods and
// a single abusive client, not bill anyone, so a simple map is enough.
// per_minute <= 0 disables the limit for that class of client.
bool rate_limited(const std::string& ip, int per_minute) {
    if (per_minute <= 0) return false;
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lk(rate_mutex());
    auto& e = rate_map()[ip];
    if (e.window.time_since_epoch().count() == 0 ||
        now - e.window > std::chrono::seconds(60)) {
        e.window = now;
        e.count = 0;
    }
    return ++e.count > per_minute;
}

size_t utf8_chars(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}

bool check_api_access(const httplib::Request& req, httplib::Response& res,
                      bool apply_rate_limit) {
    if (!g_cfg) return true;
    if (!g_cfg->api_key.empty() &&
        req.get_header_value("X-API-Key") != g_cfg->api_key) {
        reply_json(req, res, 401,
                   {{"error", "API açarı tələb olunur (X-API-Key başlığı)"}});
        return false;
    }
    // Loopback (the built-in UI, tests, a same-machine proxy) has its own limit
    // and is exempt by default: 14 quick manual questions used to hit 429.
    int limit = g_cfg->rate_limit_per_minute;
    if (g_cfg->rate_limit_exempt_loopback && is_loopback(req.remote_addr))
        limit = g_cfg->rate_limit_localhost_per_minute;
    if (apply_rate_limit && rate_limited(req.remote_addr, limit)) {
        res.set_header("Retry-After", "60");
        reply_json(req, res, 429,
                   {{"error", "çox sorğu - dəqiqədə " +
                                  std::to_string(g_cfg->rate_limit_per_minute) +
                                  " sorğu limiti"}});
        return false;
    }
    return true;
}

// Turns an arbitrary uploaded filename into something safe inside pdfs/:
// strips directories, keeps letters/digits/dot/dash/underscore, forces .pdf.
std::string sanitize_filename(std::string name) {
    size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    std::string out;
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u) || c == '.' || c == '-' || c == '_' || u >= 0x80)
            out.push_back(c);
        else
            out.push_back('_');
    }
    // no leading dots (hidden / traversal-ish names like "..x")
    while (!out.empty() && (out[0] == '.')) out.erase(0, 1);
    if (out.empty()) out = "upload.pdf";
    if (!ends_with(to_lower_utf8(out), ".pdf")) out += ".pdf";
    if (out.size() > 180) {
        out = out.substr(0, 170) + ".pdf";
    }
    return out;
}

// "file.pdf səh. 12" labels stored with each assistant turn.
std::vector<std::string> source_labels(const AnswerResult& r) {
    std::vector<std::string> out;
    for (const auto& s : r.sources_json()) {
        if (!s.is_object()) continue;
        const std::string type = s.value("type", std::string());
        const std::string src = s.value("source", std::string());
        if (type == "pdf" && !src.empty())
            out.push_back(src + " səh. " + std::to_string(s.value("page", 0)));
        else if (type == "web" && !src.empty())
            out.push_back(src);
    }
    return out;
}

// After a server restart the in-memory session is empty; seed it from the
// stored conversation so the model still sees the last few turns.
void seed_session_history(SessionManager& sessions, ConversationStore& store,
                          const std::string& client, const std::string& conv,
                          const std::string& session_id, size_t keep) {
    if (!sessions.history(session_id).empty()) return;
    const auto stored = store.recent(client, conv, keep);
    for (size_t i = 0; i + 1 < stored.size(); ++i) {
        if (stored[i].role == "user" && stored[i + 1].role == "assistant")
            sessions.append(session_id, {"user", stored[i].text},
                            {"assistant", stored[i + 1].text});
    }
}

// Machine-readable flags for the UI (no prose in the answer text).
nlohmann::json notes_json(const AnswerResult& r) {
    int pdfs = 0;
    for (const auto& s : r.sources_json())
        if (s.is_object() && s.value("type", std::string()) == "pdf") ++pdfs;
    return {{"own_knowledge", r.from_own_knowledge},
            {"memory_used", r.used_memory},
            {"safety_warning", r.safety_warning},
            {"no_pdf_context", pdfs == 0},
            {"refused", r.refused},
            {"web_used", r.used_web}};
}

}  // namespace

struct HttpServer::Impl {
    httplib::Server svr;
    std::thread thread;
    std::atomic<bool> bound{false};
    std::atomic<bool> ingest_running{false};   // POST /ingest -> 409 when busy
    std::mutex health_mutex;                   // serializes the live Ollama probe
    bool ollama_alive = false;
    std::chrono::steady_clock::time_point ollama_checked_at{};
    // Identity of THIS process, so a test harness can prove /health belongs to
    // the server it launched (and detect a crash/restart mid-suite).
    std::string build_id = std::string(__DATE__) + " " + __TIME__;
    long long started_at = 0;
    long long pid = 0;
};

HttpServer::~HttpServer() = default;

HttpServer::HttpServer(const Config& cfg, RagPipeline& pipeline, Indexer& indexer,
                       SessionManager& sessions, ConversationStore& conversations)
    : cfg_(cfg), pipeline_(pipeline), indexer_(indexer), sessions_(sessions),
      store_(conversations), impl_(std::make_unique<Impl>()) {}

bool HttpServer::start(std::string* err) {
    auto& svr = impl_->svr;
    g_cfg = &cfg_;
    impl_->started_at = static_cast<long long>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    impl_->pid = static_cast<long long>(DUMY_GETPID());
    // Uploads can be large PDFs; chat/JSON bodies stay small.
    svr.set_payload_max_length(static_cast<size_t>(cfg_.upload_max_mb) * 1024 * 1024 +
                               1024 * 1024);
    svr.new_task_queue = [] { return new httplib::ThreadPool(16); };

    // Where the UI lives on disk (resolved against the config folder).
    const fs::path web_dir = cfg_.resolve(cfg_.web_folder);
    const fs::path index_html = web_dir / "index.html";

    // --- CORS preflight ----------------------------------------------------
    for (const char* path : {"/", "/chat", "/chat/stream", "/ingest", "/health",
                             "/documents", "/upload", R"(/conversations.*)",
                             R"(/memory.*)"}) {
        svr.Options(path, [](const httplib::Request& req, httplib::Response& res) {
            apply_cors(req, res);
            res.status = 204;
        });
    }

    // --- GET / : browser chat UI ------------------------------------------
    svr.Get("/", [index_html](const httplib::Request& req, httplib::Response& res) {
        apply_cors(req, res);
        std::string html;
        if (read_text_file(index_html, html) && !html.empty()) {
            res.set_content(html, "text/html; charset=utf-8");
        } else {
            res.set_content(FALLBACK_HTML, "text/html; charset=utf-8");
        }
    });

    // --- GET /health -------------------------------------------------------
    svr.Get("/health", [this](const httplib::Request& req, httplib::Response& res) {
        // Live Ollama probe with a hard 2 s cap (cached 5 s) so the status
        // badge can never hang the page when Ollama is down or busy.
        bool alive;
        {
            std::lock_guard<std::mutex> lk(impl_->health_mutex);
            const auto now = std::chrono::steady_clock::now();
            if (now - impl_->ollama_checked_at < std::chrono::seconds(5)) {
                alive = impl_->ollama_alive;
            } else {
                alive = pipeline_.check_ollama(2);
                impl_->ollama_alive = alive;
                impl_->ollama_checked_at = now;
            }
        }
        // Step 1: models Ollama currently has loaded (GET /api/ps) and the
        // startup warm-up result, so an evicted model is visible here.
        nlohmann::json loaded = nlohmann::json::array();
        if (alive) {
            std::string ps_err;
            for (const auto& m : pipeline_.running_models(&ps_err)) {
                loaded.push_back({{"name", m.name},
                                  {"size", m.size},
                                  {"size_vram", m.size_vram},
                                  {"cpu_only", m.size_vram == 0}});
            }
        }
        const WarmupInfo& w = pipeline_.warmup_info();
        const long long now_s = static_cast<long long>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());
        nlohmann::json body = {
            {"status", "ok"},
            {"build_id", impl_->build_id},
            {"started_at", impl_->started_at},
            {"pid", impl_->pid},
            {"uptime_sec", now_s - impl_->started_at},
            {"ollama", alive},
            {"chat_model", pipeline_.chat_model()},
            {"helper_model", pipeline_.helper_model()},
            {"embedding_model", pipeline_.embedding_model()},
            {"pdf_support", true},
            {"web_search_enabled", cfg_.web_search_enabled},
            {"sessions", sessions_.session_count()},
            {"chunks", indexer_.chunk_count()},
            {"loaded_models", loaded},
            {"warmup", {{"attempted", w.attempted},
                        {"main_ready", w.main_ready},
                        {"main_resident", w.main_resident},
                        {"embed_ready", w.embed_ready},
                        {"embed_on_cpu", w.embed_on_cpu},
                        {"main_load_ms", w.main_load_ms},
                        {"embed_load_ms", w.embed_load_ms},
                        {"note", w.note}}},
        };
        reply_json(req, res, 200, body);
    });

    // --- GET /documents ----------------------------------------------------
    svr.Get("/documents", [this](const httplib::Request& req, httplib::Response& res) {
        reply_json(req, res, 200, {{"documents", indexer_.documents_json()}});
    });

    // --- POST /chat --------------------------------------------------------
    svr.Post("/chat", [this](const httplib::Request& req, httplib::Response& res) {
        if (!check_api_access(req, res, true)) return;
        if (req.body.size() >
            static_cast<size_t>(std::max(8, cfg_.max_body_kb)) * 1024) {
            reply_json(req, res, 413, {{"error", "sorğu gövdəsi çox böyükdür"}});
            return;
        }
        nlohmann::json body;
        if (!parse_body(req, res, body)) return;
        const std::string message = body.value("message", std::string());
        if (trim(message).empty()) {
            reply_json(req, res, 400, {{"error", "'message' is required"}});
            return;
        }
        if (utf8_chars(message) >
            static_cast<size_t>(std::max(50, cfg_.max_question_chars))) {
            reply_json(req, res, 413,
                       {{"error", "sual çox uzundur (max " +
                                      std::to_string(cfg_.max_question_chars) +
                                      " simvol)"}});
            return;
        }
        const std::string client =
            ConversationStore::sanitize_client(req.get_header_value("X-Session-Id"));
        std::string session_id = body.value("session_id", std::string());
        if (session_id.empty()) session_id = SessionManager::make_session_id();

        // Optional conversation: created when missing, always returned.
        std::string conversation_id = body.value("conversation_id", std::string());
        if (!conversation_id.empty() && !ConversationStore::valid_id(conversation_id)) {
            reply_json(req, res, 400,
                       {{"error", "conversation_id yalnız hərf, rəqəm, '-' və '_' "
                                  "ola bilər"}});
            return;
        }
        if (conversation_id.empty() || !store_.exists(client, conversation_id))
            conversation_id = store_.create(client, message).id;
        seed_session_history(sessions_, store_, client, conversation_id, session_id,
                             static_cast<size_t>(std::max(2, cfg_.max_history_messages)));

        const auto t_start = std::chrono::steady_clock::now();
        try {
            AnswerResult r = pipeline_.answer(
                message, session_id, MemoryRef{client, conversation_id}, nullptr);
            const long long latency_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - t_start)
                    .count();
            if (cfg_.memory_enabled)
                store_.append_turn(client, conversation_id, message, r.answer,
                                   r.qtype, source_labels(r), latency_ms);
            reply_json(req, res, 200,
                       {{"session_id", session_id},
                        {"conversation_id", conversation_id},
                        {"memory_used", r.used_memory},
                        {"latency_ms", latency_ms},
                        {"route", r.qtype},
                        {"notes_ui", notes_json(r)},
                        // sanitize: an invalid UTF-8 byte would make dump() throw
                        {"answer", sanitize_utf8(r.answer)},
                        {"sources", r.sources_json()},
                        {"qtype", r.qtype},
                        {"refused", r.refused},
                        {"web_used", r.used_web},
                        {"map_reduce", r.used_map_reduce},
                        {"own_knowledge", r.from_own_knowledge},
                        {"self_check_failed", r.self_check_failed},
                        {"self_check_retries", r.self_check_retries},
                        {"warning", r.warning}});
        } catch (const std::exception& e) {
            reply_json(req, res, 500, {{"error", std::string("pipeline error: ") + e.what()}});
        }
    });

    // --- POST /chat/stream (SSE) ------------------------------------------
    svr.Post("/chat/stream", [this](const httplib::Request& req, httplib::Response& res) {
        if (!check_api_access(req, res, true)) return;
        if (req.body.size() >
            static_cast<size_t>(std::max(8, cfg_.max_body_kb)) * 1024) {
            reply_json(req, res, 413, {{"error", "sorğu gövdəsi çox böyükdür"}});
            return;
        }
        nlohmann::json body;
        if (!parse_body(req, res, body)) return;
        const std::string message = body.value("message", std::string());
        if (trim(message).empty()) {
            reply_json(req, res, 400, {{"error", "'message' is required"}});
            return;
        }
        if (utf8_chars(message) >
            static_cast<size_t>(std::max(50, cfg_.max_question_chars))) {
            reply_json(req, res, 413,
                       {{"error", "sual çox uzundur (max " +
                                      std::to_string(cfg_.max_question_chars) +
                                      " simvol)"}});
            return;
        }
        const std::string client =
            ConversationStore::sanitize_client(req.get_header_value("X-Session-Id"));
        std::string session_id = body.value("session_id", std::string());
        if (session_id.empty()) session_id = SessionManager::make_session_id();
        std::string conversation_id = body.value("conversation_id", std::string());
        if (!conversation_id.empty() && !ConversationStore::valid_id(conversation_id)) {
            reply_json(req, res, 400,
                       {{"error", "conversation_id yalnız hərf, rəqəm, '-' və '_' "
                                  "ola bilər"}});
            return;
        }
        if (conversation_id.empty() || !store_.exists(client, conversation_id))
            conversation_id = store_.create(client, message).id;
        seed_session_history(sessions_, store_, client, conversation_id, session_id,
                             static_cast<size_t>(std::max(2, cfg_.max_history_messages)));

        apply_cors(req, res);
        res.set_header("Cache-Control", "no-cache");
        res.set_header("Connection", "keep-alive");
        res.status = 200;

        // The provider body runs on this connection's thread; deltas are
        // forwarded to the client as SSE events while the model generates.
        // httplib 0.59: ContentProviderWithoutLength returns bool; the write
        // loop keeps calling until sink.done() is invoked.
        auto session_copy = session_id;  // captured by value (req may die)
        res.set_chunked_content_provider(
            "text/event-stream",
            [this, message, session_copy, client, conversation_id](
                size_t offset, httplib::DataSink& sink) -> bool {
                const auto t_start = std::chrono::steady_clock::now();
                if (offset > 0) {  // everything was produced in the first call
                    sink.done();
                    return true;
                }

                auto send = [&sink](const nlohmann::json& ev) {
                    std::string line;
                    try {
                        line = "data: " + ev.dump() + "\n\n";
                    } catch (const std::exception& e) {
                        // Never let an unserializable event abort the stream.
                        log_warn(std::string("stream: event dump failed: ") + e.what());
                        return true;
                    }
                    return sink.write(line.data(), line.size());
                };

                AnswerResult r;
                try {
                    r = pipeline_.answer(
                        message, session_copy, MemoryRef{client, conversation_id},
                        [&](const std::string& delta) {
                            if (!delta.empty())
                                send({{"type", "delta"}, {"text", delta}});
                        },
                        [&](const std::string& phase) {
                            // "selfcheck" -> UI shows "Yoxlanılır..."
                            send({{"type", "status"}, {"phase", phase}});
                        });
                } catch (const std::exception& e) {
                    send({{"type", "error"},
                          {"message", std::string("pipeline error: ") + e.what()}});
                    sink.done();
                    return true;
                }

                const long long latency_ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t_start)
                        .count();
                if (cfg_.memory_enabled)
                    store_.append_turn(client, conversation_id, message, r.answer,
                                       r.qtype, source_labels(r), latency_ms);
                log_info("stream: answer_chars=" + std::to_string(r.answer.size()) +
                         " qtype=" + r.qtype + " warning=" + r.warning);
                // The done event is built outside the try/catch above, so its
                // own try/catch guarantees a stream always ends cleanly.
                try {
                    const nlohmann::json done = {
                        {"type", "done"},
                        {"session_id", session_copy},
                        {"conversation_id", conversation_id},
                        {"memory_used", r.used_memory},
                        {"latency_ms", latency_ms},
                        {"route", r.qtype},
                        {"notes_ui", notes_json(r)},
                        {"answer", sanitize_utf8(r.answer)},
                        {"sources", r.sources_json()},
                        {"qtype", r.qtype},
                        {"refused", r.refused},
                        {"web_used", r.used_web},
                        {"map_reduce", r.used_map_reduce},
                        {"own_knowledge", r.from_own_knowledge},
                        {"self_check_failed", r.self_check_failed},
                        {"self_check_retries", r.self_check_retries},
                        {"warning", sanitize_utf8(r.warning)}};
                    if (!send(done))
                        log_warn("stream: done event write failed (client disconnected)");
                } catch (const std::exception& e) {
                    log_error(std::string("stream: done event failed: ") + e.what());
                    try {
                        send({{"type", "error"},
                              {"message", "cavab göndərilə bilmədi"}});
                    } catch (...) {
                    }
                }
                sink.done();
                return true;
            });
    });

    // --- POST /ingest ------------------------------------------------------
    svr.Post("/ingest", [this](const httplib::Request& req, httplib::Response& res) {
        // Reindexing is an administrative action: localhost only, even when an
        // API key is configured.
        if (!is_loopback(req.remote_addr)) {
            reply_json(req, res, 403, {{"error", "ingest yalnız localhost-dandır"}});
            return;
        }
        if (!check_api_access(req, res, false)) return;
        nlohmann::json body;
        if (!parse_body(req, res, body)) return;
        const bool force = body.value("force", false);
        // Only one ingest at a time; a second call gets 409 (the UI shows it).
        bool expected = false;
        if (!impl_->ingest_running.compare_exchange_strong(expected, true)) {
            reply_json(req, res, 409,
                       {{"error", "ingest artıq işləyir - bitməsini gözləyin"}});
            return;
        }
        struct FlagGuard {
            std::atomic<bool>& f;
            ~FlagGuard() { f = false; }
        } guard{impl_->ingest_running};
        try {
            Indexer::Report rep = indexer_.ingest(force);
            reply_json(req, res, rep.ok ? 200 : 500,
                       {{"status", rep.ok ? "ok" : "partial_error"},
                        {"files_seen", rep.files_seen},
                        {"files_indexed", rep.files_indexed},
                        {"files_skipped", rep.files_skipped},
                        {"files_failed", rep.files_failed},
                        {"files_duplicate", rep.files_duplicate},
                        {"chunks_added", rep.chunks_added},
                        {"chunks_removed", rep.chunks_removed},
                        {"scanned_pages", rep.scanned_pages},
                        {"message", rep.message}});
        } catch (const std::exception& e) {
            reply_json(req, res, 500, {{"error", std::string("ingest error: ") + e.what()}});
        }
    });

    // --- POST /upload (multipart PDF) --------------------------------------
    svr.Post("/upload", [this](const httplib::Request& req, httplib::Response& res) {
        if (!is_loopback(req.remote_addr)) {
            reply_json(req, res, 403,
                       {{"error", "upload yalnız localhost-dan icazəlidir "
                                  "(config.json: allow_upload_from_remote)"}});
            return;
        }
        // cpp-httplib 0.59: multipart files live in req.form.
        if (!req.form.has_file("file")) {
            reply_json(req, res, 400, {{"error", "multipart field 'file' is required"}});
            return;
        }
        const httplib::FormData file = req.form.get_file("file");
        if (file.content.empty()) {
            reply_json(req, res, 400, {{"error", "empty file"}});
            return;
        }
        if (file.content.size() >
            static_cast<size_t>(cfg_.upload_max_mb) * 1024 * 1024) {
            reply_json(req, res, 413,
                       {{"error", "file too large (max " +
                                      std::to_string(cfg_.upload_max_mb) + " MB)"}});
            return;
        }
        // Basic magic-byte check: real PDFs start with %PDF.
        if (file.content.compare(0, 4, "%PDF") != 0) {
            reply_json(req, res, 400, {{"error", "not a PDF file (missing %PDF header)"}});
            return;
        }

        const std::string safe0 = sanitize_filename(file.filename);
        // Never overwrite an existing PDF: add " (1)", " (2)" ... suffix.
        std::error_code ec;
        fs::path dest;
        std::string safe = safe0;
        {
            const size_t dot = safe0.rfind('.');
            const std::string stem = safe0.substr(0, dot);
            const std::string ext = safe0.substr(dot);
            int n = 0;
            dest = cfg_.pdf_dir() / fs::u8path(safe);
            while (fs::exists(dest, ec)) {
                safe = stem + " (" + std::to_string(++n) + ")" + ext;
                dest = cfg_.pdf_dir() / fs::u8path(safe);
            }
        }
        fs::create_directories(cfg_.pdf_dir(), ec);
        {
            std::ofstream out(fs::u8path(dest.u8string()),
                              std::ios::binary | std::ios::trunc);
            if (!out) {
                reply_json(req, res, 500,
                           {{"error", "cannot write into " + cfg_.pdf_folder}});
                return;
            }
            out.write(file.content.data(),
                      static_cast<std::streamsize>(file.content.size()));
        }
        log_info("Uploaded PDF: " + safe + " (" +
                 std::to_string(file.content.size() / 1024) + " KB)");
        reply_json(req, res, 200,
                   {{"status", "ok"},
                    {"file", safe},
                    {"size", file.content.size()},
                    {"message", "PDF saxlanıldı; POST /ingest ilə indeksləşdirin"}});
    });

    // --- /conversations: persistent conversation list ----------------------
    svr.Get(R"(/conversations)", [this](const httplib::Request& req,
                                        httplib::Response& res) {
        if (!check_api_access(req, res, true)) return;
        const std::string client =
            ConversationStore::sanitize_client(req.get_header_value("X-Session-Id"));
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& c : store_.list(client)) arr.push_back(c.to_json(false));
        reply_json(req, res, 200, {{"conversations", arr}, {"count", arr.size()}});
    });

    svr.Post(R"(/conversations)", [this](const httplib::Request& req,
                                         httplib::Response& res) {
        if (!check_api_access(req, res, true)) return;
        nlohmann::json body;
        if (!parse_body(req, res, body)) return;
        const std::string client =
            ConversationStore::sanitize_client(req.get_header_value("X-Session-Id"));
        const Conversation c = store_.create(client, body.value("title", std::string()));
        reply_json(req, res, 200, {{"conversation", c.to_json(true)}});
    });

    svr.Get(R"(/conversations/([A-Za-z0-9_-]{1,64}))",
            [this](const httplib::Request& req, httplib::Response& res) {
                if (!check_api_access(req, res, true)) return;
                const std::string client = ConversationStore::sanitize_client(
                    req.get_header_value("X-Session-Id"));
                Conversation c;
                if (!store_.get(client, req.matches[1].str(), &c)) {
                    reply_json(req, res, 404, {{"error", "söhbət tapılmadı"}});
                    return;
                }
                reply_json(req, res, 200, {{"conversation", c.to_json(true)}});
            });

    svr.Patch(R"(/conversations/([A-Za-z0-9_-]{1,64}))",
              [this](const httplib::Request& req, httplib::Response& res) {
                  if (!check_api_access(req, res, true)) return;
                  nlohmann::json body;
                  if (!parse_body(req, res, body)) return;
                  const std::string client = ConversationStore::sanitize_client(
                      req.get_header_value("X-Session-Id"));
                  const std::string title = body.value("title", std::string());
                  if (trim(title).empty()) {
                      reply_json(req, res, 400,
                                 {{"error", "title boş ola bilməz"}});
                      return;
                  }
                  if (!store_.rename(client, req.matches[1].str(), title)) {
                      reply_json(req, res, 404, {{"error", "söhbət tapılmadı"}});
                      return;
                  }
                  reply_json(req, res, 200, {{"status", "ok"}});
              });

    svr.Delete(R"(/conversations/([A-Za-z0-9_-]{1,64}))",
               [this](const httplib::Request& req, httplib::Response& res) {
                   if (!check_api_access(req, res, true)) return;
                   const std::string client = ConversationStore::sanitize_client(
                       req.get_header_value("X-Session-Id"));
                   if (!store_.remove(client, req.matches[1].str())) {
                       reply_json(req, res, 404, {{"error", "söhbət tapılmadı"}});
                       return;
                   }
                   reply_json(req, res, 200, {{"status", "ok"}});
               });

    // --- /memory: long-term facts ------------------------------------------
    svr.Get(R"(/memory)", [this](const httplib::Request& req,
                                 httplib::Response& res) {
        if (!check_api_access(req, res, true)) return;
        const std::string client =
            ConversationStore::sanitize_client(req.get_header_value("X-Session-Id"));
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& f : store_.facts(client)) arr.push_back(f.to_json());
        reply_json(req, res, 200,
                   {{"enabled", cfg_.memory_enabled && store_.enabled()},
                    {"facts", arr},
                    {"count", arr.size()},
                    {"max_facts", cfg_.memory_max_facts}});
    });

    svr.Delete(R"(/memory/([A-Za-z0-9_-]{1,64}))",
               [this](const httplib::Request& req, httplib::Response& res) {
                   if (!check_api_access(req, res, true)) return;
                   const std::string client = ConversationStore::sanitize_client(
                       req.get_header_value("X-Session-Id"));
                   if (!store_.remove_fact(client, req.matches[1].str())) {
                       reply_json(req, res, 404, {{"error", "yaddaş qeydi tapılmadı"}});
                       return;
                   }
                   reply_json(req, res, 200, {{"status", "ok"}});
               });

    svr.Delete(R"(/memory)", [this](const httplib::Request& req,
                                    httplib::Response& res) {
        if (!check_api_access(req, res, true)) return;
        const std::string client =
            ConversationStore::sanitize_client(req.get_header_value("X-Session-Id"));
        store_.clear_facts(client);
        reply_json(req, res, 200, {{"status", "ok"}, {"count", 0}});
    });

    svr.Post(R"(/memory/toggle)", [this](const httplib::Request& req,
                                         httplib::Response& res) {
        if (!check_api_access(req, res, true)) return;
        nlohmann::json body;
        if (!parse_body(req, res, body)) return;
        if (!body.contains("enabled") || !body["enabled"].is_boolean()) {
            reply_json(req, res, 400,
                       {{"error", "'enabled' true və ya false olmalıdır"}});
            return;
        }
        const bool on = body["enabled"].get<bool>();
        store_.set_enabled(on);
        reply_json(req, res, 200, {{"enabled", cfg_.memory_enabled && store_.enabled()}});
    });

    // --- 404 / error handler ------------------------------------------------
    // httplib calls this for EVERY status >= 400, including replies our own
    // handlers already produced (400/403/413 with a JSON body). Fill in a
    // body only for genuinely body-less replies, and NEVER relabel a real
    // failure as "not found" - the original status is preserved so a
    // handler exception shows up as 500, not a fake 404. Everything is
    // logged so sporadic routing problems can be diagnosed.
    svr.set_error_handler([](const httplib::Request& req, httplib::Response& res) {
        // CORS is applied to EVERY error response, including 429/401/500, so a
        // browser sees the JSON reason instead of a network/CORS failure.
        apply_cors(req, res);
        const int code = (res.status >= 400) ? res.status : 500;
        res.status = code;
        log_warn("HTTP " + std::to_string(code) + " on " + req.method + " " +
                 req.path + (res.body.empty() ? "" : " (handled)"));
        if (!res.body.empty()) return;  // handler already replied with a reason
        res.set_header("Cache-Control", "no-store");
        if (code == 404) {
            res.set_content(
                safe_dump(nlohmann::json({{"error", "tapılmadı (404)"},
                                          {"endpoints", {"/", "/chat", "/chat/stream",
                                                         "/ingest", "/health",
                                                         "/documents", "/upload"}}})),
                "application/json; charset=utf-8");
        } else {
            res.set_content(
                safe_dump(nlohmann::json({{"error", "server xətası (HTTP " +
                                                       std::to_string(code) + ")"}})),
                "application/json; charset=utf-8");
        }
    });

    // Last line of defence: an exception escaping a handler (bad UTF-8, a
    // std::bad_alloc, ...) must become one clean JSON error - it must never
    // take the process or the SSE connection down.
    svr.set_exception_handler([](const httplib::Request& req, httplib::Response& res,
                                 std::exception_ptr ep) {
        std::string what = "naməlum xəta";
        try {
            if (ep) std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            what = e.what();
        } catch (...) {
        }
        log_error("unhandled exception on " + req.method + " " + req.path + ": " +
                  what);
        reply_json(req, res, 500, {{"error", "server xətası: " + what}});
    });

    // --- bind + serve in a background thread -------------------------------
    const std::string host = cfg_.host;
    const int port = cfg_.port;
    impl_->thread = std::thread([this, host, port] {
        if (impl_->svr.bind_to_port(host, port)) {
            impl_->bound = true;
            log_info("HTTP server listening on http://" + host + ":" +
                     std::to_string(port));
            impl_->svr.listen_after_bind();
        } else {
            log_error("failed to bind " + host + ":" + std::to_string(port) +
                      " (port already in use?)");
        }
    });

    // Wait briefly for the bind result.
    for (int i = 0; i < 100 && !impl_->bound; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!impl_->bound) {
        if (err) {
            *err = "could not bind " + host + ":" + std::to_string(port) +
                   " - is another program using that port?";
        }
        impl_->svr.stop();
        if (impl_->thread.joinable()) impl_->thread.join();
        return false;
    }

    // Model warm-up lives in main.cpp (RagPipeline::warmup) and runs BEFORE
    // the server reports ready, so /health never claims "ready" while the
    // first question still pays a cold model load.
    return true;
}

void HttpServer::stop() {
    if (impl_) impl_->svr.stop();
}

void HttpServer::wait() {
    if (impl_ && impl_->thread.joinable()) impl_->thread.join();
}

}  // namespace dumy
