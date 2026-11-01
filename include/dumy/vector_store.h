// vector_store.h - chunk storage + cosine vector search behind an abstract
// interface, so the implementation can be swapped for Qdrant/SQLite later.
// The reference implementation keeps everything in memory and persists to a
// compact binary file (metadata as JSON + raw float32 embeddings).
#pragma once

#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

#include "dumy/common.h"

namespace dumy {

struct Chunk {
    int id = -1;
    std::string text;     // chunk text (already normalized)
    std::string source;   // PDF file name, or host+path for web pages
    int page = 0;         // 1-based PDF page, 0 for web
    std::string url;      // full URL for web chunks, empty for PDFs
    std::string title;    // web page title ("" for PDFs)
    std::string kind;     // "pdf" | "web"
    bool trusted = false; // true for configured trusted domains
    std::vector<float> embedding;  // L2-normalized

    // Human-readable source label used inside prompts and answers:
    //   [Mənbə: fayl.pdf, səh. 12]  /  [İnternet: https://...]
    std::string label() const;

    nlohmann::json meta_json() const;   // everything except the embedding
    static Chunk from_meta_json(const nlohmann::json& j);
};

class IVectorStore {
public:
    virtual ~IVectorStore() = default;

    virtual void add(std::vector<Chunk> chunks) = 0;
    virtual bool remove_source(const std::string& source) = 0;
    virtual void clear() = 0;
    virtual size_t size() const = 0;

    // Copies of the chunks with the given ids (order preserved).
    virtual std::vector<Chunk> get(const std::vector<int>& ids) const = 0;
    // Cosine top-k over stored embeddings (query is normalized internally).
    virtual std::vector<std::pair<int, float>> search(const std::vector<float>& query,
                                                      size_t k) const = 0;
    // Every chunk (used to rebuild BM25 and to re-save).
    virtual std::vector<Chunk> all() const = 0;

    virtual bool save(const fs::path& file, std::string* err) const = 0;
    virtual bool load(const fs::path& file, std::string* err) = 0;
};

class InMemoryVectorStore : public IVectorStore {
public:
    void add(std::vector<Chunk> chunks) override;
    bool remove_source(const std::string& source) override;
    void clear() override;
    size_t size() const override;
    std::vector<Chunk> get(const std::vector<int>& ids) const override;
    std::vector<std::pair<int, float>> search(const std::vector<float>& query,
                                              size_t k) const override;
    std::vector<Chunk> all() const override;
    bool save(const fs::path& file, std::string* err) const override;
    bool load(const fs::path& file, std::string* err) override;

    // Assigns ids to incoming chunks (used by the indexer before embedding).
    static void assign_ids(std::vector<Chunk>& chunks, int start_id);
    static void normalize(std::vector<float>& v);

private:
    mutable std::mutex mutex_;
    std::vector<Chunk> chunks_;
    int next_id_ = 0;
};

using VectorStorePtr = std::shared_ptr<IVectorStore>;

}  // namespace dumy
