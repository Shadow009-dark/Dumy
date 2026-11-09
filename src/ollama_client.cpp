#include "dumy/ollama_client.h"

#include <curl/curl.h>

#include <functional>
#include <memory>

#include "dumy/common.h"

namespace dumy {
namespace {

// curl has global state; initialize exactly once for the whole process.
struct CurlGlobal {
    CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobal() { curl_global_cleanup(); }
};
void ensure_curl() {
    static CurlGlobal g;
    (void)g;
}

size_t write_string(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t n = size * nmemb;
    static_cast<std::string*>(userdata)->append(ptr, n);
    return n;
}

// Splits a streaming NDJSON body into lines and hands each line to a callback.
struct LineSink {
    std::string pending;
    const std::function<void(const std::string&)>* on_line = nullptr;
};
size_t write_lines(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* sink = static_cast<LineSink*>(userdata);
    size_t n = size * nmemb;
    sink->pending.append(ptr, n);
    size_t pos;
    while ((pos = sink->pending.find('\n')) != std::string::npos) {
        std::string line = sink->pending.substr(0, pos);
        sink->pending.erase(0, pos + 1);
        if (!line.empty() && (*sink->on_line)) (*sink->on_line)(line);
    }
    return n;
}

struct RequestSpec {
    std::string url;
    std::string body;          // empty => GET
    int timeout_sec = 60;
    bool streaming = false;    // NDJSON line-by-line mode
};

// Shared HTTP execution. Returns true on HTTP success (2xx).
bool do_request(const RequestSpec& spec,
                const std::function<void(const std::string&)>& on_line,
                std::string& response_out,
                std::string* err) {
    ensure_curl();
    CURL* raw = curl_easy_init();
    if (!raw) {
        if (err) *err = "curl_easy_init failed";
        return false;
    }
    std::unique_ptr<CURL, void (*)(CURL*)> curl(raw, curl_easy_cleanup);

    struct curl_slist* headers = nullptr;
    if (!spec.body.empty()) {
        headers = curl_slist_append(headers, "Content-Type: application/json");
    }

    curl_easy_setopt(curl.get(), CURLOPT_URL, spec.url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, static_cast<long>(spec.timeout_sec));
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);  // thread safety

    if (!spec.body.empty()) {
        curl_easy_setopt(curl.get(), CURLOPT_POST, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, spec.body.data());
        curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE,
                         static_cast<long>(spec.body.size()));
    }

    std::string buffered;
    LineSink sink;
    sink.on_line = &on_line;
    if (spec.streaming) {
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, write_lines);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &sink);
    } else {
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, write_string);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &buffered);
    }

    CURLcode rc = curl_easy_perform(curl.get());
    long http_code = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);
    if (headers) curl_slist_free_all(headers);

    if (rc != CURLE_OK) {
        if (err) {
            *err = std::string("HTTP request failed: ") + curl_easy_strerror(rc) +
                   " (" + spec.url + ")";
        }
        return false;
    }
    if (http_code < 200 || http_code >= 300) {
        if (err) {
            std::string body = buffered.empty() ? sink.pending : buffered;
            body = trim(body);
            if (body.size() > 400) body = body.substr(0, 400);
            *err = "HTTP " + std::to_string(http_code) + " from " + spec.url +
                   (body.empty() ? "" : (": " + body));
        }
        return false;
    }

    if (!spec.streaming) response_out = std::move(buffered);
    else if (!sink.pending.empty() && on_line) on_line(sink.pending);
    return true;
}

nlohmann::json options_json(const ChatOptions& o) {
    nlohmann::json j = {{"temperature", o.temperature}};
    if (o.num_predict > 0) j["num_predict"] = o.num_predict;
    if (o.num_ctx > 0) j["num_ctx"] = o.num_ctx;
    // num_gpu changes the load plan: -1 (default) must be omitted so Ollama
    // uses its own default, 0 forces CPU. Every caller for a given model has
    // to send the same value or Ollama reloads the model mid-request.
    if (o.num_gpu >= 0) j["num_gpu"] = o.num_gpu;
    return j;
}

// Top-level "keep_alive" field shared by /api/chat and /api/embed.
// Step 1: every request keeps its model resident - never unload (-1).
nlohmann::json keep_alive_json(const ChatOptions& o) {
    if (o.keep_alive.empty() || o.keep_alive == "-1" || o.keep_alive == "default")
        return nlohmann::json(-1);
    // A plain integer string ("0", "300") is sent as a JSON number.
    bool numeric = !o.keep_alive.empty();
    for (char c : o.keep_alive)
        if (!std::isdigit(static_cast<unsigned char>(c)) && c != '-') numeric = false;
    if (numeric) {
        try {
            return nlohmann::json(std::stoll(o.keep_alive));
        } catch (...) {
        }
    }
    return nlohmann::json(o.keep_alive);  // "30m" style duration
}

// Reads one duration field (Ollama reports nanoseconds) into milliseconds.
long long ms_from_ns(const nlohmann::json& j, const char* key) {
    if (!j.contains(key) || !j[key].is_number()) return 0;
    return static_cast<long long>(j[key].get<double>() / 1.0e6 + 0.5);
}

ChatTiming timing_from_json(const nlohmann::json& j) {
    ChatTiming t;
    t.load_ms = ms_from_ns(j, "load_duration");
    t.prompt_eval_ms = ms_from_ns(j, "prompt_eval_duration");
    t.eval_ms = ms_from_ns(j, "eval_duration");
    t.total_ms = ms_from_ns(j, "total_duration");
    if (j.contains("prompt_eval_count") && j["prompt_eval_count"].is_number())
        t.prompt_tokens = j["prompt_eval_count"].get<long long>();
    if (j.contains("eval_count") && j["eval_count"].is_number())
        t.eval_tokens = j["eval_count"].get<long long>();
    if (j.contains("done_reason") && j["done_reason"].is_string())
        t.done_reason = j["done_reason"].get<std::string>();
    return t;
}

nlohmann::json messages_json(const std::vector<ChatMessage>& msgs) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& m : msgs)
        arr.push_back({{"role", m.role},
                       // sanitize: nlohmann rejects invalid UTF-8 when dumping
                       {"content", sanitize_utf8(m.content)}});
    return arr;
}

}  // namespace

nlohmann::json ChatMessage::to_json() const {
    return {{"role", role}, {"content", content}};
}

OllamaClient::OllamaClient(std::string base_url, int timeout_seconds)
    : base_url_(std::move(base_url)), timeout_(timeout_seconds) {
    while (!base_url_.empty() && base_url_.back() == '/') base_url_.pop_back();
    ensure_curl();
}

std::string OllamaClient::strip_thinking(const std::string& s) {
    std::string out = s;
    auto strip_pair = [&out](const char* open_tag, const char* close_tag,
                             size_t open_len, size_t close_len) {
        for (;;) {
            size_t open = out.find(open_tag);
            if (open == std::string::npos) return;
            size_t close = out.find(close_tag, open + open_len);
            if (close == std::string::npos) {  // unclosed block: drop to the end
                out.erase(open);
                return;
            }
            out.erase(open, close + close_len - open);
        }
    };
    strip_pair("<think>", "</tool_call>", 7, 10);
    strip_pair("<thinking>", "</thinking>", 10, 11);
    return trim(out);
}

std::string OllamaClient::chat(const std::string& model,
                               const std::vector<ChatMessage>& messages,
                               const ChatOptions& opts,
                               std::string* err, ChatTiming* timing) const {
    nlohmann::json body = {
        {"model", model},
        {"messages", messages_json(messages)},
        {"stream", false},
        {"think", opts.think},
        {"keep_alive", keep_alive_json(opts)},
        {"options", options_json(opts)},
    };

    RequestSpec spec;
    spec.url = base_url_ + "/api/chat";
    spec.body = body.dump();
    spec.timeout_sec = timeout_;
    spec.streaming = false;

    const auto noop = [](const std::string&) {};
    std::string response;
    if (!do_request(spec, noop, response, err)) return "";

    try {
        auto j = nlohmann::json::parse(response);
        if (j.contains("error")) {
            if (err) *err = j["error"].get<std::string>();
            return "";
        }
        if (timing) *timing = timing_from_json(j);
        std::string content = j.value("message", nlohmann::json::object())
                                  .value("content", std::string());
        return strip_thinking(content);
    } catch (const std::exception& e) {
        if (err) *err = std::string("bad Ollama response: ") + e.what();
        return "";
    }
}

std::string OllamaClient::chat_stream(const std::string& model,
                                      const std::vector<ChatMessage>& messages,
                                      const ChatOptions& opts,
                                      const StreamFn& on_delta,
                                      std::string* err, ChatTiming* timing) const {
    nlohmann::json body = {
        {"model", model},
        {"messages", messages_json(messages)},
        {"stream", true},
        {"think", opts.think},
        {"keep_alive", keep_alive_json(opts)},
        {"options", options_json(opts)},
    };

    RequestSpec spec;
    spec.url = base_url_ + "/api/chat";
    spec.body = body.dump();
    spec.timeout_sec = timeout_;
    spec.streaming = true;

    std::string full;
    bool failed = false;
    std::string failure;
    const auto on_line = [&](const std::string& line) {
        if (failed) return;
        nlohmann::json j = nlohmann::json::parse(line, nullptr, false);
        if (j.is_discarded()) return;  // tolerate partial/garbage lines
        if (j.contains("error")) {
            failed = true;
            failure = j["error"].is_string() ? j["error"].get<std::string>()
                                             : "unknown Ollama stream error";
            return;
        }
        // The final chunk (done=true) carries the timing metrics.
        if (j.value("done", false) && timing) *timing = timing_from_json(j);
        if (!j.contains("message")) return;
        // The reasoning model may put text into "thinking"; we only keep
        // the visible "content" and never forward thinking to the user.
        std::string delta = j["message"].value("content", std::string());
        if (delta.empty()) return;
        full += delta;
        if (on_delta) on_delta(delta);
    };

    std::string response;  // unused in streaming mode
    if (!do_request(spec, on_line, response, err)) return "";
    if (failed) {
        if (err) *err = failure;
        return "";
    }
    // Emit the cleaned (thinking-stripped) text as a single correction pass
    // only when stripping actually changed something - the stream already
    // showed the raw deltas, so just return the final cleaned text.
    return strip_thinking(full);
}

bool OllamaClient::embed(const std::string& model,
                         const std::vector<std::string>& inputs,
                         std::vector<std::vector<float>>& out,
                         std::string* err, ChatTiming* timing) const {
    if (inputs.empty()) return true;
    std::vector<std::string> clean;
    clean.reserve(inputs.size());
    for (const auto& t : inputs) clean.push_back(sanitize_utf8(t));
    // bge-m3 must always run on CPU: it is small, and keeping it off the GPU
    // leaves the whole 6 GB of VRAM for the chat model. num_gpu=0 is a
    // load-affecting option, so it is sent on EVERY embed call (warm-up too).
    ChatOptions emb;
    emb.num_gpu = 0;
    emb.keep_alive = "-1";
    nlohmann::json body = {{"model", model},
                           {"input", clean},
                           {"keep_alive", keep_alive_json(emb)},
                           {"options", options_json(emb)}};

    RequestSpec spec;
    spec.url = base_url_ + "/api/embed";
    spec.body = body.dump();
    spec.timeout_sec = std::max(timeout_, 300);  // big batches need more time

    const auto noop = [](const std::string&) {};
    std::string response;
    if (!do_request(spec, noop, response, err)) return false;

    try {
        auto j = nlohmann::json::parse(response);
        if (j.contains("error")) {
            if (err) *err = j["error"].get<std::string>();
            return false;
        }
        if (!j.contains("embeddings") || !j["embeddings"].is_array()) {
            if (err) *err = "Ollama /api/embed returned no embeddings";
            return false;
        }
        if (timing) *timing = timing_from_json(j);
        out.clear();
        for (const auto& e : j["embeddings"]) {
            std::vector<float> v;
            v.reserve(e.size());
            for (const auto& x : e) v.push_back(x.get<float>());
            out.push_back(std::move(v));
        }
        if (out.size() != inputs.size()) {
            if (err) *err = "embedding count mismatch (got " +
                            std::to_string(out.size()) + ", expected " +
                            std::to_string(inputs.size()) + ")";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        if (err) *err = std::string("bad Ollama embed response: ") + e.what();
        return false;
    }
}

bool OllamaClient::ping(std::string* err, int timeout_sec) const {
    RequestSpec spec;
    spec.url = base_url_ + "/api/version";
    spec.timeout_sec = timeout_sec > 0 ? timeout_sec : 5;
    const auto noop = [](const std::string&) {};
    std::string response;
    return do_request(spec, noop, response, err);
}

std::vector<RunningModel> OllamaClient::running_models(std::string* err) const {
    std::vector<RunningModel> out;
    RequestSpec spec;
    spec.url = base_url_ + "/api/ps";
    spec.timeout_sec = 5;
    const auto noop = [](const std::string&) {};
    std::string response;
    if (!do_request(spec, noop, response, err)) return out;
    try {
        auto j = nlohmann::json::parse(response);
        for (const auto& m : j.value("models", nlohmann::json::array())) {
            RunningModel rm;
            rm.name = m.value("name", m.value("model", std::string()));
            rm.size = m.value("size", 0LL);
            rm.size_vram = m.value("size_vram", 0LL);
            if (!rm.name.empty()) out.push_back(std::move(rm));
        }
    } catch (const std::exception& e) {
        if (err) *err = std::string("bad /api/ps response: ") + e.what();
    }
    return out;
}

std::vector<std::string> OllamaClient::list_models(std::string* err) const {
    std::vector<std::string> names;
    RequestSpec spec;
    spec.url = base_url_ + "/api/tags";
    spec.timeout_sec = 10;
    const auto noop = [](const std::string&) {};
    std::string response;
    if (!do_request(spec, noop, response, err)) return names;
    try {
        auto j = nlohmann::json::parse(response);
        for (const auto& m : j.value("models", nlohmann::json::array())) {
            if (m.contains("name")) names.push_back(m["name"].get<std::string>());
            else if (m.contains("model")) names.push_back(m["model"].get<std::string>());
        }
    } catch (const std::exception& e) {
        if (err) *err = std::string("bad /api/tags response: ") + e.what();
    }
    return names;
}

}  // namespace dumy
