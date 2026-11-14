// meta_commands.h - deterministic conversational commands ("cavab ver də",
// "davam et", "ətraflı yaz", "qısa yaz", "yenidən yaz", "səhv cavab verdin").
// No model call, no retrieval: pure string work, well under 50 ms.
#pragma once

#include <cctype>
#include <set>
#include <string>
#include <vector>

#include "dumy/common.h"

namespace dumy {

enum class MetaKind {
    None,
    AnswerPrevious,  // cavab ver, sualıma cavab ver, cavab ver də, cavabla
    Continue,        // davam et, davam elə, bəs sonra
    Detail,          // ətraflı yaz, daha ətraflı, genişləndir
    Shorten,         // qısa yaz, qısalt
    Retry,           // yenidən yaz, təkrar et, bir də cəhd et
    Protest,         // cavab vermədin, səhv cavab verdin, bu sual deyil
};

struct MetaResult {
    MetaKind kind = MetaKind::None;
    bool has_content = false;   // the message also carried a real question
    std::string content;        // that question, original spelling kept
};

inline const char* meta_kind_name(MetaKind k) {
    switch (k) {
        case MetaKind::AnswerPrevious: return "answer_previous";
        case MetaKind::Continue: return "continue";
        case MetaKind::Detail: return "detail";
        case MetaKind::Shorten: return "shorten";
        case MetaKind::Retry: return "retry";
        case MetaKind::Protest: return "protest";
        default: return "none";
    }
}

namespace meta_detail {

inline std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (unsigned char c : s) {
        if (c < 0x80 && (std::isspace(c) || std::ispunct(c))) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(static_cast<char>(c));
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// Particles that carry no topic of their own. Words like "ver" are NOT here:
// "yem ver" must survive, so verbs are removed only inside a matched command.
inline bool is_particle(const std::string& folded) {
    static const std::set<std::string> parts = {
        "da", "de", "ki", "bes", "indi", "artiq", "zehmet", "olmasa", "lutfen",
        "mene", "sene", "onu", "bunu", "sualima", "sualimi", "sual", "sualin",
        "xais", "edirem", "ise", "bir", "he", "beli", "onda", "ele", "elə",
    };
    return parts.count(folded) > 0;
}

struct Phrase {
    const char* text;
    MetaKind kind;
};

inline const Phrase* match_phrase(const std::string& padded) {
    static const Phrase table[] = {
        {" cavab vermedin ", MetaKind::Protest},
        {" sehv cavab verdin ", MetaKind::Protest},
        {" sehv cavab ", MetaKind::Protest},
        {" bu sual deyil ", MetaKind::Protest},
        {" duz deyil ", MetaKind::Protest},
        {" cavab duz deyil ", MetaKind::Protest},
        {" sualima cavab ver ", MetaKind::AnswerPrevious},
        {" sualima cavab ", MetaKind::AnswerPrevious},
        {" sualima bax ", MetaKind::AnswerPrevious},
        {" cavab ver ", MetaKind::AnswerPrevious},
        {" cavab verin ", MetaKind::AnswerPrevious},
        {" cavab ver de ", MetaKind::AnswerPrevious},
        {" cavab de ", MetaKind::AnswerPrevious},
        {" cavab deyin ", MetaKind::AnswerPrevious},
        {" cavabla ", MetaKind::AnswerPrevious},
        {" yeniden yaz ", MetaKind::Retry},
        {" yeniden cavab ", MetaKind::Retry},
        {" tekrar et ", MetaKind::Retry},
        {" bir daha cehd ", MetaKind::Retry},
        {" bir daha yaz ", MetaKind::Retry},
        {" davam et ", MetaKind::Continue},
        {" davam ele ", MetaKind::Continue},
        {" bes sonra ", MetaKind::Continue},
        {" daha etrafli ", MetaKind::Detail},
        {" etrafli yaz ", MetaKind::Detail},
        {" etrafli izah ", MetaKind::Detail},
        {" genis et ", MetaKind::Detail},
        {" genislendir ", MetaKind::Detail},
        {" qisa yaz ", MetaKind::Shorten},
        {" qisa cavab ", MetaKind::Shorten},
        {" qisa de ", MetaKind::Shorten},
        {" qisalt ", MetaKind::Shorten},
    };
    for (const auto& p : table) {
        if (padded.find(p.text) != std::string::npos) return &p;
    }
    return nullptr;
}

// Folded words that belong to the command itself (removed from the topic).
inline bool is_command_word(const std::string& folded) {
    static const std::set<std::string> words_set = {
        "cavab", "cavabla", "ver", "verin", "de", "deyin", "bax", "etrafli",
        "genis", "genislendir", "qisa", "qisalt", "davam", "et", "ele",
        "yeniden", "tekrar", "cehd", "yaz", "yazmaq", "izah", "sualima",
        "sualimi", "sual", "sehv", "duz", "deyil", "vermedi", "vermedin",
        "verdiyin", "bu", "bir", "daha", "zaman",
    };
    return words_set.count(folded) > 0;
}

}  // namespace meta_detail

// Classifies a normalized question. When the message also carries a real
// question ("cavab ver: inək neçə ay boğaz qalır"), `content` holds it with
// the original spelling so routing and retrieval see only the question.
inline MetaResult classify_meta(const std::string& text) {
    MetaResult out;
    const auto tokens = meta_detail::words(text);
    if (tokens.empty() || tokens.size() > 14) return out;

    std::string padded = " ";
    for (const auto& t : tokens) padded += az_fold(t) + " ";
    const auto* hit = meta_detail::match_phrase(padded);
    if (!hit) return out;

    out.kind = hit->kind;

    std::string rest;
    int kept = 0;
    for (const auto& t : tokens) {
        const std::string f = az_fold(t);
        if (meta_detail::is_command_word(f) || meta_detail::is_particle(f)) continue;
        rest += (kept++ ? " " : "") + t;
    }
    if (kept >= 2) {
        out.has_content = true;
        out.content = rest;
    }
    return out;
}

// "ümumiyyətlə xəstəliklər haqqında" / "qoyunlar barədə" are topics, not
// questions: they must become a request for an overview.
inline bool is_topic_fragment(const std::string& text) {
    const auto tokens = meta_detail::words(text);
    if (tokens.size() > 6) return false;
    std::string padded = " ";
    for (const auto& t : tokens) padded += az_fold(t) + " ";
    static const char* marks[] = {" haqqinda ", " barede ", " baresinde ",
                                  " haqqda ", " dair "};
    for (const char* m : marks) {
        if (padded.find(m) != std::string::npos) return true;
    }
    return false;
}

}  // namespace dumy
