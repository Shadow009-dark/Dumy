#include "dumy/html_cleaner.h"

#include <cctype>
#include <cstdlib>
#include <unordered_set>
#include <vector>

#include "dumy/common.h"

namespace dumy {
namespace {

// Elements whose *content* (not just the tags) is dropped entirely.
const std::unordered_set<std::string>& drop_content_tags() {
    static const std::unordered_set<std::string> s = {
        "script", "style", "noscript", "template", "svg", "iframe", "object",
        "embed", "canvas", "video", "audio", "form", "button", "select",
        "option", "head", "nav", "header", "footer", "aside",
    };
    return s;
}

// Tags that become a paragraph break.
const std::unordered_set<std::string>& block_tags() {
    static const std::unordered_set<std::string> s = {
        "p", "div", "br", "li", "ul", "ol", "table", "tr", "td", "th",
        "h1", "h2", "h3", "h4", "h5", "h6", "section", "article", "blockquote",
        "pre", "figcaption", "dl", "dt", "dd", "address", "hr",
    };
    return s;
}

std::string tag_name_of(const std::string& html, size_t lt, size_t& gt_out) {
    // html[lt] == '<'; find '>' (naive but sufficient for our use).
    size_t gt = html.find('>', lt + 1);
    if (gt == std::string::npos) gt_out = std::string::npos, gt_out = html.size();
    else gt_out = gt;
    if (gt_out == std::string::npos) return "";

    size_t i = lt + 1;
    bool closing = false;
    if (i < gt_out && (html[i] == '/' || html[i] == '!')) {
        if (html[i] == '/') closing = true;
        ++i;
    }
    std::string name;
    while (i < gt_out &&
           (std::isalnum(static_cast<unsigned char>(html[i])) || html[i] == '-')) {
        name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(html[i]))));
        ++i;
    }
    (void)closing;
    return name;
}

bool ci_starts_with(const std::string& haystack, size_t pos, const std::string& needle) {
    if (pos + needle.size() > haystack.size()) return false;
    for (size_t i = 0; i < needle.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(haystack[pos + i])) !=
            std::tolower(static_cast<unsigned char>(needle[i])))
            return false;
    }
    return true;
}

// Case-insensitive search for "</name" starting at `from`.
size_t find_close_tag(const std::string& html, const std::string& name, size_t from) {
    for (size_t i = from; i + name.size() + 2 <= html.size(); ++i) {
        if (html[i] == '<' && ci_starts_with(html, i + 1, "/" + name)) return i;
    }
    return std::string::npos;
}

void append_char(std::string& out, char c) {
    if (c == '\n' || c == '\r' || c == '\t') {
        if (out.empty() || out.back() != '\n') out.push_back('\n');
        return;
    }
    out.push_back(c);
}

}  // namespace

std::string decode_entities(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] != '&') {
            out.push_back(s[i++]);
            continue;
        }
        size_t semi = s.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 12) {
            out.push_back(s[i++]);
            continue;
        }
        std::string ent = s.substr(i + 1, semi - i - 1);
        std::string replacement;
        if (ent == "amp") replacement = "&";
        else if (ent == "lt") replacement = "<";
        else if (ent == "gt") replacement = ">";
        else if (ent == "quot") replacement = "\"";
        else if (ent == "apos" || ent == "#39") replacement = "'";
        else if (ent == "nbsp" || ent == "#160") replacement = " ";
        else if (ent == "mdash") replacement = "-";
        else if (ent == "ndash") replacement = "-";
        else if (ent == "hellip") replacement = "...";
        else if (!ent.empty() && ent[0] == '#') {
            long code = -1;
            if (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X')) {
                code = std::strtol(ent.c_str() + 2, nullptr, 16);
            } else {
                code = std::strtol(ent.c_str() + 1, nullptr, 10);
            }
            if (code > 0 && code < 0x110000) {
                // Encode code point as UTF-8.
                unsigned long cp = static_cast<unsigned long>(code);
                if (cp < 0x80) replacement.push_back(static_cast<char>(cp));
                else if (cp < 0x800) {
                    replacement.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                    replacement.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                } else if (cp < 0x10000) {
                    replacement.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                    replacement.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    replacement.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                } else {
                    replacement.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                    replacement.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                    replacement.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    replacement.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
            } else {
                out += s.substr(i, semi - i + 1);
                i = semi + 1;
                continue;
            }
        } else {
            out.push_back(s[i++]);  // unknown entity: keep literally
            continue;
        }
        out += replacement;
        i = semi + 1;
    }
    return out;
}

std::string html_title(const std::string& html) {
    for (size_t i = 0; i + 7 < html.size(); ++i) {
        if (html[i] == '<' && ci_starts_with(html, i + 1, "title")) {
            size_t close = find_close_tag(html, "title", i);
            if (close == std::string::npos) return "";
            size_t start = html.find('>', i);
            if (start == std::string::npos || start > close) return "";
            return trim(decode_entities(html.substr(start + 1, close - start - 1)));
        }
    }
    return "";
}

std::string html_to_text(const std::string& html) {
    std::string out;
    out.reserve(html.size() / 4);

    size_t i = 0;
    const size_t n = html.size();
    while (i < n) {
        // Comments and CDATA: skip.
        if (i + 3 < n && html[i] == '<' && html[i + 1] == '!' && html[i + 2] == '-' &&
            html[i + 3] == '-') {
            size_t end = html.find("-->", i);
            i = (end == std::string::npos) ? n : end + 3;
            continue;
        }
        if (html[i] != '<') {
            append_char(out, html[i]);
            ++i;
            continue;
        }

        size_t gt = std::string::npos;
        std::string name = tag_name_of(html, i, gt);
        if (gt == std::string::npos) break;  // malformed tail

        if (drop_content_tags().count(name)) {
            size_t close = find_close_tag(html, name, gt + 1);
            i = (close == std::string::npos) ? n : close + 1;
            continue;
        }
        if (block_tags().count(name)) append_char(out, '\n');
        i = gt + 1;
    }

    // Collapse whitespace per line, drop empty lines.
    std::vector<std::string> lines;
    std::string cur;
    auto flush_line = [&] {
        std::string t = trim(cur);
        cur.clear();
        if (!t.empty()) lines.push_back(t);
    };
    for (char c : out) {
        if (c == '\n') flush_line();
        else if (c == ' ' || c == '\t' || c == '\r') {
            if (!cur.empty() && cur.back() != ' ') cur.push_back(' ');
        } else {
            cur.push_back(c);
        }
    }
    flush_line();
    return join(lines, "\n");
}

}  // namespace dumy
