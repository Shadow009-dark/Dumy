// az_quality.h - Azerbaijani-specific text handling that is independent of the
// prompt text: conservative ASCII->Azerbaijani restoration, feed-calculation
// parsing, repetition detection and the no-context prompt.
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "dumy/common.h"

namespace dumy {

// ---------------------------------------------------------------------------
// Conservative ASCII -> Azerbaijani restoration
// ---------------------------------------------------------------------------
// Only words in this table are changed. Anything else (names, numbers, units,
// code, already-correct words) is left untouched, so "Bakı", "qaramal" and
// "25 kq" survive unchanged.
inline const std::map<std::string, std::string>& az_ascii_dictionary() {
    static const std::map<std::string, std::string> d = {
        {"men", "mən"}, {"sen", "sən"}, {"biz", "biz"}, {"siz", "siz"},
        {"sabah", "sabah"}, {"bu", "bu"}, {"o", "o"},
        {"mekteb", "məktəb"}, {"mektebe", "məktəbə"}, {"mekteve", "məktəbə"},
        {"mektebde", "məktəbdə"}, {"mektebden", "məktəbdən"},
        {"mektey", "məktəb"}, {"mekteye", "məktəbə"},
        {"gedecem", "gedəcəyəm"}, {"gedecyem", "gedəcəyəm"},
        {"gedirem", "gedirəm"}, {"gedir", "gedir"}, {"gedecek", "gedəcək"},
        {"gedek", "gedək"}, {"getmek", "getmək"}, {"gedmek", "getmək"},
        {"gelirem", "gəlirəm"}, {"gelir", "gəlir"}, {"gelmek", "gəlmək"},
        {"geldim", "gəldim"}, {"gelerem", "gələrəm"},
        {"necesen", "necəsən"}, {"necesiniz", "necəsiniz"},
        {"nece", "necə"}, {"niye", "niyə"}, {"nedir", "nədir"},
        {"hansi", "hansı"}, {"harada", "harada"}, {"kim", "kim"},
        {"ne", "nə"}, {"qeder", "qədər"}, {"ucun", "üçün"},
        {"cox", "çox"}, {"az", "az"}, {"yaxsi", "yaxşı"}, {"pis", "pis"},
        {"boyuk", "böyük"}, {"kicik", "kiçik"},
        {"soz", "söz"}, {"sozler", "sözlər"}, {"sozun", "sözün"},
        {"cumle", "cümlə"}, {"cumleni", "cümləni"}, {"cumlede", "cümlədə"},
        {"dil", "dil"}, {"dilde", "dildə"}, {"azerbaycan", "azərbaycan"},
        {"azerbaycanda", "azərbaycanda"}, {"azeri", "azəri"},
        {"melumat", "məlumat"}, {"sual", "sual"}, {"cavab", "cavab"},
        {"lazim", "lazım"}, {"lazimdir", "lazımdır"}, {"meselen", "məsələn"},
        {"her", "hər"}, {"hem", "həm"}, {"ve", "və"}, {"ile", "ilə"},
        {"gun", "gün"}, {"gunde", "gündə"}, {"gunluk", "günlük"},
        {"gunler", "günlər"}, {"gunlerde", "günlərdə"},
        {"kend", "kənd"}, {"kende", "kəndə"}, {"kendde", "kənddə"},
        {"kendli", "kəndli"}, {"teserrufat", "təsərrüfat"},
        {"heyvan", "heyvan"}, {"heyvanlar", "heyvanlar"},
        {"heyvandarlig", "heyvandarlıq"}, {"heyvandarliq", "heyvandarlıq"},
        {"sut", "süd"}, {"sudu", "südü"}, {"sudun", "südün"},
        {"yem", "yem"}, {"yemleme", "yemləmə"}, {"yemlemek", "yəmləmək"},
        {"yemek", "yemək"}, {"inek", "inək"}, {"inege", "inəyə"},
        {"ineyin", "inəyin"}, {"qoyun", "qoyun"}, {"qoyunlar", "qoyunlar"},
        {"camis", "camış"}, {"buzov", "buzov"}, {"quzu", "quzu"},
        {"toyuq", "toyuq"}, {"toyuqlar", "toyuqlar"},
        {"xeste", "xəstə"}, {"xestelik", "xəstəlik"},
        {"xestelikler", "xəstəliklər"}, {"mualice", "müalicə"},
        {"derman", "dərman"}, {"peyvend", "peyvənd"}, {"vaksin", "vaksin"},
        {"taxil", "taxıl"}, {"bugda", "buğda"}, {"arpa", "arpa"},
        {"torpaq", "torpaq"}, {"bitki", "bitki"}, {"bitkiler", "bitkilər"},
        {"sag", "sağ"}, {"saglig", "sağlıq"}, {"yasil", "yaşıl"},
        {"agac", "ağac"}, {"meyve", "meyvə"}, {"terevez", "tərəvəz"},
        {"bag", "bağ"}, {"uzum", "üzüm"}, {"pambiq", "pambıq"},
        {"cay", "çay"}, {"cayi", "çayı"}, {"sud", "süd"},
        {"is", "iş"}, {"ise", "işə"}, {"islemek", "işləmək"},
        {"isleyir", "işləyir"}, {"gorus", "görüş"}, {"gorusuruk", "görüşürük"},
        {"salam", "salam"}, {"sagol", "sağ ol"}, {"sagolun", "sağ olun"},
        {"tesekkur", "təşəkkür"}, {"zehemt", "zəhmət"},
        {"indi", "indi"}, {"sonra", "sonra"}, {"evvel", "əvvəl"},
        {"daha", "daha"}, {"kimi", "kimi"}, {"ucun", "üçün"},
        {"olsun", "olsun"}, {"olur", "olur"}, {"olacaq", "olacaq"},
        {"yaz", "yaz"}, {"yazmaq", "yazmaq"}, {"danis", "danış"},
        {"ver", "ver"}, {"verir", "verir"}, {"vermek", "vermək"},
        {"bilirem", "bilirəm"}, {"bilmek", "bilmək"},
        {"isteyirem", "istəyirəm"}, {"lazimdi", "lazımdır"},
        {"bir", "bir"}, {"iki", "iki"}, {"uc", "üç"}, {"dort", "dörd"},
        {"bes", "beş"}, {"alti", "altı"}, {"yeddi", "yeddi"},
        {"sekkiz", "səkkiz"}, {"doqquz", "doqquz"}, {"on", "on"},
        {"qardas", "qardaş"}, {"qadas", "qardaş"}, {"dost", "dost"},
        {"kiloqram", "kiloqram"}, {"kq", "kq"}, {"ton", "ton"},
        {"faiz", "faiz"}, {"hesabla", "hesabla"}, {"hesablanir", "hesablanır"},
    };
    return d;
}

inline bool text_has_az_letters(const std::string& s) {
    static const char* L[] = {"\xC9\x99", "\xC3\xB6", "\xC3\xBC", "\xC4\xB1",
                              "\xC3\xA7", "\xC5\x9F", "\xC4\x9F"};
    for (const char* l : L)
        if (s.find(l) != std::string::npos) return true;
    return false;
}

// Uppercases the first codepoint of a UTF-8 word (AZ special letters included).
inline std::string az_upper_first(const std::string& w) {
    if (w.empty()) return w;
    const unsigned char c = static_cast<unsigned char>(w[0]);
    if (c < 0x80) {
        std::string out = w;
        out[0] = static_cast<char>(std::toupper(c));
        return out;
    }
    if (w.size() >= 2) {
        const unsigned char c1 = static_cast<unsigned char>(w[1]);
        if (c == 0xC9 && c1 == 0x99) return "\xC6\x8F" + w.substr(2);  // ə -> Ə
        if (c == 0xC3 && c1 == 0xB6) return "\xC3\x96" + w.substr(2);  // ö -> Ö
        if (c == 0xC3 && c1 == 0xBC) return "\xC3\x9C" + w.substr(2);  // ü -> Ü
        if (c == 0xC4 && c1 == 0xB1) return "I" + w.substr(2);         // ı -> I
        if (c == 0xC3 && c1 == 0xA7) return "\xC3\x87" + w.substr(2);  // ç -> Ç
        if (c == 0xC5 && c1 == 0x9F) return "\xC5\x9E" + w.substr(2);  // ş -> Ş
        if (c == 0xC4 && c1 == 0x9F) return "\xC4\x9E" + w.substr(2);  // ğ -> Ğ
    }
    return w;
}

// Restores Azerbaijani letters word by word. Returns the input unchanged when
// it already contains Azerbaijani letters (mixed input is not guessed).
inline std::string normalize_az_input(const std::string& text, int* changed_words = nullptr,
                                      int* total_words = nullptr) {
    if (text_has_az_letters(text)) {
        if (changed_words) *changed_words = 0;
        if (total_words) *total_words = 0;
        return text;
    }
    const auto& dict = az_ascii_dictionary();
    std::string out;
    out.reserve(text.size());
    int changed = 0, total = 0;
    size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (!(std::isalnum(c) || c >= 0x80)) {
            out.push_back(text[i++]);
            continue;
        }
        const size_t start = i;
        while (i < text.size()) {
            const unsigned char d = static_cast<unsigned char>(text[i]);
            if (!(std::isalnum(d) || d >= 0x80)) break;
            ++i;
        }
        const std::string word = text.substr(start, i - start);
        std::string lower = word;
        for (char& ch : lower)
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        ++total;
        auto it = dict.find(lower);
        if (it != dict.end()) {
            out += (std::isupper(static_cast<unsigned char>(word[0])))
                       ? az_upper_first(it->second)
                       : it->second;
            ++changed;
        } else {
            out += word;
        }
    }
    if (changed_words) *changed_words = changed;
    if (total_words) *total_words = total;
    return out;
}

// A statement the user typed without Azerbaijani letters, e.g.
// "men sabah mekteve gedecem" - not a question, not a command, but a sentence
// whose spelling the bot can correct.
inline bool looks_like_bare_sentence(const std::string& original,
                                     const std::string& normalized) {
    if (original.find('?') != std::string::npos) return false;
    if (original.find('\n') != std::string::npos) return false;
    const auto words = az_words(original);
    if (words.size() < 3 || words.size() > 14) return false;
    int changed = 0, total = 0;
    normalize_az_input(original, &changed, &total);
    if (changed < 2 || total == 0) return false;
    const std::string f = " " + az_fold(original) + " ";
    static const char* qwords[] = {" ne ", " nece ", " niye ", " nedir ", " hansi ",
                                   " harada ", " kim ", " ne vaxt ", " ne qeder ",
                                   " ne ucun ", " nece ", " nece edir"};
    for (const char* qw : qwords)
        if (f.find(qw) != std::string::npos) return false;
    return true;
}

inline std::string build_sentence_correction_prompt(const std::string& sentence) {
    return "Aşağıdakı cümlədə Azərbaycan hərfləri (ə, ö, ü, ı, ç, ş, ğ) yoxdur və "
           "bəzi sözlər səhv yazılıb. Cümləni düzgün Azərbaycan dilində yaz. "
           "Yalnız düzəldilmiş cümləni yaz; ondan sonra yeni sətirdə qısa bir "
           "sətirlə əsas dəyişikliyi göstər (məsələn: \"Dəyişiklik: hərflər və "
           "söz formaları bərpa edildi.\"). Əlavə sual, giriş sözü və mənbə "
           "etiketi YAZMA.\n\nCÜMLƏ: " + sentence;
}

// "men sabah mekteve gedecem" -> "Mən sabah məktəbə gedəcəyəm."
inline std::string deterministic_sentence_correction(const std::string& original) {
    std::string s = trim(normalize_az_input(original));
    if (s.empty()) return s;
    s = az_upper_first(s);
    if (s.back() != '.' && s.back() != '!' && s.back() != '?') s += '.';
    return s + "\n\nDəyişiklik: hərflər (ə, ö, ü, ı, ç, ş, ğ) və söz formaları bərpa edildi.";
}

// ---------------------------------------------------------------------------
// Repetition / degeneration detection
// ---------------------------------------------------------------------------
inline bool has_repetition_loop(const std::string& text) {
    const std::string t = trim(text);
    if (t.size() < 80) return false;
    const auto words = az_words(t);
    if (words.size() < 12) return false;

    // The same 4-word sequence three times is a generation loop.
    std::map<std::string, int> grams;
    for (size_t i = 0; i + 4 <= words.size(); ++i) {
        const std::string g = words[i] + " " + words[i + 1] + " " + words[i + 2] + " " +
                              words[i + 3];
        if (++grams[g] >= 3) return true;
    }
    // One long word repeated more than four times.
    std::unordered_map<std::string, int> counts;
    for (const auto& w : words)
        if (w.size() >= 5 && ++counts[w] > 4) return true;
    // The same sentence twice.
    std::map<std::string, int> sentences;
    std::string cur;
    for (size_t i = 0; i < t.size(); ++i) {
        cur.push_back(t[i]);
        if (t[i] == '.' || t[i] == '!' || t[i] == '?') {
            const std::string s = trim(az_fold(cur));
            if (s.size() >= 30 && ++sentences[s] >= 2) return true;
            cur.clear();
        }
    }
    return false;
}

// True when a general-knowledge answer drifts into the agriculture domain
// (the classic "Şəkərçörək" -> sugar-beet failure).
inline bool answer_uses_domain_markers(const std::string& answer) {
    const std::string f = " " + az_fold(answer) + " ";
    static const char* markers[] = {"heyvandar", "mal-qara", "malqara", "yem ",
                                    "seker bitki", "seker cugundu", "bitki xesteliy",
                                    "aqronom", "baytar", "sagim", "südün", "süd ",
                                    "taxil", "torpaq", "gubre"};
    for (const char* m : markers)
        if (f.find(m) != std::string::npos) return true;
    return false;
}

// No-context prompt: short, honest, no invented certainty.
inline std::string build_no_context_user_message(const std::string& question,
                                                 const std::string& sentence_rule) {
    return std::string(
               "KONTEKST: Bu sual üzrə PDF məlumatı tapılmadı.\n"
               "Cavabı yalnız əmin olduğun ümumi biliklərlə, ən çox 4-5 qısa cümlə "
               "ilə ver. Dəqiq bilmədiyin faktı, rəqəmi və adı yazma; əmin "
               "deyilsənsə birbaşa \"Bu barədə dəqiq məlumatım yoxdur\" yaz. "
               "Heç bir [Mənbə: ...] etiketi yaz, yalnız [Ümumi bilik] istifadə et. "
               "Suala birbaşa, tam cavab ver.") +
           sentence_rule + "\n\nSUAL: " + question;
}

inline const char* SHORT_NO_CONTEXT_NOTE =
    "Qeyd: cavab [Ümumi bilik] əsasında verilib; vacib qərarlar üçün mənbəni yoxlayın.";

// ---------------------------------------------------------------------------
// Feed calculations
// ---------------------------------------------------------------------------
namespace feed_detail {

inline std::string fmt(long long v) {
    std::string s = std::to_string(v);
    std::string out;
    int count = 0;
    for (size_t i = s.size(); i > 0; --i) {
        out.push_back(s[i - 1]);
        if (++count == 3 && i > 1) { out.push_back(' '); count = 0; }
    }
    std::reverse(out.begin(), out.end());
    return out;
}

inline std::string kg(double v) { return fmt(static_cast<long long>(std::llround(v))); }

inline bool is_number_token(const std::string& t) {
    bool digit = false, dot = false;
    for (char c : t) {
        if (std::isdigit(static_cast<unsigned char>(c))) { digit = true; continue; }
        if ((c == '.' || c == ',') && !dot) { dot = true; continue; }
        return false;
    }
    return digit;
}

inline double number_value(const std::string& t) {
    std::string s = t;
    std::replace(s.begin(), s.end(), ',', '.');
    try { return std::stod(s); } catch (...) { return 0.0; }
}

inline bool starts(const std::string& w, const char* p) {
    return w.rfind(p, 0) == 0;
}

inline bool is_kg_unit(const std::string& w) {
    return w == "kq" || w == "kg" || starts(w, "kiloqram") || starts(w, "kq/") ||
           starts(w, "kg/");
}

struct Inputs {
    long long heads = 0;
    long long days = 0;
    double daily = 0.0;   // kg per head per day, explicitly given
    double weight = 0.0;  // live weight per head, explicitly given
    double rate = 0.0;    // % of live weight, explicitly given
    bool daily_given = false;
    bool weight_given = false;
    bool rate_given = false;
};

// Token order independent: "120 baş ... gündə 25 kq ... 30 günlük" and
// "30 gün, gündəlik 25 kq, 120 baş" both parse.
inline Inputs parse_inputs(const std::string& question) {
    Inputs in;
    std::vector<std::string> tok;
    {
        std::string cur;
        for (char c : az_fold(question)) {
            if (c == ' ') {
                if (!cur.empty()) { tok.push_back(cur); cur.clear(); }
            } else {
                cur.push_back(c);
            }
        }
        if (!cur.empty()) tok.push_back(cur);
    }
    for (size_t i = 0; i < tok.size(); ++i) {
        if (!is_number_token(tok[i])) continue;
        const double v = number_value(tok[i]);
        const std::string prev = i > 0 ? tok[i - 1] : std::string();
        const std::string prev2 = i > 1 ? tok[i - 2] : std::string();
        const std::string next = i + 1 < tok.size() ? tok[i + 1] : std::string();
        const std::string next2 = i + 2 < tok.size() ? tok[i + 2] : std::string();

        if (starts(next, "bas") ||
            (starts(prev, "say") && starts(prev2, "bas"))) {
            in.heads = static_cast<long long>(std::llround(v));
            continue;
        }
        if (starts(next, "gun") && !starts(next, "gunde")) {
            in.days = static_cast<long long>(std::llround(v));
            continue;
        }
        if (is_kg_unit(next) || (next == "/" && is_kg_unit(next2))) {
            // "gündə", "günlük", "gün ərzində", "hər gün" and the same words
            // AFTER the number ("25 kq günlük") all mean per-head per-day.
            const bool per_day =
                starts(prev, "gunde") || starts(prev, "gundelik") ||
                starts(prev, "gunluk") || starts(prev2, "gunde") ||
                (starts(prev2, "gun") && starts(prev, "erzinde")) ||
                (prev == "her" && starts(prev2, "gun")) ||
                (next2 == "gun" || next2 == "gunde" || next2 == "gunluk") ||
                (i + 3 < tok.size() && starts(tok[i + 3], "gunde"));
            if (per_day) {
                in.daily = v;
                in.daily_given = true;
            } else {
                in.weight = v;
                in.weight_given = true;
            }
            continue;
        }
        if (starts(next, "faiz")) {
            in.rate = v;
            in.rate_given = true;
        }
    }
    // "2.5%" - az_fold drops the sign, so read it from the raw question.
    if (!in.rate_given && question.find('%') != std::string::npos) {
        for (size_t i = 0; i < question.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(question[i]))) continue;
            size_t j = i;
            while (j < question.size() &&
                   (std::isdigit(static_cast<unsigned char>(question[j])) ||
                    question[j] == '.' || question[j] == ','))
                ++j;
            size_t k = j;
            while (k < question.size() && question[k] == ' ') ++k;
            if (k < question.size() && question[k] == '%') {
                std::string num = question.substr(i, j - i);
                std::replace(num.begin(), num.end(), ',', '.');
                try { in.rate = std::stod(num); in.rate_given = true; } catch (...) {}
                break;
            }
            i = j;
        }
    }
    return in;
}

}  // namespace feed_detail

// "120 baş ... yem ... 30 gün(lük) ... nə qədər" - a feed calculation.
inline bool is_feed_calculation_question(const std::string& question) {
    const std::string q = " " + az_fold(question) + " ";
    bool digit = false;
    for (char c : q)
        if (std::isdigit(static_cast<unsigned char>(c))) { digit = true; break; }
    if (!digit) return false;
    const bool heads = q.find(" bas ") != std::string::npos;
    const bool feed = q.find("yem") != std::string::npos;
    const bool period = q.find(" gun") != std::string::npos ||
                        q.find(" ay ") != std::string::npos ||
                        q.find(" heft") != std::string::npos;
    const bool asks = q.find("ne qeder") != std::string::npos ||
                      q.find("nece") != std::string::npos ||
                      q.find("hesabla") != std::string::npos ||
                      q.find("lazim") != std::string::npos ||
                      q.find("teleb") != std::string::npos;
    return heads && feed && period && asks;
}

// Explicit numbers are always used. Missing ones are shown as assumptions,
// never as sourced facts.
inline std::string build_feed_calculation_answer(const std::string& question) {
    const feed_detail::Inputs in = feed_detail::parse_inputs(question);
    if (in.heads <= 0 || in.days <= 0) {
        return "Bu hesablama üçün baş sayı və dövr (gün) lazımdır. Zəhmət olmasa "
               "belə yazın: \"120 baş, gündə 25 kq, 30 gün\".";
    }
    double daily = 0.0;
    bool assumed = false;
    std::string assumption_line;
    if (in.daily_given) {
        daily = in.daily;
    } else if (in.weight_given && in.rate_given) {
        daily = in.weight * in.rate / 100.0;
    } else {
        const double weight = in.weight_given ? in.weight : 300.0;
        const double rate = in.rate_given ? in.rate : 2.5;
        daily = weight * rate / 100.0;
        assumed = true;
        assumption_line =
            "Fərziyyə: bir başın canlı çəkisi " + feed_detail::kg(weight) +
            " kq, günlük norma " + feed_detail::kg(rate) +
            "%-dir (sualda göstərilməyib).\n";
    }
    const double total = daily * static_cast<double>(in.days) *
                         static_cast<double>(in.heads);
    std::ostringstream os;
    os << "Hesablama [Ümumi bilik]:\n"
       << "- Baş sayı: " << feed_detail::fmt(in.heads) << " baş\n"
       << "- Gündəlik yem: " << feed_detail::kg(daily) << " kq/baş\n"
       << "- Dövr: " << feed_detail::fmt(in.days) << " gün\n";
    if (assumed) os << assumption_line;
    os << "\nNəticə: " << feed_detail::fmt(in.heads) << " × " << feed_detail::kg(daily)
       << " × " << feed_detail::fmt(in.days) << " = " << feed_detail::kg(total)
       << " kq";
    if (total >= 1000.0)
        os << " (" << feed_detail::kg(total / 1000.0) << " ton)";
    os << ".\n";
    os << (assumed ? "Qeyd: fərziyyə ilə götürülən rəqəmlər sənədə istinad etmir."
                   : "Qeyd: bütün rəqəmlər sualdan götürülüb.");
    return os.str();
}

}  // namespace dumy
