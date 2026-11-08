// reranker.cpp - scores chunks with one LLM call and keeps the best ones.
#include "dumy/reranker.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_set>

#include "dumy/common.h"

namespace dumy {
namespace {

// Preview used inside the rerank prompt (keeps tokens bounded).
// MUST NOT cut in the middle of a UTF-8 sequence (JSON dump would fail).
std::string preview(const std::string& s, size_t max_chars = 320) {
    if (s.size() <= max_chars) return s;
    size_t cut = max_chars;
    // back off while the byte at `cut` is a UTF-8 continuation byte
    while (cut > 0 &&
           (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80)
        --cut;
    return s.substr(0, cut) + "...";
}

}  // namespace

Reranker::Reranker(const Config& cfg, const OllamaClient& ollama)
    : cfg_(cfg), ollama_(ollama) {}

void Reranker::set_models(const std::string& chat_model, const std::string& helper_model) {
    chat_model_ = chat_model;
    helper_model_ = helper_model;
}

std::vector<Chunk> Reranker::rerank(const std::string& query,
                                    const std::vector<Chunk>& candidates,
                                    size_t keep, std::string* err) const {
    if (candidates.size() <= keep) {
        // Still deduplicate even when nothing has to be dropped.
        std::vector<Chunk> out;
        std::unordered_set<size_t> seen;
        for (const auto& c : candidates) {
            size_t h = fnv1a64(c.text.data(), c.text.size());
            if (!seen.insert(h).second) continue;
            out.push_back(c);
            if (out.size() >= keep) break;
        }
        return out;
    }

    // 1) Deduplicate identical texts before spending an LLM call on them.
    std::vector<Chunk> uniq;
    std::unordered_set<size_t> seen;
    for (const auto& c : candidates) {
        size_t h = fnv1a64(c.text.data(), c.text.size());
        if (seen.insert(h).second) uniq.push_back(c);
    }
    // Bound the scoring cost: only the retrieval-order head is ever worth
    // scoring (the tail does not survive reranking anyway). This keeps the
    // draft-model rerank call fast on a 6 GB GPU.
    constexpr size_t MAX_TO_SCORE = 16;
    if (uniq.size() > MAX_TO_SCORE) uniq.resize(MAX_TO_SCORE);
    log_info("rerank: " + std::to_string(candidates.size()) + " candidates, " +
             std::to_string(uniq.size()) + " unique");

    // 2) Ask the LLM to score each snippet 0-10 for relevance.
    // Small models (qwen3:4b) tend to write a prose preamble before the JSON
    // and burn their token budget on it, so: a strict JSON-only system
    // message, a compact prompt, the format example repeated at the END.
    std::ostringstream prompt;
    prompt << "Question: " << query << "\n\nSnippets:\n";
    for (size_t i = 0; i < uniq.size(); ++i) {
        prompt << "[" << (i + 1) << "] (" << uniq[i].label() << ")\n"
               << preview(uniq[i].text, 220) << "\n\n";
    }
    prompt << "Score ALL " << uniq.size()
           << " snippets 0-10 for relevance to the question. The JSON array "
              "must contain exactly " << uniq.size()
           << " objects, one per snippet, in order from id=1 to id="
           << uniq.size() << ". Reply with ONLY the JSON array, e.g. "
              "[{\"id\":1,\"score\":8},{\"id\":2,\"score\":3}] - no words "
              "before or after, and do not stop after the first item.";

    // Score with the DRAFT model, not the small helper: the helper's scores
    // on long Azerbaijani medical texts are noise (10,2,2,1,1,0... then
    // 3,2,0,... on the same question). The draft model is loaded right before
    // drafting anyway, so this costs no extra model swap on a 6 GB GPU.
    const std::string model = chat_model_.empty() ? helper_model_ : chat_model_;
    if (model.empty()) {
        if (err) *err = "no model configured for reranking";
        std::vector<Chunk> out(uniq.begin(),
                               uniq.begin() + static_cast<long>(std::min(keep, uniq.size())));
        return out;
    }

    // Centralized load-affecting options (num_ctx, keep_alive): a different
    // num_ctx here would force a second 7B load between rerank and draft.
    ChatOptions opts = cfg_.chat_options(600, 0.0);

    std::string llm_err;
    log_info("rerank: calling " + model + " (" + std::to_string(prompt.str().size()) +
             " chars prompt)...");
    ChatTiming rk_timing;
    const auto t0 = std::chrono::steady_clock::now();
    std::string reply = ollama_.chat(
        model,
        {{"system", "You are a JSON-only classifier. Output ONLY valid JSON. "
                     "Never write prose, explanations, greetings or thinking."},
         {"user", prompt.str()}},
        opts, &llm_err, &rk_timing);
    log_model_call("rerank", model,
                   std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - t0)
                       .count(),
                   rk_timing.load_ms, rk_timing.prompt_eval_ms, rk_timing.eval_ms,
                   rk_timing.prompt_tokens, rk_timing.eval_tokens);
    log_info("rerank: LLM replied " + std::to_string(reply.size()) + " chars" +
             (reply.empty() ? (" (err: " + llm_err + ")") : std::string()));
    if (reply.empty()) {
        if (err) *err = "rerank LLM call failed: " + llm_err;
        std::vector<Chunk> out(uniq.begin(),
                               uniq.begin() + static_cast<long>(std::min(keep, uniq.size())));
        return out;
    }

    // 3) Parse scores tolerantly: a JSON array anywhere in the reply, then a
    // regex-like scan for {"id":N,"score":N} pairs (models sometimes emit
    // objects or prose around them).
    std::vector<std::pair<int, double>> scores;  // (0-based index, score)
    size_t lb = reply.find('[');
    size_t rb = reply.rfind(']');
    if (lb != std::string::npos && rb != std::string::npos && rb > lb) {
        try {
            auto arr = nlohmann::json::parse(reply.substr(lb, rb - lb + 1));
            for (const auto& item : arr) {
                int id = item.value("id", -1);
                double sc = item.value("score", -1.0);
                if (id < 1 || id > static_cast<int>(uniq.size())) continue;
                if (sc < 0) continue;
                scores.emplace_back(id - 1, std::min(sc, 10.0));
            }
        } catch (const std::exception& e) {
            if (err) *err = std::string("rerank JSON parse error: ") + e.what();
        }
    }
    // Fallback: scan the whole reply for {"id":N,"score":N} occurrences.
    if (scores.empty()) {
        size_t pos = 0;
        while ((pos = reply.find("\"id\"", pos)) != std::string::npos) {
            try {
                size_t end = reply.find('}', pos);
                if (end == std::string::npos) break;
                std::string frag = reply.substr(pos - 1, end - pos + 2);
                auto item = nlohmann::json::parse("{" + frag);
                int id = item.value("id", -1);
                double sc = item.value("score", -1.0);
                if (id >= 1 && id <= static_cast<int>(uniq.size()) && sc >= 0)
                    scores.emplace_back(id - 1, std::min(sc, 10.0));
            } catch (...) {
            }
            pos += 4;
        }
        if (!scores.empty() && err) err->clear();
    }

    if (scores.size() < 4) {
        log_warn("rerank: only " + std::to_string(scores.size()) +
                 " usable scores - keeping original order");
        if (err && err->empty()) *err = "rerank scores incomplete - kept original order";
        std::vector<Chunk> out(uniq.begin(),
                               uniq.begin() + static_cast<long>(std::min(keep, uniq.size())));
        return out;
    }

    std::stable_sort(scores.begin(), scores.end(),
                     [](const auto& a, const auto& b) { return a.second > b.second; });

    {
        std::string dbg;
        for (size_t i = 0; i < scores.size() && i < 14; ++i) {
            dbg += std::to_string(static_cast<int>(scores[i].second + 0.5));
            if (i + 1 < scores.size() && i < 13) dbg += ",";
        }
        log_info("rerank scores (best first): " + dbg);
    }

    // Step 3: keep ONLY chunks the model scored >= rerank_min_score (default
    // 8/10). Weak chunks are worse than no context: they produce unrelated
    // citations and trigger map-reduce, so they are dropped here and the
    // pipeline falls back to general knowledge when nothing survives.
    const double min_score = static_cast<double>(cfg_.rerank_min_score);
    std::vector<Chunk> out;
    for (const auto& s : scores) {
        if (s.second < min_score) break;  // scores are sorted best-first
        out.push_back(uniq[s.first]);
        if (out.size() >= keep) break;
    }
    log_info("rerank: kept " + std::to_string(out.size()) + "/" +
             std::to_string(uniq.size()) + " chunks (" +
             std::to_string(scores.size()) + " scored, min_score=" +
             std::to_string(min_score) + ")");
    if (out.empty()) {
        log_info("rerank: heç bir chunk min_score-a çatmadı - ümumi bilik yolu");
    }
    if (err) err->clear();
    return out;
}

}  // namespace dumy
