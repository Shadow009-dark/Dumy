#include "dumy/bm25_index.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "dumy/common.h"

namespace dumy {
namespace {

// A very small stopword list: enough to stop "və", "the" style noise from
// dominating BM25 without removing terms that carry meaning.
const std::unordered_set<std::string>& stopwords() {
    static const std::unordered_set<std::string> s = {
        // Azerbaijani
        "və", "bir", "bu", "o", "ki", "üçün", "ilə", "nə", "isə", "dır", "dir",
        "var", "yox", "ən", "bu", "həm", "də", "da",
        // English
        "the", "a", "an", "of", "and", "or", "is", "are", "was", "were", "to",
        "in", "on", "for", "with", "as", "by", "be", "it", "this", "that", "at",
        "from", "we", "you",
    };
    return s;
}

}  // namespace

std::vector<std::string> Bm25Index::tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::string lowered = to_lower_utf8(text);
    std::string cur;
    auto flush = [&] {
        if (cur.empty()) return;
        if (cur.size() > 1 || !stopwords().count(cur)) tokens.push_back(cur);
        cur.clear();
    };
    for (unsigned char c : lowered) {
        // ASCII letters/digits continue a token; bytes >= 0x80 are UTF-8
        // continuation/lead bytes and must also stay in the token.
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c >= 0x80) {
            cur.push_back(static_cast<char>(c));
        } else {
            flush();
        }
    }
    flush();
    return tokens;
}

void Bm25Index::build(const std::vector<Chunk>& chunks) {
    std::lock_guard<std::mutex> lk(mutex_);
    postings_.clear();
    doclen_.clear();
    doc_ids_.clear();
    doclen_.reserve(chunks.size());
    doc_ids_.reserve(chunks.size());

    std::vector<std::unordered_map<std::string, int>> docs(chunks.size());
    size_t total = 0;
    for (size_t i = 0; i < chunks.size(); ++i) {
        auto& counts = docs[i];
        for (const auto& tok : tokenize(chunks[i].text)) counts[tok]++;
        int len = 0;
        for (const auto& kv : counts) len += kv.second;
        doclen_.push_back(len);
        doc_ids_.push_back(chunks[i].id);
        total += static_cast<size_t>(len);
    }
    avgdl_ = doclen_.empty() ? 0.0 : static_cast<double>(total) / doclen_.size();

    for (size_t i = 0; i < docs.size(); ++i) {
        for (const auto& kv : docs[i]) {
            postings_[kv.first].push_back(
                Posting{static_cast<int>(i), kv.second});
        }
    }
}

std::vector<int> Bm25Index::search(const std::string& query, size_t k) const {
    std::lock_guard<std::mutex> lk(mutex_);
    const size_t N = doclen_.size();
    if (N == 0) return {};

    std::vector<float> scores(N, 0.f);
    std::unordered_set<std::string> seen;  // ignore repeated query terms
    for (const auto& term : tokenize(query)) {
        if (!seen.insert(term).second) continue;
        auto it = postings_.find(term);
        if (it == postings_.end()) continue;
        const auto& post = it->second;
        const double df = static_cast<double>(post.size());
        const double idf = std::log((N - df + 0.5) / (df + 0.5) + 1.0);
        for (const auto& p : post) {
            const double dl = static_cast<double>(doclen_[p.doc]);
            const double tf = static_cast<double>(p.tf);
            const double denom = tf + K1 * (1.0 - B + B * dl / (avgdl_ > 0 ? avgdl_ : 1.0));
            scores[p.doc] += static_cast<float>(idf * tf * (K1 + 1.0) / denom);
        }
    }

    std::vector<std::pair<int, float>> ranked;
    for (size_t i = 0; i < scores.size(); ++i)
        if (scores[i] > 0) ranked.emplace_back(static_cast<int>(i), scores[i]);

    std::sort(ranked.begin(), ranked.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    if (ranked.size() > k) ranked.resize(k);

    std::vector<int> out;
    out.reserve(ranked.size());
    for (auto& p : ranked) out.push_back(doc_ids_[p.first]);
    return out;
}

}  // namespace dumy
