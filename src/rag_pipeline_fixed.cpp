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

    // --- bge-m3: tiny embed call, num_gpu=0 (CPU), keep_alive -1 -----------
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

    // --- main model: 1 token, identical options to the draft request --------
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

    // --- verify both are still resident (and bge-m3 is on CPU) -------------
    {
        std::string perr;
        const auto running = ollama_.running_models(&perr);
        auto same_model = [](const std::string& a, const std::string& b) {
            return a == b || strip_latest(a) == strip_latest(b);
        };
        for (const auto& m : running) {
            if (same_model(m.name, chat_model_)) warmup_.main_resident = true;
            if (same_model(m.name, embedding_model_)) {
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
            if (!warmup_.embed_on_cpu) {
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
                                               std::string* err) const {
    const bool fast_simple = cfg_.fast_mode &&
                             (qtype == QuestionType::SimpleFact ||
                              qtype == QuestionType::General);
    if (fast_simple) {
        // fast mode: skip step 4 (rerank) - RRF order is already good.
        if (candidates.size() > static_cast<size_t>(cfg_.top_k_simple)) {
            candidates.resize(static_cast<size_t>(
                std::max(cfg_.top_k_simple, cfg_.rerank_top_n / 2)));
        }
        if (err) err->clear();
        return candidates;
    }
    return reranker_.rerank(question, candidates,
                            static_cast<size_t>(cfg_.rerank_top_n), err);
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

    // --- 2. routing (question-type analysis) --------------------------------
    StageTimer t_route;
    const QuestionType qtype = retriever_.analyze(q);
    res.qtype = question_type_name(qtype);
    // Short, non-topic messages ("bəli", "yox", "hmm") must not trigger web
    // search - greetings are already handled by the fast path above.
    const bool short_non_topic =
        meaningful_word_count(q) < 3 && qtype == QuestionType::General;
    const int top_k = retriever_.top_k_for(qtype);
    const double ms_route = t_route.ms();

    // --- 2b. query expansion (one model call, helper model) ----------------
    StageTimer t_expand;
    const std::vector<std::string> queries =
        short_non_topic ? std::vector<std::string>{q} : retriever_.expand_query(q, qtype);
    const double ms_expand = t_expand.ms();

    // --- 3. PDF hybrid retrieval + web search IN PARALLEL ------------------
    StageTimer t_parallel;
    std::future<Retriever::StoreResult> pdf_fut = std::async(
        std::launch::async, [&] { return retriever_.retrieve_store(queries, (size_t)top_k); });

    struct WebOut {
        std::vector<Chunk> chunks;
        std::string warning;
    };
    std::future<WebOut> web_fut;
    const bool web_enabled = cfg_.web_search_enabled && !short_non_topic;
    if (cfg_.web_search_enabled && short_non_topic) {
        log_info("fastpath: web skipped (short non-topic message)");
    }
    if (web_enabled) {
        web_fut = std::async(std::launch::async, [&] {
            WebOut o;
            o.chunks = retriever_.retrieve_web(q, queries, (size_t)top_k, &o.warning);
            return o;
        });
    }

    std::vector<Chunk> pdf_chunks = pdf_fut.get().chunks;
    const double ms_pdf = t_parallel.ms();
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
        selected = rerank_or_trim(q, std::move(candidates), qtype, &rk_err);
        // Source diversity + near-duplicate removal for the final context.
        selected = diversify(std::move(selected));
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
    const bool do_map_reduce = total_tokens(selected) > mapred_limit &&
                               selected.size() > 6;
    std::vector<Chunk> context_chunks = selected;
    if (do_map_reduce) {
        log_info("Kontekst çox böyükdür (" + std::to_string(total_tokens(selected)) +
                 " token > " + std::to_string(mapred_limit) +
                 ") - map-reduce xülasələri işə salınır");
        context_chunks = map_reduce_summarize(q, selected);
        res.used_map_reduce = true;
    }

    // --- 6. draft answer (streamed) ----------------------------------------
    std::vector<ChatMessage> messages;
    messages.push_back({"system", SYSTEM_PROMPT});
    for (const auto& m : sessions_.history(session_id)) messages.push_back(m);

    std::string user_msg;
    if (context_chunks.empty()) {
        user_msg =
            "KONTEKST: Bu sual üzrə PDF və ya internet məlumatı tapılmadı.\n"
            "Cavabı öz biliklərinlə ver və mütləq [Ümumi bilik] etiketini qeyd et, "
            "mənbəsiz faktları mənbəli kimi təqdim etmə.\n\nSUAL: " + q;
    } else {
        user_msg = build_context_block(context_chunks) + "\n\n" +
                   build_draft_user_message(q);
    }
    messages.push_back({"user", user_msg});

    // Same load-affecting options as every other call to this model.
    ChatOptions draft_opts = cfg_.chat_options(cfg_.num_predict_draft, 0.3);

    StageTimer t_draft;
    std::string derr;
    std::string draft;
    ChatTiming draft_timing;
    if (on_delta) {
        draft = ollama_.chat_stream(chat_model_, messages, draft_opts, on_delta, &derr,
                                    &draft_timing);
    } else {
        draft = ollama_.chat(chat_model_, messages, draft_opts, &derr, &draft_timing);
    }
    const double ms_draft = t_draft.ms();
    log_model_call("draft", chat_model_, static_cast<long long>(ms_draft),
                   draft_timing.load_ms, draft_timing.prompt_eval_ms,
                   draft_timing.eval_ms, draft_timing.prompt_tokens,
                   draft_timing.eval_tokens);
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
                           fix_timing.prompt_tokens, fix_timing.eval_tokens);
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
        fix_url_labels(strip_summary_labels(OllamaClient::strip_thinking(final_answer))),
        selected);

    // --- 8. strip model-written source list, add notes + safety warning ----
    // Sources appear ONLY in the API's sources list (UI collapsible), never
    // as a model-written "İstifadə olunan mənbələr" block in the answer text.
    // Remove a model-written source list, any echoed silent-check block, and
    // every copy of the safety line (it is appended back exactly once, at the
    // very end, only when the question concerns health/diagnosis/treatment).
    final_answer =
        strip_silent_check(strip_safety_line(strip_own_sources(final_answer)));
    res.used_chunks = cited_chunks(final_answer, selected);

    std::string out = final_answer;
    if (selected.empty()) {
        res.from_own_knowledge = true;
        out += "\n\n";
        out += NO_CONTEXT_NOTE;
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
             ", TOTAL=" + std::to_string((int)ms_total));

    // --- 9. remember the turn for follow-up questions ----------------------
    sessions_.append(session_id, {"user", q}, {"assistant", final_answer});
    return res;
}

}  // namespace dumy


