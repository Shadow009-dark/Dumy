// rag_pipeline.h - the full quality pipeline:
//   analyze -> expand -> parallel (PDF hybrid + web) -> rerank ->
//   context build (map-reduce if too large) -> draft (streamed) ->
//   self-check -> labels + safety + sources footer.
#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "dumy/az_quality.h"
#include "dumy/bm25_index.h"
#include "dumy/config.h"
#include "dumy/conversation_store.h"
#include "dumy/ollama_client.h"
#include "dumy/reranker.h"
#include "dumy/retriever.h"
#include "dumy/session_manager.h"
#include "dumy/knowledge_base.h"
#include "dumy/vector_store.h"
#include "dumy/web_search.h"

namespace dumy {

// Result of the startup preload (Step 1). Reported by GET /health so a
// silently evicted model is visible instead of only showing up as latency.
struct WarmupInfo {
    bool attempted = false;
    bool main_ready = false;    // 1-token chat call returned text
    bool embed_ready = false;   // tiny embed call returned a vector
    bool main_resident = false; // main model present in /api/ps afterwards
    bool embed_on_cpu = false;  // embed model present in /api/ps with size_vram == 0
    long long main_load_ms = 0;
    long long embed_load_ms = 0;
    std::string note;
};

struct AnswerResult {
    std::string answer;                 // final text (labels + footer included)
    std::vector<Chunk> used_chunks;     // chunks that made it into the context
    std::vector<std::string> knowledge_ids;  // Tier 2 entries used
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
    bool used_memory = false;           // a stored fact influenced the answer
    bool computed = false;              // arithmetic / feed calculator result
    bool general_answer = false;        // answered without PDF context
    bool species_no_source = false;     // asked species has no source (Tier 4)
    bool safety_warning = false;        // UI shows the veterinary badge
    std::string warning;                // pipeline-level warning ("" = none)

    nlohmann::json trace;               // debug trace (D1); empty when off
    nlohmann::json sources_json() const;  // REST-friendly source list
};

class RagPipeline {
public:
    using StreamFn = std::function<void(const std::string&)>;
    // Phase notifications for the UI ("selfcheck" -> show "Yoxlanılır...").
    using StatusFn = std::function<void(const std::string&)>;

    RagPipeline(const Config& cfg, OllamaClient& ollama, VectorStorePtr store,
                Bm25Index& bm25, WebSearch& web, Retriever& retriever,
                Reranker& reranker, SessionManager& sessions,
                ConversationStore* conversations = nullptr);

    // Checks Ollama, resolves chat/helper models with fallbacks.
    // Returns false when Ollama is unreachable (clear message in `err`).
    bool initialize(std::string* err);

    // Document-level species labels, built from the whole index at load time
    // and rebuilt after an ingest so a re-indexed book gets a fresh label.
    void refresh_document_species();

    // Preloads the main model (num_predict=1, identical options to a real
    // draft) and bge-m3 (CPU), then verifies both through /api/ps.
    // Called at startup BEFORE the server reports ready.
    bool warmup(std::string* err = nullptr);
    const WarmupInfo& warmup_info() const { return warmup_; }
    std::vector<RunningModel> running_models(std::string* err = nullptr) const {
        return ollama_.running_models(err);
    }

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

    // Memory-aware variant: `mem` scopes the stored facts/conversation.
    AnswerResult answer(const std::string& question,
                        const std::string& session_id,
                        const MemoryRef& mem,
                        const StreamFn& on_delta,
                        const StatusFn& on_status = nullptr) const;

private:
    // The real pipeline; the public answer() adds deterministic text hygiene.
    AnswerResult answer_impl(const std::string& question,
                             const std::string& session_id,
                             const MemoryRef& mem,
                             const StreamFn& on_delta,
                             const StatusFn& on_status) const;

    std::vector<Chunk> rerank_or_trim(const std::string& question,
                                      std::vector<Chunk> candidates,
                                      QuestionType qtype,
                                      float score_margin,
                                      std::string* err) const;

    std::vector<Chunk> map_reduce_summarize(const std::string& question,
                                            const std::vector<Chunk>& chunks) const;

    // Caps chunks per source (max 3 per PDF, 2 per web URL) and drops
    // near-duplicate embeddings (cosine > 0.95) so the same text is never
    // cited twice while keeping source diversity.
    std::vector<Chunk> diversify(const std::vector<Chunk>& chunks) const;

    // Word-validity guard: one vocabulary built from the indexed chunks, so a
    // garbled 4B answer (non-words) can be detected and retried.
    void ensure_vocabulary() const;
    AzWordStats unknown_word_stats(const std::string& text) const;

    mutable std::unordered_set<std::string> vocab_;
    mutable std::unordered_set<std::string> vocab_prefixes_;
    mutable std::once_flag vocab_once_;

    // source -> species label of the whole document ("species-specific" vs
    // "multi-species"). Empty until refresh_document_species() runs; empty at
    // answer time means the chunk-level guard rule is used.
    std::map<std::string, DocSpeciesLabel> doc_species_;

    const Config& cfg_;
    OllamaClient& ollama_;
    VectorStorePtr store_;
    Bm25Index& bm25_;
    WebSearch& web_;
    Retriever& retriever_;
    Reranker& reranker_;
    SessionManager& sessions_;
    ConversationStore* conversations_ = nullptr;  // optional persistent memory

    bool ollama_ok_ = false;
    std::string chat_model_;       // "models.draft" - main answer model
    mutable KnowledgeBase knowledge_;   // Tier 2 curated knowledge (hot-reloaded)
    std::string helper_model_;     // "models.helper" - expand/rerank/summarize
    std::string selfcheck_model_;  // "models.selfcheck" - fact-check pass
    std::string embedding_model_;
    WarmupInfo warmup_;
};

}  // namespace dumy
