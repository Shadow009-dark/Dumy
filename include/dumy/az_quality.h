// az_quality.h - Azerbaijani-specific text handling that is independent of the
// prompt text: conservative ASCII->Azerbaijani restoration, feed-calculation
// parsing, repetition detection and the no-context prompt.
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "dumy/common.h"
#include "dumy/vector_store.h"

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
           "YALNIZ düzəldilmiş cümləni yaz. İzah, \"Dəyişiklik:\" sətri, giriş "
           "sözü, təkrarlanan sual və mənbə etiketi YAZMA.\n\nCÜMLƏ: " + sentence;
}

// "men sabah mekteve gedecem" -> "Mən sabah məktəbə gedəcəyəm."
inline std::string deterministic_sentence_correction(const std::string& original) {
    std::string s = trim(normalize_az_input(original));
    if (s.empty()) return s;
    s = az_upper_first(s);
    if (s.back() != '.' && s.back() != '!' && s.back() != '?') s += '.';
    return s;  // the answer is the corrected sentence, nothing else
}

// ---------------------------------------------------------------------------
// Repetition / degeneration detection
// ---------------------------------------------------------------------------
// Citation labels are metadata, not prose: [Mənbə: file.pdf, səh. 12],
// [İnternet: url], [Ümumi bilik], [Xülasə: ...], [Söhbət yaddaşı]. They are
// removed before any text-quality analysis - otherwise an answer that cites
// the same book five times repeats the file name as a 4-gram and looks like a
// generation loop (measured in test_results/gate_diagnosis.md).
inline std::string strip_citation_tags(const std::string& in) {
    std::string s = in;
    size_t pos = 0;
    while ((pos = s.find('[', pos)) != std::string::npos) {
        const size_t close = s.find(']', pos);
        if (close == std::string::npos) break;
        const std::string inner = az_fold(s.substr(pos + 1, close - pos - 1));
        const bool tag = inner.rfind("menbe", 0) == 0 ||
                         inner.rfind("internet", 0) == 0 ||
                         inner.rfind("xulase", 0) == 0 ||
                         inner.rfind("umumi bilik", 0) == 0 ||
                         inner.rfind("sohbet yaddasi", 0) == 0;
        if (tag)
            s.erase(pos, close - pos + 1);
        else
            pos = close + 1;
    }
    return s;
}

// A citation label that sat between a word and its punctuation leaves a
// dangling " ." or " :" once the label is gone ("... bağlıdır [Mənbə: X.pdf,
// səh. 7] ." -> "... bağlıdır ."). Only a space immediately before '.' or ':'
// is removed, so decimals (3.5), ellipses and "səh. 5" stay untouched.
inline std::string strip_dangling_punctuation(std::string s) {
    for (;;) {
        bool changed = false;
        for (const char* p : {" .", " :"}) {
            size_t pos = 0;
            while ((pos = s.find(p, pos)) != std::string::npos) {
                s.erase(pos, 1);
                changed = true;
            }
        }
        if (!changed) break;
    }
    return s;
}

// Defined after has_agri_keyword below (it reuses the same stem list).
inline bool az_is_domain_term(const std::string& word);

inline bool has_repetition_loop(const std::string& text) {
    // Citation labels are metadata: strip them first, or citing one book four
    // times is indistinguishable from a loop (tests/gate_diagnosis.md).
    const std::string t = trim(strip_citation_tags(text));
    if (t.size() < 80) return false;
    // Markdown structure is not prose: a bulleted/numbered answer repeats its
    // lead-in phrasing by design ("... azaldır", "... təmin edir"), and the
    // 4-gram rule read that as a generation loop and rejected whole drafts.
    // Structure lines (- , * , + , # , | and "1. ") are dropped before counting,
    // so only continuous prose is checked.
    std::string prose;
    size_t ls = 0;
    while (ls <= t.size()) {
        size_t le = t.find('\n', ls);
        if (le == std::string::npos) le = t.size();
        const std::string line = trim(t.substr(ls, le - ls));
        bool structure = line.rfind("- ", 0) == 0 || line.rfind("* ", 0) == 0 ||
                         line.rfind("+ ", 0) == 0 || line.rfind("#", 0) == 0 ||
                         line.rfind("|", 0) == 0;
        size_t digits = 0;
        while (digits < line.size() && line[digits] >= '0' && line[digits] <= '9') ++digits;
        if (digits > 0 && digits + 1 < line.size() && line[digits] == '.' &&
            line[digits + 1] == ' ')
            structure = true;
        if (!structure && !line.empty()) {
            prose += line;
            prose += '\n';
        }
        if (le == t.size()) break;
        ls = le + 1;
    }
    // A structure-only answer has no prose in which a loop could hide.
    if (prose.size() < 80) return false;
    const std::string body = prose;
    const auto words = az_words(body);
    if (words.size() < 12) return false;

    // The same 4-word sequence three times is a generation loop.
    std::map<std::string, int> grams;
    for (size_t i = 0; i + 4 <= words.size(); ++i) {
        const std::string g = words[i] + " " + words[i + 1] + " " + words[i + 2] + " " +
                              words[i + 3];
        // 4 repeats: three is ordinary for structured lists ("... təmin edir",
        // repeated lead-ins) and made the gate reject good drafts.
        if (++grams[g] >= 4) return true;
    }
    // One long word repeated many times. Domain terms are excluded: a dairy
    // answer repeats "qaramal", "saqim" or "yem" in every paragraph, and the
    // previous rule flagged exactly those (measured: gate_diagnosis.md Q4/Q8).
    std::unordered_map<std::string, int> counts;
    for (const auto& w : words)
        if (w.size() >= 6 && !az_is_domain_term(w) && ++counts[w] >= 6) return true;
    // The same sentence three times. Two was too tight: a numbered list that
    // repeats a lead-in phrase ("cədvəl 14-də göstərildiyi kimi") is not a
    // loop, and the earlier threshold forced needless strict retries.
    std::map<std::string, int> sentences;
    std::string cur;
    for (size_t i = 0; i < body.size(); ++i) {
        cur.push_back(body[i]);
        if (body[i] == '.' || body[i] == '!' || body[i] == '?') {
            const std::string s = trim(az_fold(cur));
            if (s.size() >= 30 && ++sentences[s] >= 3) return true;
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
// Domain / routing heuristics
// ---------------------------------------------------------------------------
// A PDF citation is only allowed for a livestock / agronomy / dairy /
// veterinary topic. Every off-domain decision below starts from this check,
// so a general question can never inherit an agriculture book as its source.
inline bool has_agri_keyword(const std::string& text) {
    const std::string f = " " + az_fold(text) + " ";
    static const char* PHRASES[] = {
        "mal qara", "kend teserrufat", "sut emal", "et istehsal",
        "yumurta istehsal", "heyvan saglig", "bitki muhafize"
    };
    for (const char* p : PHRASES)
        if (f.find(p) != std::string::npos) return true;

    // Folded stems. Short stems (2-3 letters) only match the whole token so
    // common unrelated words are not pulled into the domain.
    static const char* STEMS[] = {
        "heyvan", "heyvand", "maldar", "qaramal", "inek", "iney", "buzov",
        "dana", "qoyun", "quzu", "qoc", "keci", "camis", "deve", "toyuq",
        "cuce", "hindus", "ordek", "ariciliq", "ari", "bal", "petek", "mum",
        "sud", "sut", "sagim", "sagma", "yem", "yemleme", "otlaq", "bicin",
        "silos", "saman", "taxil", "bugda", "arpa", "qargidali", "cugundur",
        "yonca", "kartof", "pomidor", "xiyar", "badimcan", "lahanay",
        "pambiq", "uzum", "bag", "meyve", "terevez", "agac", "ting", "toxum",
        "torpaq", "torpag", "torpaql", "gubre", "gubrel", "suvarma", "suvarm",
        "ekin", "beceri", "seleksiya", "sort",
        // Plant / soil / plant-nutrition vocabulary. A stem ending in "-q"
        // also needs its "-g" form: az_fold maps ğ->g, so "torpağın" folds
        // to "torpagin" and only the "torpag" stem can see it.
        "toyug", "aricilig", "otlag", "pambig",
        "bitki", "bitkil", "bitkin", "yarpag", "yarpaq", "kok", "kokde",
        "kokun", "kokl", "govde", "cicek", "cicekl",
        "fosfor", "azot", "kalium", "kalsium", "maqnezium", "mikroelement",
        "makroelement", "fotosintez", "xloroz", "xlorofil", "humus",
        "nemlik", "quragliq", "iqlim", "temperatur", "ph",
        // Short stems (3 letters) match the whole token only, so the plural
        // forms a real question uses are listed too: arılar, quşlar, atlar.
        "at", "atlar", "qus", "qusl", "aril", "coban", "traktor",
        "mehsul", "fermer", "ferma", "baytar", "veterinariya", "agronom",
        "agronomiya", "diaqnoz", "mualice", "xestelik", "vaksin", "derman",
        "doza", "antibiot", "peyvend", "parazit", "kend", "teserrufat",
        "animal", "livestock", "sheep", "cattle", "poultry", "farming",
        "crop", "agric", "soil", "harvest", "beekeep"
    };
    for (const auto& w : az_words(text)) {
        for (const char* s : STEMS) {
            const std::string stem(s);
            if (w == stem) return true;
            if (stem.size() >= 4 && w.rfind(stem, 0) == 0 &&
                w.size() <= stem.size() + 7)
                return true;
            if (w.size() >= 4 && stem.rfind(w, 0) == 0 &&
                stem.size() <= w.size() + 7)
                return true;
        }
    }
    return false;
}

// One folded word that names a livestock / agronomy concept. A veterinary
// answer mentions "qaramal" or "saqim" in every paragraph; that is normal
// prose, not a generation loop, so the word rule below skips such terms.
inline bool az_is_domain_term(const std::string& word) {
    return has_agri_keyword(word);
}

// How the strict retry is allowed to run (STEP 3). With surviving chunks it
// must reuse them: a context-free retry answers from the model's own memory
// (test_results/gate_diagnosis.md Q8: livestock diarrhoea came back as
// infant-feeding advice). Without chunks a domain question may not fall back
// to free generation at all - it gets the Tier-4 text instead.
enum class StrictRetryMode { WithContext, WithoutContext, Tier4Text };

inline StrictRetryMode strict_retry_mode(bool has_context, bool domain_sensitive) {
    if (has_context) return StrictRetryMode::WithContext;
    return domain_sensitive ? StrictRetryMode::Tier4Text : StrictRetryMode::WithoutContext;
}

// ---------------------------------------------------------------------------
// Species guard
// ---------------------------------------------------------------------------
// The animal groups named in a question. Cattle (inək, mal, dana, buzov,
// qaramal), sheep/lamb (qoyun, quzu, toğlu), goat/kid (keçi, oğlaq), buffalo,
// horse, poultry and bee are separate groups on purpose - a goat question must
// not be answered from the cattle chapter (test_results/species_diagnosis.md).
// ONE table: the retriever guard and the later synonym module both read it.
struct AzSpeciesGroup {
    const char* label;  // printed in the answer: "keçi, oğlaq"
    const char* name;   // stable id for logs: "goat"
};

inline const std::vector<AzSpeciesGroup>& az_species_groups() {
    static const std::vector<AzSpeciesGroup> groups = {
        {"inək, dana, mal-qara", "cattle"},
        {"qoyun, quzu", "sheep"},
        {"keçi, oğlaq", "goat"},
        {"camış", "buffalo"},
        {"at", "horse"},
        {"toyuq, quş", "poultry"},
        {"arı", "bee"},
    };
    return groups;
}

// Folded stems, index-aligned with az_species_groups().
inline const std::vector<std::vector<std::string>>& az_species_stems() {
    static const std::vector<std::vector<std::string>> stems = {
        {"inek", "iney", "qaramal", "dana", "buzov", "duye", "mal", "malqara", "buzag"},
        {"qoyun", "quzu", "toglu", "qoc"},
        {"keci", "oglag"},
        {"camis"},
        {"at", "atlar", "qulun", "aygir"},
        {"toyuq", "cuce", "xoruz", "hindus", "ordek", "qaz", "qus", "qusl", "firang"},
        {"ari", "aril", "arici", "petek", "mum"},
    };
    return stems;
}

// The only continuation a stem may take is ONE complete nominal ending (plural,
// possessive or case). An arbitrary prefix is not an inflection: measured over
// the 6751 indexed chunks, the old "+7 letters" rule counted "keçirilir" (the
// verb keçmək) as a goat 432 times and "mümkün" as bee wax 1306 times
// (test_results/doc_species.md).
inline bool az_species_ending(const std::string& rest) {
    static const char* E[] = {"lar",   "ler",   "larin", "lerin", "lara",  "lere",
                              "larda", "lerde", "lardan", "lerden", "lari", "leri",
                              "in",    "un",    "nin",   "nun",   "ini",   "unu",
                              "inin",  "unun",  "i",     "u",     "si",    "su",
                              "a",     "e",     "da",    "de",    "dan",   "den",
                              "ya",    "ye",    "ni",    "nu",    "liq",   "lik",
                              "luq",   "luk"};
    for (const char* e : E)
        if (rest == e) return true;
    return false;
}

// Short stems ("at", "mal", "qaz", "mum", "ari") may only take a plural marker
// and a case ending: "arılarda" is a bee, "atlar" a horse, while "ata"
// (father), "mümkün" (possible) and "qazanılmış" stay out.
inline bool az_species_token_matches(const std::string& w, const std::string& stem) {
    if (w == stem) return true;
    if (w.size() <= stem.size() || w.rfind(stem, 0) != 0) return false;
    const std::string rest = w.substr(stem.size());
    if (stem.size() < 4) {
        for (const char* plural : {"lar", "ler"}) {
            if (rest.rfind(plural, 0) != 0) continue;
            const std::string tail = rest.substr(3);
            if (tail.empty() || az_species_ending(tail)) return true;
        }
        return false;
    }
    return az_species_ending(rest);
}

// Token hits per group (index-aligned with az_species_stems()).
inline void az_species_count(const std::string& text, std::vector<int>& counts) {
    const auto& table = az_species_stems();
    counts.assign(table.size(), 0);
    for (const auto& w : az_words(text)) {
        for (size_t g = 0; g < table.size(); ++g) {
            for (const auto& st : table[g]) {
                if (az_species_token_matches(w, st)) {
                    ++counts[g];
                    break;
                }
            }
        }
    }
}

// Which species groups does this text name? Empty = generic text.
inline std::vector<int> az_species_groups_in(const std::string& text) {
    std::vector<int> counts;
    az_species_count(text, counts);
    std::vector<int> out;
    for (size_t g = 0; g < counts.size(); ++g)
        if (counts[g] > 0) out.push_back(static_cast<int>(g));
    return out;
}

struct SpeciesQuery {
    std::vector<int> groups;
    bool any() const { return !groups.empty(); }
    std::string label() const {
        const auto& table = az_species_groups();
        std::string out;
        for (int g : groups) {
            if (!out.empty()) out += " / ";
            out += table[static_cast<size_t>(g)].label;
        }
        return out;
    }
    std::string name() const {
        const auto& table = az_species_groups();
        std::string out;
        for (int g : groups) {
            if (!out.empty()) out += "+";
            out += table[static_cast<size_t>(g)].name;
        }
        return out;
    }
};

inline SpeciesQuery az_species_in_question(const std::string& question) {
    SpeciesQuery sq;
    sq.groups = az_species_groups_in(question);
    return sq;
}

// ---------------------------------------------------------------------------
// Document-level species label
// ---------------------------------------------------------------------------
// A book about cattle often answers a cattle question without repeating
// "inək" on every page: the qarayara pages are cattle pages that name no
// animal, and the chunk-level guard used to drop them (Tier 4 for
// "Mal-qarada qarayara", test_results/species_after.md A1). The label below
// says which species the WHOLE document is about, so a generic chunk inherits
// it. Computed once at load time from every chunk of the source, through the
// same shared table and stems.
enum class DocSpeciesKind {
    None,    // the document never names a species (a plant or lab book)
    Single,  // one group dominates: generic chunks belong to it
    Multi    // several groups are discussed: generic chunks are usable by all
};

struct DocSpeciesLabel {
    DocSpeciesKind kind = DocSpeciesKind::None;
    int group = -1;     // index into az_species_groups() when kind == Single
    double share = 0.0; // dominant group mentions / all group mentions
    int mentions = 0;   // dominant group token hits

    std::string name() const {
        if (kind == DocSpeciesKind::Single && group >= 0)
            return az_species_groups()[static_cast<size_t>(group)].name;
        return kind == DocSpeciesKind::Multi ? "multi" : "none";
    }
};

// Thresholds read off the measured corpus distribution (test_results/
// doc_species.md, recomputed after the stem fix): the labelled documents sit at
// 0.90-0.97 with 287-3707 mentions and the highest unlabelled one at 0.64 with
// 252 mentions, so 0.80 is inside that gap, and the 50-mention floor keeps a
// 10-word document from getting a label.
constexpr double kSpeciesDocShareMin = 0.80;
constexpr int kSpeciesDocMentionsMin = 50;

inline DocSpeciesLabel az_doc_species_label(const std::vector<const Chunk*>& doc_chunks) {
    std::vector<long long> total_counts(az_species_stems().size(), 0);
    std::vector<int> counts;
    for (const Chunk* c : doc_chunks) {
        if (!c) continue;
        az_species_count(c->text, counts);
        for (size_t g = 0; g < counts.size(); ++g) total_counts[g] += counts[g];
    }
    long long total = 0, best = 0;
    int best_group = -1;
    for (size_t g = 0; g < total_counts.size(); ++g) {
        total += total_counts[g];
        if (total_counts[g] > best) { best = total_counts[g]; best_group = static_cast<int>(g); }
    }
    DocSpeciesLabel label;
    if (total == 0 || best_group < 0) return label;  // None
    label.mentions = static_cast<int>(best);
    label.share = static_cast<double>(best) / static_cast<double>(total);
    if (best >= kSpeciesDocMentionsMin && label.share >= kSpeciesDocShareMin) {
        label.kind = DocSpeciesKind::Single;
        label.group = best_group;
    } else {
        label.kind = DocSpeciesKind::Multi;
    }
    return label;
}

inline std::map<std::string, DocSpeciesLabel> az_document_species_labels(
    const std::vector<Chunk>& all_chunks) {
    std::map<std::string, std::vector<const Chunk*>> by_source;
    for (const auto& c : all_chunks) {
        if (c.kind == "web") continue;  // a page is not a book
        by_source[c.source].push_back(&c);
    }
    std::map<std::string, DocSpeciesLabel> labels;
    for (const auto& kv : by_source) labels[kv.first] = az_doc_species_label(kv.second);
    return labels;
}

// The document label of a chunk's source, or nullptr when unknown.
inline const DocSpeciesLabel* az_source_label(
    const std::map<std::string, DocSpeciesLabel>* labels, const std::string& source) {
    if (!labels) return nullptr;
    const auto it = labels->find(source);
    return it == labels->end() ? nullptr : &it->second;
}

struct SpeciesFilterResult {
    std::vector<Chunk> kept;
    size_t dropped = 0;
    // True when at least one chunk names the questioned species.
    bool matched = false;
    // True when no chunk names it but generic chunks of same-group or
    // multi-species documents may still answer it.
    bool by_document = false;
    // True when no document is labelled with the questioned species, so only a
    // chunk that names it could pass (generic chunks are dropped).
    bool named_only = false;
};

// Rule: keep chunks that name the questioned species and generic chunks whose
// document is about that species (or about several species); drop chunks that
// name only other species and generic chunks from a document labelled with a
// different species. When NO document is labelled with the questioned species,
// keep only the chunks that name it (a generic chunk belongs to no one).
// Without document labels the old chunk-level rule applies.
// An empty `kept` means the caller must not answer from these chunks.
inline SpeciesFilterResult filter_chunks_by_species(
    const SpeciesQuery& sq, const std::vector<Chunk>& chunks,
    const std::map<std::string, DocSpeciesLabel>* doc_labels = nullptr) {
    SpeciesFilterResult r;
    if (!sq.any()) {  // no species in the question: nothing to guard
        r.kept = chunks;
        return r;
    }
    std::vector<std::vector<int>> per_chunk;
    per_chunk.reserve(chunks.size());
    for (const auto& c : chunks) per_chunk.push_back(az_species_groups_in(c.text));
    for (const auto& gs : per_chunk) {
        for (int want : sq.groups) {
            if (std::find(gs.begin(), gs.end(), want) != gs.end()) { r.matched = true; break; }
        }
        if (r.matched) break;
    }
    // Does ANY document carry a Single label for the questioned species? When
    // none does, a generic chunk has no owner to inherit from and the only
    // admissible evidence is a chunk that names the species. Without this, a
    // bee question (A6, varroa) was answered from the generic TB pages of
    // multi-species books and invented "vərəm vaksinləri" for bees
    // (test_results/doc_level_after.md A6).
    bool single_for_query = false;
    if (doc_labels) {
        for (const auto& kv : *doc_labels) {
            const DocSpeciesLabel& l = kv.second;
            if (l.kind != DocSpeciesKind::Single) continue;
            if (std::find(sq.groups.begin(), sq.groups.end(), l.group) != sq.groups.end()) {
                single_for_query = true;
                break;
            }
        }
    }
    r.named_only = doc_labels && !single_for_query;
    auto generic_is_allowed = [&](const Chunk& c) -> bool {
        if (!doc_labels) return r.matched;    // no document information: old rule
        if (!single_for_query) return false;  // nobody to inherit from: name it
        const DocSpeciesLabel* l = az_source_label(doc_labels, c.source);
        if (!l || l->kind != DocSpeciesKind::Single) return true;
        return std::find(sq.groups.begin(), sq.groups.end(), l->group) != sq.groups.end();
    };
    for (size_t i = 0; i < chunks.size(); ++i) {
        bool target = false, other = false;
        for (int g : per_chunk[i]) {
            if (std::find(sq.groups.begin(), sq.groups.end(), g) != sq.groups.end()) target = true;
            else other = true;
        }
        if (target) r.kept.push_back(chunks[i]);
        else if (other) ++r.dropped;
        else if (generic_is_allowed(chunks[i])) r.kept.push_back(chunks[i]);
        else ++r.dropped;
    }
    if (!r.matched && !r.kept.empty()) r.by_document = true;
    return r;
}

// Tier 4 for this case: name the species that was asked and say that no source
// covers it, instead of answering with another animal's chapter.
inline std::string species_no_source_text(const std::string& label) {
    return "Bu heyvan növü üçün aydın mənbə tapılmadı (sorğu: " + label + "). "
           "Kitablarda bu növ üçün uyğun bölmə yoxdur, ona görə cavabı başqa "
           "heyvan növündən götürmürəm. Sualı dəqiqləşdirin (xəstəliyin adı, "
           "heyvanın yaşı) və ya baytara müraciət edin.";
}

// Query expansion is one extra helper-model call whose alternative queries are
// sampled, so an identical request retrieves different chunks each run (S4
// "Camışda mastit" -> "camel mastitis" on one run, "qamış qəlbəyin iltihabı" on
// the next), which then flips the relevance gate; see
// test_results/gate_regression.md. A short question gains nothing from that
// sampling, so expansion is skipped when the question has at most max_words
// words (the config value expansion.max_simple_words).
// A non-positive max_words means "no word limit" (never skip on length).
inline bool az_skip_expansion_for_length(const std::string& question, int max_words) {
    if (max_words < 1) return false;
    return static_cast<int>(az_words(question).size()) <= max_words;
}

// "Title: statement" - the manual-test format. A short title, a colon, then a
// declarative sentence (no question mark).
inline bool is_title_statement(const std::string& q) {
    const size_t nl = q.find('\n');
    const std::string first = trim(q.substr(0, nl == std::string::npos ? q.size() : nl));
    if (first.find('?') != std::string::npos) return false;
    const size_t colon = first.find(':');
    if (colon == std::string::npos || colon < 2 || colon > 60) return false;
    const std::string title = trim(first.substr(0, colon));
    const std::string rest = trim(first.substr(colon + 1));
    if (title.empty() || rest.size() < 12) return false;
    if (title.find('.') != std::string::npos || title.find('!') != std::string::npos)
        return false;
    const auto tw = az_words(title);
    if (tw.size() > 6) return false;
    // A title is a label, not a whole clause: "İnəklərin dostluğu", not
    // "Bilmək lazımdır ki". Short but complete statements ("Polşa: Polşanın
    // paytaxtı Varşavadır.") end in a copula and are still statements.
    const auto rw = az_words(rest);
    if (rw.size() < 2) return false;
    if (rw.size() < 4) {
        const std::string last = az_fold(rw.back());
        const bool copula = last.size() > 3 &&
            (last.compare(last.size() - 3, 3, "dir") == 0 ||
             last.compare(last.size() - 3, 3, "dur") == 0);
        if (!copula) return false;
    }
    return true;
}

// A plain declarative sentence without a question mark ("Pingvinlər uça
// bilmir, üzürlər"). Conservative: 7+ words, no question/imperative marker.
inline bool is_declarative_statement(const std::string& q) {
    if (q.find('?') != std::string::npos) return false;
    if (q.find('\n') != std::string::npos) return false;
    const auto words = az_words(q);
    if (words.size() < 7 || words.size() > 60) return false;
    const std::string f = " " + az_fold(q) + " ";
    static const char* QW[] = {
        " ne ", " nece ", " niye ", " nedir ", " hansi ", " harada ",
        " harda ", " kim ", " ne vaxt ", " ne zaman ", " ne qeder ",
        " ne ucun ", " neler ", " nelerdir ", " hansilari ", " izah et",
        " yaz ", " goster ", " hesabla ", " soyle ", " etmeli ", " lazimdir ",
        " lazim ", " olar ", " olarmi ", " mumkundur ", " qaydasi "
    };
    for (const char* w : QW)
        if (f.find(w) != std::string::npos) return false;
    static const char* IMP[] = {"yaz", "izah", "goster", "hesabla", "tap",
                               "soyle", "de", "ver", "et", "qur", "duzelt",
                               "tercume", "qeyd", "yoxla"};
    if (!words.empty())
        for (const char* im : IMP)
            if (words.front() == im) return false;
    return true;
}

inline bool is_statement_input(const std::string& q) {
    // A real question with a question mark is never a statement.
    if (q.find('?') != std::string::npos) return false;

    const std::string f = " " + az_fold(q) + " ";
    const std::vector<std::string> words = az_words(q);

    // Command verbs: user is asking us to DO something, not asserting a fact.
    // Single words are matched as WHOLE words: "de"/"yaz" as substrings made
    // "Delfinlər" (de) and "Yerdən" (de) look like commands, so every
    // "Title: fact" statement fell out of fact-check mode (8 suite failures).
    static const char* COMMAND_WORDS[] = {
        "denen", "de", "deyin", "yaz", "yazin", "yazmaq",
        "goster", "gosterin", "soyle", "soyleyin"
    };
    for (const auto& w : words) {
        for (const char* v : COMMAND_WORDS)
            if (w == v) return false;
    }
    // Multi-word requests and request formulas: safe as phrases.
    static const char* COMMAND_PHRASES[] = {
        "gosterebilersen", "izah et", "izah edin",
        "deyebilersen", "deyebilersiniz", "deyebilermisiniz",
        "zehemet olmasa", "zehmet olmasa",
        "lutfen", "soyleyebilersen",
        "cavab ver", "cavablandir",
        "mualice yollarini", "adini de", "adlarini de",
        "melumat ver", "melumatlandir"
    };
    for (const char* v : COMMAND_PHRASES) {
        if (f.find(v) != std::string::npos) return false;
    }

    // "X: Y" input, e.g. a titled fact or a task label: treat as a statement
    // when the part BEFORE the colon is a short label and the part AFTER it is
    // a full clause.
    const size_t colon = q.find(':');
    if (colon != std::string::npos && colon < 50) {
        const std::string before = trim(q.substr(0, colon));
        const std::string after = trim(q.substr(colon + 1));
        if (before.size() <= 40 && after.size() >= 20) return true;
    }

    // Plain declarative statement ("Ay Yerdən təxminən 384 000 kilometr
    // uzaqdır."): nothing to answer, everything to verify. A question without
    // a '?' is kept out by its question word, so "Torpağın pH-ı nədir" and
    // "İnək gündə neçə dəfə sağılmalıdır" still take the normal question path,
    // and a short topic fragment ("ümumiyyətlə xəstəliklər haqqında", 3 words)
    // stays a fragment instead of becoming a claim to fact-check. 7 words is
    // the same floor is_declarative_statement uses, and it keeps a short
    // clinical complaint ("İnəyin südü azalıb, qızdırması var", 5 words) on
    // the diagnosis path instead of answering it with a verdict.
    if (words.size() >= 7) {
        static const char* QUESTION_WORDS[] = {
            "ne", "nece", "nedir", "niye", "hansi", "harada", "harda",
            "haradir", "kim", "neler", "nelerdir", "hansilari", "qeder",
            "vaxt", "zaman", "ucun", "lazim", "lazimdir", "etmeli",
            "olar", "olarmi", "mumkundur", "qaydasi", "haqqinda",
            "barede", "dair"
        };
        for (const auto& w : words)
            for (const char* qw : QUESTION_WORDS)
                if (w == qw) return false;
        return true;
    }

    return false;
}

// Grammar topics: the agriculture PDFs are not a source for them, and a 4B
// model invents these lists, so they get an honest "no exact information".
inline bool is_grammar_topic(const std::string& q) {
    const std::string f = " " + az_fold(q) + " ";
    static const char* KW[] = {
        " qosma ", " qosmalar ", " qosmalarin ", " evezlik ", " evezlikler ",
        " feil ", " fel ", " isim ", " ismin ", " isimler ", " sifat ",
        " zerf ", " sintaksis ", " sintaktik ", " morfologiya ", " qrammatika ",
        " grammatika ", " sekilci ", " soz yaradiciligi ", " cumle uzvu ",
        " sait ", " samit ", " ismin hallari ", " hallar ", " fel formalari ",
        " felin ", " feiller ", " felin formalari "
    };
    for (const char* k : KW)
        if (f.find(k) != std::string::npos) return true;
    return false;
}

inline bool is_procedure_request(const std::string& q) {
    const std::string f = az_fold(q);
    static const char* KW[] = {
        "nece bisir", "nece demle", "resept", "qaydasi", "qaydasini",
        "hazirlanmasi", "hazirlanir", "hazirlamaq", "addimlar", "telimat",
        "nece guclendir", "nece etmek olar", "nece etmeli", "nece secilir",
        "nece qurulur", "nece duzeldilir", "nece temizlenir"
    };
    for (const char* k : KW)
        if (f.find(k) != std::string::npos) return true;
    return false;
}

inline bool is_list_question(const std::string& q) {
    const std::string f = " " + az_fold(q) + " ";
    static const char* KW[] = {" nelerdir ", " nelardir ", " hansilardir ",
                               " siyahi ", " faydalari ", " xususiyyetleri ",
                               " novleri ", " cesidleri ", " neler edir "};
    for (const char* k : KW)
        if (f.find(k) != std::string::npos) return true;
    return false;
}

inline bool is_definition_question(const std::string& q) {
    const std::string f = az_fold(q);
    static const char* KW[] = {"nedir", "ne demekdir", "izah et", "izah ele",
                               "menasi nedir", "what is", "kimdir"};
    for (const char* k : KW)
        if (f.find(k) != std::string::npos) return true;
    return false;
}

inline bool is_simple_fact_question(const std::string& q) {
    const std::string f = " " + az_fold(q) + " ";
    static const char* KW[] = {" nece ", " ne qeder ", " harada ", " harda ",
                               " hansi ", " ne vaxt ", " ne zaman ", " kim ",
                               " necedir "};
    for (const char* k : KW)
        if (f.find(k) != std::string::npos) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Fact-check answer shaping
// ---------------------------------------------------------------------------
inline std::string strip_meta_labels(std::string s) {
    static const char* MARKS[] = {"[Mənbə", "[Mənb", "[İnternet", "[Internet",
                                  "[Ümumi bilik", "[Umumi bilik", "[Xülasə",
                                  "[Xulasə", "[Xulase"};
    for (;;) {
        size_t best = std::string::npos;
        for (const char* m : MARKS) {
            const size_t p = s.find(m);
            if (p != std::string::npos && (best == std::string::npos || p < best)) best = p;
        }
        if (best == std::string::npos) break;
        const size_t close = s.find(']', best);
        if (close == std::string::npos) { s.erase(best); break; }
        s.erase(best, close - best + 1);
    }
    return trim(s);
}

// Remove leading "**PDF Adı:** ...", "**Giriş Sözü:** ..." style header lines
// and unwrap a leading "**Cavab:**" label. The 4B model emits this template
// when a prompt so much as mentions those words.
inline std::string strip_meta_headers(std::string s) {
    size_t pos = 0;
    for (;;) {
        if (pos >= s.size()) return "";
        size_t eol = s.find('\n', pos);
        if (eol == std::string::npos) eol = s.size();
        const std::string line = trim(s.substr(pos, eol - pos));
        if (line.empty()) { pos = eol + 1; continue; }
        // A bare bold title line ("**Günəş Sistemindəki Planetlər**") or a
        // markdown heading is a header, not content.
        const bool bold_title =
            line.size() > 4 && line.rfind("**", 0) == 0 &&
            line.compare(line.size() - 2, 2, "**") == 0 &&
            line.find(':', 2) == std::string::npos;
        if (bold_title || line[0] == '#') { pos = eol + 1; continue; }
        const size_t colon = line.find(':');
        if (colon == std::string::npos || colon > 40) break;
        std::string label = to_lower_utf8(line.substr(0, colon));
        const std::string rest = trim(line.substr(colon + 1));
        if (label.rfind("**", 0) == 0) label = label.substr(2);
        const size_t close = label.find("**");
        if (close != std::string::npos) label = label.substr(0, close);
        label = trim(label);
        const bool meta = label.find("pdf") != std::string::npos ||
                          label.find("giris") != std::string::npos ||
                          label.find("sual") != std::string::npos ||
                          label.find("mənbə") != std::string::npos ||
                          label.find("menbe") != std::string::npos ||
                          label.find("cavab") != std::string::npos;
        if (!meta) break;
        if (label.find("cavab") != std::string::npos && !rest.empty()) {
            s = rest + (eol < s.size() ? s.substr(eol) : std::string());
            pos = 0;
            continue;
        }
        pos = eol + 1;
    }
    return trim(s.substr(pos));
}

// --- Numeric-answer grounding ----------------------------------------------
// "İnək gündə neçə saat gövşəyir?" retrieved a cattle page whose only number
// describes oestrus ("orta hesabla 18 saat davam edən həvəsgəlmə"), and the
// model reported that 18 as the rumination time. A question that asks for a
// number may only be answered from a chunk that also names what is asked
// about, so the caller drops the chunks that do not.
//
// "neçə" has to be read from the raw text: az_fold turns ç into c, so a folded
// check cannot tell "neçə" (how many) from "necə" (how), and "necə
// hazırlanır?" is not a numeric question.
inline bool asks_for_number(const std::string& question) {
    const std::string f = az_fold(question);
    if (f.find("ne qeder") != std::string::npos) return true;   // nə qədər
    if (f.find("muddet") != std::string::npos) return true;     // müddət
    for (std::size_t i = 0; i + 6 <= question.size(); ++i) {
        const char a = question[i];
        const char b = question[i + 1];
        if ((a != 'n' && a != 'N') || (b != 'e' && b != 'E')) continue;
        if (static_cast<unsigned char>(question[i + 2]) != 0xC3 ||
            static_cast<unsigned char>(question[i + 3]) != 0xA7)   // ç
            continue;
        if (static_cast<unsigned char>(question[i + 4]) != 0xC9 ||
            static_cast<unsigned char>(question[i + 5]) != 0x99)   // ə
            continue;
        return true;
    }
    return false;
}

// The quantity, not its subject: "neçə saat", "neçə gündə", "neçə kq".
inline bool is_quantity_word(const std::string& w) {
    static const char* Q[] = {"nece", "ne", "qeder", "qederdir", "muddet",
        "muddeti", "saat", "saatliq", "gun", "gune", "gunde", "gunluk",
        "gundelik", "hefte", "heftelik", "ay", "ayliq", "il", "illik",
        "defe", "dene", "eded", "kq", "kilo", "kiloqram", "qram", "ton",
        "litr", "millilitr", "ml", "faiz", "derece", "santimetr", "metr",
        "sm", "mm", "olcu", "miqdar", "norma"};
    for (const char* q : Q)
        if (w == q) return true;
    return false;
}

// A livestock noun is in every chunk of a livestock corpus, so it cannot say
// which chunk answers the question ("İnək" appears on the oestrus page too).
inline bool is_species_word(const std::string& w) {
    static const char* S[] = {"inek", "mal", "qara", "dana", "buzov", "quzu",
        "qoyun", "keci", "at", "camis", "donuz", "toyuq", "cuce", "xoruz",
        "ari", "balik", "heyvan", "malqara"};
    for (const char* s : S)
        if (w == s) return true;
    return false;
}

// Generic predicates ("yeyir", "içir", "verir") say nothing about which
// paragraph holds the number; only a specific noun does ("gövşəyir",
// "doğulur", "boğaz"). Removing them keeps "Toyuq gündə neçə qram yem yeyir?"
// on the old path instead of filtering it down to a verb alone.
inline bool is_generic_verb(const std::string& w) {
    static const char* V[] = {"yeyir", "yeyirler", "yeyilir", "yeyilib",
        "yeyen", "icir", "icirler", "icilir", "icilib", "icen", "verir",
        "verirler", "verilir", "verilib", "veren", "edir", "edirler",
        "edilir", "edilib", "eden", "olur", "olurlar", "olunur", "olub"};
    for (const char* v : V)
        if (w == v) return true;
    return false;
}

// Inflected forms change the tail (gövşəyir / gövşək), so compare the first
// four folded letters, as the curated-knowledge matcher does.
inline bool stem4_match(const std::string& a, const std::string& b) {
    if (a.size() < 4 || b.size() < 4) return a == b;
    return a.compare(0, 4, b, 0, 4) == 0;
}

// True when the text names every subject term (empty terms = no constraint).
inline bool text_mentions_all(const std::string& text,
                              const std::vector<std::string>& terms) {
    if (terms.empty()) return true;
    const std::vector<std::string> words = az_words(text);
    for (const auto& t : terms) {
        bool hit = false;
        for (const auto& w : words)
            if (stem4_match(t, w)) { hit = true; break; }
        if (!hit) return false;
    }
    return true;
}

// "X neçə Y-dən ibarətdir?" is answered by the count alone. Models often add
// an invented list after a colon ("8 planet: ... Zülal ..."), which then fails
// the word check and turns a correct count into a refusal.
inline std::string trim_count_answer(const std::string& question,
                                     std::string answer) {
    const std::string f = az_fold(question);
    if (f.find("nece") == std::string::npos || f.find("ibaret") == std::string::npos)
        return answer;
    const size_t colon = answer.find(':');
    const size_t digit = answer.find_first_of("0123456789");
    if (colon != std::string::npos && digit != std::string::npos && digit < colon) {
        answer = trim(answer.substr(0, colon));
        if (!answer.empty() && answer.back() != '.') answer += ".";
    }
    return answer;
}

// The model sometimes wraps a whole answer in a malformed bold block
// ("**\nSu 100°C-də qaynayır.\n**"), which then fails the word check. Markdown
// emphasis carries no meaning here, so drop it before any other cleanup.
inline std::string strip_bold_markers(std::string s) {
    size_t p = 0;
    while ((p = s.find("**", p)) != std::string::npos) s.erase(p, 2);
    return trim(s);
}

// A correct short answer followed by "Bu mövzuda dəqiq məlumatım yoxdur" is a
// contradiction; keep the informative part.
inline std::string drop_trailing_uncertainty(std::string s) {
    const size_t p = s.find("Bu mövzuda dəqiq məlumatım yoxdur");
    if (p != std::string::npos && p > 40) s = trim(s.substr(0, p));
    return s;
}

inline std::string fc_first_sentence(const std::string& s) {
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c != '.' && c != '!' && c != '?') continue;
        if (i + 1 < s.size() && s[i + 1] != ' ' && s[i + 1] != '\n' && s[i + 1] != '\r')
            continue;
        return trim(s.substr(0, i + 1));
    }
    return trim(s);
}

inline int fc_sentence_count(const std::string& s) {
    int n = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c != '.' && c != '!' && c != '?') continue;
        if (i + 1 < s.size() && s[i + 1] != ' ' && s[i + 1] != '\n' && s[i + 1] != '\r')
            continue;
        ++n;
    }
    return n;
}

// Defined below; shape_fact_check_answer() uses it to cut a long explanation.
inline std::string trim_to_last_sentence(const std::string& text);

// Turns a raw model reply into "<verdict>. <up to two short sentences>".
// Confident and uncertain statements can never appear together: any
// uncertainty marker downgrades the verdict to "Dəqiq bilmirəm.".
// Verdict class of an already-shaped fact-check answer (for the two-pass
// agreement check).
inline std::string fc_verdict(const std::string& answer) {
    if (answer.rfind("Qismən", 0) == 0 || answer.rfind("Qismen", 0) == 0)
        return "qismen";
    if (answer.rfind("Doğrudur", 0) == 0 || answer.rfind("Dogrudur", 0) == 0)
        return "dogru";
    if (answer.rfind("Səhvdir", 0) == 0 || answer.rfind("Sehvdir", 0) == 0)
        return "sehv";
    return "bilmirem";
}

inline std::string shape_fact_check_answer(const std::string& raw_in,
                                           const std::string& statement) {
    std::string raw = strip_meta_labels(trim(raw_in));
    if (raw.empty()) return "Dəqiq bilmirəm. Bu iddianı etibarla yoxlaya bilmirəm.";

    struct Cand { const char* phrase; const char* canon; };
    static const Cand CANDS[] = {
        {"qismən doğrudur", "Qismən doğrudur."}, {"qismen dogrudur", "Qismən doğrudur."},
        {"doğrudur", "Doğrudur."}, {"dogrudur", "Doğrudur."},
        {"səhvdir", "Səhvdir."}, {"sehvdir", "Səhvdir."},
        {"dəqiq bilmirəm", "Dəqiq bilmirəm."}, {"deqiq bilmirem", "Dəqiq bilmirəm."},
        {"bilmirəm", "Dəqiq bilmirəm."}, {"bilmirem", "Dəqiq bilmirəm."}
    };
    const std::string low = to_lower_utf8(raw);
    size_t best = std::string::npos;
    size_t best_len = 0;
    std::string verdict;
    for (const auto& c : CANDS) {
        const size_t p = low.find(c.phrase);
        if (p == std::string::npos) continue;
        if (best == std::string::npos || p < best) {
            best = p;
            best_len = std::string(c.phrase).size();
            verdict = c.canon;
        }
    }

    std::string expl;
    if (best != std::string::npos) {
        expl = trim(raw.substr(best + best_len));
        // Drop a leading "...", ":", "-", "–" left after the verdict.
        while (!expl.empty() && (expl[0] == '.' || expl[0] == ':' || expl[0] == '-' ||
                                 expl[0] == '–' || expl[0] == ','))
            expl = trim(expl.substr(1));
    } else {
        verdict = "Dəqiq bilmirəm.";
        expl = "Bu iddianı etibarla yoxlaya bilmirəm.";
    }

    // Echo guard: a model that repeats the statement has not explained it.
    if (!statement.empty() && !expl.empty()) {
        const std::string fs = fc_first_sentence(expl);
        const auto fw = az_words(fs);
        const auto sw = az_words(statement);
        std::set<std::string> st(sw.begin(), sw.end());
        size_t hit = 0;
        for (const auto& w : fw)
            if (st.count(w)) ++hit;
        if (fw.size() >= 4 && hit * 10 >= fw.size() * 6 && hit >= 3)
            expl = trim(expl.substr(fs.size()));
    }

    // Uncertainty anywhere wins over a confident verdict.
    const std::string low_ex = to_lower_utf8(expl);
    static const char* UNCERTAIN[] = {
        "dəqiq məlumatım yoxdur", "deqiq melumatim yoxdur", "əmin deyiləm",
        "emin deyilem", "dəqiq bilmirəm", "deqiq bilmirem", "bilmirəm",
        "bilmirem", "şübhə", "subhe", "əmin ola bilmərəm", "etibar edə bilmərəm"
    };
    if (verdict != "Dəqiq bilmirəm.") {
        for (const char* u : UNCERTAIN) {
            if (low_ex.find(u) == std::string::npos) continue;
            verdict = "Dəqiq bilmirəm.";
            const std::string fs = fc_first_sentence(expl);
            expl = fs.empty() ? std::string("Bu iddianı etibarla yoxlaya bilmirəm.") : fs;
            break;
        }
    }

    if (verdict == "Dəqiq bilmirəm." && expl.empty())
        expl = "Bu iddianı etibarla yoxlaya bilmirəm.";

    // Verdict sentence + at most two explanation sentences (3 total).
    std::string out = verdict;
    if (!expl.empty()) {
        const std::string first = fc_first_sentence(expl);
        std::string rest = trim(expl.substr(first.size()));
        const std::string second = fc_first_sentence(rest);
        std::string body = first;
        if (!second.empty()) body += " " + second;
        if (body.size() > 320) body = trim_to_last_sentence(body);
        if (!body.empty()) {
            body = az_upper_first(body);
            out += " " + body;
        }
    }
    out = trim_to_last_sentence(out);
    return out;
}

// Cuts back to the last complete sentence; if there is none, cuts at the last
// whitespace (never mid-word) and marks the cut.
inline std::string trim_to_last_sentence(const std::string& text) {
    const std::string s = trim(text);
    if (s.empty()) return s;
    const char last = s.back();
    if (last == '.' || last == '!' || last == '?' || last == ']' || last == ')' ||
        last == '"' || last == '\'' || last == '…' || last == '»')
        return s;
    size_t cut = std::string::npos;
    for (size_t i = s.size(); i > 0; --i) {
        const char c = s[i - 1];
        if (c != '.' && c != '!' && c != '?') continue;
        if (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) continue;
        cut = i;
        break;
    }
    if (cut != std::string::npos && cut >= 20) return trim(s.substr(0, cut));
    const size_t sp = s.find_last_of(" \n\t");
    if (sp != std::string::npos && sp >= 20) return trim(s.substr(0, sp)) + "…";
    return s;
}

// ---------------------------------------------------------------------------
// Word validity (garbled-output guard)
// ---------------------------------------------------------------------------
// A token counts as known when it is in the PDF vocabulary, the Azerbaijani
// dictionary, the common-word list, a plausible suffixed form of a known root,
// or shares a 6-character prefix with a vocabulary word.
inline const std::unordered_set<std::string>& az_common_words() {
    static const std::unordered_set<std::string> w = {
        "men", "sen", "biz", "siz", "onlar", "bunlar", "onun", "bunun", "bele",
        "beli", "xeyr", "yox", "var", "yoxdur", "olur", "olan", "olaraq", "oldu",
        "olacaq", "olmalidir", "olmali", "edir", "edilir", "edilmis", "etmek",
        "etmeli", "ederek", "eder", "ederken", "lazim", "lazimdir", "kimi", "gore",
        "sonra", "evvel", "indi", "hem", "amma", "ancaq", "lakin", "cunki", "ile",
        "ucun", "daha", "cox", "az", "en", "her", "bir", "iki", "uc", "dord",
        "bes", "alti", "yeddi", "sekkiz", "doqquz", "on", "yuz", "min", "milyon",
        "milyard", "faiz", "derece", "santimetr", "metr", "kilometr", "kiloqram",
        "qram", "litr", "ton", "gun", "saat", "deqiqe", "il", "ay", "hefte",
        "zaman", "yer", "adam", "insan", "usaq", "qadin", "kisi", "heyat", "dunya",
        "su", "hava", "od", "torpaq", "bitki", "heyvan", "quş", "qus", "balıq",
        "balig", "agac", "gul", "ciçek", "cicek", "meyve", "tervez", "yemek", "icmek",
        "getmek", "gelmek", "bilmek", "gormek", "demek", "vermek", "almaq", "etmek",
        "isteyir", "isteyirem", "bilir", "bilirem", "gorunur", "deyil", "deyildir",
        "hansı", "hansi", "nece", "niye", "nedir", "harada", "kim", "ne", "vaxt",
        "her", "butun", "hamisi", "coxu", "azliq", "coxluq", "boyuk", "kicik",
        "yaxsi", "pis", "yeni", "kohne", "asagi", "yuxari", "iceride", "bayirda",
        "basliq", "metn", "soz", "cumle", "cavab", "sual", "melumat", "fakt",
        "esas", "sebeb", "netice", "numune", "misal", "hal", "veziyyet", "proses",
        "usul", "qayda", "teref", "hisse", "merhele", "addim", "vaxt", "formula",
        "hesab", "olcu", "vahid", "qiymet", "gosterici", "seviyye", "artim", "azalma"
    };
    return w;
}

inline bool az_token_is_known(const std::string& w,
                              const std::unordered_set<std::string>& vocab,
                              const std::unordered_set<std::string>& prefixes) {
    if (w.size() < 4) return true;
    if (vocab.count(w) || az_common_words().count(w)) return true;
    const auto& dict = az_ascii_dictionary();
    if (dict.count(w)) return true;
    for (const auto& kv : dict)
        if (az_fold(kv.second) == w) return true;

    static const char* SUF[] = {
        "larindan", "lerinden", "larinda", "lerinde", "larinin", "lerinin",
        "lardan", "lerden", "larda", "lerde", "larin", "lerin", "lara", "lere",
        "lari", "leri", "lar", "ler", "dir", "dur", "den", "dan", "de", "da",
        "nin", "nun", "ini", "unu", "ine", "una", "inin", "unun", "liq", "lik",
        "luq", "luk", "ci", "cu", "siz", "suz", "mis", "mus", "acaq", "ecek",
        "ir", "ur", "ar", "er", "in", "un", "li", "lu", "ya", "ye", "si", "su",
        "i", "u", "a", "e"
    };
    std::string rest = w;
    for (int step = 0; step < 3; ++step) {
        bool cut = false;
        for (const char* s : SUF) {
            const std::string suf(s);
            if (rest.size() <= suf.size() + 3) continue;
            if (!ends_with(rest, suf)) continue;
            rest = rest.substr(0, rest.size() - suf.size());
            cut = true;
            break;
        }
        if (!cut) break;
        if (vocab.count(rest) || az_common_words().count(rest)) return true;
    }
    if (w.size() >= 7 && !prefixes.empty() && prefixes.count(w.substr(0, 6)))
        return true;
    return false;
}

struct AzWordStats {
    size_t total = 0;
    size_t unknown = 0;
    double ratio() const { return total ? static_cast<double>(unknown) / total : 0.0; }
};

// Share of invented words that makes an UNCITED answer unusable. The 0.12
// document-level threshold cannot see a few invented words in a long answer:
// "Dana ishalı olduqda nə etmək lazımdır?" measured 4/97 = 0.041 and 3/73 =
// 0.041 with `bəluruminin` / `qaraşdırıq` still in the text
// (test_results/species_diagnosis.md). Across the 16 answers the 30-question
// probe could measure, clean answers stayed at 0.000-0.020 and the damaged
// Q7 samples at 0.041-0.048; 0.035 sits in that gap. Used only when no
// citation survived validation - an answer with sources keeps the 0.12 rule.
constexpr double kUncitedUnknownRatioMax = 0.035;

inline AzWordStats measure_unknown_words(const std::string& text,
                                         const std::unordered_set<std::string>& vocab,
                                         const std::unordered_set<std::string>& prefixes) {
    AzWordStats st;
    if (vocab.size() < 1000) return st;  // corpus missing: check disabled
    const std::string clean = az_fold(strip_meta_labels(text));
    std::string cur;
    auto flush = [&]() {
        if (cur.size() >= 4) {
            bool digit = false;
            for (char c : cur)
                if (std::isdigit(static_cast<unsigned char>(c))) digit = true;
            if (!digit) {
                ++st.total;
                if (!az_token_is_known(cur, vocab, prefixes)) ++st.unknown;
            }
        }
        cur.clear();
    };
    for (char c : clean) {
        if (c == ' ') flush();
        else cur.push_back(c);
    }
    flush();
    return st;
}

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
    // Formula + result only. When a value was missing, one short assumption
    // clause travels with it (it is part of the answer, not a note).
    os << "Nəticə: " << feed_detail::fmt(in.heads) << " × " << feed_detail::kg(daily)
       << " × " << feed_detail::fmt(in.days) << " = " << feed_detail::kg(total)
       << " kq";
    if (total >= 1000.0)
        os << " (" << feed_detail::kg(total / 1000.0) << " ton)";
    os << ".";
    if (assumed) {
        os << "\nFərziyyə: canlı çəki "
           << feed_detail::kg(in.weight_given ? in.weight : 300.0)
           << " kq, günlük norma "
           << feed_detail::kg(in.rate_given ? in.rate : 2.5)
           << "% (sualda göstərilməyib).";
    }
    return os.str();
}

// ---------------------------------------------------------------------------
// Answer text hygiene (PART 1)
// ---------------------------------------------------------------------------
// The API's `answer` field must be the answer only. Source labels, "Qeyd:",
// "Dəyişiklik:", preambles and closing offers are removed here; the same
// information is carried by sources_json() and the notes_ui flags.
// A token of a line with its byte offsets and its folded (case-, diacritic-
// and punctuation-free) form. az_fold() turns ':' into a space, so markers
// MUST be compared as words, never as "qeyd:" substrings.
struct AnswerToken {
    size_t begin = 0;
    size_t end = 0;
    std::string folded;
};

inline std::vector<AnswerToken> answer_tokens(const std::string& raw_line) {
    // Normalise the fullwidth colon (U+FF1A) so "Qeyd：..." tokenises like
    // "Qeyd: ..."; az_fold would otherwise merge the label into the content.
    std::string line;
    for (size_t i = 0; i < raw_line.size();) {
        if (i + 2 < raw_line.size() &&
            static_cast<unsigned char>(raw_line[i]) == 0xEF &&
            static_cast<unsigned char>(raw_line[i + 1]) == 0xBC &&
            static_cast<unsigned char>(raw_line[i + 2]) == 0x9A) {
            line += ':';
            i += 3;
        } else {
            line += raw_line[i++];
        }
    }
    std::vector<AnswerToken> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size()) {
            const unsigned char c = static_cast<unsigned char>(line[i]);
            if (c < 0x80 && (std::isspace(c) || std::ispunct(c))) ++i;
            else break;
        }
        if (i >= line.size()) break;
        const size_t begin = i;
        while (i < line.size()) {
            const unsigned char c = static_cast<unsigned char>(line[i]);
            if (c < 0x80 && (std::isspace(c) || std::ispunct(c))) break;
            ++i;
        }
        if (i > begin)
            out.push_back({begin, i, az_fold(line.substr(begin, i - begin))});
    }
    return out;
}

inline bool is_answer_marker_word(const std::string& f) {
    static const char* words[] = {"qeyd",    "qeydler", "qeydi",    "qeydler",
                                  "menbe",   "menbeler", "menbeye", "menbeleri",
                                  "menbeyi", "menbelerin", "pdf",  "deyisiklik",
                                  "deyisiklikler", "note", "notes", "nb", "sources"};
    for (const char* w : words)
        if (f == w) return true;
    return false;
}

inline std::string normalize_answer_colons(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size();) {
        if (i + 2 < raw.size() && static_cast<unsigned char>(raw[i]) == 0xEF &&
            static_cast<unsigned char>(raw[i + 1]) == 0xBC &&
            static_cast<unsigned char>(raw[i + 2]) == 0x9A) {  // fullwidth '：'
            out += ':';
            i += 3;
        } else {
            out += raw[i++];
        }
    }
    return out;
}

// A marker is a LABEL only when a colon follows it (after markdown/bullet
// punctuation). "Yaddaşımda qeyd olunub: 30 qoyunum var." is content, not meta.
inline bool marker_has_colon(const std::string& line, size_t end) {
    size_t i = end;
    while (i < line.size()) {
        const char c = line[i];
        if (c == ' ' || c == '*' || c == '_' || c == '#' || c == '-' || c == '.')
            ++i;
        else
            break;
    }
    return i < line.size() && line[i] == ':';
}

// Byte offset of the first meta marker in the line (npos when clean).
inline size_t answer_marker_pos(const std::string& raw) {
    const std::string line = normalize_answer_colons(raw);
    for (const auto& t : answer_tokens(line))
        if (is_answer_marker_word(t.folded) && marker_has_colon(line, t.end))
            return t.begin;
    return std::string::npos;
}

// "Qeyd: Su 100 °C-də qaynayır." -> "Su 100 °C-də qaynayır."
// "Su qaynayır. Qeyd: ..."      -> "Su qaynayır."
inline std::string cut_answer_marker(const std::string& line) {
    const size_t pos = answer_marker_pos(line);
    if (pos == std::string::npos) return line;
    const auto toks = answer_tokens(line);
    const bool leading = !toks.empty() && pos == toks.front().begin;
    if (leading) {
        size_t cut = toks.front().end;
        // eat the label separator: ':', '-', '*', '.', spaces, UTF-8 colon
        while (cut < line.size()) {
            const unsigned char c = static_cast<unsigned char>(line[cut]);
            if (c < 0x80 && (std::isspace(c) || c == ':' || c == '-' || c == '*' ||
                             c == '.' || c == '#' || c == '_')) {
                ++cut;
                continue;
            }
            if (c == 0xEF && cut + 2 < line.size() &&
                static_cast<unsigned char>(line[cut + 1]) == 0xBC &&
                static_cast<unsigned char>(line[cut + 2]) == 0x9A) {  // '：'
                cut += 3;
                continue;
            }
            break;
        }
        const std::string rest = trim(line.substr(cut));
        // "Mənbə: kitab.pdf, səh. 4" leaves a bare reference behind: every
        // remaining marker makes the whole line meta, so drop it entirely.
        if (answer_marker_pos(rest) != std::string::npos) return std::string();
        // A bare file reference ("kitab.pdf, səh. 4") is meta even without a
        // colon after the word "pdf".
        for (const auto& t : answer_tokens(rest))
            if (t.folded == "pdf") return std::string();
        if (rest.empty()) return rest;
        // A stranded list number ("1. Qeyd: ..." -> "1.") is noise.
        bool digits_only = true;
        for (char c : rest)
            if (!(c >= '0' && c <= '9') && c != '.') { digits_only = false; break; }
        if (digits_only) return std::string();
        return rest;
    }
    return trim(line.substr(0, pos));
}

inline bool is_answer_meta_line(const std::string& folded) {
    // First word of the folded line (colons became spaces already).
    size_t i = 0;
    while (i < folded.size() && (folded[i] == '-' || folded[i] == '*' || folded[i] == ' '))
        ++i;
    size_t j = i;
    while (j < folded.size() && folded[j] >= '0' && folded[j] <= '9') ++j;
    if (j > i && j < folded.size() && folded[j] == '.') i = j + 1;
    while (i < folded.size() && folded[i] == ' ') ++i;
    size_t k = i;
    while (k < folded.size() && folded[k] != ' ') ++k;
    const std::string head = folded.substr(i, k - i);
    if (!head.empty() && is_answer_marker_word(head)) return true;
    // "sual" is deliberately absent: it prefixes real sentences such as
    // "Sualınızı yazın." and would swallow a valid answer.
    static const char* PREFIX[] = {"deyisiklik", "istifade olunan menbeler"};
    for (const char* p : PREFIX) {
        if (folded.compare(i, std::string(p).size(), p) == 0) return true;
    }
    static const char* ANYWHERE[] = {
        "esasinda verilib",
        "vacib qerarlar ucun menbeni yoxlayin",
        "deqiq diaqnoz ve mualice ucun mutleq baytar hekime",
        "elave sualiniz varsa",
        "baska sualiniz varsa",
        "diger suallariniz varsa"};
    for (const char* a : ANYWHERE) {
        if (folded.find(a) != std::string::npos) return true;
    }
    return false;
}

// Drops meta sentences that share a line with real content
// ("Su 100 °C-də qaynayır. Qeyd: ..." -> "Su 100 °C-də qaynayır.").
inline std::string filter_answer_sentences(const std::string& line) {
    std::string out;
    std::string cur;
    auto flush = [&]() {
        const std::string t = trim(cur);
        const std::string f = az_fold(t);
        if (!t.empty() && !is_answer_meta_line(f) &&
            f.find("esasinda verilib") == std::string::npos &&
            f.find("vacib qerarlar ucun menbeni yoxlayin") == std::string::npos) {
            if (!out.empty()) out += " ";
            out += t;
        }
        cur.clear();
    };
    for (size_t i = 0; i < line.size(); ++i) {
        cur.push_back(line[i]);
        const char c = line[i];
        if ((c == '.' || c == '!' || c == '?') &&
            (i + 1 == line.size() || line[i + 1] == ' ')) {
            flush();
            if (i + 1 < line.size()) ++i;  // swallow the separator space
        }
    }
    flush();
    return out;
}

inline std::string clean_answer_text(const std::string& in) {
    // 1) inline tags: [Mənbə: ...], [İnternet: ...], [Ümumi bilik], ...
    std::string s = strip_dangling_punctuation(strip_citation_tags(in));
    // 2) meta lines anywhere in the text
    std::string kept;
    size_t start = 0;
    while (start <= s.size()) {
        const size_t nl = s.find('\n', start);
        const std::string line =
            nl == std::string::npos ? s.substr(start) : s.substr(start, nl - start);
        // Strip a leading label and cut a trailing meta sentence, then check
        // the whole line for disclaimers that survive as a sentence.
        const std::string cut = cut_answer_marker(line);
        if (!trim(cut).empty() && !is_answer_meta_line(az_fold(trim(cut)))) {
            const std::string body = filter_answer_sentences(cut);
            if (!trim(body).empty()) {
                kept += body;
                kept += '\n';
            }
        }
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    s = trim(kept);
    // 3) preambles / echoed labels at the very start
    static const char* PRE[] = {"elbette,", "elbette ", "tebii ki,", "sualiniza gore,",
                                "qisa cavab:", "duzeldilmis cumle:", "cumle:",
                                "cavab:"};
    for (bool changed = true; changed;) {
        changed = false;
        const std::string f = az_fold(s);
        for (const char* p : PRE) {
            if (f.rfind(p, 0) == 0) {
                s = trim(s.substr(std::string(p).size()));
                changed = true;
                break;
            }
        }
    }
    // 4) collapse blank-line runs
    std::string out;
    int blanks = 0;
    for (char c : s) {
        if (c == '\n') {
            if (++blanks > 2) continue;
        } else {
            blanks = 0;
        }
        out.push_back(c);
    }
    return trim(out);
}

// ---------------------------------------------------------------------------
// Source excerpts (STEP 4)
// ---------------------------------------------------------------------------
// Sentence-matching stems: folded tokens truncated to five letters (E3a).
inline std::vector<std::string> az_stems5(const std::string& s) {
    const std::string f = az_fold(s);
    std::vector<std::string> out;
    std::string cur;
    for (unsigned char ch : f) {
        if (ch >= 'a' && ch <= 'z') {
            cur += static_cast<char>(ch);
        } else {
            if (cur.size() >= 5) out.push_back(cur.substr(0, 5));
            cur.clear();
        }
    }
    if (cur.size() >= 5) out.push_back(cur.substr(0, 5));
    return out;
}

}  // namespace dumy
