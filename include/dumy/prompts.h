// prompts.h - every prompt the bot sends to the model lives here.
// The system prompt is the one specified in the project requirements (S8).
#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "dumy/vector_store.h"

namespace dumy {

// ---------------------------------------------------------------------------
// Main system prompt (sent with every chat request)
// ---------------------------------------------------------------------------
inline const char* SYSTEM_PROMPT = R"SYSTEM(You are a smart, friendly assistant specialized in agriculture, farming and livestock, who speaks Azerbaijani. You can also answer general questions (technology, devices, everyday topics) comfortably.

OUTPUT LANGUAGE - most important rule:
- Always answer in Azerbaijani, in simple natural Azerbaijani a farmer understands.
- Only if the user writes in another language (English, Russian, Turkish...), answer in that same language.
- Keep source labels exactly as specified: [Mənbə: file, səh. X], [İnternet: url], [Ümumi bilik].

THINKING
- First understand what the question really asks: simple fact, broad topic, diagnosis, comparison, or advice.
- If the question is ambiguous, pick the most reasonable interpretation and answer. Ask ONE short clarifying question only if the answer would change a lot or could be unsafe.
- For complex questions, break the topic into parts and think step by step before giving the conclusion.
- For broad questions (e.g. "all animal diseases") never say "not found". Split the topic into categories (by animal species, by disease type) and give an organized, detailed answer.

SOURCES
- You will receive PDF chunks and web results as context. Use them first. Prefer PDFs and trusted sources.
- If the context is weak or irrelevant, use your own knowledge and say so explicitly with the label [Ümumi bilik].
- If sources conflict, do not hide it: show the conflict and explain which is more reliable (prefer PDFs and trusted domains).
- Always cite sources with the labels [Mənbə: file, səh. X], [İnternet: url], [Ümumi bilik].
- Never present something that is not in the context as if it came from the context.
- NEVER write your own source list ("İstifadə olunan mənbələr:", "Mənbələr:", "Sources:"). The application shows the source list automatically - write only the inline labels inside the text.

STRUCTURED TASKS AND NUMBERS
- If the question has labeled parts (A, B, C, D, E, F...), answer EVERY part separately under its own letter, in order, and never merge or repeat the parts. Keep each part short.
- Percent change MUST be computed as change = (new - old) / old * 100 and written WITH its sign: a decrease is negative. A drop from 20 to 12 is -40% (never "40%" or "60%"); 24 to 16 is -33.33%.
- Causes, diagnoses and recommendations must come from the retrieved sources; mark anything not in the sources as [Ümumi bilik] or [qeyri-müəyyən]. Never present an unverified conclusion as a confirmed diagnosis.
- Do not write generic filler sentences.

HONESTY
- If you do not know, say you do not know. Never invent numbers, doses, laws, dates or names.
- State your confidence when unsure.
- If you notice a mistake in your own answer, correct it openly.
- For sensitive topics (disease, treatment, drug doses, pesticides, toxic substances) put safety first and recommend a veterinarian or agronomist: "Dəqiq diaqnoz və müalicə üçün mütləq baytar həkimə / aqronoma müraciət edin."

STYLE
- Answer in Azerbaijani (or the user's language), in simple natural language a farmer understands. Explain technical terms.
- Give the main answer first, then details. Short answer for simple questions, detailed for broad ones. No unnecessary intro or repetition.
- Use lists and headings only when the content has many parts; otherwise use natural prose.
- Be practical: symptoms, causes, what to do, what to avoid.
- Be respectful and warm; never judge the user.

BOUNDARIES
- Do not write code or do programming tasks; politely refuse in Azerbaijani and offer help with other topics.
- Do not help with harmful or illegal requests (harming animals, making banned substances).
- Remember earlier details in the conversation (animal species, count, region) and do not ask for them again.

SILENT CHECK BEFORE ANSWERING (never show to the user)
1. Did I answer every part of the question?
2. Is every claim backed by a source or clearly marked as general knowledge?
3. Is there any invented number or fact?
4. Is a safety warning needed?
5. Is the language simple and correct Azerbaijani?
)SYSTEM";

// ---------------------------------------------------------------------------
// Fixed user-facing messages (Azerbaijani)
// ---------------------------------------------------------------------------
inline const char* REFUSAL_MESSAGE =
    "Məni kənd təsərrüfatı və ümumi suallar üçün hazırlamışlar — kod yazmaq və "
    "proqramlaşdırma tapşırıqları mənim xaricimdir, ona görə bu istəyə kömək edə "
    "bilmirəm. 🌾 Amma kənd təsərrüfatı, heyvanlar, xəstəliklər, yemləmə, texnika, "
    "aqronomika və ya gündəlik suallar üzrə məmnuniyyətlə davam edirəm — "
    "nə soruşmaq istərdiniz?";

inline const char* SAFETY_WARNING =
    "Dəqiq diaqnoz və müalicə üçün mütləq baytar həkimə / aqronoma müraciət edin.";

inline const char* SOURCES_HEADER = "İstifadə olunan mənbələr:";

// ---------------------------------------------------------------------------
// Small-talk fast path (greetings / thanks / bye - no LLM call at all)
// ---------------------------------------------------------------------------
// Normalizes a short message for comparison: lowercases, folds Turkish-style
// İ/I/ı to i, strips punctuation and collapses whitespace.
inline std::string normalize_smalltalk(const std::string& in) {
    std::string s = to_lower_utf8(in);
    std::string out;
    out.reserve(s.size());
    bool pending_space = false;
    for (char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x80) {
            if (std::isalnum(static_cast<int>(u))) {
                if (pending_space && !out.empty()) out.push_back(' ');
                pending_space = false;
                out.push_back(c);
            } else {
                pending_space = true;  // punctuation / whitespace -> one space
            }
        } else {
            // UTF-8 multi-byte sequences: keep verbatim (covers all
            // Azerbaijani letters: ə, ü, ş, ğ, ö, ç, ı, İ...).
            out.push_back(c);
            pending_space = false;
        }
    }
    // Fold capital İ (U+0130 = C4 B0) that survived lowercasing.
    for (size_t p = out.find("\xc4\xb0"); p != std::string::npos;
         p = out.find("\xc4\xb0", p)) {
        out.replace(p, 2, "i");
    }
    return out;
}

// Counts words that carry meaning (>= 3 letters, digits count too).
inline size_t meaningful_word_count(const std::string& message);

// True for short, question-free greetings/thanks/bye words.
inline bool is_smalltalk(const std::string& message) {
    static const char* PHRASES[] = {
        "salam", "salam necəsən", "salam necesen", "necəsən", "necesen",
        "necəsiniz", "necesiniz", "sabahın xeyir", "sabahin xeyir",
        "gün aydın", "gun aydin", "axşamın xeyir", "axşamin xeyir",
        "sağ ol", "sag ol", "təşəkkür", "tesekkur", "təşəkkür edirəm",
        "hələlik", "helelik", "sağ olun", "sag olun", "hello", "hi",
        "hey", "salam!", "sağol", "sağolun", "thanks", "thank you",
        "bye", "goodbye", "sabah sağ ol", "yaxşı", "yaxsi",
    };
    const std::string n = normalize_smalltalk(message);
    if (n.empty()) return true;  // whitespace-only counts as nothing to process
    for (const char* p : PHRASES) {
        if (n == p) return true;
    }
    // diagnostic: show why a short message missed the phrase table
    if (message.size() < 24) {
        log_warn("smalltalk miss: norm=[" + n + "] words=" +
                 std::to_string(meaningful_word_count(message)));
    }
    return false;
}

// Fixed friendly replies (rotated by message hash; deterministic per input).
inline const char* smalltalk_reply(const std::string& message) {
    static const char* REPLIES[] = {
        "Salam! Mən DUMY-yəm, heyvandarlıq köməkçisi. Kənd təsərrüfatı, "
        "heyvanlar və ya gündəlik suallar üzrə nə soruşmaq istərdiniz?",
        "Salam! 😊 DUMY-yəm. Heyvandarlıq, xəstəliklər, yemləmə və "
        "gündəlik suallar üzrə kömək edə bilərəm - buyurun, sualınızı yazın.",
        "Salam! Əliaçıq köməkçiniz DUMY buradadır. Kənd təsərrüfatı, "
        "baytarlıq və ya sadə gündəlik sual - nə ilə kömək edim?",
    };
    size_t h = 0;
    for (unsigned char c : message) h = h * 131 + c;
    return REPLIES[h % 3];
}

// Counts words that carry meaning (>= 3 letters, digits count too).
inline size_t meaningful_word_count(const std::string& message) {
    const std::string n = normalize_smalltalk(message);
    size_t words = 0;
    size_t i = 0;
    while (i < n.size()) {
        size_t j = n.find(' ', i);
        if (j == std::string::npos) j = n.size();
        if (j - i >= 3) ++words;
        i = j + 1;
    }
    return words;
}

// ---------------------------------------------------------------------------
// Offset-preserving, case-insensitive UTF-8 search.
// to_lower_utf8 can CHANGE the byte length (İ -> i, Cyrillic), so an offset
// taken from its output must never be reused on the original string - that
// silently erased the wrong bytes and left the model's own source list in the
// answer. These helpers return the offset in the ORIGINAL text.
// ---------------------------------------------------------------------------
inline unsigned int utf8_next_cp(const std::string& s, size_t i, size_t& len) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) { len = 1; return c; }
    if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
        len = 2;
        return ((c & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
    }
    if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
        len = 3;
        return ((c & 0x0F) << 12) |
               ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) |
               (static_cast<unsigned char>(s[i + 2]) & 0x3F);
    }
    if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) {
        len = 4;
        return ((c & 0x07) << 18) |
               ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12) |
               ((static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6) |
               (static_cast<unsigned char>(s[i + 3]) & 0x3F);
    }
    len = 1;
    return c;
}

// Case fold for one codepoint (same mapping as to_lower_utf8).
inline unsigned int fold_cp(unsigned int cp) {
    if (cp >= 'A' && cp <= 'Z') return cp + 32;
    switch (cp) {
        case 0xC7: return 0xE7;    // Ç -> ç
        case 0xD6: return 0xF6;    // Ö -> ö
        case 0xDC: return 0xFC;    // Ü -> ü
        case 0x130: return 'i';    // İ -> i
        case 0x15E: return 0x15F;  // Ş -> ş
        case 0x18F: return 0x259;  // Ə -> ə
        default: return cp;
    }
}

// Case-insensitive substring search, returns the byte offset in `haystack`
// (or std::string::npos). `needle` must be lowercase.
inline size_t ci_find(const std::string& haystack, const std::string& needle,
                      size_t from = 0) {
    if (needle.empty() || haystack.size() < needle.size()) return std::string::npos;
    for (size_t i = from; i + 1 <= haystack.size();) {
        size_t hi = i, ni = 0;
        bool ok = true;
        while (ni < needle.size()) {
            if (hi >= haystack.size()) { ok = false; break; }
            size_t hl = 0, nl = 0;
            const unsigned int hc = utf8_next_cp(haystack, hi, hl);
            const unsigned int nc = utf8_next_cp(needle, ni, nl);
            if (fold_cp(hc) != fold_cp(nc)) { ok = false; break; }
            hi += hl;
            ni += nl;
        }
        if (ok) return i;
        size_t adv = 0;
        utf8_next_cp(haystack, i, adv);
        i += adv;
    }
    return std::string::npos;
}

// Removes a model-written "İstifadə olunan mənbələr: ..." / "Mənbələr: ..."
// / "Sources: ..." block. Sources are delivered by the API (JSON) and the
// UI's collapsible list - the model must never write its own list.
inline std::string strip_own_sources(const std::string& in) {
    std::string out = in;
    auto is_line_start = [](const std::string& s, size_t pos) {
        return pos == 0 || s[pos - 1] == '\n' || s[pos - 1] == '\r';
    };
    // Erases the heading line at `start` plus every following list-like line.
    auto erase_block = [](std::string& s, size_t start) {
        size_t pos = start;
        while (pos < s.size()) {
            size_t nl = s.find('\n', pos);
            if (nl == std::string::npos) nl = s.size();
            std::string line = s.substr(pos, nl - pos);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            const std::string ls = to_lower_utf8(line);
            const bool item =
                line.empty() || ls.rfind("-", 0) == 0 || ls.rfind("•", 0) == 0 ||
                ls.rfind("*", 0) == 0 || ls.rfind("[", 0) == 0 ||
                ls.rfind("mənbə", 0) == 0 || ls.rfind("sources", 0) == 0 ||
                ls.rfind("istifad", 0) == 0;
            if (!item) break;  // real prose after the list -> stop
            pos = (nl < s.size()) ? nl + 1 : s.size();
        }
        s.erase(start, pos - start);
    };
    for (;;) {
        size_t p = ci_find(out, "istifadə olunan mənbələr");
        size_t p2 = ci_find(out, "mənbələr:");
        while (p2 != std::string::npos && !is_line_start(out, p2))
            p2 = ci_find(out, "mənbələr:", p2 + 1);
        size_t p3 = ci_find(out, "sources:");
        while (p3 != std::string::npos && !is_line_start(out, p3))
            p3 = ci_find(out, "sources:", p3 + 1);
        const size_t best = std::min(std::min(p, p2), p3);
        if (best == std::string::npos) break;
        const size_t before = out.size();
        erase_block(out, best);
        if (out.size() == before) break;  // safety: always make progress
    }
    return trim(out);
}

// Removes every occurrence of the exact safety line so the pipeline can add
// it back EXACTLY once, at the very end of the answer.
inline std::string strip_safety_line(const std::string& in) {
    const std::string line = SAFETY_WARNING;
    std::string out = in;
    for (;;) {
        size_t pos = out.find(line);
        if (pos == std::string::npos) break;
        out.erase(pos, line.size());
        // collapse the blank line the removal may leave behind
        while (pos < out.size() && (out[pos] == ' ' || out[pos] == '\t'))
            out.erase(pos, 1);
    }
    return trim(out);
}

// Defensive: the model is told to run its check silently, but it may still
// echo the block. Only a standalone heading line is treated as a check block
// (never normal prose), and its following lines are dropped up to a blank
// line, so the user never sees the internal verification.
inline std::string strip_silent_check(const std::string& in) {
    static const char* MARKERS[] = {
        "silent check before answering", "silent check", "silent yoxlama",
        "daxili yoxlama", "yoxlama addımları", "yoxlama nəticəsi",
        "self-check (never show", "özünü yoxlama",
    };
    std::string out = in;
    for (;;) {
        size_t best = std::string::npos;
        for (const char* m : MARKERS) {
            size_t pos = 0;
            while ((pos = ci_find(out, m, pos)) != std::string::npos) {
                const bool line_start = pos == 0 || out[pos - 1] == '\n' ||
                                        out[pos - 1] == '\r' || out[pos - 1] == '#' ||
                                        out[pos - 1] == ' ';
                if (line_start) {
                    if (best == std::string::npos || pos < best) best = pos;
                    break;
                }
                pos += 1;
            }
        }
        if (best == std::string::npos) break;
        size_t nl = out.find('\n', best);
        if (nl == std::string::npos) { out.erase(best); break; }
        // drop the heading line and the bullet/step lines that follow it
        size_t end = nl + 1;
        while (end < out.size()) {
            size_t eol = out.find('\n', end);
            if (eol == std::string::npos) eol = out.size();
            std::string l = trim(out.substr(end, eol - end));
            const bool looks_list = l.empty() || l[0] == '-' || l[0] == '*' ||
                                    l[0] == '•' || l[0] == '[' ||
                                    (l.size() > 1 && std::isdigit(static_cast<unsigned char>(l[0])) &&
                                     (l[1] == '.' || l[1] == ')'));
            if (!looks_list) break;
            end = (eol < out.size()) ? eol + 1 : out.size();
        }
        const size_t before = out.size();
        out.erase(best, end - best);
        if (out.size() == before) break;
    }
    return trim(out);
}

inline const char* OLLAMA_DOWN_MESSAGE =
    "Hal-hazırda kimi cavab verə bilmirəm: lokal Ollama serveri işləmir. "
    "Zəhmət olmasa Ollama-nı işə salın (Windows-da 'ollama' proqramını açın) "
    "və sonra yenidən cəhd edin.";

inline const char* NO_CONTEXT_NOTE =
    "Qeyd: bu sual üzrə PDF mənbələrimdə və internet axtarışında kifayət qədər "
    "məlumat tapılmadı, ona görə cavab modelin ümumi bilikləri əsasında verilib "
    "[Ümumi bilik].";

// ---------------------------------------------------------------------------
// Prompt builders
// ---------------------------------------------------------------------------
inline std::string build_context_block(const std::vector<Chunk>& chunks) {
    std::string out = "MƏNBƏ KONTEKSTİ (hər faktı bu etiketlərlə istifadə et):\n";
    for (const auto& c : chunks) {
        out += "\n" + c.label() + "\n";
        if (c.kind == "web" && !c.title.empty()) {
            out += "Səhifə başlığı: " + c.title + "\n";
        }
        if (c.kind == "web" && !c.trusted) {
            out += "(təsdiqlənməmiş mənbə)\n";
        }
        out += c.text + "\n";
    }
    return out;
}

// Finds labeled parts ("A)", "B.", "C:") at the start of a line in the
// question so the model can be told exactly which parts to answer under their
// own letter (rather than merging or dropping some).
inline std::vector<std::string> extract_part_labels(const std::string& q) {
    std::vector<std::string> labels;
    bool at_line_start = true;
    for (size_t i = 0; i < q.size(); ++i) {
        const char c = q[i];
        if (c == '\n' || c == '\r') { at_line_start = true; continue; }
        if (c == ' ' || c == '\t') { at_line_start = false; continue; }
        if (at_line_start && c >= 'A' && c <= 'Z' && i + 1 < q.size() &&
            (q[i + 1] == ')' || q[i + 1] == '.' || q[i + 1] == ':')) {
            labels.push_back(std::string(1, c));
            if (labels.size() >= 10) break;
        }
        at_line_start = false;
    }
    return labels;
}

inline std::string build_draft_user_message(const std::string& question) {
    std::string s =
        "İstifadə etdiyim məlumatlar yuxarıda verilib. İndi suala cavab ver.\n";
    // Labeled task (A, B, C...): enumerate the labels so every part is answered
    // under its own letter, in order, and none is dropped or merged.
    const std::vector<std::string> parts = extract_part_labels(question);
    if (parts.size() >= 2) {
        s += "QAYDA 0: Bu tapşırıq " + std::to_string(parts.size()) +
             " hissədən ibarətdir. Cavabı MÜTLƏQ hər hissəni ÖZ hərfi altında, "
             "göstərilən sıra ilə ver və HEÇ BİR hissəni buraxma və ya "
             "birləşdirmə: ";
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i) s += ", ";
            s += parts[i] + ")";
        }
        s += ". Hər hissəni MƏZMUNLA cavablandır (hissənin tapşırıq mətnini "
             "eynilə təkrarlama), 1-3 qısa cümlə kifayətdir. Faiz dəyişməsini "
             "(yeni-köhnə)/köhnə*100 kimi hesabla və İŞARƏSİ İLƏ yaz (azalma "
             "mənfi: 20→12 = -40%, 24→16 = -33.33%). \"Qeyri-müəyyənlik\" "
             "hissəsində birbaşa yaz ki, bu təsdiqlənmiş diaqnoz deyil.\n";
    }
    s +=
           "QAYDA 1: Əgər kontekstdə kifayət qədər məlumat varsa, suala BİRBAŞA "
           "ətraflı cavab ver - sadəcə sualı geri atma, yalnız \"Məlumat vermək "
           "istəyirsiniz?\" kimi cavab yazma. Yalnız sual həqiqətən qeyri-müəyyəndirsə "
           "və ya təhlükəlidirsə BİR qısa aydınlaşdırıcı sual ver.\n"
           "QAYDA 2: Cavabda mənbə etiketlərini mətnin DAXİLİNDƏ, iddiadan dərhal "
           "sonra göstər: [Mənbə: fayl, səh. X], [İnternet: url], [Ümumi bilik]. "
           "[Xülasə: ...] etiketini heç vaxt yazma.\n"
           "QAYDA 3: Cavabın sonunda \"İstifadə olunan mənbələr\" və ya hər hansı "
           "başqa mənbə siyahısı YAZMA ( \"Mənbələr:\" başlığı, \"Sources:\", "
           "siyahı şəklində mənbələr) - mənbə siyahısını proqram özü əlavə edir. "
           "Yalnız yuxarıdakı mətn-daxi etiketləri işlət.\n\n"
           "SUAL: " + question;
    return s;
}

// Compact verification prompt: the checker returns a SHORT structured verdict
// (never the answer itself), so the pass is cheap and the verdict is only
// logged. PASS keeps the draft; FAIL feeds `build_correction_user_message`.
inline std::string build_self_check_user_message(const std::string& question,
                                                 const std::string& draft,
                                                 bool has_context) {
    std::string s =
        "Sen bir fakt-yoxlayıcısans (fact-checker). Aşağıdakı sualı və ilkin "
        "cavabı (draft) yoxla və YALNIZ bir JSON obyekt qaytar:\n"
        "{\"verdict\":\"PASS\" və ya \"FAIL\",\"problems\":[\"qısa problem\"]}\n"
        "QAYDALAR:\n"
        "- PASS: draft suala cavab verir, iddialar verilən kontekstə uyğundur, "
        "rəqəm/doza/faizlər düzgündür, hissələr (A, B, C...) tamdır.\n"
        "- FAIL: YALNIZ ciddi problem varsa - kontekstdə olmayan iddia mənbəli "
        "kimi təqdim olunub, səhv rəqəm/faiz, buraxılmış hissə, təhlükəli doza. "
        "Üslub/xırda məsələlər FAIL deyil.\n"
        "- Faiz dəyişməsini (yeni-köhnə)/köhnə*100 ilə yoxla (azalma mənfi).\n"
        "- problems: yalnız FAIL olanda, ən çoxu 3 qısa maddə.\n"
        "- Cavabın ÖZÜNÜ yazma, yalnız JSON verdict qaytar.\n\n";
    if (!has_context) {
        s += "Qeyd: kontekst zəif idi - draft boş yerə mənbəli görünürsə FAIL.\n\n";
    }
    s += "SUAL:\n" + question + "\n\nİLKİL CAVAB (draft):\n" + draft +
         "\n\nYALNIZ JSON: {\"verdict\":\"PASS\",\"problems\":[]}";
    return s;
}

// One-shot correction hint list for the single FAIL retry.
inline std::string build_correction_user_message(const std::string& problems) {
    return "Yuxarıdakı ilkin cavabda yalnız bu problemlər tapıldı:\n- " + problems +
           "\n\nYalnız bu problemləri düzəlt. Cavabın QALAN HİSSƏSİNİ EYNİ SAXLA: "
           "strukturu, hissələri (A, B, C...), rəqəmləri və mənbə etiketlərini "
           "dəyişmə. Yeni mövzu, yeni hissə və ya sualdan kənar məlumat ƏLAVƏ "
           "ETMƏ. Cavabı Azərbaycan dilində yaz. Yalnız düzəldilmiş TAM cavabı "
           "yaz (heç bir izah, başlıq və ya mənbə siyahısı yox).";
}

inline std::string build_summarize_user_message(const std::string& question,
                                                const std::string& group_text) {
    return "Aşağıdakı məlumat parçalarından SUALA kömək edən faktları BİRBAŞA yaz: "
           "\"mənbədə deyilir\", \"məlumat verilir\", \"aşağıdakı mətn\" kimi şərh / "
           "meta-ifadələri heç işlətmə, yalnız faktların özünü yaz. [Xülasə: ...] "
           "etiketini istifadə etmə. [Mənbə: ...] və [İnternet: ...] etiketlərini olduğu "
           "kimi KOPYALA (nümunə olaraq \"file\", \"url\" sözlərini yazma). "
           "Rəqəmləri və adları dəyişdirmə. 120-200 sözlə sıx yaz. Yalnız xülasə "
           "mətnini çıx et.\n\nSUAL: " +
           question + "\n\nMƏLUMAT:\n" + group_text;
}

// Heuristic detectors -------------------------------------------------------
// Programming / code request -> polite Azerbaijani refusal (no LLM call).
inline bool is_code_request(const std::string& question) {
    const std::string q = " " + to_lower_utf8(question) + " ";
    static const char* kw[] = {
        "kod yaz", "kodu yaz", "kodunu yaz", "kodla", "kod ", "kodu", "kodları",
        "proqram yaz", "proqramı", "proqramlas", "skript", "script", "python",
        "c++", "javascript", "java ", "html", "css", "sql", "php", "kod əvəz",
        "debug", "xətanı tap", "funksiya yaz", "alqoritm", "write code",
        "write a program", "coding", "compiler", "compile", "regex", "bash ",
        "powershell", "kod nümun", "kod örnək", "kodu nümun",
    };
    for (const char* k : kw)
        if (q.find(k) != std::string::npos) return true;
    return false;
}

// Sensitive topic -> safety warning appended to the answer.
inline bool needs_safety_warning(const std::string& question) {
    const std::string q = " " + to_lower_utf8(question) + " ";
    static const char* kw[] = {
        "xəstəlik", "xestelik", "xəstə", "xeste", "simptom", "əlamət", "elamet",
        "diaqnoz", "müalicə", "mualice", "dərman", "derman", "doza", "antibiot",
        "peyvənd", "peyvend", "vaksin", "pestisid", "zəhər", "zehir", "zəhərlən",
        "intoksikasiya", "qırğın", "agir", "ağır", "ölüm", "olum", "sick",
        "disease", "treatment", "symptom", "poison", "pesticide", "antibiotic",
        "dose", "mortality", "epizoot", "parazit", "mənşəyi", "infeksi", "virus",
        "bakter", "mantar", "göbələk", "qurd", "diareya", "iştahsız", "ishal",
    };
    for (const char* k : kw)
        if (q.find(k) != std::string::npos) return true;
    return false;
}

}  // namespace dumy
