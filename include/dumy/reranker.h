// reranker.h - LLM-based relevance reranking (scores every candidate chunk
// 0-10, keeps the best N, removes duplicates).
#pragma once

#include <string>
#include <vector>

#include "dumy/config.h"
#include "dumy/ollama_client.h"
#include "dumy/vector_store.h"

namespace dumy {

class Reranker {
public:
    Reranker(const Config& cfg, const OllamaClient& ollama);

    void set_models(const std::string& chat_model, const std::string& helper_model);

    // Returns up to `keep` chunks, best first. Duplicate texts are dropped.
    // If the LLM fails or returns garbage, falls back to the original order
    // (retrieval quality degrades gracefully, never errors out).
    std::vector<Chunk> rerank(const std::string& query,
                              const std::vector<Chunk>& candidates,
                              size_t keep, std::string* err) const;

private:
    const Config& cfg_;
    const OllamaClient& ollama_;
    std::string chat_model_;
    std::string helper_model_;
};

}  // namespace dumy
