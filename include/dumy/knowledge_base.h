#pragma once
// Tier 2 of the answer ladder: curated, deterministic, model-free answers.
// knowledge/common_sense.json is hot-reloaded by mtime, so entries can be
// added while the server runs.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace dumy {

struct KnowledgeEntry {
    std::string id;
    std::string answer;
    std::vector<std::string> keywords;
    std::vector<std::vector<std::string>> combos;
    std::vector<std::string> tags;
    bool safe = true;
    std::string confidence = "high";
};

class KnowledgeBase {
public:
    void load(const std::string& path) {
        path_ = path;
        entries_.clear();
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            loaded_ = false;
            return;
        }
        try {
            nlohmann::json j;
            in >> j;
            for (const auto& e : j.value("entries", nlohmann::json::array())) {
                KnowledgeEntry ke;
                ke.id = e.value("id", std::string());
                ke.answer = e.value("answer", std::string());
                ke.safe = e.value("safe", true);
                ke.confidence = e.value("confidence", std::string("high"));
                for (const auto& k : e.value("keywords", nlohmann::json::array()))
                    ke.keywords.push_back(k.get<std::string>());
                for (const auto& g : e.value("combos", nlohmann::json::array())) {
                    std::vector<std::string> group;
                    for (const auto& k : g) group.push_back(k.get<std::string>());
                    ke.combos.push_back(std::move(group));
                }
                for (const auto& t : e.value("tags", nlohmann::json::array()))
                    ke.tags.push_back(t.get<std::string>());
                if (!ke.id.empty() && !ke.answer.empty())
                    entries_.push_back(std::move(ke));
            }
            loaded_ = true;
            loaded_at_ = std::filesystem::last_write_time(path);
        } catch (...) {
            loaded_ = false;
        }
    }

    // Re-reads the file when its mtime changed (cheap stat on every question).
    void refresh() {
        if (path_.empty()) return;
        std::error_code ec;
        const auto t = std::filesystem::last_write_time(path_, ec);
        if (ec) return;
        if (!loaded_ || t != loaded_at_) load(path_);
    }

    // Only entries that declare combos are specific enough to pre-empt the PDF
    // tier; keyword-only entries wait until retrieval has had its chance.
    const KnowledgeEntry* match_combo(const std::string& question) {
        refresh();
        const std::vector<std::string> stems = tokenize(question);
        const KnowledgeEntry* best = nullptr;
        std::size_t best_score = 0;
        for (const auto& e : entries_) {
            if (!e.safe || e.combos.empty()) continue;
            std::size_t score = 0;
            bool ok = true;
            for (const auto& group : e.combos) {
                bool hit = false;
                for (const auto& g : group)
                    if (has_stem(stems, g)) { hit = true; break; }
                if (!hit) { ok = false; break; }
                score += 10;
            }
            if (ok && score > best_score) { best_score = score; best = &e; }
        }
        return best;
    }

    const KnowledgeEntry* match(const std::string& question) {
        refresh();
        if (entries_.empty()) return nullptr;
        const std::vector<std::string> stems = tokenize(question);
        const KnowledgeEntry* best = nullptr;
        std::size_t best_score = 0;
        for (const auto& e : entries_) {
            if (!e.safe) continue;
            std::size_t score = 0;
            bool ok = true;
            for (const auto& group : e.combos) {
                bool hit = false;
                for (const auto& g : group)
                    if (has_stem(stems, g)) { hit = true; break; }
                if (!hit) { ok = false; break; }
                score += 10;   // an explicit combo outranks a single keyword
            }
            if (!ok) continue;
            std::size_t kw = 0;
            for (const auto& k : e.keywords)
                if (has_stem5(stems, k)) ++kw;
            if (e.combos.empty() && kw < 3) continue;   // F6.3: 3+ keyword hits only
            score += kw;
            if (score > best_score) { best_score = score; best = &e; }
        }
        return best;
    }

    // One scoring pass over BOTH kinds of entry, so a combo can no longer
    // pre-empt a keyword entry with a clearly better match (an incubation
    // question used to be answered by the chick-colour entry because
    // "nə etməli" completed its second combo part).
    //   * an entry WITH combos must match every part, else score 0 - keyword
    //     hits never rescue it;
    //   * an entry WITHOUT combos needs kw >= 3 and scores 10 per hit;
    //   * ties: more keyword hits, then a combo made of topic words rather
    //     than question cues, then more combo parts, then the id.
    const KnowledgeEntry* match_best(const std::string& question) {
        refresh();
        const std::vector<std::string> stems = tokenize(question);
        const KnowledgeEntry* best = nullptr;
        std::size_t best_score = 0, best_kw = 0, best_parts = 0;
        bool best_cue = false;
        for (const auto& e : entries_) {
            if (!e.safe) continue;
            std::size_t kw = 0;
            for (const auto& k : e.keywords)
                if (has_stem5(stems, k)) ++kw;
            std::size_t score = 0, parts = 0;
            bool cue = false;
            if (!e.combos.empty()) {
                bool all = true;
                for (const auto& group : e.combos) {
                    bool hit = false;
                    for (const auto& g : group) {
                        if (has_stem(stems, g)) {
                            hit = true;
                            if (is_cue_part(g)) cue = true;
                        }
                    }
                    if (!hit) { all = false; break; }
                    ++parts;
                }
                if (!all) continue;          // keyword count cannot rescue a combo
                score = 10 * parts;
            } else {
                if (kw < 3) continue;
                score = 10 * kw;
            }
            if (best == nullptr) {
                best = &e; best_score = score; best_kw = kw;
                best_parts = parts; best_cue = cue;
                continue;
            }
            bool better = false;
            if (score != best_score) better = score > best_score;
            else if (kw != best_kw) better = kw > best_kw;
            else if (cue != best_cue) better = best_cue && !cue;
            else if (parts != best_parts) better = parts > best_parts;
            else better = e.id < best->id;
            if (better) {
                best = &e; best_score = score; best_kw = kw;
                best_parts = parts; best_cue = cue;
            }
        }
        return best;
    }

    std::size_t size() const { return entries_.size(); }

private:
    static std::string fold(const std::string& s) {
        // ASCII-only fold of Azerbaijani letters; punctuation -> space.
        std::string out;
        out.reserve(s.size());
        for (std::size_t i = 0; i < s.size();) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (c < 0x80) {
                out += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a')
                                              : (std::isalnum(c) ? static_cast<char>(c) : ' ');
                ++i;
                continue;
            }
            // UTF-8 two-byte sequences used by az: ə(É™) ı ğ ş ç ö ü
            const unsigned char c2 = i + 1 < s.size() ? static_cast<unsigned char>(s[i + 1]) : 0;
            char rep = 0;
            if (c == 0xC9 && c2 == 0x99) rep = 'e';        // ə
            else if (c == 0xC4 && c2 == 0xB1) rep = 'i';   // ı
            else if (c == 0xC4 && c2 == 0x9F) rep = 'g';   // ğ
            else if (c == 0xC5 && c2 == 0x9F) rep = 's';   // ş
            else if (c == 0xC3 && c2 == 0xA7) rep = 'c';   // ç
            else if (c == 0xC3 && c2 == 0xB6) rep = 'o';   // ö
            else if (c == 0xC3 && c2 == 0xBC) rep = 'u';   // ü
            else if (c == 0xC4 && c2 == 0xB0) rep = 'i';   // İ
            if (rep) {
                out += rep;
                i += 2;
            } else {
                ++i;
            }
        }
        return out;
    }

    static std::vector<std::string> tokenize(const std::string& s) {
        const std::string f = fold(s);
        std::vector<std::string> out;
        std::string cur;
        for (unsigned char ch : f) {
            if (ch >= 'a' && ch <= 'z') {
                cur += static_cast<char>(ch);
            } else {
                if (cur.size() >= 4) out.push_back(cur.substr(0, 5));
                cur.clear();
            }
        }
        if (cur.size() >= 4) out.push_back(cur.substr(0, 5));
        return out;
    }

    // Strict stem: both sides must be at least five letters (F6.3).
    static bool has_stem5(const std::vector<std::string>& stems, const std::string& word) {
        const std::vector<std::string> w = tokenize(word);
        if (w.empty() || w.front().size() < 5) return false;
        const std::string want = w.front().substr(0, 5);
        for (const auto& st : stems)
            if (st.size() >= 5 && st.compare(0, 5, want) == 0) return true;
        return false;
    }

    // A combo part that only asks the question ("nə etməli") must not decide
    // which curated entry answers it.
    static bool is_cue_part(const std::string& part) {
        static const char* CUE[] = {"ne et", "ne ed", "etmeli", "edim", "bilerem",
                                    "nece", "hansi", "nedir", "neden", "lazim"};
        for (const char* c : CUE)
            if (part == c) return true;
        return false;
    }

    static bool has_stem(const std::vector<std::string>& stems, const std::string& word) {
        const std::vector<std::string> w = tokenize(word);
        if (w.empty()) return false;
        // Suffixes change the tail (cuceni vs cuce), so compare the first four
        // letters of both sides instead of requiring an exact 5-letter stem.
        const std::string want = w.front().substr(0, 4);
        for (const auto& st : stems) {
            if (st.size() < 4) continue;
            if (st.compare(0, 4, want) == 0) return true;
        }
        return false;
    }

    std::string path_;
    std::vector<KnowledgeEntry> entries_;
    bool loaded_ = false;
    std::filesystem::file_time_type loaded_at_{};
};

}  // namespace dumy
