// rag_pipeline.cpp - orchestrates the whole answer quality pipeline.
#include "dumy/rag_pipeline.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <future>
#include <set>
#include <sstream>
#include <unordered_map>

#include "dumy/chunker.h"
#include "dumy/common.h"
#include "dumy/prompts.h"

namespace dumy {
namespace {

// Per-stage timing helper (logged so slow stages are visible on the GPU).
struct StageTimer {
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    double ms() const {
        return std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now() - t0)
            .count();
    }
};

// Result for a single sub-question.
struct SingleAnswer {
    std::string answer;
    std::vector<Chunk> used_chunks;
    std::string qtype;
    bool refused = false;
    bool used_web = false;
    bool used_map_reduce = false;
    bool used_self_check = false;
    bool self_check_failed = false;
    int self_check_retries = 0;
    bool from_own_knowledge = false;
    bool no_sources = false;
    bool smalltalk = false;
    std::string warning;
};

// Processes a single question through the full pipeline.
// Returns a SingleAnswer with all metadata.
SingleAnswer process_single_question(const std::string& question,
                                     const std::string& session_id,
                                     const Config& cfg,
                                     OllamaClient& ollama,
                                     VectorStorePtr store,
                                     Bm25Index& bm25,
                                     WebSearch& web,
                                     Retriever& retriever,
                                     Reranker& reranker,
                                     SessionManager& sessions,
                                     const std::string& chat_model,
                                     const std::string& helper_model,
                                     const std::string& selfcheck_model,
                                     const std::string& embedding_model,
                                     const std::function<void(const std::string&)>& on_delta,
                                     const std::function<void(const std::string&)>& on_status);

// Removes stray map-reduce summary labels ([Xülasə: ...]) that a model may
// still cite despite the prompt; only [Mənbə:], [İnternet:], [Ümumi bilik]
// are allowed in final answers.
std::string strip_summary_labels(std::string s) {
    for (;;) {
        size_t open = s.find("[Xülasə");
        if (open == std::string::npos) break;
        size_t close = s.find(']', open);
        if (close == std::string::npos) { s.erase(open); break; }
        s.erase(open, close - open + 1);
    }
    return s;
}

// URLs must always use [İnternet: ...]; models sometimes mislabel them
// as [Mənbə: https://...] or [Mənbə: İnternet, https://...] - correct that
// in the final answer.
std::string fix_url_labels(std::string s) {
    const std::string open = "[Mənbə: ";
    size_t pos = 0;
    while ((pos = s.find(open, pos)) != std::string::npos) {
        const size_t inner = pos + open.size();
        const size_t close = s.find(']', inner);
        if (close == std::string::npos) break;
        const std::string content = s.substr(inner, close - inner);
        if (content.find("://") == std::string::npos) {
            pos = close + 1;  // a normal PDF label - leave it alone
            continue;
        }
        // Strip a redundant "İnternet,"/"internet:" style prefix.
        std::string url_part = content;
        static const char* junk[] = {"İnternet, ", "İnternet: ", "İnternet ",
                                     "internet, ", "internet: ", "internet "};
        for (const char* j : junk) {
            const std::string js(j);
            if (url_part.rfind(js, 0) == 0) {
                url_part.erase(0, js.size());
                break;
            }
        }
        s.replace(pos, close - pos + 1, "[İnternet: " + url_part + "]");
        pos += 11;  // past "[İnternet: "
    }
    return s;
}

std::string strip_latest(const std::string& model) {
    if (ends_with(model, ":latest")) return model.substr(0, model.size() - 7);
    return model;
}



// Honesty guard: a [Mənbə: ...] / [İnternet: ...] label must point at a
// source that was actually in the context. Models occasionally invent URLs
// ("example.com/...") or cite files that were never provided - those labels
// become [Ümumi bilik] instead of pretending to be sourced.
std::string validate_source_labels(const std::string& in,
                                   const std::vector<Chunk>& used) {
    std::vector<std::string> files, urls;
    for (const auto& c : used) {
        if (c.kind == "web") {
            urls.push_back(c.url.empty() ? c.source : c.url);
        } else {
            std::string name = c.source;
            size_t slash = name.find_last_of("/\\");
            if (slash != std::string::npos) name = name.substr(slash + 1);
            files.push_back(name);
        }
    }
    auto known_url = [&urls](const std::string& u) {
        for (const auto& k : urls) {
            if (k.empty()) continue;
            if (u == k || k.rfind(u, 0) == 0 || u.rfind(k, 0) == 0) return true;
        }
        return false;
    };
    auto known_file = [&files](const std::string& content) {
        for (const auto& k : files) {
            if (!k.empty() && content.find(k) != std::string::npos) return true;
        }
        return false;
    };

    std::string out = in;
    const std::string openers[] = {"[Mənbə: ", "[İnternet: "};
    for (const std::string& open : openers) {
        size_t pos = 0;
        while ((pos = out.find(open, pos)) != std::string::npos) {
            const size_t inner = pos + open.size();
            const size_t close = out.find(']', inner);
            if (close == std::string::npos) break;
            const std::string content = out.substr(inner, close - inner);
            bool ok;
            if (content.rfind("http", 0) == 0) {
                ok = known_url(content);
            } else {
                ok = known_file(content);
            }
            if (!ok) {
                out.replace(pos, close - pos + 1, "[Ümumi bilik]");
                pos += 12;
            } else {
                pos = close + 1;
            }
        }
    }
    return out;
}

bool looks_like_embedding_model(const std::string& name) {
    const std::string l = to_lower_utf8(name);
    return l.find("embed") != std::string::npos || l.find("bge") != std::string::npos ||
           l.find("gte") != std::string::npos || l.find("minilm") != std::string::npos;
}

// Word-set Jaccard similarity (words of >= 4 bytes) used to sanity-check the
// self-check correction: a genuine correction keeps most of the draft's
// content, while a hallucinated rewrite shares almost nothing. Protects the
// answer from being replaced by an off-topic regeneration.
double word_jaccard(const std::string& a, const std::string& b) {
    auto tok = [](const std::string& s) {
        std::set<std::string> t;
        std::string cur;
        for (char c : to_lower_utf8(s)) {
            const unsigned char u = static_cast<unsigned char>(c);
            if (std::isalnum(u) || u >= 0x80)
                cur.push_back(c);
            else {
                if (cur.size() >= 4) t.insert(cur);
                cur.clear();
            }
        }
        if (cur.size() >= 4) t.insert(cur);
        return t;
    };
    const auto A = tok(a);
    const auto B = tok(b);
    if (A.empty() || B.empty()) return 0.0;
    size_t inter = 0;
    for (const auto& w : A)
        if (B.count(w)) ++inter;
    const size_t uni = A.size() + B.size() - inter;
    return uni ? static_cast<double>(inter) / static_cast<double>(uni) : 0.0;
}

size_t total_tokens(const std::vector<Chunk>& chunks) {
    size_t t = 0;
    for (const auto& c : chunks) t += estimate_tokens(c.text);
    return t;
}

// (The sources footer is no longer appended to the answer text: the UI shows
// the sources in its own collapsible "Mənbələr" list, fed by sources_json().
// Models that still write their own list are stripped by strip_own_sources.)

// Sources actually cited inside the answer text: a chunk counts as cited
// when its identity (PDF base name / URL) appears anywhere in the answer,
// so slight label variations ("səh. 203-204") still match. Everything else
// is NOT attached to the sources list.
std::vector<Chunk> cited_chunks(const std::string& answer,
                                const std::vector<Chunk>& used) {
    std::vector<Chunk> out;
    for (const auto& c : used) {
        std::string key;
        if (c.kind == "web") {
            key = c.url.empty() ? c.source : c.url;
        } else {
            key = c.source;
            size_t slash = key.find_last_of("/\\");
            if (slash != std::string::npos) key = key.substr(slash + 1);
        }
        if (key.empty()) continue;
        if (answer.find(key) != std::string::npos) out.push_back(c);
    }
    return out;
}

}  // namespace

nlohmann::json AnswerResult::sources_json() const {
    nlohmann::json arr = nlohmann::json::array();
    std::vector<std::string> seen;
    for (const auto& c : used_chunks) {
        const std::string lbl = c.label();
        if (std::find(seen.begin(), seen.end(), lbl) != seen.end()) continue;
        seen.push_back(lbl);
        arr.push_back({{"label", lbl},
                       {"type", c.kind},
                       {"source", c.source},
                       {"page", c.page},
                       {"url", c.url},
                       {"trusted", c.trusted}});
        if (arr.size() >= 14) break;
    }
    // Sources are ONLY what the answer actually cites. Small-talk / no-source
    // turns return an empty list (UI hides the collapsible section).
    if (arr.empty() && no_sources) return nlohmann::json::array();
    if (arr.empty()) {
        arr.push_back({{"label", "[Ümumi bilik]"},
                       {"type", "knowledge"},
                       {"source", ""},
                       {"page", 0},
                       {"url", ""},
                       {"trusted", false}});
    }
    return arr;
}

RagPipeline::RagPipeline(const Config& cfg, OllamaClient& ollama, VectorStorePtr store,
                         Bm25Index& bm25, WebSearch& web, Retriever& retriever,
                         Reranker& reranker, SessionManager& sessions)
    : cfg_(cfg),
      ollama_(ollama),
      store_(std::move(store)),
      bm25_(bm25),
      web_(web),
      retriever_(retriever),
      reranker_(reranker),
      sessions_(sessions) {
    embedding_model_ = cfg_.embedding_model;
}

bool RagPipeline::initialize(std::string* err) {
    std::string perr;
    if (!ollama_.ping(&perr)) {
        ollama_ok_ = false;
        if (err) {
            *err = "Ollama serveri işə düşməyib (" + cfg_.ollama_url + "): " + perr +
                   ". Ollama proqramını işə salın və ya 'ollama serve' çalışdırın.";
        }
        return false;
    }

    std::string lerr;
    const std::vector<std::string> models = ollama_.list_models(&lerr);
    auto present = [&models](const std::string& name) {
        if (name.empty()) return false;
        for (const auto& m : models) {
            if (m == name || strip_latest(m) == strip_latest(name)) return true;
        }
        return false;
    };

    chat_model_ = cfg_.chat_model;
    if (!present(chat_model_)) {
        log_warn("Model '" + cfg_.chat_model +
                 "' quraşdırılmayıb - fallback modellərə baxılıram");
        bool found = false;
        if (present(cfg_.fallback_model)) {
            chat_model_ = cfg_.fallback_model;
            found = true;
        }
        if (!found) {
            for (const auto& fb : cfg_.model_fallbacks) {
                if (present(fb)) {
                    chat_model_ = fb;
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            for (const auto& m : models) {
                if (!looks_like_embedding_model(m)) {
                    chat_model_ = m;
                    found = true;
                    break;
                }
            }
        }
        if (!found && models.empty()) {
            if (err) {
                *err = "Ollama-da heç bir model yoxdur. 'ollama pull qwen3:8b' "
                       "və 'ollama pull bge-m3' əmrlərini işə salın (scripts/pull_models).";
            }
            ollama_ok_ = false;
            return false;
        }
        log_warn("Çat modeli kimi '" + chat_model_ + "' istifadə olunur");
    }

    // --- small helper models (expansion/rerank/summarize + self-check): ----
    // configured name -> any installed small model -> the draft model.
    // Warns once per role; never fails.
    static const char* SMALL_HELPERS[] = {"qwen2.5:3b", "qwen3:4b", "llama3.2:3b",
                                          "qwen2.5:1.5b", "llama3.2:1b", "phi3.5:3.8b",
                                          "gemma2:2b"};
    auto resolve_small = [&](const std::string& configured, const char* role) {
        if (present(configured)) return configured;
        if (!configured.empty()) {
            log_warn(std::string(role) + " modeli '" + configured +
                     "' quraşdırılmayıb - kiçik model axtarılır");
        }
        for (const char* cand : SMALL_HELPERS) {
            if (present(cand)) return std::string(cand);
        }
        log_warn(std::string("Kiçik köməkçi model tapılmadı; ") + role +
                 " üçün çat modeli (" + chat_model_ + ") istifadə olunacaq");
        return chat_model_;
    };
    helper_model_ = resolve_small(cfg_.helper_model, "helper");
    selfcheck_model_ = resolve_small(
        cfg_.selfcheck_model.empty() ? cfg_.helper_model : cfg_.selfcheck_model,
        "selfcheck");

    embedding_model_ = cfg_.embedding_model;
    if (!present(embedding_model_)) {
        log_warn("Embedding model '" + embedding_model_ +
                 "' quraşdırılmayıb - PDF indeksləşməsi uğursuz olacaq. "
                 "Çalışdırın: ollama pull " + embedding_model_);
    }

    retriever_.set_models(chat_model_, helper_model_);
    reranker_.set_models(chat_model_, helper_model_);
    ollama_ok_ = true;
    log_info("Modellər: draft=" + chat_model_ + ", helper=" + helper_model_ +
             ", selfcheck=" + selfcheck_model_ + ", embed=" + embedding_model_);
    return true;
}

// Step 1: load both models before the server reports ready, with exactly the
// options a real request uses, then verify through /api/ps. This removes the
// 5-20 s model load from the FIRST question and from every later swap.
bool RagPipeline::warmup(std::string* err) {
    warmup_ = WarmupInfo();
    if (!ollama_ok_) {
        warmup_.note = "Ollama bağlı deyil - warm-up buraxıldı";
        if (err) *err = warmup_.note;
        return false;
    }
    warmup_.attempted = true;
    bool ok = true;

    // --- main model FIRST: 1 token, identical options to the draft request --
    // Order matters: loading the 7B evicts a model loaded earlier on this
    // 6 GB GPU, so the CPU-only embedding model is warmed after it.
    {
        std::string cerr;
        ChatTiming ct;
        StageTimer t;
        const std::string r = ollama_.chat(chat_model_, {{"user", "salam"}},
                                           cfg_.chat_options(1, 0.3), &cerr, &ct);
        warmup_.main_ready = !r.empty();
        warmup_.main_load_ms = ct.load_ms;
        log_model_call("warmup.draft", chat_model_,
                       static_cast<long long>(t.ms()), ct.load_ms,
                       ct.prompt_eval_ms, ct.eval_ms, ct.prompt_tokens,
                       ct.eval_tokens);
        if (!warmup_.main_ready) {
            warmup_.note = "main model warm-up failed: " + cerr;
            log_warn(warmup_.note);
            ok = false;
        }
    }

    // --- bge-m3 second: tiny embed call, num_gpu=0 (CPU), keep_alive -1 -----
    {
        std::vector<std::vector<float>> out;
        std::string eerr;
        ChatTiming et;
        StageTimer t;
        const bool eok = ollama_.embed(embedding_model_, {"salam"}, out, &eerr, &et);
        warmup_.embed_ready = eok && !out.empty();
        warmup_.embed_load_ms = et.load_ms;
        log_model_call("warmup.embed", embedding_model_,
                       static_cast<long long>(t.ms()), et.load_ms,
                       et.prompt_eval_ms, et.eval_ms, et.prompt_tokens,
                       et.eval_tokens);
        if (!warmup_.embed_ready) {
            warmup_.note = "embed warm-up failed: " + eerr;
            log_warn(warmup_.note);
            ok = false;
        }
    }

    // --- verify both are still resident (and bge-m3 is on CPU) -------------
    {
        std::string perr;
        const auto running = ollama_.running_models(&perr);
        auto same_model = [](const std::string& a, const std::string& b) {
            return a == b || strip_latest(a) == strip_latest(b);
        };
        bool embed_resident = false;
        for (const auto& m : running) {
            if (same_model(m.name, chat_model_)) warmup_.main_resident = true;
            if (same_model(m.name, embedding_model_)) {
                embed_resident = true;
                warmup_.embed_on_cpu = (m.size_vram == 0);
            }
        }
        if (running.empty()) {
            warmup_.note = "could not verify loaded models via /api/ps: " + perr;
            log_warn(warmup_.note);
        } else {
            if (!warmup_.main_resident) {
                log_warn("WARN: '" + chat_model_ +
                         "' yüklənməyib / yaddaşdan çıxarılıb (yenidən yüklənəcək)");
            }
            if (!embed_resident) {
                log_warn("WARN: embedding modeli '" + embedding_model_ +
                         "' warm-up-dan sonra yüklü deyil (ilk sorğuda yenidən yüklənəcək)");
            } else if (!warmup_.embed_on_cpu) {
                log_warn("WARN: embedding modeli '" + embedding_model_ +
                         "' GPU-da görünür - num_gpu=0 tətbiq olunmayıb");
            }
        }
        log_info("Warm-up: draft=" + chat_model_ +
                 " (load=" + std::to_string(warmup_.main_load_ms) + "ms, resident=" +
                 (warmup_.main_resident ? "1" : "0") + ") | embed=" +
                 embedding_model_ + " (load=" + std::to_string(warmup_.embed_load_ms) +
                 "ms, CPU=" + (warmup_.embed_on_cpu ? "1" : "0") + ")");
    }
    if (err && !ok) *err = warmup_.note;
    return ok;
}

std::vector<Chunk> RagPipeline::rerank_or_trim(const std::string& question,
                                               std::vector<Chunk> candidates,
                                               QuestionType qtype,
                                               float score_margin,
                                               std::string* err) const {
    const size_t keep = static_cast<size_t>(std::max(1, cfg_.rerank_top_n));

    // Step 4: when the vector ranking already separates the head clearly
    // (top-1 cosine is far ahead of the top-k tail), the LLM rerank call adds
    // 10-20 s and changes nothing - skip it and keep retrieval order.
    if (cfg_.rerank_skip_margin > 0.0f && score_margin >= cfg_.rerank_skip_margin) {
        // Keep the same cap the LLM reranker would have used (default 6): the
        // skipped call must not leave a larger context than the scored path,
        // otherwise prompt + num_predict can exceed num_ctx.
        const size_t skip_keep =
            std::min(keep, static_cast<size_t>(std::max(1, cfg_.rerank_max_candidates)));
        log_info("rerank skipped: cosine margin " + std::to_string(score_margin) +
                 " >= " + std::to_string(cfg_.rerank_skip_margin) + " (keep=" +
                 std::to_string(skip_keep) + ")");
        if (candidates.size() > skip_keep) candidates.resize(skip_keep);
        if (err) err->clear();
        return candidates;
    }

    const bool fast_simple = cfg_.fast_mode &&
                             (qtype == QuestionType::SimpleFact ||
                              qtype == QuestionType::General);
    if (fast_simple) {
        // fast mode: skip reranking - RRF order is already good.
        if (candidates.size() > static_cast<size_t>(cfg_.top_k_simple)) {
            candidates.resize(static_cast<size_t>(
                std::max(cfg_.top_k_simple, cfg_.rerank_top_n / 2)));
        }
        if (err) err->clear();
        return candidates;
    }
    return reranker_.rerank(question, candidates, keep, err);
}

std::vector<Chunk> RagPipeline::map_reduce_summarize(
    const std::string& question, const std::vector<Chunk>& chunks) const {
    // Split into groups so each summary fits the context budget.
    const size_t budget = static_cast<size_t>(std::max(cfg_.context_budget_tokens, 800));
    const size_t total = total_tokens(chunks);
    size_t group_count = (total + budget - 1) / budget;
    if (group_count < 2) group_count = 2;
    if (group_count > 5) group_count = 5;

    const size_t per_group = (chunks.size() + group_count - 1) / group_count;
    std::vector<Chunk> summaries;
    // Centralized options: same num_ctx/keep_alive as the draft call, so the
    // summary pass never triggers a model reload.
    ChatOptions opts = cfg_.chat_options(150, 0.1);

    for (size_t g = 0; g < group_count; ++g) {
        size_t begin = g * per_group;
        if (begin >= chunks.size()) break;
        size_t end = std::min(begin + per_group, chunks.size());

        std::string group_text;
        for (size_t i = begin; i < end; ++i) {
            group_text += "\n" + chunks[i].label() + "\n" + chunks[i].text + "\n";
        }

        std::string err;
        ChatTiming sum_timing;
        StageTimer t_sum;
        std::string summary = ollama_.chat(
            helper_model_,
            {{"user", build_summarize_user_message(question, group_text)}}, opts, &err,
            &sum_timing);
        log_model_call("map_reduce", helper_model_,
                       static_cast<long long>(t_sum.ms()), sum_timing.load_ms,
                       sum_timing.prompt_eval_ms, sum_timing.eval_ms,
                       sum_timing.prompt_tokens, sum_timing.eval_tokens);

        if (summary.empty()) {
            // Fallback: keep raw chunks instead of losing information.
            log_warn("map-reduce summary failed (" + err + ") - keeping raw chunks");
            summaries.insert(summaries.end(), chunks.begin() + static_cast<long>(begin),
                             chunks.begin() + static_cast<long>(end));
            continue;
        }
        Chunk s;
        s.kind = "summary";
        // Label = the group's real sources. "[Xülasə: ...]" labels are banned
        // in answers, so summaries cite their underlying files instead.
        std::string srcs;
        std::vector<std::string> seen_src;
        for (size_t i = begin; i < end; ++i) {
            std::string name = chunks[i].source;
            if (chunks[i].kind != "web") {
                size_t slash = name.find_last_of("/\\");
                if (slash != std::string::npos) name = name.substr(slash + 1);
            }
            if (std::find(seen_src.begin(), seen_src.end(), name) != seen_src.end())
                continue;
            seen_src.push_back(name);
            if (!srcs.empty()) srcs += " + ";
            srcs += name;
            if (seen_src.size() >= 3) {
                srcs += " + ...";
                break;
            }
        }
        s.source = srcs.empty() ? std::string("qrup xülasəsi") : srcs;
        s.text = OllamaClient::strip_thinking(summary);
        summaries.push_back(std::move(s));
    }
    return summaries;
}

// Caps chunks per source and drops near-duplicate embeddings so the same
// text is never cited twice while the final context stays source-diverse.
std::vector<Chunk> RagPipeline::diversify(const std::vector<Chunk>& chunks) const {
    constexpr size_t MAX_PER_PDF = 3;   // max 3 chunks per PDF file
    constexpr size_t MAX_PER_URL = 2;   // max 2 chunks per web page
    constexpr double NEAR_DUP_COSINE = 0.95;

    std::vector<Chunk> out;
    std::unordered_map<std::string, size_t> per_source;
    for (const auto& c : chunks) {
        const size_t cap = (c.kind == "web") ? MAX_PER_URL : MAX_PER_PDF;
        if (per_source[c.source] >= cap) continue;

        bool near_dup = false;  // embeddings are L2-normalized -> dot = cosine
        if (!c.embedding.empty()) {
            for (const auto& k : out) {
                if (k.embedding.empty()) continue;
                double dot = 0;
                const size_t n = std::min(c.embedding.size(), k.embedding.size());
                for (size_t i = 0; i < n; ++i)
                    dot += static_cast<double>(c.embedding[i]) * k.embedding[i];
                if (dot > NEAR_DUP_COSINE) {
                    near_dup = true;
                    break;
                }
            }
        }
        if (near_dup) continue;
        ++per_source[c.source];
        out.push_back(c);
    }
    return out.empty() ? chunks : out;
}

AnswerResult RagPipeline::answer(const std::string& question,
                                 const std::string& session_id,
                                 const StreamFn& on_delta,
                                 const StatusFn& on_status) const {
    AnswerResult res;
    StageTimer t_total;
    const std::string q = trim(question);
    if (q.empty()) {
        res.answer = "Sualınızı yazın - məmnuniyyətlə kömək edərəm.";
        if (on_delta) on_delta(res.answer);
        return res;
    }

    // --- 0a. Identity question about the bot: direct answer, no retrieval ---
    if (is_identity_question(q)) {
        log_info("fastpath: identity question");
        res.smalltalk = true;
        res.qtype = "identity";
        res.no_sources = true;
        const std::string creator = cfg_.creator.empty() ? "DUMY Team" : cfg_.creator;
        res.answer = "Mən DUMY-yəm, aqronomluq və maldarlıq üzrə köməkçiyəm. "
                     "Məni " + creator + " hazırlayıb.";
        if (on_delta) on_delta(res.answer);
        return res;
    }

    // --- 0b. Date / weekday question: answer from the system clock ----------
    if (is_datetime_question(q)) {
        log_info("fastpath: datetime question");
        res.smalltalk = true;
        res.qtype = "datetime";
        res.no_sources = true;
        res.answer = "Bu gün " + get_current_datetime_az() + ".";
        if (on_delta) on_delta(res.answer);
        return res;
    }

    // --- 0b2. Feed calculation with missing inputs: explicit assumptions ----
    // "120 baş mal-qaranın 30 günlük yemi nə qədərdir?" needs live weight and a
    // ration rate that are not in the question; answer deterministically with
    // the formula, labelled assumptions and no [Mənbə: ...] labels.
    if (is_feed_calculation_question(q)) {
        log_info("fastpath: feed calculation - explicit assumptions, no PDF citations");
        res.qtype = "calculation";
        res.from_own_knowledge = true;
        res.no_sources = true;
        res.answer = build_feed_calculation_answer(q);
        if (on_delta) on_delta(res.answer);
        return res;
    }

    // --- 0c. Arithmetic: computed here, never from PDF context --------------
    const bool arithmetic_q = is_arithmetic_question(q);
    if (arithmetic_q) {
        double value = 0.0;
        if (eval_simple_arithmetic(extract_arithmetic_expression(q), value)) {
            log_info("fastpath: arithmetic question");
            res.qtype = "arithmetic";
            res.smalltalk = true;
            res.no_sources = true;
            res.answer = "Nəticə: " + format_arithmetic_result(value) + ".";
            if (on_delta) on_delta(res.answer);
            return res;
        }
        // A word problem we cannot evaluate: still answer with general
        // knowledge, never with unrelated PDF citations (context dropped
        // after retrieval below).
        log_info("arithmetic-like question could not be parsed - answering without PDF context");
    }

    // --- 0. Small-talk fast path: greetings/thanks/bye, NO LLM call -------
    if (is_smalltalk(q)) {
        log_info("fastpath: smalltalk");
        res.smalltalk = true;
        res.qtype = "smalltalk";
        res.no_sources = true;
        res.answer = smalltalk_reply(q);
        if (on_delta) on_delta(res.answer);
        return res;
    }

    // --- 0b. Programming request -> polite refusal in Azerbaijani -----------
    if (is_code_request(q)) {
        res.refused = true;
        res.qtype = "code_request";
        res.answer = REFUSAL_MESSAGE;
        if (on_delta) on_delta(res.answer);
        return res;
    }

    // --- 1. Ollama availability --------------------------------------------
    if (!ollama_ok_) {
        res.warning = "ollama_down";
        res.answer = OLLAMA_DOWN_MESSAGE;
        if (on_delta) on_delta(res.answer);
        return res;
    }

    // --- 1b. Language tasks: no retrieval, no context, no notes ------------
    // Rewrite/correct/translate/define/invent-a-sentence requests are text
    // tasks, not knowledge questions: answer directly with only the result.
    if (is_language_task_question(q)) {
        log_info("fastpath: language task (no retrieval, no PDF context)");
        res.qtype = "language_task";
        res.no_sources = true;
        res.from_own_knowledge = true;
        const bool letters = is_letters_task(az_fold(q));
        const std::string prompt = build_language_task_prompt(q);
        ChatOptions task_opts =
            cfg_.chat_options(std::min(cfg_.num_predict_draft, 300), 0.0);
        std::string terr;
        ChatTiming ttiming;
        StageTimer t_task;
        std::string raw =
            on_delta ? ollama_.chat_stream(chat_model_, {{"user", prompt}}, task_opts,
                                           on_delta, &terr, &ttiming)
                     : ollama_.chat(chat_model_, {{"user", prompt}}, task_opts, &terr,
                                    &ttiming);
        log_model_call("language_task", chat_model_,
                       static_cast<long long>(t_task.ms()), ttiming.load_ms,
                       ttiming.prompt_eval_ms, ttiming.eval_ms, ttiming.prompt_tokens,
                       ttiming.eval_tokens, ttiming.done_reason);
        std::string out = clean_language_task_output(raw);
        int attempts = 1;
        if (letters && !out.empty()) {
            const std::string missing = missing_az_letters(out);
            if (!missing.empty()) {
                log_warn("language task: sentence misses letters - retrying once");
                std::string rerr;
                ChatTiming rtiming;
                StageTimer t_retry;
                const std::string retry_prompt =
                    prompt + "\n\nDiqqət: əvvəlki cümlədə bu hərflər yox idi: " +
                    missing + ". İndi hamısı olan YENİ bir cümlə yaz.";
                const std::string raw2 = ollama_.chat(
                    chat_model_, {{"user", retry_prompt}}, task_opts, &rerr, &rtiming);
                log_model_call("language_task_retry", chat_model_,
                               static_cast<long long>(t_retry.ms()), rtiming.load_ms,
                               rtiming.prompt_eval_ms, rtiming.eval_ms,
                               rtiming.prompt_tokens, rtiming.eval_tokens,
                               rtiming.done_reason);
                ++attempts;
                const std::string out2 = clean_language_task_output(raw2);
                if (!out2.empty() &&
                    missing_az_letters(out2).size() < missing.size())
                    out = out2;
            }
        }
        if (out.empty()) {
            out = HONEST_FAILURE_MESSAGE;
            log_warn("language task: no usable output");
        } else if (!letters && az_fold(q).find("cumle") != std::string::npos &&
                   out.back() != '.' && out.back() != '!' && out.back() != '?') {
            out += '.';
        }
        log_info("language task: attempts=" + std::to_string(attempts) +
                 " chars=" + std::to_string(out.size()));
        res.answer = out;
        return res;
    }

    // --- 2. routing (question-type analysis) --------------------------------
    StageTimer t_route;
    const QuestionType qtype = retriever_.analyze(q);
    res.qtype = question_type_name(qtype);
    // Short, non-topic messages ("bəli", "yox", "hmm") must not trigger web
    // search - greetings are already handled by the fast path above.
    const bool short_non_topic =
        meaningful_word_count(q) < 3 && qtype == QuestionType::General;
    // Step 5: short factual question -> expansion -> retrieval -> draft only.
    // Rerank, map-reduce and self-check are reserved for long / multi-document
    // questions where they actually change the answer.
    const bool fast_path =
        cfg_.fast_path_enabled && qtype == QuestionType::SimpleFact &&
        meaningful_word_count(q) <= static_cast<size_t>(std::max(1, cfg_.fast_path_max_words));
    const int top_k = retriever_.top_k_for(qtype);
    const double ms_route = t_route.ms();

    // --- 2b. query expansion (one model call, helper model) ----------------
    StageTimer t_expand;
    const std::vector<std::string> queries =
        short_non_topic ? std::vector<std::string>{q} : retriever_.expand_query(q, qtype);
    const double ms_expand = t_expand.ms();
    log_info("expansion queries (" + std::to_string(queries.size()) + "): " +
             join(queries, " | "));

    // --- 3. PDF hybrid retrieval + web search IN PARALLEL ------------------
    StageTimer t_parallel;
    std::future<Retriever::StoreResult> pdf_fut = std::async(
        std::launch::async, [&] { return retriever_.retrieve_store(queries, (size_t)top_k); });

    struct WebOut {
        std::vector<Chunk> chunks;
        std::string warning;
    };
    std::future<WebOut> web_fut;
    // Step 6: web runs in parallel with PDF retrieval, but never for questions
    // that are answered directly (identity / arithmetic / date-time) and never
    // when the config flag is off. With config.json web_search_enabled=false
    // the Wikipedia fallback is not called at all.
    const bool direct_answer = is_identity_question(q) ||
                               is_arithmetic_question(q) ||
                               is_datetime_question(q);
    const bool web_enabled =
        cfg_.web_search_enabled && !short_non_topic && !direct_answer;
    if (cfg_.web_search_enabled && short_non_topic) {
        log_info("fastpath: web skipped (short non-topic message)");
    } else if (cfg_.web_search_enabled && direct_answer) {
        log_info("fastpath: web skipped (identity/arithmetic/date-time question)");
    } else if (!cfg_.web_search_enabled) {
        log_info("web search disabled in config.json (web_search_enabled=false)");
    }
    if (web_enabled) {
        web_fut = std::async(std::launch::async, [&] {
            WebOut o;
            o.chunks = retriever_.retrieve_web(q, queries, (size_t)top_k, &o.warning);
            return o;
        });
    }

    Retriever::StoreResult pdf_result = pdf_fut.get();
    std::vector<Chunk> pdf_chunks = std::move(pdf_result.chunks);
    const float pdf_score_margin = pdf_result.score_margin;  // Step 4 signal
    const double ms_pdf = t_parallel.ms();
    log_info("retrieval scores: top1=" + std::to_string(pdf_result.max_score) +
             " margin=" + std::to_string(pdf_result.score_margin) +
             " chunks=" + std::to_string(pdf_chunks.size()));

    // Step 7: never attach unrelated PDF citations. When the best vector hit
    // is below min_score the matched chunks are noise (e.g. "Telefon nədir?"),
    // so the answer comes from general knowledge instead.
    if (pdf_result.max_score > 0.0f && pdf_result.max_score < cfg_.min_score) {
        log_info("retrieval guard: top cosine " + std::to_string(pdf_result.max_score) +
                 " < min_score " + std::to_string(cfg_.min_score) +
                 " - PDF context dropped");
        pdf_chunks.clear();
    }
    bool pdf_relevance_failed = false;
    // --- 3a2. General-knowledge question: PDFs are not sources --------------
    // "Fotosintez nədir?" matched an animal-biochemistry chapter by word, but
    // an agriculture book is not a source for a general-science definition.
    // Without a livestock/agronomy term in the question, answer from general
    // knowledge instead of citing the PDF.
    if (is_general_knowledge_question(q) && !pdf_chunks.empty()) {
        log_info("general-knowledge question: PDF context dropped (" +
                 std::to_string(pdf_chunks.size()) +
                 " chunks, no livestock/agronomy term)");
        pdf_chunks.clear();
        pdf_relevance_failed = true;
    }
    // --- 3b. Relevance gate: off-topic PDF hits must not become citations ---
    // The cosine guard above cannot separate "şəkərçörək" from sugar-beet
    // text (bge-m3 scores topically-related agri text high). Require a key
    // noun of the question in a retained chunk, or ask the helper model a
    // cheap yes/no question when lexical support is missing.
    if (!pdf_chunks.empty() && cfg_.relevance_check_enabled) {
        const std::vector<std::string> terms = relevance_key_terms(q);
        const size_t probe = std::min<size_t>(pdf_chunks.size(), 6);
        size_t matched = 0;
        std::string missing;
        for (const auto& t : terms) {
            bool found = false;
            for (size_t i = 0; i < probe && !found; ++i) {
                for (const auto& w : az_words(pdf_chunks[i].text)) {
                    if (az_words_match(t, w)) { found = true; break; }
                }
            }
            if (found) {
                ++matched;
            } else {
                if (!missing.empty()) missing += ",";
                missing += t;
            }
        }
        const double overlap = terms.empty()
                                   ? 1.0
                                   : static_cast<double>(matched) / static_cast<double>(terms.size());
        // Generic action words ("hazırlanır") are already excluded from the
        // key terms, so a sufficient overlap is real evidence. Requiring ALL
        // terms dropped valid contexts (e.g. "qarın şişməsi" where a chunk
        // says "şişkinlik"), so the share is used instead.
        const bool lex_ok = !terms.empty() && matched >= 1 &&
                            overlap >= cfg_.relevance_min_overlap;
        const bool score_ok = matched >= 1 &&
                              pdf_result.max_score >= cfg_.relevance_check_score;
        bool keep = true;
        std::string branch = "lexical";
        if (!lex_ok && !score_ok) {
            branch = "llm";
            std::string snippets;
            for (size_t i = 0; i < probe; ++i) {
                snippets += "\n[" + std::to_string(i + 1) + "] ";
                snippets += utf8_prefix(pdf_chunks[i].text, 500);
            }
            std::string rerr;
            ChatTiming rtiming;
            StageTimer t_rel;
            const std::string verdict = ollama_.chat(
                helper_model_,
                {{"user", build_relevance_check_user_message(q, snippets)}},
                cfg_.chat_options(6, 0.0), &rerr, &rtiming);
            log_model_call("relevance_check", helper_model_,
                           static_cast<long long>(t_rel.ms()), rtiming.load_ms,
                           rtiming.prompt_eval_ms, rtiming.eval_ms,
                           rtiming.prompt_tokens, rtiming.eval_tokens);
            const std::string v = trim(az_fold(verdict));
            const bool yes = v.rfind("beli", 0) == 0 || v.rfind("yes", 0) == 0;
            const bool no = v.rfind("xeyr", 0) == 0 || v.rfind("yox", 0) == 0 ||
                            v.rfind("no", 0) == 0;
            if (yes) branch += "_yes";
            else if (no) branch += "_no";
            else branch += "_unknown";
            keep = !(no && !yes);  // failed/unparsable check -> keep sources
        }
        log_info("relevance gate: branch=" + branch +
                 " score=" + std::to_string(pdf_result.max_score) +
                 " overlap=" + std::to_string(matched) + "/" +
                 std::to_string(terms.size()) + " key_terms=[" + join(terms, ",") +
                 "] missing=[" + missing + "] decision=" + (keep ? "keep" : "drop"));
        if (!keep) {
            pdf_chunks.clear();
            pdf_relevance_failed = true;
        }
    }
    WebOut web_out;
    if (web_enabled) web_out = web_fut.get();
    const double ms_web = web_enabled ? t_parallel.ms() : 0.0;

    std::vector<Chunk> candidates = std::move(pdf_chunks);
    candidates.insert(candidates.end(), web_out.chunks.begin(), web_out.chunks.end());
    res.used_web = !web_out.chunks.empty();

    if (!web_enabled && !short_non_topic) web_out.warning = "web search disabled";

    log_info("Sual tipi: " + res.qtype + " | top_k=" + std::to_string(top_k) +
             " | PDF: " + std::to_string(candidates.size() - web_out.chunks.size()) +
             " | web: " + std::to_string(web_out.chunks.size()) +
             " | expansion: " + std::to_string(queries.size() - 1) + " alt-sual");
    if (arithmetic_q) {
        candidates.clear();       // word-problem fallback: no PDF context
        res.used_web = false;
        web_out.chunks.clear();
        log_info("arithmetic: retrieval context dropped");
    }

    // --- 4. rerank (skipped by fast mode for simple questions) -------------
    // Relevance gate: a short non-topic message must not get PDF/web chunks
    // attached as sources at all - answer from the model's own knowledge.
    if (short_non_topic) {
        log_info("fastpath: low-information message - retrieval context dropped "
                 "(" + std::to_string(candidates.size()) + " raw hits)");
        candidates.clear();
    }
    StageTimer t_rerank;
    std::string rk_err;
    std::vector<Chunk> selected;
    if (!candidates.empty()) {
        if (fast_path) {
            // Step 5: no LLM rerank on the fast path - keep retrieval order,
            // cap to top_k_simple and only remove duplicates / cap per source.
            log_info("pipeline: FAST PATH (simple fact, " +
                     std::to_string(meaningful_word_count(q)) +
                     " words) - rerank skipped");
            selected = diversify(std::move(candidates));
            const size_t cap = static_cast<size_t>(std::max(1, cfg_.top_k_simple));
            if (selected.size() > cap) selected.resize(cap);
        } else {
            log_info("pipeline: full path (" + res.qtype + ")");
            selected = rerank_or_trim(q, std::move(candidates), qtype,
                                      pdf_score_margin, &rk_err);
            // Source diversity + near-duplicate removal for the final context.
            selected = diversify(std::move(selected));
        }
    }
    const double ms_rerank = t_rerank.ms();
    if (!rk_err.empty()) log_warn("rerank: " + rk_err);

    // Test instrument (DUMY_DUMP_TOPK=1): log the exact post-rerank context
    // chunks so retrieval can be compared across builds. Off by default.
    if (std::getenv("DUMY_DUMP_TOPK") != nullptr) {
        std::string line = "[topk] n=" + std::to_string(selected.size());
        for (const auto& c : selected) line += " | " + c.label();
        log_info(line);
    }

    if (selected.empty()) {
        log_info("Kontekst tapılmadı - model öz bilikləri ilə cavab verəcək");
    }

    // --- 5. map-reduce when the context is too large -----------------------
    // Step 3: the trigger is its own (much higher) threshold - ~3900 tokens
    // fired on ordinary questions and paid 2-5 extra LLM calls for nothing.
    const size_t mapred_limit =
        static_cast<size_t>(std::max(1000, cfg_.map_reduce_threshold_tokens));
    const bool do_map_reduce = !fast_path && total_tokens(selected) > mapred_limit &&
                               selected.size() > 6;
    std::vector<Chunk> context_chunks = selected;
    if (do_map_reduce) {
        log_info("Kontekst çox böyükdür (" + std::to_string(total_tokens(selected)) +
                 " token > " + std::to_string(mapred_limit) +
                 ") - map-reduce xülasələri işə salınır");
        context_chunks = map_reduce_summarize(q, selected);
        res.used_map_reduce = true;
    }

    // Hard fit guard: Ollama rejects a prompt that exceeds num_ctx (qwen3:4b
    // answered HTTP 400 at 8216 tokens with num_ctx 8192). estimate_tokens is
    // optimistic for Azerbaijani, so reserve system prompt + answer + margin
    // and drop tail chunks until the estimate fits.
    {
        const size_t reserve = 900 + static_cast<size_t>(cfg_.num_predict_draft) + 1500;
        const size_t limit = cfg_.num_ctx > static_cast<int>(reserve) + 512
                                 ? static_cast<size_t>(cfg_.num_ctx) - reserve
                                 : size_t(1024);
        const size_t before_trim = context_chunks.size();
        while (context_chunks.size() > 1 && total_tokens(context_chunks) > limit) {
            context_chunks.pop_back();
        }
        if (context_chunks.size() != before_trim) {
            log_info("context trimmed to fit num_ctx: " +
                     std::to_string(before_trim) + " -> " +
                     std::to_string(context_chunks.size()) + " chunks (limit=" +
                     std::to_string(limit) + " est. tokens)");
        }
    }

    // --- 6. draft answer (streamed) ----------------------------------------
    std::vector<ChatMessage> messages;
    messages.push_back({"system", SYSTEM_PROMPT});
    for (const auto& m : sessions_.history(session_id)) messages.push_back(m);

    const int requested_sentences = requested_sentence_count(q);
    const std::string sentence_rule =
        requested_sentences > 0
            ? " Cavabı düz " + std::to_string(requested_sentences) + " cümlə ilə ver."
            : std::string();
    std::string user_msg;
    if (context_chunks.empty()) {
        user_msg =
            "KONTEKST: Bu sual üzrə PDF məlumatı tapılmadı.\n"
            "Cavabı öz biliklərinlə ver, əsaslandığın yerləri [Ümumi bilik] "
            "etiket ilə işarələ və mənbəsiz faktları mənbəli kimi təqdim etmə. "
            "Suala birbaşa, tam cavab yaz." + sentence_rule + "\n\nSUAL: " + q;
    } else {
        user_msg = build_context_block(context_chunks) + "\n\n" +
                   build_draft_user_message(q);
    }
    if (pdf_relevance_failed) {
        user_msg = "KONTEKST: Yüklənmiş PDF sənədlərində bu sual üzrə məlumat YOXDUR. "
                   "Sual aqronomiya/maldarlıq mövzusundan kənar ola bilər - bu halda imtina "
                   "ETMƏ, ümumi biliklərinlə qısa, dəqiq və faydalı cavab ver. Heç bir "
                   "[Mənbə: ...] etiketi yazma, yalnız [Ümumi bilik] istifadə et. Suala "
                   "birbaşa, tam cavab yaz." + sentence_rule + "\n\nSUAL: " + q;
    }
    messages.push_back({"user", user_msg});

    // Same load-affecting options as every other call to this model.
    ChatOptions draft_opts = cfg_.chat_options(cfg_.num_predict_draft, 0.3);

    StageTimer t_draft;
    std::string derr;
    std::string draft;
    ChatTiming draft_timing;
    auto run_draft = [&](const std::vector<ChatMessage>& msgs, const ChatOptions& opts,
                         ChatTiming& tim) -> std::string {
        return on_delta ? ollama_.chat_stream(chat_model_, msgs, opts, on_delta,
                                              &derr, &tim)
                        : ollama_.chat(chat_model_, msgs, opts, &derr, &tim);
    };
    draft = run_draft(messages, draft_opts, draft_timing);
    log_model_call("draft", chat_model_, static_cast<long long>(t_draft.ms()),
                   draft_timing.load_ms, draft_timing.prompt_eval_ms,
                   draft_timing.eval_ms, draft_timing.prompt_tokens,
                   draft_timing.eval_tokens, draft_timing.done_reason);
    // A draft that hit num_predict (done_reason=length) is regenerated once
    // with the largest budget the remaining num_ctx allows. A continuation
    // append duplicated the answer (the model restarted instead of resuming),
    // so a clean second generation is used instead.
    if (!draft.empty() && draft_timing.done_reason == "length") {
        log_warn("draft hit num_predict (done_reason=length) - regenerating once");
        ChatOptions retry_opts = draft_opts;
        const long long room =
            static_cast<long long>(cfg_.num_ctx) - draft_timing.prompt_tokens - 150;
        retry_opts.num_predict = static_cast<int>(
            std::max<long long>(300, std::min<long long>(850, room)));
        StageTimer t_regen;
        ChatTiming regen_timing;
        // Not streamed: the first (truncated) attempt is already on screen and
        // the UI replaces it with the final answer on the done event.
        std::string again =
            ollama_.chat(chat_model_, messages, retry_opts, &derr, &regen_timing);
        log_model_call("draft_regen", chat_model_,
                       static_cast<long long>(t_regen.ms()), regen_timing.load_ms,
                       regen_timing.prompt_eval_ms, regen_timing.eval_ms,
                       regen_timing.prompt_tokens, regen_timing.eval_tokens,
                       regen_timing.done_reason);
        log_info("draft_regen: budget=" + std::to_string(retry_opts.num_predict) +
                 " done_reason=" +
                 (regen_timing.done_reason.empty() ? std::string("?")
                                                    : regen_timing.done_reason));
        if (!again.empty()) {
            draft = again;
            draft_timing = regen_timing;
        }
    }
    // Last resort: never leave a mid-word or unfinished [Mənbə label. Cutting
    // back to the last complete sentence is better than a half citation.
    if (!draft.empty() &&
        (draft_timing.done_reason == "length" || has_dangling_source_label(draft))) {
        const std::string trimmed =
            trim_to_last_sentence(strip_dangling_source_label(draft));
        if (!trimmed.empty() && trimmed.size() >= 40) {
            log_warn("draft still truncated - cut back to the last complete sentence (" +
                     std::to_string(draft.size()) + " -> " + std::to_string(trimmed.size()) +
                     " chars)");
            draft = trimmed;
        }
    }
    const double ms_draft = t_draft.ms();
    // Test instrument (DUMY_DUMP_TOPK=1): prompt_eval_count of the draft call
    // next to the context knobs, so "prompt fits in num_ctx" is checkable.
    if (std::getenv("DUMY_DUMP_TOPK") != nullptr) {
        log_info("[ctx] prompt_eval_count=" +
                 std::to_string(draft_timing.prompt_tokens) +
                 " num_ctx=" + std::to_string(cfg_.num_ctx) +
                 " num_predict=" + std::to_string(cfg_.num_predict_draft) +
                 " fits=" +
                 ((draft_timing.prompt_tokens + cfg_.num_predict_draft) < cfg_.num_ctx
                      ? "1"
                      : "0"));
    }
    if (draft.empty()) {
        res.warning = "llm_failed: " + derr;
        res.answer =
            "Cavabı hazırlayarkən xəta baş verdi: " + derr +
            (derr.find("model") != std::string::npos
                 ? "\nModel adını config.json-dan yoxlayın və ya 'ollama pull " +
                       chat_model_ + "' əmrini çalışdırın."
                 : std::string());
        log_error(res.warning);
        return res;
    }

    // --- 7. self-check: compact verdict, then at most ONE correction retry --
    // The checker returns a SHORT JSON verdict (PASS/FAIL + problems) instead
    // of rewriting the answer: on PASS the draft is kept verbatim; on FAIL the
    // draft model regenerates the answer once with the problems as hints. The
    // verdict is never user-visible - it is only logged.
    std::string final_answer = draft;
    const bool needs_self_check = !cfg_.fast_mode &&
                                  (qtype == QuestionType::BroadTopic ||
                                   qtype == QuestionType::Diagnosis ||
                                   qtype == QuestionType::Comparison);
    StageTimer t_check;
    if (needs_self_check) {
        if (on_status) on_status("selfcheck");  // kept internal (not displayed)
        // Same ctx/keep_alive as the draft -> no model reload for the verdict.
        ChatOptions check_opts = cfg_.chat_options(256, 0.0);
        std::string cerr;
        ChatTiming check_timing;
        StageTimer t_check_call;
        std::string checked = ollama_.chat(
            selfcheck_model_,
            {{"user", build_self_check_user_message(q, draft, !selected.empty())}},
            check_opts, &cerr, &check_timing);
        log_model_call("self_check", selfcheck_model_,
                       static_cast<long long>(t_check_call.ms()), check_timing.load_ms,
                       check_timing.prompt_eval_ms, check_timing.eval_ms,
                       check_timing.prompt_tokens, check_timing.eval_tokens);

        std::string verdict;
        std::string problems;
        if (!checked.empty()) {
            size_t lb = checked.find('{');
            size_t rb = checked.rfind('}');
            if (lb != std::string::npos && rb != std::string::npos && rb > lb) {
                try {
                    auto j = nlohmann::json::parse(checked.substr(lb, rb - lb + 1));
                    verdict = to_lower_utf8(j.value("verdict", std::string()));
                    if (j.contains("problems") && j["problems"].is_array()) {
                        int n = 0;
                        for (const auto& p : j["problems"]) {
                            if (!p.is_string()) continue;
                            const std::string t = trim(p.get<std::string>());
                            if (t.empty()) continue;
                            if (!problems.empty()) problems += "; ";
                            problems += t;
                            if (++n >= 3) break;
                        }
                    }
                } catch (const std::exception& e) {
                    log_warn(std::string("self-check verdict parse failed: ") + e.what());
                }
            }
        }
        const bool fail = (verdict == "fail") && !problems.empty();
        res.self_check_failed = fail;
        log_info("self_check verdict=" + (verdict.empty() ? "unknown" : verdict) +
                 " problems=" + std::to_string(problems.empty() ? 0 : 1) +
                 " retries=" + std::to_string(fail ? 1 : 0));
        if (fail) {
            // Exactly ONE regeneration on the draft model with the hints.
            std::vector<ChatMessage> fix_msgs;
            fix_msgs.push_back({"system", SYSTEM_PROMPT});
            fix_msgs.push_back({"user", user_msg});
            fix_msgs.push_back({"assistant", draft});
            fix_msgs.push_back({"user", build_correction_user_message(problems)});
            std::string ferr;
            ChatTiming fix_timing;
            StageTimer t_fix;
            // Deterministic correction: temp 0, same model/ctx as the draft so
            // there is no reload. One shot only.
            ChatOptions fix_opts = draft_opts;
            fix_opts.temperature = 0.0;
            std::string fixed =
                ollama_.chat(chat_model_, fix_msgs, fix_opts, &ferr, &fix_timing);
            log_model_call("draft_retry", chat_model_,
                           static_cast<long long>(t_fix.ms()), fix_timing.load_ms,
                           fix_timing.prompt_eval_ms, fix_timing.eval_ms,
                           fix_timing.prompt_tokens, fix_timing.eval_tokens,
                           fix_timing.done_reason);
            if (fixed.empty()) {
                log_warn("self-check retry failed, keeping draft: " + ferr);
            } else {
                res.self_check_retries = 1;  // a retry was attempted
                // Accept only a correction that keeps most of the draft's
                // content; a hallucinated/off-topic rewrite is discarded.
                const double sim = word_jaccard(draft, fixed);
                const bool draft_thin = trim(draft).size() < 120;
                if (sim >= 0.25 || draft_thin) {
                    final_answer = fixed;
                    res.used_self_check = true;
                    log_info("self_check retry accepted (similarity=" +
                             std::to_string(sim) + ")");
                } else {
                    log_warn("self-check retry rejected (similarity=" +
                             std::to_string(sim) + ") - keeping draft");
                }
            }
        }
    }
    const double ms_check = t_check.ms();
    StageTimer t_post;
    final_answer = validate_source_labels(
        strip_dangling_source_label(strip_clarification_offer(fix_url_labels(
            strip_summary_labels(OllamaClient::strip_thinking(final_answer))))),
        selected);

    // --- 8. strip model-written source list, add notes + safety warning ----
    // Sources appear ONLY in the API's sources list (UI collapsible), never
    // as a model-written "İstifadə olunan mənbələr" block in the answer text.
    // Remove a model-written source list, any echoed silent-check block, and
    // every copy of the safety line (it is appended back exactly once, at the
    // very end, only when the question concerns health/diagnosis/treatment).
    final_answer = strip_trailing_source_lines(
        strip_silent_check(strip_safety_line(strip_own_sources(final_answer))));
    // --- 7b. answer-quality gate ------------------------------------------
    // Never return an empty answer or a bare clarification offer; retry once
    // with a minimal strict prompt when the draft is degenerate or shorter
    // than quality_check.min_answer_chars. The old fallback re-used the raw
    // draft, which is how a phrase-only answer reached the user.
    const bool degenerate = is_degenerate_answer(final_answer);
    const bool too_short =
        cfg_.quality_check_enabled && !degenerate &&
        final_answer.size() <
            static_cast<size_t>(std::max(0, cfg_.quality_min_answer_chars));
    if (degenerate || too_short) {
        log_warn(std::string("quality gate: ") +
                 (degenerate ? "degenerate" : "short") + " answer (" +
                 std::to_string(final_answer.size()) + " chars) - strict retry");
        std::string serr;
        ChatTiming stiming;
        StageTimer t_strict;
        std::string retry = ollama_.chat(
            chat_model_, {{"user", build_strict_answer_message(q)}},
            cfg_.chat_options(cfg_.num_predict_draft, 0.1), &serr, &stiming);
        log_model_call("strict_retry", chat_model_,
                       static_cast<long long>(t_strict.ms()), stiming.load_ms,
                       stiming.prompt_eval_ms, stiming.eval_ms,
                       stiming.prompt_tokens, stiming.eval_tokens, stiming.done_reason);
        retry = trim(strip_dangling_source_label(strip_clarification_offer(
            strip_silent_check(strip_safety_line(strip_own_sources(
                OllamaClient::strip_thinking(retry)))))));
        if (!retry.empty() && !is_degenerate_answer(retry) &&
            (degenerate || retry.size() > final_answer.size())) {
            final_answer = retry;
            log_info("quality gate: strict retry accepted (" +
                     std::to_string(final_answer.size()) + " chars)");
        } else if (degenerate) {
            final_answer = HONEST_FAILURE_MESSAGE;
            log_warn("quality gate: strict retry failed - honest failure message");
        } else {
            log_warn("quality gate: strict retry not better - keeping short draft");
        }
    }
    if (trim(final_answer).empty()) {
        final_answer = HONEST_FAILURE_MESSAGE;
        log_warn("quality gate: answer still empty - honest failure message");
    }
    if (requested_sentences > 0) {
        const std::string limited = limit_sentences(final_answer, requested_sentences);
        if (limited != final_answer) {
            log_info("answer limited to " + std::to_string(requested_sentences) +
                     " sentence(s) as requested");
            final_answer = limited;
        }
    }
    res.used_chunks = cited_chunks(final_answer, selected);

    std::string out = final_answer;
    if (selected.empty()) {
        res.from_own_knowledge = true;
        out += "\n\n";
        out += pdf_relevance_failed ? std::string(NO_RELEVANT_CONTEXT_NOTE)
                                    : no_context_note(web_enabled || res.used_web);
    }
    // The web-failure note goes before the safety line so the safety line is
    // always the very last thing in the answer.
    if (web_enabled && !web_out.warning.empty() && !res.used_web) {
        out += "\n\nQeyd: bu dəfə internet axtarışı işləmədi (" + web_out.warning +
               "); cavab PDF mənbələri və ümumi bilik əsasında verildi.";
        res.warning = "web_failed: " + web_out.warning;
    }
    if (needs_safety_warning(q)) {
        out = trim(out);
        out += "\n\n";
        out += SAFETY_WARNING;
    }
    res.answer = out;
    const double ms_post = t_post.ms();
    const double ms_total = t_total.ms();
    log_info("Mərhələ vaxtları (ms): routing=" + std::to_string((int)ms_route) +
             ", expansion=" + std::to_string((int)ms_expand) +
             ", retrieval=" + std::to_string((int)ms_pdf) +
             ", web=" + std::to_string((int)ms_web) +
             ", rerank+diversify=" + std::to_string((int)ms_rerank) +
             ", draft=" + std::to_string((int)ms_draft) +
             ", self_check=" + std::to_string((int)ms_check) +
             ", post=" + std::to_string((int)ms_post) +
             ", path=" + (fast_path ? "fast" : "full") +
             ", TOTAL=" + std::to_string((int)ms_total));

    // --- 9. remember the turn for follow-up questions ----------------------
    sessions_.append(session_id, {"user", q}, {"assistant", final_answer});
    return res;
}

}  // namespace dumy


