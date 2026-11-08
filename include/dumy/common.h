// common.h - small shared utilities used by every module.
// Header-only on purpose (no extra .cpp in the build).
#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace dumy {

// ---------------------------------------------------------------------------
// Strings
// ---------------------------------------------------------------------------
inline std::string trim(const std::string& s) {
    const char* ws = " \t\r\n\v\f";
    size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

inline bool starts_with(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

inline bool ends_with(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}

inline std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) out.push_back(item);
    return out;
}

inline std::string join(const std::vector<std::string>& parts, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}

// Lowercase ASCII plus the special Azerbaijani/Latin uppercase letters
// (Ç Ö Ü Ş İ Ə) and basic Cyrillic, so keyword matching works on UTF-8 text.
// Bytes that are not covered are copied unchanged.
inline std::string to_lower_utf8(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {                       // ASCII
            out.push_back(static_cast<char>(std::tolower(c)));
            ++i;
            continue;
        }
        if (i + 1 >= s.size()) { out.push_back(static_cast<char>(c)); ++i; continue; }
        unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
        // 2-byte UTF-8 sequence: 110xxxxx 10xxxxxx
        if ((c & 0xE0) == 0xC0) {
            int cp = ((c & 0x1F) << 6) | (c1 & 0x3F);
            switch (cp) {
                case 0xC7: out += "\xC3\xA7"; break;              // Ç -> ç
                case 0xD6: out += "\xC3\xB6"; break;              // Ö -> ö
                case 0xDC: out += "\xC3\xBC"; break;              // Ü -> ü
                case 0x130: out += "i"; break;                    // İ -> i
                case 0x15E: out += "\xC5\x9F"; break;             // Ş -> ş
                case 0x18F: out += "\xC6\x99"; break;             // Ə -> ə
                default:
                    if (cp >= 0x410 && cp <= 0x42F) {            // Cyrillic А-Я -> а-я
                        int l = cp + 0x20;                        // U+0430..U+044F
                        out.push_back(static_cast<char>(0xC0 | (l >> 6)));
                        out.push_back(static_cast<char>(0x80 | (l & 0x3F)));
                    } else if (cp == 0x401) {                    // Ё -> ё
                        out += "\xD1\x91";
                    } else {
                        out.append(s, i, 2);
                    }
                    break;
            }
            i += 2;
            continue;
        }
        // Longer sequences: copy first byte, continue normal parsing.
        out.push_back(static_cast<char>(c));
        ++i;
    }
    return out;
}

// Rough token estimate: Azerbaijani is agglutinative, ~1.6 tokens per word
// is a realistic average for modern tokenizers. Used for chunk sizing and
// context budgeting - consistency matters more than exactness here.
inline size_t estimate_tokens(const std::string& text) {
    size_t words = 0, prev_ws = 1;
    for (unsigned char c : text) {
        bool ws = std::isspace(c) != 0;
        if (!ws && prev_ws) ++words;
        prev_ws = ws;
    }
    return (words * 8) / 5;
}

// Removes byte sequences that are not valid UTF-8 (PDF/HTML extraction and
// byte-level substr() can split multi-byte characters). JSON serializers
// reject invalid UTF-8, so this runs before anything is dumped.
inline std::string sanitize_utf8(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    const size_t n = s.size();
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = 0;
        if (c < 0x80) len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        if (len == 0) { ++i; continue; }        // stray byte: drop
        if (i + len > n) break;                 // truncated tail: drop
        bool ok = true;
        for (size_t j = 1; j < len; ++j) {
            if ((static_cast<unsigned char>(s[i + j]) & 0xC0) != 0x80) {
                ok = false;
                break;
            }
        }
        if (!ok) { ++i; continue; }             // bad lead: drop it, resync
        out.append(s, i, len);
        i += len;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Hashing (FNV-1a 64 - fast content hash for files and cache keys)
// ---------------------------------------------------------------------------
inline uint64_t fnv1a64(const void* data, size_t n, uint64_t h = 1469598103934665603ULL) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

inline std::string hash_hex(uint64_t h) {
    std::ostringstream os;
    os << std::hex << std::setfill('0') << std::setw(16) << h;
    return os.str();
}

// Hash a file's content (streamed). Returns empty string on error.
inline std::string hash_file(const fs::path& p, std::string* err = nullptr) {
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        if (err) *err = "cannot open file: " + p.string();
        return "";
    }
    uint64_t h = 1469598103934665603ULL;
    std::vector<char> buf(1 << 16);
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        std::streamsize got = in.gcount();
        if (got > 0) h = fnv1a64(buf.data(), static_cast<size_t>(got), h);
    }
    return hash_hex(h);
}

// ---------------------------------------------------------------------------
// UTF-8 safe text file IO (fs::u8path keeps non-ASCII Windows paths working)
// ---------------------------------------------------------------------------
inline bool read_text_file(const fs::path& p, std::string& out, std::string* err = nullptr) {
    std::ifstream in(fs::u8path(p.u8string()), std::ios::binary);
    if (!in) {
        if (err) *err = "cannot open: " + p.u8string();
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

inline bool write_text_file(const fs::path& p, const std::string& data, std::string* err = nullptr) {
    std::error_code ec;
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::ofstream out(fs::u8path(p.u8string()), std::ios::binary | std::ios::trunc);
    if (!out) {
        if (err) *err = "cannot write: " + p.u8string();
        return false;
    }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(out);
}

// ---------------------------------------------------------------------------
// Logging (thread-safe, timestamped)
// ---------------------------------------------------------------------------
inline std::mutex& log_mutex() {
    static std::mutex m;
    return m;
}

inline std::string log_timestamp() {
    auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    return buf;
}

inline void log_line(const char* level, const std::string& msg) {
    std::lock_guard<std::mutex> lk(log_mutex());
    std::cout << "[" << log_timestamp() << "] " << level << " " << msg << std::endl;
}

inline void log_info(const std::string& msg)  { log_line("INFO ", msg); }
inline void log_warn(const std::string& msg)  { log_line("WARN ", msg); }
inline void log_error(const std::string& msg) { log_line("ERROR", msg); }

// Per-model-call timing line (StageTimer + Ollama metrics). `load_ms > 0`
// means Ollama had to load/swap the model into VRAM for this call - the
// dominant cost on a single-model 6 GB GPU. Kept as one greppable line.
inline void log_model_call(const char* stage, const std::string& model,
                           long long wall_ms, long long load_ms,
                           long long prompt_eval_ms, long long gen_ms,
                           long long prompt_tokens, long long eval_tokens) {
    log_info(std::string("[timing] ") + stage + " model=" + model +
             " wall=" + std::to_string(wall_ms) + "ms" +
             " load=" + std::to_string(load_ms) + "ms" +
             " prompt_eval=" + std::to_string(prompt_eval_ms) + "ms" +
             " gen=" + std::to_string(gen_ms) + "ms" +
             " tok=" + std::to_string(prompt_tokens) + "+" +
             std::to_string(eval_tokens) +
             // Step 2: only a real load/swap (>500 ms) deserves the marker;
             // normal calls must stay quiet.
             (load_ms > 500 ? " [MODEL LOADED/SWAPPED]" : ""));
}

}  // namespace dumy
