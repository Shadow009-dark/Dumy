// rag_pipeline.h - the full quality pipeline:
//   analyze -> expand -> parallel (PDF hybrid + web) -> rerank ->
//   context build (map-reduce if too large) -> draft (streamed) ->
//   self-check -> labels + safety + sources footer.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "dumy/bm25_index.h"
#include "dumy/config.h"
#include "dumy/ollama_client.h"
#include "dumy/reranker.h"
#include "dumy/retriever.h"
#include "dumy/session_manager.h"
#include "dumy/vector_store.h"
#include "dumy/web_search.h"

namespace dumy {

struct AnswerResult {
    std::string answer;                 // final text (labels + footer included)
    std::vector<Chunk> used_chunks;     // chunks that made it into the context
    std::string qtype;                  // question_type_name()
    bool refused = false;               // polite code refusal was returned
    bool used_web = false;
    bool used_map_reduce = false;
    bool used_self_check = false;
    bool self_check_failed = false;     // verdict=FAIL (a correction retry ran)
    int self_check_retries = 0;         // 0 or 1 (never more than one)
    bool from_own_knowledge = false;    // context was empty/weak
    bool no_sources = false;            // small-talk fast path: sources: []
    bool smalltalk = false;             // answered by the small-talk fast path
    std::string warning;                // pipeline-level warning ("" = none)

    nlohmann::json sources_json() const;  // REST-friendly source list
};

class RagPipeline {
public:
    using StreamFn = std::function<void(const std::string&)>;
    // Phase notifications for the UI ("selfcheck" -> show "Yoxlanılır...").
    using StatusFn = std::function<void(const std::string&)>;

    RagPipeline(const Config& cfg, OllamaClient& ollama, VectorStorePtr store,
                Bm25Index& bm25, WebSearch& web, Retriever& retriever,
                Reranker& reranker, SessionManager& sessions);

    // Checks Ollama, resolves chat/helper models with fallbacks.
    // Returns false when Ollama is unreachable (clear message in `err`).
    bool initialize(std::string* err);

    bool ollama_ok() const { return ollama_ok_; }
    // Live Ollama probe with a hard timeout cap (GET /health uses 2 s so a
    // dead Ollama can never hang the status badge).
    bool check_ollama(int timeout_sec) const { return ollama_.ping(nullptr, timeout_sec); }
    const std::string& chat_model() const { return chat_model_; }
    const std::string& helper_model() const { return helper_model_; }
    const std::string& embedding_model() const { return embedding_model_; }

    // Produces the final answer. `on_delta` (optional) receives draft text
    // fragments for streaming, `on_status` (optional) receives phase names;
    // the returned AnswerResult is authoritative.
    AnswerResult answer(const std::string& question,
                        const std::string& session_id,
                        const StreamFn& on_delta,
                        const StatusFn& on_status = nullptr) const;

private:
    std::vector<Chunk> rerank_or_trim(const std::string& question,
                                      std::vector<Chunk> candidates,
                                      QuestionType qtype,
                                      std::string* err) const;

    std::vector<Chunk> map_reduce_summarize(const std::string& question,
                                            const std::vector<Chunk>& chunks) const;

    // Caps chunks per source (max 3 per PDF, 2 per web URL) and drops
    // near-duplicate embeddings (cosine > 0.95) so the same text is never
    // cited twice while keeping source diversity.
    std::vector<Chunk> diversify(const std::vector<Chunk>& chunks) const;

    const Config& cfg_;
    OllamaClient& ollama_;
    VectorStorePtr store_;
    Bm25Index& bm25_;
    WebSearch& web_;
    Retriever& retriever_;
    Reranker& reranker_;
    SessionManager& sessions_;

    bool ollama_ok_ = false;
    std::string chat_model_;       // "models.draft" - main answer model
    std::string helper_model_;     // "models.helper" - expand/rerank/summarize
    std::string selfcheck_model_;  // "models.selfcheck" - fact-check pass
    std::string embedding_model_;
};

}  // namespace dumy
