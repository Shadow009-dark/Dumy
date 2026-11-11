// config.h - loads config.json and resolves relative paths against its folder.
#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "dumy/common.h"
#include "dumy/ollama_client.h"

namespace dumy {

struct Config {
    // --- Ollama ---
    // "models" block in config.json: {embed, helper, draft, selfcheck}.
    // Legacy top-level keys (chat_model/helper_model/embedding_model) are
    // still read when the block is missing.
    std::string ollama_url = "http://localhost:11434";
    std::string chat_model = "qwen3:8b";        // models.draft
    std::string helper_model = "qwen3:4b";      // models.helper
    std::string selfcheck_model;                // models.selfcheck ("" = helper)
    std::string embedding_model = "bge-m3";     // models.embed
    std::string fallback_model = "llama3.1:8b"; // used when draft is missing
    std::vector<std::string> model_fallbacks = {"llama3.1:8b", "qwen2.5:7b"};

    // --- Chunking / retrieval ---
    int chunk_size = 650;         // tokens per chunk (target 500-800)
    int chunk_overlap = 100;      // tokens of overlap between chunks
    int top_k_simple = 6;         // chunks for simple fact questions
    int top_k_broad = 12;         // chunks for broad topics (map-reduce kicks in)

    // --- Folders ---
    std::string pdf_folder = "pdfs";
    std::vector<std::string> pdf_extra_folders = {"."};  // legacy folders that also contain PDFs
    std::string data_folder = "data";

    // --- HTTP server ---
    std::string host = "127.0.0.1";
    int port = 8085;

    // --- Web search (always on by default) ---
    bool web_search_enabled = true;
    std::string searxng_url = "http://localhost:8080";
    std::vector<std::string> search_languages = {"az", "en"};
    int web_top_results = 8;
    int web_max_pages = 6;
    int web_fetch_timeout = 10;   // seconds per page download
    int web_cache_ttl_hours = 168;
    std::vector<std::string> trusted_domains;

    // --- Behaviour ---
    bool fast_mode = false;             // skip rerank + self-check for simple questions
    int request_timeout = 300;          // seconds per Ollama call
    int max_history_messages = 8;       // messages kept per session

    // --- Persistent memory (conversations + long-term facts) ---------------
    bool memory_enabled = true;             // conversations + "yaddaş" facts
    int memory_max_facts = 50;              // hard cap on stored facts
    int memory_injection_tokens = 160;      // memory prompt budget (capped at 500)
    int conversation_retention_days = 0;    // 0 = keep conversations forever

    // Welcome-screen chips: [{"text": "...", "label": "..."}]. They live in
    // config.json so they can change without editing the HTML.
    nlohmann::json welcome_examples = nlohmann::json::array();
    int rerank_top_n = 8;               // chunks kept after LLM reranking
    int rerank_max_candidates = 6;      // Step 4: max chunks sent to the LLM reranker
    float rerank_min_score = 8.0f;      // Step 3: keep only chunks scored >= this (0-10)
    float rerank_skip_margin = 0.05f;   // Step 4: skip rerank when cosine top-1 is this far ahead
    int context_budget_tokens = 0;      // 0 = auto (num_ctx - prompt/history/answer)
    int map_reduce_threshold_tokens = 6000;  // Step 3: only above this map-reduce runs
    int num_ctx = 4096;                 // Ollama context window (6 GB VRAM friendly)
    int num_predict_draft = 500;        // cap on draft answer length (tokens)
    std::string keep_alive = "-1";      // -1 = Ollama never unloads the model

    // --- Step 5: fast path for short factual questions ---------------------
    bool fast_path_enabled = true;
    int fast_path_max_words = 12;       // SimpleFact questions up to this -> no rerank/map-reduce
    bool auto_ingest = true;            // index new PDFs at startup
    bool ocr_enabled = false;           // OCR extension point (Tesseract), off by default
    std::string ocr_languages = "aze+eng";

    // --- Retrieval guard ---
    float min_score = 0.3f;             // minimum top-chunk cosine similarity to call LLM
    // --- Relevance gate: off-topic PDF hits must not become citations ------
    bool relevance_check_enabled = true;  // lexical check + cheap yes/no LLM check
    float relevance_min_overlap = 0.34f;  // share of question key terms that must appear
    float relevance_check_score = 0.75f;  // top cosine at which a partial lexical match is enough

    // --- Generation settings ---
    float temperature = 0.15f;          // sampling temperature (0.1-0.2)
    float top_p = 0.9f;                 // nucleus sampling
    float repeat_penalty = 1.1f;        // repetition penalty
    std::string response_language = "az"; // forced response language

    // --- Identity ---
    std::string creator = "DUMY Team";  // bot creator name for identity questions

    // --- Web UI / upload ---
    std::string web_folder = "web";             // optional UI override (else embedded)
    bool open_browser = true;                    // open the chat page on server start
    bool allow_upload_from_remote = false;       // PDF upload: localhost only by default
    int upload_max_mb = 200;                     // max PDF upload size

    // --- Stage 1: Diagnosis keywords (configurable) ---
    std::vector<std::string> diagnosis_keywords = {
        "niyə saralır", "niye saralir",
        "quruyur", "curuyur",
        "çürüyur", "curuyur",
        "solur",
        "ləkə", "leke",
        "xanlı", "xanli",
        "vurulub", "vuruldu",
        "xəstəlik", "xestelik",
        "simptom", "simptom",
        "müalicə", "mualice",
        "niyə öl", "niye ol",
        "doza", "dose",
        "antibiot", "peyvənd", "peyvend",
        "vaksin", "dərman", "derman",
        "nə üçün", "ne ucun",
        "ağrır", "agrir",
        "qızdır", "qizdir",
        "xəstə", "xeste",
        "əlamət", "elamet",
        "diaqnoz", "sebeb",
        "səbəb", "why",
        "treatment", "disease", "symptom"
    };

    // --- Stage 1: Stopwords for key-term extraction ---
    std::vector<std::string> stopwords = {
        "niyə", "niye", "necə", "nece", "nədir", "nedir",
        "kim", "harada", "harda", "ne zaman", "ne vaxt",
        "nə", "ne", "nə üçün", "ne ucun",
        "how", "why", "what", "where", "when", "who",
        "is", "are", "was", "were", "the", "a", "an",
        "ve", "ile", "için", "üçün", "bir", "bu", "o",
        "hansi", "hansı", "hansı", "hansili",
        "bunun", "şu", "şunun", "onun"
    };

    // --- Stage 1: Answer quality check ---
    bool quality_check_enabled = true;
    int quality_min_answer_chars = 80;

    // --- Stage 1: Key-term relevance check ---
    bool keyterm_relevance_enabled = true;
    int keyterm_min_matched_terms = 1;
    int keyterm_prefix_length = 5;
    bool keyterm_case_insensitive = true;

    // --- Stage 1: Query expansion control ---
    bool expansion_skip_for_simple = true;
    bool expansion_skip_for_general = true;
    int expansion_max_simple_words = 10;

    // --- Context budgeting (Phase 2) -------------------------------------
    double token_chars_per_token = 2.6;  // conservative Azerbaijani estimate
    int context_safety_margin = 160;     // extra tokens kept free

    // --- API exposure (Phase 8) ------------------------------------------
    std::string api_key;                 // empty = no key required
    std::vector<std::string> allowed_origins = {
        "http://localhost:8085", "http://127.0.0.1:8085"};
    int rate_limit_per_minute = 20;      // per IP; 0 = disabled
    // The local UI sends many questions back to back; loopback is exempt by
    // default (a tunnel / reverse proxy still hits the per-IP limit above).
    bool rate_limit_exempt_loopback = true;
    int rate_limit_localhost_per_minute = 0;  // 0 = unlimited on loopback
    int max_question_chars = 2000;
    int max_body_kb = 256;               // JSON body limit for /chat
    bool log_questions = false;          // do not log user text by default

    // --- Web search (Phase 6) --------------------------------------------
    std::string web_engine = "ddg";      // ddg | searxng | wikipedia
    bool web_kill_switch = false;        // hard off switch
    int web_search_timeout_sec = 3;      // total per engine call
    int web_memory_cache_minutes = 10;   // in-process result cache
    int max_general_sentences = 5;       // no-context answer cap

    // --- Statement / fact-check routing ---
    bool fact_check_enabled = true;      // "Title: statement" -> verdict
    int fact_check_num_predict = 160;    // verdict + 1-2 short sentences

    // --- Garbled-output guard (word validity) ---
    double garbled_word_ratio_max = 0.12;  // share of unknown words allowed
    int garbled_word_min_words = 10;       // shorter answers are not checked

    // --- Runtime (not from JSON) ---
    std::string config_path;            // absolute path of the loaded config file
    std::string base_dir;               // folder of the config file (project root)

    // Load a config file; on missing file or bad JSON fills `err` and returns defaults.
    static Config load(const std::string& path, std::string* err = nullptr);

    // --- Centralized Ollama options (Step 1) -------------------------------
    // Every call to a model MUST use these helpers: num_ctx / num_gpu /
    // keep_alive are load-affecting, so a mismatch makes Ollama reload the
    // model (the 5-20 s delay this task removes).
    ChatOptions chat_options(int num_predict, double temperature) const;
    ChatOptions embed_options() const;   // bge-m3: num_gpu=0 (CPU), keep_alive=-1

    nlohmann::json to_json() const;

    // Resolve a possibly-relative path against the config file's folder.
    fs::path resolve(const std::string& rel) const;
    fs::path pdf_dir() const { return resolve(pdf_folder); }
    fs::path data_dir() const { return resolve(data_folder); }
};

}  // namespace dumy
