#include "dumy/config.h"

#include <algorithm>
#include <exception>

namespace dumy {

namespace {

// Tolerant readers: a wrong type in config.json must not crash the app,
// it just falls back to the default value.
template <typename T>
void read_if(const nlohmann::json& j, const char* key, T& out) {
    if (j.contains(key) && !j[key].is_null()) {
        try {
            out = j.at(key).get<T>();
        } catch (const std::exception&) {
            // wrong type -> keep default, warn below
        }
    }
}

}  // namespace

Config Config::load(const std::string& path, std::string* err) {
    Config c;
    c.config_path = path;

    fs::path p = fs::u8path(path);
    c.base_dir = p.has_parent_path() ? p.parent_path().u8string() : std::string(".");

    if (!fs::exists(p)) {
        if (err) *err = "config file not found: " + path + " (built-in defaults will be used)";
        return c;
    }

    std::string raw;
    if (!read_text_file(p, raw, err)) return c;

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(raw);
    } catch (const std::exception& e) {
        if (err) *err = std::string("config.json parse error: ") + e.what();
        return c;
    }
    if (!j.is_object()) {
        if (err) *err = "config.json must contain a JSON object";
        return c;
    }

    read_if(j, "ollama_url", c.ollama_url);
    // Preferred: the "models" block. Legacy keys below still work.
    if (j.contains("models") && j["models"].is_object()) {
        const auto& m = j["models"];
        read_if(m, "embed", c.embedding_model);
        read_if(m, "helper", c.helper_model);
        read_if(m, "draft", c.chat_model);
        read_if(m, "selfcheck", c.selfcheck_model);
    }
    read_if(j, "chat_model", c.chat_model);
    read_if(j, "helper_model", c.helper_model);
    read_if(j, "embedding_model", c.embedding_model);
    read_if(j, "fallback_model", c.fallback_model);
    read_if(j, "model_fallbacks", c.model_fallbacks);
    read_if(j, "chunk_size", c.chunk_size);
    read_if(j, "chunk_overlap", c.chunk_overlap);
    read_if(j, "top_k_simple", c.top_k_simple);
    read_if(j, "top_k_broad", c.top_k_broad);
    read_if(j, "pdf_folder", c.pdf_folder);
    read_if(j, "pdf_extra_folders", c.pdf_extra_folders);
    read_if(j, "data_folder", c.data_folder);
    read_if(j, "host", c.host);
    read_if(j, "port", c.port);
    read_if(j, "web_search_enabled", c.web_search_enabled);
    read_if(j, "searxng_url", c.searxng_url);
    read_if(j, "search_languages", c.search_languages);
    read_if(j, "web_top_results", c.web_top_results);
    read_if(j, "web_max_pages", c.web_max_pages);
    read_if(j, "web_fetch_timeout", c.web_fetch_timeout);
    read_if(j, "web_cache_ttl_hours", c.web_cache_ttl_hours);
    read_if(j, "trusted_domains", c.trusted_domains);
    read_if(j, "fast_mode", c.fast_mode);
    read_if(j, "request_timeout", c.request_timeout);
    read_if(j, "max_history_messages", c.max_history_messages);
    read_if(j, "rerank_top_n", c.rerank_top_n);
    read_if(j, "context_budget_tokens", c.context_budget_tokens);
    read_if(j, "num_ctx", c.num_ctx);
    read_if(j, "num_predict_draft", c.num_predict_draft);
    read_if(j, "keep_alive", c.keep_alive);
    read_if(j, "auto_ingest", c.auto_ingest);
    read_if(j, "ocr_enabled", c.ocr_enabled);
    read_if(j, "ocr_languages", c.ocr_languages);
    read_if(j, "web_folder", c.web_folder);
    read_if(j, "open_browser", c.open_browser);
    read_if(j, "allow_upload_from_remote", c.allow_upload_from_remote);
    read_if(j, "upload_max_mb", c.upload_max_mb);

    // Sanity clamps so a hand-edited config cannot break retrieval.
    if (c.chunk_size < 100) c.chunk_size = 100;
    if (c.chunk_overlap < 0) c.chunk_overlap = 0;
    if (c.chunk_overlap >= c.chunk_size) c.chunk_overlap = c.chunk_size / 4;
    if (c.top_k_simple < 1) c.top_k_simple = 1;
    if (c.top_k_broad < c.top_k_simple) c.top_k_broad = c.top_k_simple;
    if (c.max_history_messages < 0) c.max_history_messages = 0;
    if (c.port <= 0 || c.port > 65535) c.port = 8085;
    if (c.request_timeout < 5) c.request_timeout = 5;
    if (c.num_ctx < 1024) c.num_ctx = 1024;
    if (c.num_predict_draft < 100) c.num_predict_draft = 100;
    if (c.rerank_top_n < 1) c.rerank_top_n = 1;
    if (c.upload_max_mb < 1) c.upload_max_mb = 1;
    // context_budget_tokens <= 0 means "auto": the space actually left for
    // retrieved chunks = num_ctx - system prompt - history - answer cap.
    // With defaults: 8192 - 900 - 600 - 800 = 5892 tokens.
    if (c.context_budget_tokens <= 0) {
        c.context_budget_tokens =
            std::max(800, c.num_ctx - 900 - 600 - c.num_predict_draft);
    }
    if (c.context_budget_tokens >= c.num_ctx - c.num_predict_draft) {
        c.context_budget_tokens =
            std::max(800, c.num_ctx - 900 - 600 - c.num_predict_draft);
    }
    if (c.selfcheck_model.empty()) c.selfcheck_model = c.helper_model;

    return c;
}

nlohmann::json Config::to_json() const {
    return nlohmann::json{
        {"ollama_url", ollama_url},
        {"chat_model", chat_model},
        {"helper_model", helper_model},
        {"selfcheck_model", selfcheck_model},
        {"embedding_model", embedding_model},
        {"fallback_model", fallback_model},
        {"model_fallbacks", model_fallbacks},
        {"chunk_size", chunk_size},
        {"chunk_overlap", chunk_overlap},
        {"top_k_simple", top_k_simple},
        {"top_k_broad", top_k_broad},
        {"pdf_folder", pdf_folder},
        {"pdf_extra_folders", pdf_extra_folders},
        {"data_folder", data_folder},
        {"host", host},
        {"port", port},
        {"web_search_enabled", web_search_enabled},
        {"searxng_url", searxng_url},
        {"search_languages", search_languages},
        {"web_top_results", web_top_results},
        {"web_max_pages", web_max_pages},
        {"web_fetch_timeout", web_fetch_timeout},
        {"web_cache_ttl_hours", web_cache_ttl_hours},
        {"trusted_domains", trusted_domains},
        {"fast_mode", fast_mode},
        {"request_timeout", request_timeout},
        {"max_history_messages", max_history_messages},
        {"rerank_top_n", rerank_top_n},
        {"context_budget_tokens", context_budget_tokens},
        {"num_ctx", num_ctx},
        {"num_predict_draft", num_predict_draft},
        {"keep_alive", keep_alive},
        {"auto_ingest", auto_ingest},
        {"ocr_enabled", ocr_enabled},
        {"ocr_languages", ocr_languages},
        {"web_folder", web_folder},
        {"open_browser", open_browser},
        {"allow_upload_from_remote", allow_upload_from_remote},
        {"upload_max_mb", upload_max_mb},
    };
}

fs::path Config::resolve(const std::string& rel) const {
    fs::path p = fs::u8path(rel);
    if (p.is_absolute()) return p;
    return fs::u8path(base_dir) / p;
}

}  // namespace dumy
