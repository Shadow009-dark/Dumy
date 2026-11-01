// vector_store.cpp - in-memory cosine store with binary persistence.
//
// File format (little-endian):
//   "DUMYVEC1"              8 bytes magic
//   uint32 version
//   uint64 chunk_count
//   repeated per chunk:
//     uint32 meta_len | meta JSON (text/source/page/url/title/kind/trusted/id)
//     uint32 dim | dim * float32 (L2-normalized embedding)
#include "dumy/vector_store.h"

#include <cmath>
#include <cstring>
#include <fstream>

namespace dumy {

// ---------------------------------------------------------------------------
// Chunk
// ---------------------------------------------------------------------------
std::string Chunk::label() const {
    if (kind == "web") {
        return "[İnternet: " + (url.empty() ? source : url) + "]";
    }
    if (kind == "summary") {
        // Map-reduce group summary: cite the real underlying sources -
        // "[Xülasə: ...]" labels are banned in answers.
        return "[Mənbə: " + source + "]";
    }
    // Show only the file name (not folders) for PDF chunks.
    std::string name = source;
    size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    std::string s = "[Mənbə: " + name;
    if (page > 0) s += ", səh. " + std::to_string(page);
    s += "]";
    return s;
}

nlohmann::json Chunk::meta_json() const {
    return nlohmann::json{
        {"id", id},
        {"text", text},
        {"source", source},
        {"page", page},
        {"url", url},
        {"title", title},
        {"kind", kind},
        {"trusted", trusted},
    };
}

Chunk Chunk::from_meta_json(const nlohmann::json& j) {
    Chunk c;
    c.id = j.value("id", -1);
    c.text = j.value("text", std::string());
    c.source = j.value("source", std::string());
    c.page = j.value("page", 0);
    c.url = j.value("url", std::string());
    c.title = j.value("title", std::string());
    c.kind = j.value("kind", std::string("pdf"));
    c.trusted = j.value("trusted", false);
    return c;
}

// ---------------------------------------------------------------------------
// InMemoryVectorStore
// ---------------------------------------------------------------------------
void InMemoryVectorStore::normalize(std::vector<float>& v) {
    double sum = 0;
    for (float x : v) sum += static_cast<double>(x) * x;
    if (sum <= 0) return;
    float inv = static_cast<float>(1.0 / std::sqrt(sum));
    for (float& x : v) x *= inv;
}

void InMemoryVectorStore::assign_ids(std::vector<Chunk>& chunks, int start_id) {
    for (auto& c : chunks) c.id = start_id++;
}

void InMemoryVectorStore::add(std::vector<Chunk> chunks) {
    std::lock_guard<std::mutex> lk(mutex_);
    for (auto& c : chunks) {
        if (c.id < 0) c.id = next_id_;
        if (c.id >= next_id_) next_id_ = c.id + 1;
        normalize(c.embedding);
        chunks_.push_back(std::move(c));
    }
}

bool InMemoryVectorStore::remove_source(const std::string& source) {
    std::lock_guard<std::mutex> lk(mutex_);
    size_t before = chunks_.size();
    chunks_.erase(std::remove_if(chunks_.begin(), chunks_.end(),
                                 [&](const Chunk& c) { return c.source == source; }),
                  chunks_.end());
    return chunks_.size() != before;
}

void InMemoryVectorStore::clear() {
    std::lock_guard<std::mutex> lk(mutex_);
    chunks_.clear();
    next_id_ = 0;
}

size_t InMemoryVectorStore::size() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return chunks_.size();
}

std::vector<Chunk> InMemoryVectorStore::get(const std::vector<int>& ids) const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<Chunk> out;
    out.reserve(ids.size());
    for (int id : ids) {
        for (const auto& c : chunks_) {
            if (c.id == id) {
                out.push_back(c);
                break;
            }
        }
    }
    return out;
}

std::vector<std::pair<int, float>> InMemoryVectorStore::search(
    const std::vector<float>& query, size_t k) const {
    std::vector<float> q = query;
    normalize(q);

    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<std::pair<int, float>> scores;
    scores.reserve(chunks_.size());
    for (const auto& c : chunks_) {
        if (c.embedding.empty()) continue;
        double dot = 0;
        const size_t n = std::min(q.size(), c.embedding.size());
        for (size_t i = 0; i < n; ++i)
            dot += static_cast<double>(q[i]) * c.embedding[i];
        scores.emplace_back(c.id, static_cast<float>(dot));
    }
    std::partial_sort(scores.begin(),
                      scores.begin() + static_cast<long>(std::min(k, scores.size())),
                      scores.end(),
                      [](const auto& a, const auto& b) { return a.second > b.second; });
    if (scores.size() > k) scores.resize(k);
    return scores;
}

std::vector<Chunk> InMemoryVectorStore::all() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return chunks_;
}

bool InMemoryVectorStore::save(const fs::path& file, std::string* err) const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::error_code ec;
    if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);

    std::ofstream out(fs::u8path(file.u8string()),
                      std::ios::binary | std::ios::trunc);
    if (!out) {
        if (err) *err = "cannot write index: " + file.u8string();
        return false;
    }
    auto put_u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto put_u64 = [&](uint64_t v) { out.write(reinterpret_cast<const char*>(&v), 8); };

    out.write("DUMYVEC1", 8);
    put_u32(1);
    put_u64(chunks_.size());
    for (const auto& c : chunks_) {
        const std::string meta = c.meta_json().dump();
        put_u32(static_cast<uint32_t>(meta.size()));
        out.write(meta.data(), static_cast<std::streamsize>(meta.size()));
        put_u32(static_cast<uint32_t>(c.embedding.size()));
        if (!c.embedding.empty()) {
            out.write(reinterpret_cast<const char*>(c.embedding.data()),
                      static_cast<std::streamsize>(c.embedding.size() * sizeof(float)));
        }
    }
    if (!out) {
        if (err) *err = "write error while saving index";
        return false;
    }
    return true;
}

bool InMemoryVectorStore::load(const fs::path& file, std::string* err) {
    std::ifstream in(fs::u8path(file.u8string()), std::ios::binary);
    if (!in) {
        if (err) *err = "index file not found: " + file.u8string();
        return false;
    }
    auto get_u32 = [&]() {
        uint32_t v = 0;
        in.read(reinterpret_cast<char*>(&v), 4);
        return v;
    };
    auto get_u64 = [&]() {
        uint64_t v = 0;
        in.read(reinterpret_cast<char*>(&v), 8);
        return v;
    };

    char magic[8];
    in.read(magic, 8);
    if (std::memcmp(magic, "DUMYVEC1", 8) != 0) {
        if (err) *err = "not a DUMY index file: " + file.u8string();
        return false;
    }
    uint32_t version = get_u32();
    if (version != 1) {
        if (err) *err = "unsupported index version " + std::to_string(version);
        return false;
    }
    uint64_t count = get_u64();
    if (!in) {
        if (err) *err = "truncated index file";
        return false;
    }

    std::vector<Chunk> loaded;
    loaded.reserve(static_cast<size_t>(count));
    for (uint64_t i = 0; i < count && in; ++i) {
        uint32_t meta_len = get_u32();
        if (!in || meta_len > (64u << 20)) {
            if (err) *err = "corrupt index (meta length)";
            return false;
        }
        std::string meta(meta_len, '\0');
        in.read(meta.data(), meta_len);
        uint32_t dim = get_u32();
        if (!in || dim > 1u << 24) {
            if (err) *err = "corrupt index (dim)";
            return false;
        }
        Chunk c = Chunk::from_meta_json(nlohmann::json::parse(meta));
        c.embedding.resize(dim);
        if (dim > 0) {
            in.read(reinterpret_cast<char*>(c.embedding.data()),
                    static_cast<std::streamsize>(dim * sizeof(float)));
        }
        if (!in) {
            if (err) *err = "truncated index file";
            return false;
        }
        if (c.id >= next_id_) next_id_ = c.id + 1;
        loaded.push_back(std::move(c));
    }

    std::lock_guard<std::mutex> lk(mutex_);
    chunks_ = std::move(loaded);
    return true;
}

}  // namespace dumy
