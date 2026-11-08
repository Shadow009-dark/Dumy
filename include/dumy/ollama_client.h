// ollama_client.h - REST client for a local Ollama server (no API keys, no cloud).
// Endpoints used: /api/chat (streaming + normal), /api/embed, /api/tags, /api/version.
#pragma once

#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace dumy {

struct ChatMessage {
    std::string role;     // "system" | "user" | "assistant"
    std::string content;

    nlohmann::json to_json() const;
};

struct ChatOptions {
    double temperature = 0.2;
    int num_predict = -1;   // max new tokens; -1 = model default
    int num_ctx = 8192;
    int num_gpu = -1;       // -1 = model default; 0 = force CPU (embeddings)
    bool think = false;     // keep qwen3-style reasoning off for latency
    std::string keep_alive = "-1";  // -1 = never unload (Step 1 requirement)
};

// One entry of GET /api/ps (models currently loaded by Ollama).
struct RunningModel {
    std::string name;
    long long size = 0;       // total bytes
    long long size_vram = 0;  // bytes resident in VRAM (0 = CPU only)
};

// Timing Ollama reports for a single call. `load_ms` > 0 means the model had
// to be (re)loaded into VRAM for this call - i.e. a model load/swap, which on
// a 6 GB GPU with OLLAMA_MAX_LOADED_MODELS=1 is the usual dominant cost.
struct ChatTiming {
    long long load_ms = 0;         // model load/swap duration
    long long prompt_eval_ms = 0;  // prompt processing
    long long eval_ms = 0;         // generation
    long long total_ms = 0;        // whole Ollama-side call
    long long prompt_tokens = 0;
    long long eval_tokens = 0;
    bool model_loaded() const { return load_ms > 0; }
};

class OllamaClient {
public:
    OllamaClient(std::string base_url, int timeout_seconds);

    // One-shot chat with a specific model. Returns the answer text
    // (thinking stripped) or "" + err.
    std::string chat(const std::string& model,
                     const std::vector<ChatMessage>& messages,
                     const ChatOptions& opts,
                     std::string* err = nullptr,
                     ChatTiming* timing = nullptr) const;

    // Streaming chat; `on_delta` receives each text fragment as it arrives.
    // Returns the full accumulated text.
    using StreamFn = std::function<void(const std::string& delta)>;
    std::string chat_stream(const std::string& model,
                            const std::vector<ChatMessage>& messages,
                            const ChatOptions& opts,
                            const StreamFn& on_delta,
                            std::string* err = nullptr,
                            ChatTiming* timing = nullptr) const;

    // Batch embeddings via /api/embed. `out` gets one vector per input.
    bool embed(const std::string& model,
               const std::vector<std::string>& inputs,
               std::vector<std::vector<float>>& out,
               std::string* err = nullptr,
               ChatTiming* timing = nullptr) const;

    // Server reachability (GET /api/version) with a hard timeout cap.
    bool ping(std::string* err = nullptr, int timeout_sec = 5) const;

    // Installed model names (GET /api/tags).
    std::vector<std::string> list_models(std::string* err = nullptr) const;

    // Models currently loaded in memory (GET /api/ps). Empty on failure.
    std::vector<RunningModel> running_models(std::string* err = nullptr) const;

    const std::string& base_url() const { return base_url_; }

    // Removes reasoning blocks: <think> ... </think> (qwen3) if they leak
    // into the answer text.
    static std::string strip_thinking(const std::string& s);

private:
    std::string base_url_;
    int timeout_;
};

}  // namespace dumy
