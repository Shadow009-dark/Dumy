// indexer.h - scans the pdf folders, extracts, chunks and embeds new or
// changed files (incremental via a content-hash manifest), keeps the vector
// store and BM25 index in sync and persists everything to data/.
//
// Ingest is resumable: the index + manifest are saved after EVERY file, so a
// stopped run continues where it left off. Near-duplicate PDFs (simhash of
// the extracted text) are detected and skipped so the same text is not
// indexed - or cited - twice.
#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "dumy/bm25_index.h"
#include "dumy/config.h"
#include "dumy/ollama_client.h"
#include "dumy/vector_store.h"

namespace dumy {

class Indexer {
public:
    // One row of the per-file ingest table.
    struct FileStat {
        std::string file;          // key relative to the project root
        int pages = 0;
        int chunks = 0;
        int scanned_pages = 0;     // pages with too little extractable text
        std::string status;        // ok | mostly_scanned | duplicate | failed
        std::string note;          // error or "duplicate of X"
    };

    struct Report {
        int files_seen = 0;
        int files_indexed = 0;
        int files_skipped = 0;    // unchanged since last run
        int files_failed = 0;
        int files_duplicate = 0;  // near-identical text, not indexed
        int chunks_added = 0;
        int chunks_removed = 0;
        int scanned_pages = 0;    // pages with too little text (OCR candidates)
        bool ok = true;
        std::string message;      // human-readable summary / first error
        std::vector<FileStat> files;  // per-file table (all files seen)
    };

    // Progress callback: (file_index_1_based, total_files, file_key, eta_seconds).
    using ProgressFn =
        std::function<void(int, int, const std::string&, long)>;

    Indexer(const Config& cfg, const OllamaClient& ollama, VectorStorePtr store,
            Bm25Index& bm25);

    void set_embedding_model(const std::string& model) { embed_model_ = model; }
    void set_progress(ProgressFn fn) { progress_ = std::move(fn); }

    // Rescans the folders. Incremental: only new/changed files are re-embedded
    // (content hash). Thread-safe: concurrent calls queue behind the mutex.
    Report ingest(bool force = false);

    // Rebuilds the BM25 index from the store (startup / after external edits).
    void rebuild_bm25();

    // Loads the persisted index from disk (call once at startup).
    bool load_index(std::string* err);

    // Chunk count currently in the store (for /health and /documents).
    size_t chunk_count() const { return store_->size(); }

    // JSON list of indexed documents: [{file, chunks, pages, status, ...}].
    nlohmann::json documents_json() const;

private:
    bool save_all(std::string* err);

    const Config& cfg_;
    const OllamaClient& ollama_;
    VectorStorePtr store_;
    Bm25Index& bm25_;
    std::string embed_model_;
    ProgressFn progress_;
    mutable std::mutex mutex_;
};

// 64-bit simhash of a text - used to detect near-identical PDFs.
std::uint64_t simhash64(const std::string& text);
// Hamming distance between two simhashes (0 = identical).
int simhash_distance(std::uint64_t a, std::uint64_t b);

}  // namespace dumy
