// indexer.cpp - incremental PDF ingestion with a content-hash manifest.
//
// Resumable: after EVERY file the index + manifest are written, so an
// interrupted run continues without losing finished work.
// Duplicate detection: a 64-bit simhash over the extracted text flags
// near-identical PDFs (e.g. two exports of the same book); flagged files
// are skipped and reported instead of doubling every chunk.
#include "dumy/indexer.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "dumy/chunker.h"
#include "dumy/common.h"
#include "dumy/pdf_loader.h"

namespace dumy {
namespace {

constexpr size_t EMBED_BATCH = 16;
constexpr size_t MIN_PAGE_WORDS = 40;  // front covers / page numbers are noise
constexpr int SIMHASH_DUP_DISTANCE = 3;  // hamming distance treated as duplicate

std::string to_slashes(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

// Key used for manifest + Chunk::source: path relative to the project root.
std::string file_key(const Config& cfg, const fs::path& p) {
    std::error_code ec;
    fs::path rel = fs::relative(p, fs::u8path(cfg.base_dir), ec);
    if (ec || rel.empty()) return to_slashes(p.u8string());
    return to_slashes(rel.u8string());
}

void collect_pdfs(const fs::path& dir, bool recursive, std::vector<fs::path>& out) {
    std::error_code ec;
    if (!fs::exists(dir, ec)) return;
    auto opts = fs::directory_options::skip_permission_denied;
    if (recursive) {
        for (fs::recursive_directory_iterator it(dir, opts, ec), end; it != end && !ec;
             it.increment(ec)) {
            if (it->is_regular_file(ec)) out.push_back(it->path());
        }
    } else {
        for (fs::directory_iterator it(dir, opts, ec), end; it != end && !ec;
             it.increment(ec)) {
            if (it->is_regular_file(ec)) out.push_back(it->path());
        }
    }
}

bool is_pdf(const fs::path& p) {
    std::string ext = to_lower_utf8(p.extension().u8string());
    return ext == ".pdf";
}

fs::path manifest_file(const Config& cfg) { return cfg.data_dir() / "manifest.json"; }
fs::path index_file(const Config& cfg) { return cfg.data_dir() / "index.bin"; }

std::string base_name(const std::string& key) {
    size_t slash = key.find_last_of("/\\");
    return slash == std::string::npos ? key : key.substr(slash + 1);
}

}  // namespace

// ---------------------------------------------------------------------------
// simhash - near-duplicate fingerprint of a text
// ---------------------------------------------------------------------------
std::uint64_t simhash64(const std::string& text) {
    // Hash 5-word shingles, not single words: two different books by the
    // same author share vocabulary (unigram simhash gives false positives),
    // while genuinely duplicated text shares its shingles.
    const std::string lower = to_lower_utf8(text);
    std::vector<int> bits(64, 0);
    std::vector<std::uint64_t> ring(5, 0);
    size_t ring_n = 0, ring_i = 0, words = 0;
    std::string word;
    auto flush_word = [&]() {
        if (word.empty()) return;
        ring[ring_i] = fnv1a64(word.data(), word.size());
        ring_i = (ring_i + 1) % ring.size();
        ring_n = std::min(ring_n + 1, ring.size());
        ++words;
        if (ring_n == ring.size()) {
            // order-sensitive hash of the current 5-word window
            std::uint64_t h = 1469598103934665603ULL;
            for (size_t r = 0; r < ring.size(); ++r) {
                const std::uint64_t v = ring[(ring_i + r) % ring.size()];
                h = fnv1a64(&v, sizeof(v), h);
            }
            for (int b = 0; b < 64; ++b)
                bits[b] += ((h >> b) & 1) ? 1 : -1;
        }
        word.clear();
    };
    for (char c : lower) {
        if (std::isspace(static_cast<unsigned char>(c)) != 0 || c == ',' ||
            c == '.' || c == ';' || c == ':' || c == '!' || c == '?' ||
            c == '(' || c == ')' || c == '"') {
            flush_word();
        } else {
            word.push_back(c);
        }
    }
    flush_word();
    if (words < 5) {  // too short to shingle: fall back to the whole text
        return fnv1a64(lower.data(), lower.size());
    }
    std::uint64_t out = 0;
    for (int b = 0; b < 64; ++b)
        if (bits[b] > 0) out |= (std::uint64_t(1) << b);
    return out;
}

int simhash_distance(std::uint64_t a, std::uint64_t b) {
    std::uint64_t x = a ^ b;
    int n = 0;
    while (x) {
        n += static_cast<int>(x & 1);
        x >>= 1;
    }
    return n;
}

// ---------------------------------------------------------------------------
Indexer::Indexer(const Config& cfg, const OllamaClient& ollama, VectorStorePtr store,
                 Bm25Index& bm25)
    : cfg_(cfg), ollama_(ollama), store_(std::move(store)), bm25_(bm25) {}

void Indexer::rebuild_bm25() {
    bm25_.build(store_->all());
    log_info("BM25 index rebuilt over " + std::to_string(store_->size()) + " chunks");
}

bool Indexer::load_index(std::string* err) {
    fs::path p = index_file(cfg_);
    if (!fs::exists(p)) return false;  // first run - not an error
    return store_->load(p, err);
}

bool Indexer::save_all(std::string* err) {
    return store_->save(index_file(cfg_), err);
}

nlohmann::json Indexer::documents_json() const {
    // Chunk counts per source from the store + manifest metadata.
    std::unordered_map<std::string, int> counts;
    for (const auto& c : store_->all()) ++counts[c.source];

    nlohmann::json manifest = nlohmann::json{{"files", nlohmann::json::object()}};
    {
        std::string raw;
        if (read_text_file(manifest_file(cfg_), raw)) {
            try {
                auto j = nlohmann::json::parse(raw);
                if (j.is_object() && j.contains("files")) manifest = j;
            } catch (...) {
            }
        }
    }

    nlohmann::json arr = nlohmann::json::array();
    std::set<std::string> keys;
    for (const auto& kv : counts) keys.insert(kv.first);
    for (const auto& kv : manifest["files"].items()) keys.insert(kv.key());

    for (const auto& key : keys) {
        const auto& entry = manifest["files"].contains(key) ? manifest["files"][key]
                                                            : nlohmann::json::object();
        nlohmann::json row = {
            {"file", base_name(key)},
            {"key", key},
            {"chunks", counts.count(key) ? counts.at(key) : 0},
            {"pages", entry.value("pages", 0)},
            {"scanned_pages", entry.value("scanned_pages", 0)},
            {"status", entry.value("status", std::string(counts.count(key) ? "ok" : "missing"))},
            {"duplicate_of", entry.value("duplicate_of", std::string())},
        };
        arr.push_back(row);
    }
    return arr;
}

Indexer::Report Indexer::ingest(bool force) {
    std::lock_guard<std::mutex> lk(mutex_);
    Report rep;
    const auto run_start = std::chrono::steady_clock::now();

    // --- 1. find PDF folders ------------------------------------------------
    std::vector<fs::path> folders;
    folders.push_back(cfg_.pdf_dir());
    for (const auto& extra : cfg_.pdf_extra_folders) folders.push_back(cfg_.resolve(extra));

    std::vector<fs::path> pdfs;
    for (size_t i = 0; i < folders.size(); ++i) {
        // The dedicated pdfs folder is scanned recursively; extra (legacy)
        // folders only at their top level to avoid surprises.
        collect_pdfs(folders[i], i == 0, pdfs);
    }
    std::map<std::string, fs::path> files;  // key -> path (dedup)
    for (const auto& p : pdfs)
        if (is_pdf(p)) files.emplace(file_key(cfg_, p), p);
    rep.files_seen = static_cast<int>(files.size());

    if (files.empty()) {
        rep.ok = true;
        rep.message = "no PDF files found - put your PDFs into '" +
                      cfg_.pdf_folder + "'";
        log_warn(rep.message);
        return rep;
    }

    // --- 2. load manifest + current sources --------------------------------
    nlohmann::json manifest = nlohmann::json{{"files", nlohmann::json::object()}};
    {
        std::string raw;
        if (read_text_file(manifest_file(cfg_), raw)) {
            try {
                auto j = nlohmann::json::parse(raw);
                if (j.is_object() && j.contains("files")) manifest = j;
            } catch (...) {
                log_warn("manifest.json unreadable - rebuilding it");
            }
        }
    }
    if (!manifest["files"].is_object()) manifest["files"] = nlohmann::json::object();

    std::set<std::string> present_sources;
    {
        auto all = store_->all();
        for (const auto& c : all) present_sources.insert(c.source);
    }

    // Simhashes of every file already in the index (for duplicate detection).
    // "simhash5" = 5-word-shingle fingerprint (v2). Old "simhash" (unigram,
    // false-positive prone) entries are ignored on purpose.
    std::vector<std::pair<std::string, std::uint64_t>> known_hashes;
    for (const auto& kv : manifest["files"].items()) {
        if (!kv.value().is_object()) continue;
        const auto& h = kv.value().value("simhash5", nlohmann::json());
        if (h.is_string() && !h.get<std::string>().empty()) {
            try {
                known_hashes.emplace_back(kv.key(),
                                          std::stoull(h.get<std::string>(), nullptr, 16));
            } catch (...) {
            }
        }
    }

    // --- 3. index new/changed files ----------------------------------------
    int done = 0;
    double sec_per_file = 0.0;  // ETA basis, measured on indexed files
    for (const auto& kv : files) {
        const std::string& key = kv.first;
        const fs::path& path = kv.second;
        ++done;

        std::string herr;
        const std::string hash = hash_file(path, &herr);
        if (hash.empty()) {
            rep.files_failed++;
            rep.ok = false;
            rep.message = "cannot read " + key + ": " + herr;
            log_error(rep.message);
            FileStat st;
            st.file = key;
            st.status = "failed";
            st.note = herr;
            rep.files.push_back(st);
            continue;
        }

        const bool known = manifest["files"].contains(key);
        const std::string old_hash =
            known ? manifest["files"][key].value("hash", std::string()) : std::string();
        if (!force && known && old_hash == hash && present_sources.count(key)) {
            rep.files_skipped++;
            FileStat st;
            st.file = key;
            st.pages = known ? manifest["files"][key].value("pages", 0) : 0;
            st.chunks = known ? manifest["files"][key].value("chunks", 0) : 0;
            st.scanned_pages =
                known ? manifest["files"][key].value("scanned_pages", 0) : 0;
            st.status = "ok";
            st.note = "unchanged";
            rep.files.push_back(st);
            continue;
        }

        long eta = -1;
        if (sec_per_file > 0.0)
            eta = static_cast<long>(sec_per_file * (rep.files_seen - done));
        if (progress_) progress_(done, rep.files_seen, key, eta);
        log_info("Indexing [" + std::to_string(done) + "/" +
                 std::to_string(rep.files_seen) + "]: " + key +
                 (eta > 0 ? (" (ETA " + std::to_string(eta / 60) + " dəq " +
                             std::to_string(eta % 60) + " san)") : std::string()) +
                 " ...");
        const auto file_start = std::chrono::steady_clock::now();

        std::vector<PdfPage> pages;
        std::string err;
        if (!extract_pdf_pages(path, pages, &err)) {
            rep.files_failed++;
            rep.ok = false;
            rep.message = key + ": " + err;
            log_error(rep.message);
            FileStat st;
            st.file = key;
            st.status = "failed";
            st.note = err;
            rep.files.push_back(st);
            continue;
        }

        // Chunk page by page so every chunk knows its page number.
        std::vector<Chunk> chunks;
        FileStat st;
        st.file = key;
        st.pages = static_cast<int>(pages.size());
        std::string full_text;
        full_text.reserve(1 << 20);
        for (const auto& page : pages) {
            const bool scanned = page_looks_scanned(page.text);
            if (scanned) {
                rep.scanned_pages++;
                st.scanned_pages++;
                // OCR extension point: when enabled and Tesseract is built in,
                // scanned pages get their text from OCR instead of being lost.
                if (cfg_.ocr_enabled && ocr_available()) {
                    std::string ocr_text, oerr;
                    if (run_ocr(path, page.page, cfg_.ocr_languages, ocr_text, &oerr) &&
                        !ocr_text.empty()) {
                        rep.scanned_pages--;   // recovered
                        st.scanned_pages--;
                        pages[page.page - 1].text = ocr_text;
                    } else if (!oerr.empty()) {
                        log_warn("OCR failed on " + key + " p." +
                                 std::to_string(page.page) + ": " + oerr);
                    }
                }
            }
            std::string text = normalize_text(page.text);
            full_text += text + "\n";
            if (estimate_tokens(text) < MIN_PAGE_WORDS) continue;
            auto parts = chunk_text(text, static_cast<size_t>(cfg_.chunk_size),
                                    static_cast<size_t>(cfg_.chunk_overlap));
            for (auto& t : parts) {
                Chunk c;
                c.kind = "pdf";
                c.source = key;
                c.page = page.page;
                c.text = std::move(t);
                chunks.push_back(std::move(c));
            }
        }
        st.chunks = static_cast<int>(chunks.size());

        // Near-duplicate detection (same book exported twice, etc.).
        const std::uint64_t simh = simhash64(full_text);
        std::string dup_of;
        for (const auto& kh : known_hashes) {
            if (simhash_distance(simh, kh.second) <= SIMHASH_DUP_DISTANCE) {
                dup_of = kh.first;
                break;
            }
        }
        if (!dup_of.empty()) {
            rep.files_duplicate++;
            st.status = "duplicate";
            st.note = "near-identical text: " + base_name(dup_of);
            rep.files.push_back(st);
            manifest["files"][key] = {{"hash", hash},
                                      {"chunks", 0},
                                      {"pages", st.pages},
                                      {"scanned_pages", st.scanned_pages},
                                      {"status", "duplicate"},
                                      {"duplicate_of", dup_of},
                                      {"simhash5", ""}};
            log_warn("SKIPPING duplicate [" + std::to_string(done) + "/" +
                     std::to_string(rep.files_seen) + "]: " + key +
                     " (eyni mətn: " + base_name(dup_of) + ")");
            write_text_file(manifest_file(cfg_), manifest.dump(2));
            continue;
        }
        known_hashes.emplace_back(key, simh);

        if (chunks.empty()) {
            rep.files_failed++;
            rep.ok = false;
            st.status = "mostly_scanned";
            st.note = "no extractable text (scanned PDF?)" +
                      (cfg_.ocr_enabled ? std::string(" - OCR failed")
                                        : std::string(" - enable ocr_enabled + Tesseract, see README"));
            rep.message = key + ": " + st.note;
            rep.files.push_back(st);
            log_error(rep.message);
            manifest["files"][key] = {{"hash", hash},
                                      {"chunks", 0},
                                      {"pages", st.pages},
                                      {"scanned_pages", st.scanned_pages},
                                      {"status", "mostly_scanned"},
                                      {"simhash5", ""}};
            write_text_file(manifest_file(cfg_), manifest.dump(2));
            continue;
        }

        // Embed all chunks before touching the store, so a failed run keeps
        // the previous index intact for this file.
        bool embed_ok = true;
        for (size_t i = 0; i < chunks.size() && embed_ok; i += EMBED_BATCH) {
            size_t end = std::min(i + EMBED_BATCH, chunks.size());
            std::vector<std::string> batch;
            batch.reserve(end - i);
            for (size_t j = i; j < end; ++j) batch.push_back(chunks[j].text);

            std::vector<std::vector<float>> embs;
            std::string eerr;
            if (!ollama_.embed(embed_model_, batch, embs, &eerr) ||
                embs.size() != batch.size()) {
                embed_ok = false;
                rep.ok = false;
                rep.message = key + ": embedding failed (" + eerr +
                              ") - is Ollama running and the model '" +
                              embed_model_ + "' pulled?";
                log_error(rep.message);
                break;
            }
            for (size_t j = 0; j < embs.size(); ++j)
                chunks[i + j].embedding = std::move(embs[j]);
        }
        if (!embed_ok) {
            rep.files_failed++;
            st.status = "failed";
            st.note = rep.message;
            rep.files.push_back(st);
            continue;  // old index entry (if any) stays untouched
        }

        // Swap old chunks for the new ones.
        if (present_sources.count(key)) {
            auto all = store_->all();
            for (const auto& c : all)
                if (c.source == key) rep.chunks_removed++;
            store_->remove_source(key);
        }
        const int added = static_cast<int>(chunks.size());
        store_->add(std::move(chunks));
        rep.chunks_added += added;
        rep.files_indexed++;
        st.status = (st.pages > 0 && st.scanned_pages * 2 > st.pages)
                        ? "mostly_scanned" : "ok";
        rep.files.push_back(st);

        manifest["files"][key] = {{"hash", hash},
                                  {"chunks", added},
                                  {"pages", st.pages},
                                  {"scanned_pages", st.scanned_pages},
                                  {"status", st.status},
                                  {"simhash5", std::to_string(simh)}};

        // --- resumable: persist after every finished file ---
        std::string serr;
        if (!save_all(&serr)) log_warn("index save failed: " + serr);
        write_text_file(manifest_file(cfg_), manifest.dump(2));
        bm25_.build(store_->all());  // keep keyword search live during long runs

        const double secs = std::chrono::duration<double>(
                                std::chrono::steady_clock::now() - file_start)
                                .count();
        sec_per_file = sec_per_file == 0.0 ? secs : (sec_per_file * 0.6 + secs * 0.4);
        log_info("  done [" + std::to_string(done) + "/" + std::to_string(rep.files_seen) +
                 "] " + base_name(key) + ": " + std::to_string(added) + " chunks, " +
                 std::to_string(st.pages) + " səhifə, " + std::to_string((int)secs) +
                 " san" +
                 (st.scanned_pages > 0
                      ? (" (" + std::to_string(st.scanned_pages) +
                         " skan səhifə az mətnli)")
                      : std::string()));
    }

    // --- 4. drop files that disappeared from disk ---------------------------
    std::vector<std::string> gone;
    for (const auto& kv : manifest["files"].items()) {
        if (!files.count(kv.key())) gone.push_back(kv.key());
    }
    for (const auto& key : gone) {
        auto all = store_->all();
        for (const auto& c : all)
            if (c.source == key) rep.chunks_removed++;
        store_->remove_source(key);
        manifest["files"].erase(key);
        log_info("Removed from index (file deleted): " + key);
    }

    // --- 5. persist ---------------------------------------------------------
    rebuild_bm25();
    std::string serr;
    if (!save_all(&serr)) {
        rep.ok = false;
        rep.message = "failed to save index: " + serr;
        log_error(rep.message);
    }
    write_text_file(manifest_file(cfg_), manifest.dump(2));

    // --- 6. per-file summary table ------------------------------------------
    {
        std::ostringstream table;
        table << "\n=== İndeksləşdirmə cədvəli ===\n";
        table << "Fayl                                     | Səhifə | Chunk | Skan | Status\n";
        table << "-----------------------------------------+--------+-------+------+--------\n";
        for (const auto& st : rep.files) {
            std::string name = base_name(st.file);
            if (name.size() > 39) name = name.substr(0, 36) + "...";
            char row[256];
            std::snprintf(row, sizeof(row), "%-39s | %6d | %5d | %4d | %s",
                          name.c_str(), st.pages, st.chunks, st.scanned_pages,
                          st.status.c_str());
            table << row << "\n";
            if (st.status == "duplicate")
                table << "    ↳ " << st.note << "\n";
        }
        table << "Cəmi: " << rep.files_indexed << " indeksləndi, "
              << rep.files_skipped << " dəyişmədi, " << rep.files_duplicate
              << " təkrar, " << rep.files_failed << " uğursuz, +"
              << rep.chunks_added << " chunk, " << rep.scanned_pages
              << " az mətnli səhifə";
        log_info(table.str());
    }

    // Scanned-PDF advice (OCR hook).
    if (rep.scanned_pages > 0) {
        if (cfg_.ocr_enabled && !ocr_available()) {
            log_warn("ocr_enabled=true amma OCR bu binary-yə daxil deyil "
                     "(Tesseract lazımdır - bax README 'OCR' bölməsi)");
        } else if (!cfg_.ocr_enabled) {
            log_info(std::to_string(rep.scanned_pages) +
                     " skan səhifə az mətn verdi. Onları OCR ilə bərpa etmək "
                     "üçün: Tesseract quraşdırın, config.json-da "
                     "\"ocr_enabled\": true edin (README 'OCR' bölməsi).");
        }
    }

    const double total_min =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - run_start)
            .count() /
        60.0;
    if (rep.files_indexed > 0 || rep.chunks_removed > 0 || rep.files_failed > 0) {
        log_info("Ingest finished in " + std::to_string(total_min).substr(0, 4) +
                 " min: " + std::to_string(rep.files_indexed) + " indexed, " +
                 std::to_string(rep.files_skipped) + " skipped, " +
                 std::to_string(rep.files_duplicate) + " duplicates, " +
                 std::to_string(rep.files_failed) + " failed, +" +
                 std::to_string(rep.chunks_added) + " chunks");
    } else {
        log_info("Ingest: all " + std::to_string(rep.files_skipped) +
                 " PDFs already up to date");
    }
    return rep;
}

}  // namespace dumy
