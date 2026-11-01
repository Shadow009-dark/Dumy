#include "dumy/chunker.h"

#include <cctype>

#include "dumy/common.h"

namespace dumy {

std::string normalize_text(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool prev_space = true;

    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);

        // Newline right after a hyphen: the word was split across lines
        // ("xəstə-\nlik" -> "xəstəlik"). Drop both.
        if (c == '-' && i + 1 < text.size() &&
            (text[i + 1] == '\n' || text[i + 1] == '\r')) {
            size_t j = i + 1;
            while (j < text.size() && (text[j] == '\n' || text[j] == '\r')) ++j;
            // Only join when both sides look like word characters.
            bool left = i > 0 && !std::isspace(static_cast<unsigned char>(text[i - 1]));
            bool right = j < text.size() && !std::isspace(static_cast<unsigned char>(text[j]));
            if (left && right) {
                i = j - 1;  // skip newline; next iteration continues after it
                continue;
            }
        }

        if (std::isspace(c)) {
            if (!prev_space) out.push_back(' ');
            prev_space = true;
            continue;
        }
        // Drop other control characters, keep everything else (UTF-8 safe:
        // bytes >= 0x80 are copied verbatim by isprint check below).
        if (c < 0x20 || c == 0x7F) continue;
        out.push_back(static_cast<char>(c));
        prev_space = false;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::vector<std::string> chunk_text(const std::string& text,
                                    size_t size_tokens,
                                    size_t overlap_tokens) {
    std::vector<std::string> chunks;
    const std::string norm = normalize_text(text);
    if (norm.empty()) return chunks;

    // Split into words.
    std::vector<std::string> words;
    {
        std::string cur;
        for (char ch : norm) {
            if (ch == ' ') {
                if (!cur.empty()) {
                    words.push_back(cur);
                    cur.clear();
                }
            } else {
                cur.push_back(ch);
            }
        }
        if (!cur.empty()) words.push_back(cur);
    }
    if (words.empty()) return chunks;

    // Convert token budgets to word counts (see estimate_tokens: 1.6 tok/word).
    size_t window = size_tokens * 5 / 8;
    size_t overlap = overlap_tokens * 5 / 8;
    if (window < 32) window = 32;
    if (overlap >= window) overlap = window / 4;
    size_t step = window - overlap;
    if (step == 0) step = 1;

    for (size_t start = 0; start < words.size(); start += step) {
        size_t end = start + window;
        if (end >= words.size()) end = words.size();

        std::string chunk;
        for (size_t i = start; i < end; ++i) {
            if (i > start) chunk.push_back(' ');
            chunk += words[i];
        }
        if (!chunk.empty()) chunks.push_back(std::move(chunk));

        if (end == words.size()) break;
    }
    return chunks;
}

}  // namespace dumy
