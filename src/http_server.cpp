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
        res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, X-API-Key");
    }
}

void reply_json(const httplib::Request& req, httplib::Response& res, int status,
                const nlohmann::json& body) {
    apply_cors(req, res);
    res.status = status;
    res.set_header("Cache-Control", "no-store");
    res.set_content(body.dump(), "application/json; charset=utf-8");
}

// Parses the request body as JSON; on failure replies 400 and returns false.
bool parse_body(const httplib::Request& req, httplib::Response& res, nlohmann::json& out) {
    try {
        out = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
        return true;
    } catch (const std::exception& e) {
        reply_json(req, res, 400, {{"error", std::string("invalid JSON body: ") + e.what()}});
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
    if (apply_rate_limit &&
        rate_limited(req.remote_addr, g_cfg->rate_limit_per_minute)) {
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

}  // namespace

struct HttpServer::Impl {
    httplib::Server svr;
    std::thread thread;
    std::atomic<bool> bound{false};
    std::atomic<bool> ingest_running{false};   // POST /ingest -> 409 when busy
    std::mutex health_mutex;                   // serializes the live Ollama probe
    bool ollama_alive = false;
    std::chrono::steady_clock::time_point ollama_checked_at{};
};

HttpServer::~HttpServer() = default;

HttpServer::HttpServer(const Config& cfg, RagPipeline& pipeline, Indexer& indexer,
                       SessionManager& sessions)
    : cfg_(cfg), pipeline_(pipeline), indexer_(indexer), sessions_(sessions),
      impl_(std::make_unique<Impl>()) {}

bool HttpServer::start(std::string* err) {
    auto& svr = impl_->svr;
    g_cfg = &cfg_;
    // Uploads can be large PDFs; chat/JSON bodies stay small.
    svr.set_payload_max_length(static_cast<size_t>(cfg_.upload_max_mb) * 1024 * 1024 +
                               1024 * 1024);
    svr.new_task_queue = [] { return new httplib::ThreadPool(16); };

    // Where the UI lives on disk (resolved against the config folder).
    const fs::path web_dir = cfg_.resolve(cfg_.web_folder);
    const fs::path index_html = web_dir / "index.html";

    // --- CORS preflight ----------------------------------------------------
    for (const char* path : {"/", "/chat", "/chat/stream", "/ingest", "/health",
                             "/documents", "/upload"}) {
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
        nlohmann::json body = {
            {"status", "ok"},
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
        std::string session_id = body.value("session_id", std::string());
        if (session_id.empty()) session_id = SessionManager::make_session_id();

        try {
            AnswerResult r = pipeline_.answer(message, session_id, nullptr);
            reply_json(req, res, 200,
                       {{"session_id", session_id},
                        {"answer", r.answer},
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
        std::string session_id = body.value("session_id", std::string());
        if (session_id.empty()) session_id = SessionManager::make_session_id();

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
            [this, message, session_copy](size_t offset,
                                          httplib::DataSink& sink) -> bool {
                if (offset > 0) {  // everything was produced in the first call
                    sink.done();
                    return true;
                }

                auto send = [&sink](const nlohmann::json& ev) {
                    const std::string line = "data: " + ev.dump() + "\n\n";
                    return sink.write(line.data(), line.size());
                };

                AnswerResult r;
                try {
                    r = pipeline_.answer(
                        message, session_copy,
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

                log_info("stream: answer_chars=" + std::to_string(r.answer.size()) +
                         " qtype=" + r.qtype + " warning=" + r.warning);
                if (!send({{"type", "done"},
                      {"session_id", session_copy},
                      {"answer", r.answer},
                      {"sources", r.sources_json()},
                      {"qtype", r.qtype},
                      {"refused", r.refused},
                      {"web_used", r.used_web},
                      {"map_reduce", r.used_map_reduce},
                      {"own_knowledge", r.from_own_knowledge},
                      {"self_check_failed", r.self_check_failed},
                      {"self_check_retries", r.self_check_retries},
                      {"warning", r.warning}}))
                    log_warn("stream: done event write failed (client disconnected)");
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

    // --- 404 / error handler ------------------------------------------------
    // httplib calls this for EVERY status >= 400, including replies our own
    // handlers already produced (400/403/413 with a JSON body). Fill in a
    // body only for genuinely body-less replies, and NEVER relabel a real
    // failure as "not found" - the original status is preserved so a
    // handler exception shows up as 500, not a fake 404. Everything is
    // logged so sporadic routing problems can be diagnosed.
    svr.set_error_handler([](const httplib::Request& req, httplib::Response& res) {
        apply_cors(req, res);
        if (!res.body.empty()) return;  // handler already replied with a reason
        const int code = (res.status >= 400) ? res.status : 500;
        res.status = code;
        log_warn("HTTP " + std::to_string(code) + " on " + req.method + " " +
                 req.path);
        if (code == 404) {
            res.set_content(
                nlohmann::json({{"error", "not found"},
                                {"endpoints", {"/", "/chat", "/chat/stream",
                                               "/ingest", "/health", "/documents",
                                               "/upload"}}})
                    .dump(),
                "application/json; charset=utf-8");
        } else {
            res.set_content(
                nlohmann::json({{"error", "server error (HTTP " +
                                               std::to_string(code) + ")"}})
                    .dump(),
                "application/json; charset=utf-8");
        }
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
