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
#include "dumy/meta_commands.h"
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

size_t total_tokens(const std::vector<Chunk>& chunks, double chars_per_token) {
    size_t sum = 0;
    for (const auto& c : chunks) sum += estimate_tokens_az(c.text, chars_per_token);
    return sum;
}

// (The sources footer is no longer appended to the answer text: the UI shows
// the sources in its own collapsible "Mənbələr" list, fed by sources_json().
// Models that still write their own list are stripped by strip_own_sources.)

// Sources actually cited inside the answer text: a chunk counts as cited
// when its identity (PDF base name / URL) appears anywhere in the answer,
// so slight label variations ("səh. 203-204") still match. Everything else
// is NOT attached to the sources list.
// Post-check for citations: a [Mənbə: file, səh. N] label survives only when
// the cited chunk is really in the context AND its text shares a key entity
// with the sentence the label supports. Otherwise it becomes [Ümumi bilik].
std::string enforce_citation_support(const std::string& in,
                                     const std::vector<Chunk>& used) {
    const std::string open = "[Mənbə: ";
    if (in.find(open) == std::string::npos) return in;
    std::string out = in;
    size_t pos = 0;
    while ((pos = out.find(open, pos)) != std::string::npos) {
        const size_t inner = pos + open.size();
        const size_t close = out.find(']', inner);
        if (close == std::string::npos) break;
        const std::string content = out.substr(inner, close - inner);

        std::string file = content;
        int page = 0;
        const size_t comma = content.rfind(',');
        if (comma != std::string::npos) {
            file = trim(content.substr(0, comma));
            const std::string tail = az_fold(content.substr(comma + 1));
            const size_t p = tail.find("seh.");
            if (p != std::string::npos) page = std::atoi(tail.c_str() + p + 4);
        }

        const Chunk* match = nullptr;
        for (const auto& c : used) {
            if (c.kind == "web") continue;
            std::string name = c.source;
            const size_t slash = name.find_last_of("/\\");
            if (slash != std::string::npos) name = name.substr(slash + 1);
            if (name != file) continue;
            if (page > 0 && c.page != page) continue;
            match = &c;
            break;
        }

        bool ok = false;
        if (match) {
            size_t start = 0;
            for (size_t i = pos; i > 0; --i) {
                const char c = out[i - 1];
                if (c == '.' || c == '!' || c == '?' || c == '\n') {
                    start = i;
                    break;
                }
            }
            if (pos > start + 400) start = pos - 400;
            const std::string sentence = out.substr(start, pos - start);
            const auto terms = relevance_key_terms(sentence);
            if (terms.empty()) {
                ok = true;  // nothing to verify: never drop a label blindly
            } else {
                const auto words = az_words(match->text);
                size_t matched = 0;
                for (const auto& t : terms) {
                    for (const auto& w : words) {
                        if (az_words_match(t, w)) { ++matched; break; }
                    }
                }
                ok = matched >= 1;
                if (!ok)
                    log_info("citation post-check: dropped '" + content +
                             "' (no shared key entity with its sentence)");
            }
        } else {
            log_info("citation post-check: dropped '" + content +
                     "' (chunk not in context)");
        }

        if (ok) {
            pos = close + 1;
        } else {
            out.replace(pos, close - pos + 1, "[Ümumi bilik]");
            pos += 12;
        }
    }
    return out;
}

// True when at least one key term of `question` appears in `answer` (same
// 5-character stem or a real inflection). Used for off-domain short answers.
bool subject_mentioned(const std::vector<std::string>& terms,
                       const std::string& answer) {
    if (terms.empty()) return true;
    const auto words = az_words(answer);
    auto stem = [](const std::string& w) { return w.substr(0, std::min<size_t>(5, w.size())); };
    for (const auto& t : terms) {
        const std::string ts = stem(t);
        for (const auto& w : words) {
            if (stem(w) == ts) return true;
            if (az_words_match(t, w)) return true;
        }
    }
    return false;
}

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
    // Structured provenance: {type: pdf|web|general|computed|memory, file,
    // page, snippet}. The answer TEXT never carries source labels.
    nlohmann::json arr = nlohmann::json::array();
    std::vector<std::string> seen;
    auto basename = [](const std::string& path) {
        const size_t slash = path.find_last_of("/\\");
        return slash == std::string::npos ? path : path.substr(slash + 1);
    };
    for (const auto& c : used_chunks) {
        if (c.source.empty() && c.url.empty()) continue;
        const bool web = c.kind == "web";
        const std::string key = (web ? c.url : c.source) + "|" +
                                std::to_string(web ? 0 : c.page);
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
        seen.push_back(key);
        const std::string file = web ? c.url : basename(c.source);
        arr.push_back({{"type", web ? "web" : "pdf"},
                       {"file", file},
                       {"page", web ? 0 : c.page},
                       {"snippet", utf8_prefix(trim(c.text), 280)},
                       {"source", c.source},
                       {"url", c.url},
                       {"trusted", c.trusted}});
        if (arr.size() >= 14) break;
    }
    if (arr.empty()) {
        if (used_memory) {
            arr.push_back({{"type", "memory"}, {"file", ""}, {"page", 0},
                           {"snippet", ""}, {"source", ""}, {"url", ""},
                           {"trusted", false}});
        } else if (computed) {
            arr.push_back({{"type", "computed"}, {"file", ""}, {"page", 0},
                           {"snippet", ""}, {"source", ""}, {"url", ""},
                           {"trusted", false}});
        } else if (general_answer) {
            arr.push_back({{"type", "general"}, {"file", ""}, {"page", 0},
                           {"snippet", ""}, {"source", ""}, {"url", ""},
                           {"trusted", false}});
        }
    }
    return arr;
}

RagPipeline::RagPipeline(const Config& cfg, OllamaClient& ollama, VectorStorePtr store,
                         Bm25Index& bm25, WebSearch& web, Retriever& retriever,
                         Reranker& reranker, SessionManager& sessions,
                         ConversationStore* conversations)
    : cfg_(cfg),
      conversations_(conversations),
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

void RagPipeline::ensure_vocabulary() const {
    std::call_once(vocab_once_, [this]() {
        if (!store_) return;
        for (const auto& c : store_->all()) {
            std::string cur;
            for (char ch : az_fold(c.text)) {
                if (ch == ' ') {
                    if (cur.size() >= 4) vocab_.insert(cur);
                    cur.clear();
                } else {
                    cur.push_back(ch);
                }
            }
            if (cur.size() >= 4) vocab_.insert(cur);
        }
        for (const auto& w : vocab_)
            if (w.size() >= 6) vocab_prefixes_.insert(w.substr(0, 6));
        log_info("word check: PDF vocabulary=" + std::to_string(vocab_.size()) +
                 " prefixes=" + std::to_string(vocab_prefixes_.size()));
    });
}

AzWordStats RagPipeline::unknown_word_stats(const std::string& text) const {
    ensure_vocabulary();
    return measure_unknown_words(text, vocab_, vocab_prefixes_);
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
    const size_t total = total_tokens(
        chunks, cfg_.token_chars_per_token > 0.0 ? cfg_.token_chars_per_token : 2.6);
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

namespace {
// A short message that only makes sense with the previous turn ("bəs onun
// müalicəsi necədir?", "daha ətraflı yaz"). Conservative on purpose: a long
// question or one without a reference marker is never rewritten.
bool is_followup_question(const std::string& q) {
    if (meaningful_word_count(q) > 8) return false;
    const std::string f = " " + az_fold(q) + " ";
    static const char* MARK[] = {" bes ",   " onun ",  " onlarin ", " daha ",
                                 " etrafli ", " davam ", " bu ",      " o ",
                                 " hansi ", " niye ",  " nece "};
    for (const char* m : MARK)
        if (f.find(m) != std::string::npos) return true;
    return false;
}
}  // namespace

AnswerResult RagPipeline::answer(const std::string& question,
                                 const std::string& session_id,
                                 const StreamFn& on_delta,
                                 const StatusFn& on_status) const {
    return answer(question, session_id, MemoryRef{}, on_delta, on_status);
}

AnswerResult RagPipeline::answer(const std::string& question,
                                 const std::string& session_id,
                                 const MemoryRef& mem,
                                 const StreamFn& on_delta,
                                 const StatusFn& on_status) const {
    AnswerResult r = answer_impl(question, session_id, mem, on_delta, on_status);
    // Deterministic text hygiene: the API's `answer` is the answer only.
    // Sources live in sources_json(), disclaimers/flags in notes_ui.
    r.answer = clean_answer_text(r.answer);
    // Belt and braces: a marker that survived sentence filtering removes its
    // whole line, so the invariant holds for every generation path.
    {
        std::string kept;
        size_t start = 0;
        while (start <= r.answer.size()) {
            const size_t nl = r.answer.find('\n', start);
            const std::string line =
                nl == std::string::npos ? r.answer.substr(start)
                                        : r.answer.substr(start, nl - start);
            const std::string cut = cut_answer_marker(line);
            const std::string f = az_fold(cut);
            const bool bad = trim(cut).empty() ||
                             f.find("esasinda verilib") != std::string::npos ||
                             f.find("vacib qerarlar ucun menbeni yoxlayin") != std::string::npos;
            if (!bad) {
                kept += cut;
                kept += '\n';
            }
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
        r.answer = trim(kept);
    }
    if (trim(r.answer).empty()) r.answer = HONEST_FAILURE_MESSAGE;
    return r;
}

AnswerResult RagPipeline::answer_impl(const std::string& question,
                                      const std::string& session_id,
                                      const MemoryRef& mem,
                                      const StreamFn& on_delta,
                                      const StatusFn& on_status) const {
    AnswerResult res;
    StageTimer t_total;
    const std::string q_original = trim(question);
    // Restore Azerbaijani letters for routing/retrieval only when the input is
    // pure ASCII; words are replaced only when they are in the local table.
    std::string q = normalize_az_input(q_original);
    if (q.empty()) {
        res.answer = "Sualınızı yazın - məmnuniyyətlə kömək edərəm.";
        if (on_delta) on_delta(res.answer);
        return res;
    }

    // --- Step 1: conversational meta commands (deterministic, no model) ----
    // "cavab ver də sualıma" is a command about the previous question, never
    // a topic: it is resolved here before routing and retrieval.
    {
        const MetaResult meta = classify_meta(q);
        if (meta.kind != MetaKind::None) {
            std::string target;
            if (meta.has_content) {
                target = meta.content;
            } else {
                const auto hist = sessions_.history(session_id);
                for (auto it = hist.rbegin(); it != hist.rend(); ++it) {
                    if (it->role != "user") continue;
                    const std::string prev = trim(it->content);
                    if (prev.empty() || is_smalltalk(prev)) continue;
                    const MetaResult m2 = classify_meta(normalize_az_input(prev));
                    if (m2.kind != MetaKind::None && !m2.has_content) continue;
                    target = m2.has_content ? m2.content : prev;
                    break;
                }
            }
            if (trim(target).empty()) {
                log_info("meta: command without a previous question");
                res.qtype = "meta";
                res.smalltalk = true;
                res.no_sources = true;
                res.answer = "Hansı suala cavab verim? Sualınızı yazın.";
                if (on_delta) on_delta(res.answer);
                return res;
            }
            q = normalize_az_input(target);
            log_info(std::string("meta: ") + meta_kind_name(meta.kind) +
                     " -> " + q);
            if (meta.kind == MetaKind::Detail)
                q += " (bu sualı ətraflı, bölmələrlə izah et)";
            else if (meta.kind == MetaKind::Shorten)
                q += " (yalnız bir-iki qısa cümlə ilə cavab ver)";
        }
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
        res.computed = true;
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
        // Azerbaijani word form ("15 vurulsun 12", "20-dən 5 çıx") is turned
        // into a plain expression first; symbol forms come back unchanged.
        const std::string arith_expr = az_arithmetic_expression(q);
        if (!arith_expr.empty() && eval_simple_arithmetic(arith_expr, value)) {
            log_info("fastpath: arithmetic question [" + arith_expr + "] = " +
                     format_arithmetic_result(value));
            res.qtype = "arithmetic";
            res.computed = true;
            res.smalltalk = true;
            res.no_sources = true;
            res.answer = "Nəticə: " + format_arithmetic_result(value) + ".";
            if (on_delta) on_delta(res.answer);
            return res;
        }
        // A bare operation we cannot parse must never be guessed by the
        // model: ask for a clearer expression instead.
        if (!arith_expr.empty() || has_arithmetic_operator(q)) {
            log_info("arithmetic: operation could not be parsed - asking to rephrase");
            res.qtype = "arithmetic";
            res.computed = true;
            res.smalltalk = true;
            res.no_sources = true;
            res.answer = ARITHMETIC_REPHRASE_MESSAGE;
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

    // --- 0d. Typed sentence without Azerbaijani letters ---------------------
    // "men sabah mekteve gedecem" is a spelling correction request, not a
    // knowledge question. The local dictionary restores the letters, so this
    // is deterministic and needs no model call.
    if (looks_like_bare_sentence(q_original, q)) {
        log_info("fastpath: bare sentence correction (local dictionary)");
        res.qtype = "language_task";
        res.no_sources = true;
        res.from_own_knowledge = true;
        res.answer = deterministic_sentence_correction(q_original);
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

    // --- 1b2. Persistent memory: facts the user stated about themselves -----
    // Deterministic and model-free, so it costs no latency and cannot be
    // polluted by the model. It runs BEFORE the fact-checker so a personal
    // statement is acknowledged instead of judged, and a personal question is
    // answered from the stored fact (labelled [Söhbət yaddaşı]).
    if (conversations_ && cfg_.memory_enabled && !mem.client.empty()) {
        const std::string stored =
            conversations_->remember_from_question(mem.client, q, mem.conversation);
        if (!stored.empty()) {
            log_info("memory: stored a new user fact");
            res.qtype = "memory";
            res.no_sources = true;
            res.from_own_knowledge = true;
            res.used_memory = true;
            res.answer = "Yaddaşa yazdım: " + stored + ".\n\n[Söhbət yaddaşı]";
            if (on_delta) on_delta(res.answer);
            return res;
        }
        MemoryFact fact;
        if (ConversationStore::best_fact_for(conversations_->facts(mem.client), q,
                                             &fact)) {
            log_info("memory: answered from a stored fact");
            res.qtype = "memory";
            res.no_sources = true;
            res.from_own_knowledge = true;
            res.used_memory = true;
            res.answer =
                "Yaddaşımda qeyd olunub: " + fact.text + ".\n\n[Söhbət yaddaşı]";
            if (on_delta) on_delta(res.answer);
            return res;
        }
    }

    // --- 1c. Declarative statement -> fact-check mode -----------------------
    // "Title: statement" is not a question: it needs a verdict, not an echoed
    // explanation. Running before retrieval also keeps general facts away from
    // the agriculture PDFs entirely.
    if (cfg_.fact_check_enabled && is_statement_input(q)) {
        log_info("fastpath: statement/fact-check mode");
        res.qtype = "fact_check";
        res.no_sources = true;
        res.from_own_knowledge = true;
        ChatOptions fc_opts = cfg_.chat_options(cfg_.fact_check_num_predict, 0.0);
        std::string ferr;
        ChatTiming ftiming;
        StageTimer t_fc;
        const std::string raw = ollama_.chat(
            chat_model_, {{"user", build_fact_check_user_message(q)}}, fc_opts,
            &ferr, &ftiming);
        log_model_call("fact_check", chat_model_,
                       static_cast<long long>(t_fc.ms()), ftiming.load_ms,
                       ftiming.prompt_eval_ms, ftiming.eval_ms,
                       ftiming.prompt_tokens, ftiming.eval_tokens,
                       ftiming.done_reason);
        // Verdict + at most two short sentences (the verdict is sentence 1).
        std::string out = limit_sentences(shape_fact_check_answer(raw, q), 3);

        // Two-pass agreement: a 4B model is over-confident, so a second
        // adversarial pass must reach the same verdict. Disagreement is an
        // honest "Dəqiq bilmirəm", never a confident wrong answer.
        {
            ChatOptions adv_opts = cfg_.chat_options(cfg_.fact_check_num_predict, 0.2);
            std::string aerr;
            ChatTiming atiming;
            StageTimer t_adv;
            const std::string raw2 = ollama_.chat(
                chat_model_, {{"user", build_fact_check_refute_message(q)}}, adv_opts,
                &aerr, &atiming);
            log_model_call("fact_check_refute", chat_model_,
                           static_cast<long long>(t_adv.ms()), atiming.load_ms,
                           atiming.prompt_eval_ms, atiming.eval_ms,
                           atiming.prompt_tokens, atiming.eval_tokens,
                           atiming.done_reason);
            if (!trim(raw2).empty()) {
                const std::string out2 = shape_fact_check_answer(raw2, q);
                const std::string v1 = fc_verdict(out);
                const std::string v2 = fc_verdict(out2);
                log_info("fact_check agreement: v1=" + v1 + " v2=" + v2);
                if (v1 != v2) {
                    out = "Dəqiq bilmirəm. İki yoxlama fərqli nəticə verdi, ona "
                          "görə bu iddianı təsdiqləyə bilmirəm.";
                }
            } else {
                log_warn("fact_check refute pass failed: " + aerr);
            }
        }
        const AzWordStats ws = unknown_word_stats(out);
        const bool garbled =
            ws.total >= static_cast<size_t>(cfg_.garbled_word_min_words) &&
            ws.ratio() > cfg_.garbled_word_ratio_max;
        if (garbled) {
            log_warn("fact_check: garbled output (unknown=" +
                     std::to_string(ws.unknown) + "/" + std::to_string(ws.total) +
                     " ratio=" + std::to_string(ws.ratio()) +
                     ") -> uncertain verdict; draft=[" + utf8_prefix(out, 200) + "]");
            out = "Dəqiq bilmirəm. Cavabın dili etibarsız göründüyü üçün bu "
                  "iddianı təsdiqləyə bilmirəm.";
        }
        log_info("fact_check: chars=" + std::to_string(out.size()) +
                 " unknown=" + std::to_string(ws.unknown) + "/" +
                 std::to_string(ws.total) +
                 " verdict_head=[" + utf8_prefix(out, 40) + "]");
        res.answer = out;
        if (on_delta) on_delta(out);
        return res;
    }

    // --- 2. routing (question-type analysis) --------------------------------
    StageTimer t_route;
    const QuestionType qtype = retriever_.analyze(q);
    res.qtype = question_type_name(qtype);

    // --- 2a. Off-domain questions: no PDF, honest short answer --------------
    // The indexed books are agriculture/veterinary. For anything else the 4B
    // model is unreliable, so procedures/grammar/lists get a deterministic
    // refusal and simple facts/definitions get one short answer that must pass
    // the word-validity, subject and domain-drift checks.
    if (!has_agri_keyword(q)) {
        res.qtype = "general";
        res.no_sources = true;
        res.from_own_knowledge = true;
        res.general_answer = true;
        const bool grammar = is_grammar_topic(q);
        const bool procedure = is_procedure_request(q) || is_list_question(q);
        if (grammar || procedure) {
            res.answer = OUT_OF_SCOPE_MESSAGE;
            log_info(std::string("off-domain: ") +
                     (grammar ? "grammar" : "procedure/list") +
                     " -> honest refusal");
        } else {
            // Memory goes into the general answer prompt only: the PDF and
            // fact-check paths never see it, so it can never leak into a
            // citation. The block is capped by memory_injection_tokens.
            std::string memory_block;
            if (conversations_ && cfg_.memory_enabled &&
                cfg_.memory_injection_tokens > 0) {
                memory_block = ConversationStore::build_memory_block(
                    conversations_->facts(mem.client), q,
                    static_cast<size_t>(cfg_.memory_injection_tokens),
                    cfg_.token_chars_per_token > 0.0 ? cfg_.token_chars_per_token
                                                     : 2.6);
            }
            auto with_memory = [&](std::string prompt) {
                if (memory_block.empty()) return prompt;
                return memory_block + "\n" + prompt;
            };
            auto attempt = [&](const std::string& prompt,
                               const char* tag) -> std::string {
                ChatOptions g_opts = cfg_.chat_options(
                    std::min(cfg_.fact_check_num_predict, 220), 0.0);
                std::string gerr;
                ChatTiming gtiming;
                StageTimer t_g;
                const std::string raw = ollama_.chat(
                    chat_model_, {{"user", prompt}}, g_opts, &gerr, &gtiming);
                log_model_call(tag, chat_model_,
                               static_cast<long long>(t_g.ms()), gtiming.load_ms,
                               gtiming.prompt_eval_ms, gtiming.eval_ms,
                               gtiming.prompt_tokens, gtiming.eval_tokens,
                               gtiming.done_reason);
                return trim_count_answer(q, trim_to_last_sentence(
                    drop_trailing_uncertainty(strip_meta_headers(strip_bold_markers(
                        strip_meta_labels(trim(strip_clarification_offer(strip_own_sources(
                            OllamaClient::strip_thinking(raw))))))))));
            };
            // The vocabulary is built from the agriculture PDFs, so a correct
            // off-domain sentence ("100 gradus Santuslu") scores worse than a
            // domain answer. Allow up to 3 rare words in a real sentence.
            auto accept = [&](std::string& out, AzWordStats& ws,
                              bool& subject_ok) -> bool {
                if (fc_sentence_count(out) > 3) out = limit_sentences(out, 3);
                ws = unknown_word_stats(out);
                subject_ok = subject_mentioned(relevance_key_terms(q), out);
                // The vocabulary is agriculture-only, so a correct short
                // off-domain sentence carries a few "unknown" tokens (units,
                // names). Up to three rare words is not garbling.
                const bool word_ok =
                    ws.total == 0 || ws.unknown <= 3 ||
                    ws.ratio() <= cfg_.garbled_word_ratio_max;
                return !out.empty() && out.size() >= 12 &&
                       !is_degenerate_answer(out) && word_ok && subject_ok &&
                       !answer_uses_domain_markers(out) &&
                       out.find("dəqiq məlumatım yoxdur") == std::string::npos;
            };

            AzWordStats ws;
            bool subject_ok = false;
            std::string out = attempt(
                with_memory(build_general_fact_user_message(q)), "general_direct");
            bool ok = accept(out, ws, subject_ok);
            log_info("general_direct: chars=" + std::to_string(out.size()) +
                     " sentences=" + std::to_string(fc_sentence_count(out)) +
                     " unknown=" + std::to_string(ws.unknown) + "/" +
                     std::to_string(ws.total) +
                     " subject=" + (subject_ok ? "1" : "0") +
                     " accepted=" + (ok ? "1" : "0"));
            if (!ok) {
                log_warn("general_direct: rejected, one clean retry; draft=[" +
                         utf8_prefix(out, 160) + "]");
                std::string out2 = attempt(
                    with_memory(build_plain_answer_message(q)), "general_retry");
                AzWordStats ws2;
                bool subject2 = false;
                if (accept(out2, ws2, subject2)) {
                    out = out2;
                    ws = ws2;
                    subject_ok = subject2;
                    ok = true;
                    log_info("general_retry: accepted chars=" +
                             std::to_string(out.size()) + " unknown=" +
                             std::to_string(ws.unknown) + "/" +
                             std::to_string(ws.total));
                }
            }
            if (ok) {
                res.answer = out;
                if (!memory_block.empty()) {
                    res.used_memory = true;
                    res.answer += "\n\n[Söhbət yaddaşı]";
                }
            } else {
                res.answer = OUT_OF_SCOPE_MESSAGE;
                log_warn("general_direct: answer rejected - honest refusal; draft=[" +
                         utf8_prefix(out, 200) + "]");
            }
        }
        if (on_delta) on_delta(res.answer);
        return res;
    }

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
    const double cpt = cfg_.token_chars_per_token > 0.0 ? cfg_.token_chars_per_token : 2.6;
    const double ms_route = t_route.ms();

    // --- 2a2. follow-up rewrite --------------------------------------------
    // "bəs onun müalicəsi necədir?" is meaningless to the retriever on its own;
    // the previous user topic is prepended for RETRIEVAL ONLY (the fact-check
    // path returns before this point, so memory never reaches a verdict).
    std::string q_retrieval = q;
    if (conversations_ && !mem.conversation.empty() && is_followup_question(q)) {
        const std::string prev =
            conversations_->last_user_text(mem.client, mem.conversation);
        if (prev.size() >= 8 && az_fold(prev) != az_fold(q)) {
            q_retrieval = prev + " - " + q;
            log_info("follow-up: retrieval query inherits the previous topic");
        }
    }

    // --- 2b. query expansion (one model call, helper model) ----------------
    StageTimer t_expand;
    // Expansion is an extra model call; general/off-topic and low-information
    // messages gain nothing from it (it once cost 20 s on a cold model).
    const bool skip_expansion =
        short_non_topic ||
        (qtype == QuestionType::General && cfg_.expansion_skip_for_general);
    const std::vector<std::string> queries =
        skip_expansion ? std::vector<std::string>{q_retrieval}
                       : retriever_.expand_query(q_retrieval, qtype);
    const double ms_expand = t_expand.ms();
    log_info("expansion queries (" + std::to_string(queries.size()) +
             (skip_expansion ? ", skipped" : "") + "): " + join(queries, " | "));

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
            o.chunks =
                retriever_.retrieve_web(q_retrieval, queries, (size_t)top_k, &o.warning);
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
        // Rule (b): at least TWO distinct main entities of the question have
        // to occur in the retained chunk (one when the question only has one).
        const size_t need = terms.empty() ? 0 : std::min<size_t>(2, terms.size());
        const bool lex_ok = !terms.empty() && matched >= need &&
                            overlap >= cfg_.relevance_min_overlap;
        const bool score_ok = matched >= need &&
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
        } else if (qtype == QuestionType::General && cfg_.expansion_skip_for_general) {
            log_info("pipeline: general question - rerank skipped");
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
    const bool do_map_reduce = !fast_path && total_tokens(selected, cpt) > mapred_limit &&
                               selected.size() > 6;
    std::vector<Chunk> context_chunks = selected;
    if (do_map_reduce) {
        log_info("Kontekst çox böyükdür (" + std::to_string(total_tokens(selected, cpt)) +
                 " token > " + std::to_string(mapred_limit) +
                 ") - map-reduce xülasələri işə salınır");
        context_chunks = map_reduce_summarize(q, selected);
        res.used_map_reduce = true;
    }

    // --- 5b. context budget from the ACTUAL request ------------------------
    // estimate_tokens (words*1.6) underestimated Azerbaijani text by ~3x and
    // let a 4.3 k-token request through a 1.2 k-token limit (HTTP 400). The
    // reserve is now the real system prompt + history + question + answer cap.
    {
        size_t fixed = estimate_tokens_az(SYSTEM_PROMPT, cpt) +
                       estimate_tokens_az(q, cpt) +
                       static_cast<size_t>(std::max(0, cfg_.num_predict_draft)) +
                       static_cast<size_t>(std::max(0, cfg_.context_safety_margin));
        for (const auto& m : sessions_.history(session_id))
            fixed += estimate_tokens_az(m.content, cpt) + 8;
        const size_t limit = cfg_.num_ctx > static_cast<int>(fixed) + 256
                                 ? static_cast<size_t>(cfg_.num_ctx) - fixed
                                 : size_t(256);
        const size_t before_trim = context_chunks.size();
        while (context_chunks.size() > 1 && total_tokens(context_chunks, cpt) > limit) {
            context_chunks.pop_back();  // lowest-ranked chunk first
        }
        if (context_chunks.size() != before_trim) {
            log_info("context trimmed to fit num_ctx: " +
                     std::to_string(before_trim) + " -> " +
                     std::to_string(context_chunks.size()) + " chunks (limit=" +
                     std::to_string(limit) + " est. tokens)");
        }
    }

    // --- 6. draft answer (streamed) ----------------------------------------
    const int requested_sentences = requested_sentence_count(q);
    const std::string sentence_rule =
        requested_sentences > 0
            ? " Cavabı düz " + std::to_string(requested_sentences) + " cümlə ilə ver."
            : std::string();

    auto build_user_msg = [&](const std::vector<Chunk>& ctx) {
        if (pdf_relevance_failed) {
            return std::string(
                       "KONTEKST: Yüklənmiş PDF sənədlərində bu sual üzrə məlumat YOXDUR. "
                       "Sual aqronomiya/maldarlıq mövzusundan kənar ola bilər - bu halda imtina "
                       "ETMƏ, ümumi biliklərinlə qısa, dəqiq və faydalı cavab ver. Heç bir "
                       "[Mənbə: ...] etiketi yazma, yalnız [Ümumi bilik] istifadə et. Suala "
                       "birbaşa, tam cavab yaz.") + sentence_rule + "\n\nSUAL: " + q;
        }
        if (ctx.empty()) return build_no_context_user_message(q, sentence_rule);
        return build_context_block(ctx) + "\n\n" + build_draft_user_message(q);
    };
    auto build_messages = [&](const std::vector<Chunk>& ctx) {
        std::vector<ChatMessage> msgs;
        msgs.push_back({"system", SYSTEM_PROMPT});
        for (const auto& m : sessions_.history(session_id)) msgs.push_back(m);
        msgs.push_back({"user", build_user_msg(ctx)});
        return msgs;
    };

    // Tuned sampling (P3): temperature 0.25 with top_p 0.9 and
    // repeat_penalty 1.1 - the previous draft used 0.3 and sent neither
    // top_p nor repeat_penalty to Ollama at all.
    ChatOptions draft_opts = cfg_.chat_options(cfg_.num_predict_draft, 0.25);
    StageTimer t_draft;
    std::string derr;
    std::string draft;
    ChatTiming draft_timing;
    std::vector<Chunk> attempt_chunks = context_chunks;
    std::vector<ChatMessage> messages;
    for (int attempt = 0; attempt < 3; ++attempt) {
        messages = build_messages(attempt_chunks);
        derr.clear();
        draft = on_delta ? ollama_.chat_stream(chat_model_, messages, draft_opts, on_delta,
                                              &derr, &draft_timing)
                         : ollama_.chat(chat_model_, messages, draft_opts, &derr,
                                        &draft_timing);
        log_model_call(attempt == 0 ? "draft" : "draft_ctx_retry", chat_model_,
                       static_cast<long long>(t_draft.ms()), draft_timing.load_ms,
                       draft_timing.prompt_eval_ms, draft_timing.eval_ms,
                       draft_timing.prompt_tokens, draft_timing.eval_tokens,
                       draft_timing.done_reason);
        if (!draft.empty()) break;
        const bool too_large = derr.find("context") != std::string::npos ||
                               derr.find("exceeds") != std::string::npos ||
                               derr.find("400") != std::string::npos;
        if (!too_large || attempt_chunks.size() <= 1) break;
        const size_t keep = std::max<size_t>(1, attempt_chunks.size() / 2);
        log_warn("draft request exceeded the context window - retrying with " +
                 std::to_string(keep) + " chunk(s)");
        attempt_chunks.resize(keep);
    }
    const std::string user_msg = build_user_msg(attempt_chunks);
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
        // Never show raw HTTP/JSON/token counts to the user; the detail is
        // logged for diagnosis and a clean Azerbaijani message is returned.
        log_error("draft failed: " + derr);
        res.warning = "llm_failed";
        res.answer =
            "Cavabı hazırlaya bilmədəm. Zəhmət olmasa sualı bir az qısaldın və ya "
            "yenidən cəhd edin.";
        if (on_delta) on_delta(res.answer);
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
    // Rule (c): a label whose page text shares no key entity with the sentence
    // it supports is not a source - drop it.
    final_answer = enforce_citation_support(final_answer, selected);

    // --- 8. strip model-written source list, add notes + safety warning ----
    // Sources appear ONLY in the API's sources list (UI collapsible), never
    // as a model-written "İstifadə olunan mənbələr" block in the answer text.
    // Remove a model-written source list, any echoed silent-check block, and
    // every copy of the safety line (it is appended back exactly once, at the
    // very end, only when the question concerns health/diagnosis/treatment).
    final_answer = strip_trailing_source_lines(
        strip_silent_check(strip_safety_line(strip_own_sources(final_answer))));

    // --- 7a. word-validity (garbled text) guard ----------------------------
    // If more than the allowed share of words are non-words, one deterministic
    // retry with a shorter budget; if that is still garbled, an honest short
    // answer is better than a confident nonsense paragraph.
    {
        const AzWordStats ws = unknown_word_stats(final_answer);
        const bool checkable =
            ws.total >= static_cast<size_t>(cfg_.garbled_word_min_words);
        if (checkable && ws.ratio() > cfg_.garbled_word_ratio_max) {
            log_warn("garbled check: unknown=" + std::to_string(ws.unknown) + "/" +
                     std::to_string(ws.total) + " ratio=" + std::to_string(ws.ratio()) +
                     " > " + std::to_string(cfg_.garbled_word_ratio_max) +
                     " - retry at temperature 0.1 with shorter num_predict");
            ChatOptions retry_opts =
                cfg_.chat_options(std::min(cfg_.num_predict_draft, 260), 0.1);
            std::string gerr;
            ChatTiming gtiming;
            StageTimer t_g;
            std::string again =
                ollama_.chat(chat_model_, messages, retry_opts, &gerr, &gtiming);
            log_model_call("draft_garbled_retry", chat_model_,
                           static_cast<long long>(t_g.ms()), gtiming.load_ms,
                           gtiming.prompt_eval_ms, gtiming.eval_ms,
                           gtiming.prompt_tokens, gtiming.eval_tokens,
                           gtiming.done_reason);
            again = trim_to_last_sentence(strip_meta_labels(
                trim(strip_clarification_offer(strip_own_sources(
                    OllamaClient::strip_thinking(again))))));
            const AzWordStats ws2 = unknown_word_stats(again);
            if (!again.empty() && !is_degenerate_answer(again) &&
                (!ws2.total || ws2.ratio() <= cfg_.garbled_word_ratio_max)) {
                final_answer = again;
                log_info("garbled retry accepted (unknown=" +
                         std::to_string(ws2.unknown) + "/" +
                         std::to_string(ws2.total) + ")");
            } else {
                final_answer = GARBLED_FALLBACK_MESSAGE;
                log_warn("garbled retry still invalid - honest short answer");
            }
        } else if (ws.total) {
            log_info("word check: unknown=" + std::to_string(ws.unknown) + "/" +
                     std::to_string(ws.total) + " ratio=" +
                     std::to_string(ws.ratio()));
        }
    }
    // --- 7b. answer-quality gate ------------------------------------------
    // Never return an empty answer, a bare clarification offer, a generation
    // loop or (for a general question) an answer that drifted into the
    // agriculture domain. One strict retry, then an honest failure.
    const bool degenerate = is_degenerate_answer(final_answer);
    const bool repetitive = has_repetition_loop(final_answer);
    const bool off_domain = pdf_relevance_failed &&
                            answer_uses_domain_markers(final_answer) &&
                            !answer_uses_domain_markers(q);
    // Relaxed false-rejection rule (P4): an explicit sentence limit ("bir
    // cümlə ilə") and short factual answers are legitimately short; the old
    // 80-character floor turned them into "cavab yarada bilmədim".
    const bool short_ok = requested_sentences > 0 || qtype == QuestionType::General;
    const bool too_short =
        cfg_.quality_check_enabled && !degenerate && !short_ok &&
        final_answer.size() <
            static_cast<size_t>(std::max(0, cfg_.quality_min_answer_chars));
    if (degenerate || too_short || repetitive || off_domain) {
        log_warn("quality gate: reject reasons=[degenerate=" +
                 std::string(degenerate ? "1" : "0") + ",repetitive=" +
                 std::string(repetitive ? "1" : "0") + ",off_domain=" +
                 std::string(off_domain ? "1" : "0") + ",too_short=" +
                 std::string(too_short ? "1" : "0") + "(" +
                 std::to_string(final_answer.size()) + "<" +
                 std::to_string(cfg_.quality_min_answer_chars) + ")] draft=[" +
                 utf8_prefix(final_answer, 120) + "] - strict retry");
        std::string serr;
        ChatTiming stiming;
        StageTimer t_strict;
        std::string retry = ollama_.chat(
            chat_model_, {{"user", build_strict_answer_message(q)}},
            cfg_.chat_options(std::min(cfg_.num_predict_draft, 400), 0.0), &serr, &stiming);
        log_model_call("strict_retry", chat_model_,
                       static_cast<long long>(t_strict.ms()), stiming.load_ms,
                       stiming.prompt_eval_ms, stiming.eval_ms,
                       stiming.prompt_tokens, stiming.eval_tokens, stiming.done_reason);
        retry = trim(strip_dangling_source_label(strip_clarification_offer(
            strip_silent_check(strip_safety_line(strip_own_sources(
                OllamaClient::strip_thinking(retry)))))));
        const bool retry_ok = !retry.empty() && !is_degenerate_answer(retry) &&
                              !has_repetition_loop(retry) &&
                              (!off_domain || !answer_uses_domain_markers(retry)) &&
                              (degenerate || retry.size() > final_answer.size());
        if (retry_ok) {
            final_answer = retry;
            log_info("quality gate: strict retry accepted (" +
                     std::to_string(final_answer.size()) + " chars)");
        } else if (degenerate || repetitive || off_domain) {
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
    if (selected.empty() && requested_sentences == 0 &&
        cfg_.max_general_sentences > 0) {
        final_answer = limit_sentences(final_answer, cfg_.max_general_sentences);
    }
    // Never return an answer that stops mid-word / mid-sentence.
    final_answer = trim_to_last_sentence(final_answer);
    res.used_chunks = cited_chunks(final_answer, selected);

    // Deterministic provenance fallback: the 4B model often answers from the
    // kept context without writing a "[Mənbə: ...]" label, which left grounded
    // livestock answers with zero PDF sources. Cite the best non-web chunk once
    // when the answer really shares key terms with it.
    if (final_answer.find("[Mənbə:") == std::string::npos && !selected.empty()) {
        const auto terms = relevance_key_terms(q);
        for (const auto& c : selected) {
            if (c.kind == "web" || c.source.empty()) continue;
            const std::string body = az_fold(c.text);
            size_t shared = 0;
            for (const auto& t : terms)
                if (t.size() >= 3 && body.find(t) != std::string::npos) ++shared;
            if (shared < 2) continue;
            std::string name = c.source;
            const size_t slash = name.find_last_of("/\\");
            if (slash != std::string::npos) name = name.substr(slash + 1);
            // Provenance goes to sources, never into the answer text.
            res.used_chunks.push_back(c);
            log_info("citation fallback: attached " + name + " p." +
                     std::to_string(c.page) + " (shared terms=" +
                     std::to_string(shared) + ")");
            break;
        }
    }

    // --- 8b. the text is the answer only -----------------------------------
    // Notes, disclaimers and citations are structured fields now (sources /
    // notes_ui); nothing is appended to the answer text.
    if (selected.empty()) {
        res.from_own_knowledge = true;
        res.general_answer = true;
    }
    if (web_enabled && !web_out.warning.empty() && !res.used_web) {
        res.warning = "web_failed: " + web_out.warning;
    }
    res.safety_warning = needs_safety_warning(q);
    // --- debug trace: collected always, returned only for a debug request ---
    {
        nlohmann::json pc = nlohmann::json::array();
        for (const auto& c : pdf_chunks)
            pc.push_back({{"source", c.source}, {"page", c.page}});
        nlohmann::json sel = nlohmann::json::array();
        for (const auto& c : selected)
            sel.push_back({{"source", c.source}, {"page", c.page}});
        nlohmann::json qs = nlohmann::json::array();
        for (const auto& x : queries) qs.push_back(x);
        nlohmann::json t;
        t["route"] = res.qtype;
        t["route_why"] = fast_path ? "fast_path: simple fact, rerank skipped"
                                    : "full path: rerank + diversify";
        t["query"] = q;
        t["rewritten"] = q_retrieval;
        t["expansion"] = {{"skipped", skip_expansion}, {"queries", qs}};
        t["retrieval"] = {{"top_k", top_k},
                           {"max_score", pdf_result.max_score},
                           {"min_score", cfg_.min_score},
                           {"dropped_by_min_score",
                            pdf_result.max_score > 0.0f && pdf_result.max_score < cfg_.min_score},
                           {"chunks", pc}};
        t["selected"] = sel;
        t["prompt"] = messages.empty() ? std::string()
                                        : utf8_prefix(messages.back().content, 6000);
        t["sampling"] = {{"model", chat_model_},
                          {"num_predict", cfg_.num_predict_draft},
                          {"num_ctx", cfg_.num_ctx},
                          {"temperature", 0.25}};
        t["timings_ms"] = {{"route", ms_route}, {"expand", ms_expand},
                            {"pdf", ms_pdf}, {"web", ms_web},
                            {"rerank", ms_rerank}, {"draft", ms_draft},
                            {"self_check", ms_check}, {"post", t_post.ms()},
                            {"total", t_total.ms()}};
        res.trace = t;
    }
    res.answer = final_answer;
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


