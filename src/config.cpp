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
    read_if(j, "rerank_max_candidates", c.rerank_max_candidates);
    read_if(j, "rerank_min_score", c.rerank_min_score);
    read_if(j, "rerank_skip_margin", c.rerank_skip_margin);
    read_if(j, "fast_path_enabled", c.fast_path_enabled);
    read_if(j, "fast_path_max_words", c.fast_path_max_words);
    read_if(j, "map_reduce_threshold_tokens", c.map_reduce_threshold_tokens);
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
    read_if(j, "min_score", c.min_score);
    read_if(j, "relevance_check_enabled", c.relevance_check_enabled);
    read_if(j, "relevance_min_overlap", c.relevance_min_overlap);
    read_if(j, "relevance_check_score", c.relevance_check_score);
    read_if(j, "creator", c.creator);
    if (j.contains("generation") && j["generation"].is_object()) {
        const auto& gen = j["generation"];
        read_if(gen, "temperature", c.temperature);
        read_if(gen, "top_p", c.top_p);
        read_if(gen, "repeat_penalty", c.repeat_penalty);
        read_if(gen, "response_language", c.response_language);
    }

    // --- Stage 1: Diagnosis keywords, stopwords, quality check, key-term relevance, expansion ---
    read_if(j, "diagnosis_keywords", c.diagnosis_keywords);
    read_if(j, "stopwords", c.stopwords);
    if (j.contains("quality_check") && j["quality_check"].is_object()) {
        const auto& qc = j["quality_check"];
        read_if(qc, "enabled", c.quality_check_enabled);
        read_if(qc, "min_answer_chars", c.quality_min_answer_chars);
    }
    if (j.contains("keyterm_relevance") && j["keyterm_relevance"].is_object()) {
        const auto& kr = j["keyterm_relevance"];
        read_if(kr, "enabled", c.keyterm_relevance_enabled);
        read_if(kr, "min_matched_terms", c.keyterm_min_matched_terms);
        read_if(kr, "prefix_length", c.keyterm_prefix_length);
        read_if(kr, "case_insensitive", c.keyterm_case_insensitive);
    }
    if (j.contains("expansion") && j["expansion"].is_object()) {
        const auto& exp = j["expansion"];
        read_if(exp, "skip_for_simple", c.expansion_skip_for_simple);
        read_if(exp, "skip_for_general", c.expansion_skip_for_general);
        read_if(exp, "max_simple_words", c.expansion_max_simple_words);
    }

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
    if (c.rerank_max_candidates < 1) c.rerank_max_candidates = 1;
    if (c.rerank_max_candidates > 24) c.rerank_max_candidates = 24;
    if (c.rerank_min_score < 0.0f) c.rerank_min_score = 0.0f;
    if (c.rerank_min_score > 10.0f) c.rerank_min_score = 10.0f;
    if (c.rerank_skip_margin < 0.0f) c.rerank_skip_margin = 0.0f;
    if (c.rerank_skip_margin > 1.0f) c.rerank_skip_margin = 1.0f;
    if (c.fast_path_max_words < 1) c.fast_path_max_words = 1;
    if (c.map_reduce_threshold_tokens < 1000) c.map_reduce_threshold_tokens = 1000;
    if (c.upload_max_mb < 1) c.upload_max_mb = 1;
    if (c.min_score < 0.0f) c.min_score = 0.0f;
    if (c.min_score > 1.0f) c.min_score = 1.0f;
    if (c.relevance_min_overlap < 0.0f) c.relevance_min_overlap = 0.0f;
    if (c.relevance_min_overlap > 1.0f) c.relevance_min_overlap = 1.0f;
    if (c.relevance_check_score < 0.0f) c.relevance_check_score = 0.0f;
    if (c.relevance_check_score > 1.0f) c.relevance_check_score = 1.0f;
    if (c.temperature < 0.0f) c.temperature = 0.0f;
    if (c.temperature > 2.0f) c.temperature = 2.0f;
    if (c.top_p < 0.0f) c.top_p = 0.0f;
    if (c.top_p > 1.0f) c.top_p = 1.0f;
    if (c.repeat_penalty < 0.0f) c.repeat_penalty = 0.0f;
    if (c.repeat_penalty > 2.0f) c.repeat_penalty = 2.0f;
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

    // Stage 1 validation clamps
    if (c.quality_min_answer_chars < 0) c.quality_min_answer_chars = 0;
    if (c.keyterm_min_matched_terms < 0) c.keyterm_min_matched_terms = 0;
    if (c.keyterm_prefix_length < 1) c.keyterm_prefix_length = 1;
    if (c.keyterm_prefix_length > 20) c.keyterm_prefix_length = 20;
    if (c.expansion_max_simple_words < 1) c.expansion_max_simple_words = 1;

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
        {"rerank_max_candidates", rerank_max_candidates},
        {"rerank_min_score", rerank_min_score},
        {"rerank_skip_margin", rerank_skip_margin},
        {"fast_path_enabled", fast_path_enabled},
        {"fast_path_max_words", fast_path_max_words},
        {"map_reduce_threshold_tokens", map_reduce_threshold_tokens},
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
        {"min_score", min_score},
        {"relevance_check_enabled", relevance_check_enabled},
        {"relevance_min_overlap", relevance_min_overlap},
        {"relevance_check_score", relevance_check_score},
        {"creator", creator},
        {"generation", {
            {"temperature", temperature},
            {"top_p", top_p},
            {"repeat_penalty", repeat_penalty},
            {"response_language", response_language}
        }},
        {"diagnosis_keywords", diagnosis_keywords},
        {"stopwords", stopwords},
        {"quality_check", {
            {"enabled", quality_check_enabled},
            {"min_answer_chars", quality_min_answer_chars}
        }},
        {"keyterm_relevance", {
            {"enabled", keyterm_relevance_enabled},
            {"min_matched_terms", keyterm_min_matched_terms},
            {"prefix_length", keyterm_prefix_length},
            {"case_insensitive", keyterm_case_insensitive}
        }},
        {"expansion", {
            {"skip_for_simple", expansion_skip_for_simple},
            {"skip_for_general", expansion_skip_for_general},
            {"max_simple_words", expansion_max_simple_words}
        }}
    };
}

ChatOptions Config::chat_options(int num_predict, double temperature) const {
    ChatOptions o;
    o.temperature = temperature;
    o.num_predict = num_predict;
    o.num_ctx = num_ctx;
    o.num_gpu = -1;             // GPU / partial offload: Ollama's default
    o.think = false;
    o.keep_alive = keep_alive;  // "-1" -> never unload
    return o;
}

ChatOptions Config::embed_options() const {
    ChatOptions o;
    o.temperature = 0.0;
    o.num_predict = -1;
    o.num_ctx = 0;              // embeddings ignore the chat context window
    o.num_gpu = 0;              // bge-m3 always on CPU
    o.think = false;
    o.keep_alive = keep_alive;  // "-1" -> never unload
    return o;
}

fs::path Config::resolve(const std::string& rel) const {
    fs::path p = fs::u8path(rel);
    if (p.is_absolute()) return p;
    return fs::u8path(base_dir) / p;
}

}  // namespace dumy
