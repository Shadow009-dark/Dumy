// retriever.cpp - hybrid retrieval implementation.
#include "dumy/retriever.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

#include "dumy/chunker.h"
#include "dumy/az_quality.h"
#include "dumy/common.h"
#include "dumy/meta_commands.h"

namespace dumy {

const char* question_type_name(QuestionType t) {
    switch (t) {
        case QuestionType::SimpleFact: return "simple_fact";
        case QuestionType::BroadTopic: return "broad_topic";
        case QuestionType::Diagnosis: return "diagnosis";
        case QuestionType::Comparison: return "comparison";
        case QuestionType::General: return "general";
    }
    return "simple_fact";
}

std::vector<int> rrf_fuse(const std::vector<std::vector<int>>& lists, size_t k) {
    constexpr double RRK = 60.0;
    std::unordered_map<int, double> scores;
    for (const auto& list : lists) {
        for (size_t rank = 0; rank < list.size(); ++rank) {
            scores[list[rank]] += 1.0 / (RRK + static_cast<double>(rank) + 1.0);
        }
    }
    std::vector<std::pair<int, double>> ranked(scores.begin(), scores.end());
    std::sort(ranked.begin(), ranked.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    std::vector<int> out;
    for (size_t i = 0; i < ranked.size() && i < k; ++i) out.push_back(ranked[i].first);
    return out;
}

namespace {

bool contains_any(const std::string& text, const std::vector<const char*>& words) {
    for (const char* w : words)
        if (text.find(w) != std::string::npos) return true;
    return false;
}

}  // namespace

Retriever::Retriever(const Config& cfg, const OllamaClient& ollama, VectorStorePtr store,
                     const Bm25Index* bm25, WebSearch* web)
    : cfg_(cfg), ollama_(ollama), store_(std::move(store)), bm25_(bm25), web_(web) {}

void Retriever::set_models(const std::string& chat_model, const std::string& helper_model) {
    chat_model_ = chat_model;
    helper_model_ = helper_model;
}

QuestionType Retriever::analyze(const std::string& question) const {
    const std::string q = " " + to_lower_utf8(question) + " ";

    // A bare topic ("... haqqında", "... barədə") is an implicit request for
    // an overview, not a factual question and never a claim.
    if (is_topic_fragment(question)) return QuestionType::BroadTopic;

    // Comparison first ("fərq", "müqayisə", "vs").
    if (contains_any(q, {"fərq", "müqayis", " müqayis", "vs", " yoxsa ", " nə yaxşı",
                         "difference", "compare", "versus", "better than",
                         " üstünlüy", " hansı daha yaxşı"}))
        return QuestionType::Comparison;

    // Broad topic ("bütün", "hamısı", "nələrdir", "all", "every").
    if (contains_any(q, {"bütün", "butun", "hamısı", "hamisi", " ətraflı", "etrafli",
                         "nələrdir", "nelerdir", "hansılardır", "hansilardir",
                         "siyahı", "siyahi", "toplu", "müxtəlif", "muxtelif",
                         " hər ", "her bir", "all the", "every ", "list of",
                         "comprehensive", "overview", "everything", "in general",
                         " generall", "wide range"}))
        return QuestionType::BroadTopic;

    // Diagnosis / treatment (safety warnings apply) - use config keywords.
    if (cfg_.diagnosis_keywords.empty()) {
        // Fallback to hardcoded list if config is empty
        if (contains_any(q, {"xəstəlik", "xestelik", "xəstə", "xeste", "simptom", "əlamət",
                             "elamet", "diaqnoz", "müalicə", "mualice", "niyə öl", "niye ol",
                             "nə üçün", "ne ucun", "ağrır", "agrir", "qızdır", "qizdir",
                             "antibiot", "peyvənd", "peyvend", "vaksin", "dərman", "derman",
                             "doza", "dose", "səbəb", "sebeb", "symptom", "disease",
                             "treatment", "sick", "mortality", "diagnos"}))
            return QuestionType::Diagnosis;
    } else {
        // Convert config keywords to vector<const char*> for contains_any
        std::vector<const char*> diag_keywords;
        diag_keywords.reserve(cfg_.diagnosis_keywords.size());
        for (const auto& kw : cfg_.diagnosis_keywords) {
            diag_keywords.push_back(kw.c_str());
        }
        if (contains_any(q, diag_keywords))
            return QuestionType::Diagnosis;
    }

    // Everyday/general questions are usually short and not agriculture-related.
    // STEP 5: one shared list (has_agri_keyword in az_quality.h). The local
    // copy here missed dana/buzov/toyuq/keci/ari/quzu/at/camis/qaramal/
    // mal-qara/ordek/hinduska, so "Dana ishalı olduqda nə etmək lazımdır?"
    // was routed as a general question and answered from the model's memory.
    if (!has_agri_keyword(q)) return QuestionType::General;

    return QuestionType::SimpleFact;
}

int Retriever::top_k_for(QuestionType t) const {
    switch (t) {
        case QuestionType::BroadTopic: return cfg_.top_k_broad;
        case QuestionType::Diagnosis: return std::max(cfg_.top_k_simple * 2, 12);
        case QuestionType::Comparison: return std::max(cfg_.top_k_simple + 4, 10);
        case QuestionType::General: return cfg_.top_k_simple;
        case QuestionType::SimpleFact: break;
    }
    return cfg_.top_k_simple;
}

std::vector<float> Retriever::embed_query(const std::string& text) const {
    std::vector<std::vector<float>> out;
    std::string err;
    ChatTiming emb_timing;
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = ollama_.embed(cfg_.embedding_model, {text}, out, &err, &emb_timing);
    const long long wall_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - t0)
                                  .count();
    log_model_call("retrieval.embed", cfg_.embedding_model, wall_ms, emb_timing.load_ms,
                   emb_timing.prompt_eval_ms, emb_timing.eval_ms, emb_timing.prompt_tokens,
                   emb_timing.eval_tokens);
    if (!ok) {
        if (!err.empty()) log_warn("query embedding failed: " + err);
        return {};
    }
    if (out.empty()) return {};
    return out[0];
}

std::vector<std::string> Retriever::expand_query(const std::string& question,
                                                 QuestionType t) const {
    std::vector<std::string> out;
    out.push_back(question);

    // Expansion matters most for broad/diagnosis questions; for simple facts
    // one short extra call is enough.
    const std::string model = helper_model_.empty() ? chat_model_ : helper_model_;
    if (model.empty()) return out;

    const std::string sys =
        "You are a JSON-only search-query expander for an agriculture knowledge "
        "base. Output ONLY a JSON array of strings - never prose, never "
        "explanations, never thinking.";
    std::string user = "Question: " + question +
                       "\nTopic class: " + question_type_name(t) +
                       "\nProduce 4 shorter, more specific sub-queries covering the "
                       "whole topic (split by animal species or subtopic, include "
                       "synonyms, BOTH Azerbaijani and English terms).\n"
                       "Example: \"all animal diseases\" -> [\"qoyun xəstəlikləri\", "
                       "\"mal xəstəlikləri\", \"poultry diseases\", "
                       "\"infectious livestock diseases\"]\n"
                       "QAYDA: Mürəkkəb sözü HİSSƏLƏRƏ BÖLMƏ (məsələn \"şəkərçörək\" -> \"şəkər\" YOX; tam sözü saxla).\n"
                       "Reply with ONLY the JSON array.";

    // Centralized options: with helper == draft model a different num_ctx
    // would make Ollama reload the 7B before the draft.
    ChatOptions opts = cfg_.chat_options(320, 0.2);

    std::string err;
    // Tolerant JSON array extraction: first '[' ... last ']'.
    auto extract = [](const std::string& text) {
        std::vector<std::string> subs;
        size_t lb = text.find('[');
        size_t rb = text.rfind(']');
        if (lb == std::string::npos || rb == std::string::npos || rb <= lb) return subs;
        try {
            auto arr = nlohmann::json::parse(text.substr(lb, rb - lb + 1));
            auto push = [&subs](const nlohmann::json& v) {
                if (!v.is_string()) return;
                std::string s = trim(v.get<std::string>());
                if (!s.empty() && s.size() <= 200) subs.push_back(std::move(s));
            };
            for (const auto& item : arr) {
                // Small models like qwen2.5:3b answer with an array of PAIRS
                // [["az","en"], ...] or objects {"query": "..."} instead of a
                // flat string array - flatten those too.
                if (item.is_array()) {
                    for (const auto& sub : item) push(sub);
                } else if (item.is_object()) {
                    for (const auto& el : item.items()) {
                        if (el.value().is_string()) push(el.value());
                    }
                } else {
                    push(item);
                }
            }
        } catch (const std::exception& e) {
            log_warn(std::string("query expansion JSON error: ") + e.what());
        }
        return subs;
    };

    const auto t0 = std::chrono::steady_clock::now();
    ChatTiming exp_timing;
    std::string reply = ollama_.chat(model,
                                     {{"system", sys}, {"user", user}}, opts, &err,
                                     &exp_timing);
    std::vector<std::string> subs = extract(reply);
    if (subs.size() < 2 && !reply.empty()) {
        // Small models sometimes answer with prose or a single query:
        // one sharp retry asking for at least 3 sub-queries.
        reply = ollama_.chat(
            model,
            {{"system", sys},
             {"user", user + "\nREMINDER: reply with ONLY a JSON array of AT "
                            "LEAST 3 strings, e.g. [\"qoyun xəstəlikləri\", "
                            "\"cattle diseases\", \"poultry diseases\"]."}},
            opts, &err, &exp_timing);
        subs = extract(reply);
    }

    // Never let expansion split a compound question word: "şəkərçörək" must
    // not become the much broader "şəkər" (that is how an agriculture PDF
    // about sugar beet ended up as a citation for a pastry recipe).
    {
        const std::vector<std::string> qwords = az_words(question);
        std::vector<std::string> kept_subs;
        kept_subs.reserve(subs.size());
        for (const auto& s : subs) {
            bool splits = false;
            for (const auto& t : az_words(s)) {
                if (t.size() < 4) continue;
                for (const auto& w : qwords) {
                    if (w.size() < t.size() + 3 || w == t) continue;
                    if (w.rfind(t, 0) != 0) continue;
                    if (!az_is_inflection(w.substr(t.size()))) {
                        splits = true;
                        break;
                    }
                }
                if (splits) break;
            }
            if (splits) {
                log_warn("query expansion: dropped sub-query '" + s +
                         "' (splits a compound word of the question)");
                continue;
            }
            kept_subs.push_back(s);
        }
        subs.swap(kept_subs);
    }
    log_model_call("expand", model,
                   std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - t0)
                       .count(),
                   exp_timing.load_ms, exp_timing.prompt_eval_ms, exp_timing.eval_ms,
                   exp_timing.prompt_tokens, exp_timing.eval_tokens);
    if (reply.empty()) {
        log_warn("query expansion failed (" + err + ") - using original query only");
        return out;
    }
    std::unordered_set<std::string> seen;
    seen.insert(to_lower_utf8(question));
    for (const auto& s : subs) {
        if (!seen.insert(to_lower_utf8(s)).second) continue;
        out.push_back(s);
        if (out.size() >= 6) break;  // original + up to 5 sub-queries
    }
    if (out.size() == 1) {
        log_warn("query expansion produced no sub-queries - using original query only");
    } else if (out.size() < 3 &&
               (t == QuestionType::BroadTopic || t == QuestionType::Diagnosis)) {
        log_warn("query expansion produced only " + std::to_string(out.size() - 1) +
                 " sub-queries for a broad/diagnosis question");
    }
    return out;
}

Retriever::StoreResult Retriever::retrieve_store(const std::vector<std::string>& queries,
                                                  size_t k) const {
    StoreResult result;
    const size_t per_query = std::max<size_t>(k * 2, 20);
    std::vector<std::vector<int>> lists;

    // Track query embeddings for max_score computation
    std::vector<float> first_qv;

    for (size_t qi = 0; qi < queries.size(); ++qi) {
        const auto& q = queries[qi];
        // (a) vector side
        std::vector<float> qv = embed_query(q);
        if (!qv.empty()) {
            if (first_qv.empty()) first_qv = qv;  // keep first query embedding for scoring
            auto hits = store_->search(qv, per_query);
            std::vector<int> ids;
            ids.reserve(hits.size());
            for (auto& h : hits) ids.push_back(h.first);
            if (!ids.empty()) lists.push_back(std::move(ids));
        }
        // (b) BM25 side
        if (bm25_) {
            auto ids = bm25_->search(q, per_query);
            if (!ids.empty()) lists.push_back(std::move(ids));
        }
    }

    if (lists.empty()) return result;
    std::vector<int> fused = rrf_fuse(lists, k);
    if (fused.empty()) return result;
    result.chunks = store_->get(fused);

    // --- Compute max raw vector cosine (top-1) for the retrieval guard and
    // the top-1/top-k margin used to decide whether LLM reranking is needed.
    if (!first_qv.empty() && !result.chunks.empty()) {
        InMemoryVectorStore::normalize(first_qv);
        std::vector<float> sims;
        sims.reserve(result.chunks.size());
        for (const auto& c : result.chunks) {
            if (c.embedding.empty()) continue;
            double dot = 0;
            size_t n = std::min(first_qv.size(), c.embedding.size());
            for (size_t i = 0; i < n; ++i) dot += double(first_qv[i]) * c.embedding[i];
            sims.push_back(static_cast<float>(dot));
        }
        if (!sims.empty()) {
            std::sort(sims.begin(), sims.end(), std::greater<float>());
            result.max_score = sims.front();
            const size_t cut = std::min<size_t>(k > 0 ? k : 1, sims.size()) - 1;
            result.score_margin = sims.front() - sims[cut];
        }
    }

    return result;
}

std::vector<Chunk> Retriever::retrieve_web(const std::string& question,
                                           const std::vector<std::string>& queries,
                                           size_t k, std::string* warning) const {
    if (!cfg_.web_search_enabled || !web_) {
        if (warning) *warning = "web search disabled in config";
        return {};
    }

    std::string err;
    std::vector<WebResult> results = web_->search(queries, &err);
    if (results.empty()) {
        if (warning) *warning = err.empty() ? "no web results" : err;
        return {};
    }

    std::vector<WebPage> pages = web_->fetch_pages(results, cfg_.web_max_pages);
    if (pages.empty()) {
        if (warning) *warning = "web results found but pages could not be downloaded";
        return {};
    }

    // Chunk every page, tagging metadata (URL = source of truth for labels).
    std::vector<Chunk> chunks;
    for (const auto& page : pages) {
        auto texts = chunk_text(page.text, static_cast<size_t>(cfg_.chunk_size),
                                static_cast<size_t>(cfg_.chunk_overlap));
        size_t idx = 0;
        for (auto& t : texts) {
            Chunk c;
            c.kind = "web";
            c.url = page.result.url;
            c.title = page.result.title;
            c.source = page.result.url;
            c.page = 0;
            c.trusted = web_->is_trusted(page.result.url);
            c.text = std::move(t);
            chunks.push_back(std::move(c));
            // At most 4 chunks per page: first pages carry the core content.
            if (++idx >= 4) break;
        }
        if (chunks.size() >= 48) break;  // hard cap for context + latency
    }
    if (chunks.empty()) {
        if (warning) *warning = "downloaded pages contained no usable text";
        return {};
    }

    // Embeddings help rank web chunks too; failure is not fatal (BM25 still works).
    {
        std::vector<std::string> texts;
        texts.reserve(chunks.size());
        for (const auto& c : chunks) texts.push_back(c.text);
        std::vector<std::vector<float>> embs;
        std::string emb_err;
        ChatTiming web_emb_timing;
        const auto t0 = std::chrono::steady_clock::now();
        const bool emb_ok =
            ollama_.embed(cfg_.embedding_model, texts, embs, &emb_err, &web_emb_timing);
        log_model_call("retrieval.web_embed", cfg_.embedding_model,
                       std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - t0)
                           .count(),
                       web_emb_timing.load_ms, web_emb_timing.prompt_eval_ms,
                       web_emb_timing.eval_ms, web_emb_timing.prompt_tokens,
                       web_emb_timing.eval_tokens);
        if (emb_ok && embs.size() == chunks.size()) {
            for (size_t i = 0; i < chunks.size(); ++i)
                chunks[i].embedding = std::move(embs[i]);
        } else {
            log_warn("web chunk embedding failed (BM25-only ranking): " + emb_err);
        }
    }

    // Rank web chunks with the same hybrid recipe (temp BM25 + cosine + RRF).
    InMemoryVectorStore::assign_ids(chunks, -1000000);
    Bm25Index tmp_bm25;
    tmp_bm25.build(chunks);

    std::vector<std::vector<int>> lists;
    for (const auto& q : queries) {
        auto ids = tmp_bm25.search(q, 40);
        if (!ids.empty()) lists.push_back(std::move(ids));

        std::vector<float> qv = embed_query(q);
        if (!qv.empty()) {
            InMemoryVectorStore::normalize(qv);
            std::vector<std::pair<int, float>> scores;
            for (const auto& c : chunks) {
                if (c.embedding.empty()) continue;
                double dot = 0;
                size_t n = std::min(qv.size(), c.embedding.size());
                for (size_t i = 0; i < n; ++i) dot += double(qv[i]) * c.embedding[i];
                scores.emplace_back(c.id, float(dot));
            }
            std::sort(scores.begin(), scores.end(),
                      [](const auto& a, const auto& b) { return a.second > b.second; });
            std::vector<int> ids2;
            for (size_t i = 0; i < scores.size() && i < 40; ++i)
                ids2.push_back(scores[i].first);
            if (!ids2.empty()) lists.push_back(std::move(ids2));
        }
    }
    if (lists.empty()) return {};

    std::vector<int> fused = rrf_fuse(lists, k);
    std::unordered_map<int, size_t> pos;
    for (size_t i = 0; i < chunks.size(); ++i) pos[chunks[i].id] = i;

    std::vector<Chunk> ranked;
    for (int id : fused) {
        auto it = pos.find(id);
        if (it != pos.end()) ranked.push_back(chunks[it->second]);
    }
    if (warning) warning->clear();
    return ranked;
}

RetrievalOutcome Retriever::retrieve(const std::string& question) const {
    RetrievalOutcome out;
    out.qtype = analyze(question);
    out.top_k = static_cast<size_t>(top_k_for(out.qtype)) > 0
                    ? top_k_for(out.qtype)
                    : cfg_.top_k_simple;

    out.queries = expand_query(question, out.qtype);
    out.expansion_ok = out.queries.size() > 1;

    // Both retrievals are started by the pipeline in parallel; this
    // convenience path runs them sequentially (used in console diagnostics).
    StoreResult store_result = retrieve_store(out.queries, static_cast<size_t>(out.top_k));
    out.pdf_chunks = std::move(store_result.chunks);
    out.max_score = store_result.max_score;
    out.score_margin = store_result.score_margin;
    out.web_chunks = retrieve_web(question, out.queries,
                                  static_cast<size_t>(out.top_k),
                                  &out.web_warning);
    return out;
}

}  // namespace dumy
