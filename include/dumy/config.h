// config.h - loads config.json and resolves relative paths against its folder.
#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "dumy/common.h"

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
    int rerank_top_n = 8;               // chunks kept after LLM reranking
    int context_budget_tokens = 4500;   // above this the pipeline switches to map-reduce
    int num_ctx = 8192;                 // Ollama context window (6 GB VRAM friendly)
    int num_predict_draft = 850;        // cap on draft answer length (tokens)
    std::string keep_alive = "30m";     // how long Ollama keeps models loaded
    bool auto_ingest = true;            // index new PDFs at startup
    bool ocr_enabled = false;           // OCR extension point (Tesseract), off by default
    std::string ocr_languages = "aze+eng";

    // --- Web UI / upload ---
    std::string web_folder = "web";             // optional UI override (else embedded)
    bool open_browser = true;                    // open the chat page on server start
    bool allow_upload_from_remote = false;       // PDF upload: localhost only by default
    int upload_max_mb = 200;                     // max PDF upload size

    // --- Runtime (not from JSON) ---
    std::string config_path;            // absolute path of the loaded config file
    std::string base_dir;               // folder of the config file (project root)

    // Load a config file; on missing file or bad JSON fills `err` and returns defaults.
    static Config load(const std::string& path, std::string* err = nullptr);

    nlohmann::json to_json() const;

    // Resolve a possibly-relative path against the config file's folder.
    fs::path resolve(const std::string& rel) const;
    fs::path pdf_dir() const { return resolve(pdf_folder); }
    fs::path data_dir() const { return resolve(data_folder); }
};

}  // namespace dumy
