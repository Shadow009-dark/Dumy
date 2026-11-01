// retriever.h - hybrid retrieval: cosine vectors + BM25 + Reciprocal Rank
// Fusion, LLM query expansion, question-type analysis, and parallel web
// retrieval that produces web chunks merged into the same context.
#pragma once

#include <string>
#include <vector>

#include "dumy/bm25_index.h"
#include "dumy/config.h"
#include "dumy/ollama_client.h"
#include "dumy/vector_store.h"
#include "dumy/web_search.h"

namespace dumy {

enum class QuestionType {
    SimpleFact,   // short, narrow fact -> small top-k, fast path
    BroadTopic,   // "bütün xəstəliklər" -> large top-k + expansion + map-reduce
    Diagnosis,    // symptoms / disease / treatment -> safety warnings apply
    Comparison,   // A vs B
    General,      // everyday question, little dependence on PDFs
};

const char* question_type_name(QuestionType t);

struct RetrievalOutcome {
    QuestionType qtype = QuestionType::SimpleFact;
    int top_k = 6;
    std::vector<std::string> queries;      // original + expanded sub-queries
    std::vector<Chunk> pdf_chunks;         // hybrid PDF store hits
    std::vector<Chunk> web_chunks;         // web hits (already ranked)
    std::string web_warning;               // why web failed ("" = fine)
    bool expansion_ok = false;
};

class Retriever {
public:
    Retriever(const Config& cfg, const OllamaClient& ollama, VectorStorePtr store,
              const Bm25Index* bm25, WebSearch* web);

    // The models actually in use (resolved by the pipeline with fallbacks).
    void set_models(const std::string& chat_model, const std::string& helper_model);

    // Keyword heuristic classification (no LLM call - keeps latency low).
    QuestionType analyze(const std::string& question) const;
    int top_k_for(QuestionType t) const;

    // Asks the LLM for 3-5 more specific sub-queries (az + en). On any
    // failure returns just the original query - retrieval never breaks.
    std::vector<std::string> expand_query(const std::string& question,
                                          QuestionType t) const;

    // Vector + BM25 + RRF over the PDF/web store. Returns up to k chunks.
    std::vector<Chunk> retrieve_store(const std::vector<std::string>& queries,
                                      size_t k) const;

    // Web search in parallel with PDF retrieval: search -> download ->
    // clean -> chunk -> embed -> hybrid rank. Never throws; failures are
    // reported through `warning` so the pipeline can tell the user.
    std::vector<Chunk> retrieve_web(const std::string& question,
                                    const std::vector<std::string>& queries,
                                    size_t k, std::string* warning) const;

    // Convenience: runs analysis + expansion + both retrievals.
    RetrievalOutcome retrieve(const std::string& question) const;

    // Embeds one query string ("" on failure).
    std::vector<float> embed_query(const std::string& text) const;

private:
    const Config& cfg_;
    const OllamaClient& ollama_;
    VectorStorePtr store_;
    const Bm25Index* bm25_;
    WebSearch* web_;
    std::string chat_model_;
    std::string helper_model_;
};

// Reciprocal Rank Fusion over several ranked id lists (k = 60, standard).
// `lists` entries are ids in best-first order; returns ids best-first.
std::vector<int> rrf_fuse(const std::vector<std::vector<int>>& lists, size_t k);

}  // namespace dumy
