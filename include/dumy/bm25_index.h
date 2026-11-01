// bm25_index.h - classic BM25 keyword index over the same chunks the vector
// store holds. Rebuilt from chunk texts in memory at startup (cheap), so it
// never needs its own persistence.
#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "dumy/vector_store.h"

namespace dumy {

class Bm25Index {
public:
    // (Re)builds the index from the given chunks. Thread-safe.
    void build(const std::vector<Chunk>& chunks);

    // Top-k chunk ids for a query, best first. Thread-safe.
    std::vector<int> search(const std::string& query, size_t k) const;

    size_t doc_count() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return doclen_.size();
    }

    // Lowercases, strips punctuation, drops a tiny stopword list.
    static std::vector<std::string> tokenize(const std::string& text);

private:
    struct Posting {
        int doc = 0;
        int tf = 0;
    };
    std::unordered_map<std::string, std::vector<Posting>> postings_;
    std::vector<int> doclen_;
    std::vector<int> doc_ids_;  // doc position -> Chunk::id
    double avgdl_ = 0.0;
    mutable std::mutex mutex_;  // guards build() vs concurrent search()

    static constexpr double K1 = 1.5;
    static constexpr double B = 0.75;
};

}  // namespace dumy
