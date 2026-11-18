// test_quality_gate.cpp - unit checks for the text-quality helpers in
// include/dumy/az_quality.h. Built as dumy_unit_tests (see CMakeLists.txt).
// No server, no model, no network: each case is a fixed string.
//
// The cases come from test_results/gate_diagnosis.md: the detector used to
// reject correct answers because it counted [Mənbə: ...] citation labels and
// repeated domain terms as prose.
#include <cstdio>
#include <string>

#include "dumy/az_quality.h"
#include "dumy/prompts.h"

namespace {

int g_failed = 0;
int g_total = 0;

std::string repeat(const std::string& unit, int n) {
    std::string out;
    for (int i = 0; i < n; ++i) out += unit;
    return out;
}

void check(bool ok, const char* name) {
    ++g_total;
    if (!ok) ++g_failed;
    std::printf("%-58s %s\n", name, ok ? "PASS" : "FAIL");
}

}  // namespace

int main() {
    using namespace dumy;

    // 1) A valid numbered list must pass. This is the shape the pipeline wants
    //    for BroadTopic answers, and the old rules rejected it.
    const std::string numbered_list =
        "1. Qidalanma: gundə iki dəfə yem verin, su həmişə təmiz olsun. "
        "2. Sağım: hər sağımdan əvvəl yelin təmiz su ilə yuyulmalıdır. "
        "3. Gigiyena: yataq quru saxlanmalı və hər gün dəyişdirilməlidir. "
        "4. Süd: sağılan süd dərhal süzülməli və soyudulmalıdır. "
        "5. Nəzarət: hər həftə baytar heyvanları yoxlamalıdır.";
    check(!has_repetition_loop(numbered_list), "numbered list (5 items) is not a loop");

    // 2) The same PDF cited four times must pass (gate_diagnosis.md Q2/Q8).
    const std::string cited =
        "İnəklər gündə iki dəfə sağılmalıdır [Mənbə: Qaramal naxırlarında-kitab A5.pdf, səh. 12]. "
        "Sağımlar arasındakı interval bərabər olmalıdır [Mənbə: Qaramal naxırlarında-kitab A5.pdf, səh. 13]. "
        "Yelin gigiyenasına diqqət yetirmək vacibdir [Mənbə: Qaramal naxırlarında-kitab A5.pdf, səh. 14]. "
        "Südün yağlılığı sağım qaydasından asılıdır [Mənbə: Qaramal naxırlarında-kitab A5.pdf, səh. 15].";
    check(!has_repetition_loop(cited), "one PDF cited 4x is not a loop");

    // 3) A domain term repeated in every sentence must pass.
    const std::string domain_terms =
        "Qaramal üçün quru yem əsasdır. Qaramal gündə iki dəfə yemlənməlidir. "
        "Qaramal üçün su həmişə təmiz olmalıdır. Qaramal yatağı quru saxlanmalıdır. "
        "Qaramal üçün du da verilməlidir. Qaramal hər gün yoxlanmalıdır.";
    check(!has_repetition_loop(domain_terms), "domain term repeated 6x is not a loop");

    // 4) The same sentence twice is not a loop (threshold is three).
    const std::string twice =
        "Süd verimini artırmaq üçün yem rasionu düzgün tərtib edilməlidir. "
        "Sağım vaxtı dəyişməməlidir və yelin hər sağımdan sonra yoxlanmalıdır. "
        "Süd verimini artırmaq üçün yem rasionu düzgün tərtib edilməlidir.";
    check(!has_repetition_loop(twice), "same sentence twice is not a loop");

    // 5) A real loop: the same sentence three times must be rejected.
    const std::string loop3 =
        "Süd verimi gündə iki sağımla artır və yem rasionu düzgün olmalıdır. " +
        std::string("Süd verimi gündə iki sağımla artır və yem rasionu düzgün olmalıdır. ") +
        "Süd verimi gündə iki sağımla artır və yem rasionu düzgün olmalıdır.";
    check(has_repetition_loop(loop3), "same sentence 3x is a loop");

    // 6) A 4-word phrase repeated three times is a loop.
    const std::string gram4 =
        "Yelin hər sağımdan əvvəl yuyulması vacibdir. Sonra inək yemlənməlidir. "
        "Yelin hər sağımdan əvvəl yuyulması vacibdir. Sonra su dəyişdirilməlidir. "
        "Yelin hər sağımdan əvvəl yuyulması vacibdir. Sonda süd süzülməlidir.";
    check(has_repetition_loop(gram4), "4-word phrase 3x is a loop");

    // 7) The helper itself: labels go, normal brackets stay.
    const std::string tagged = "Süd faydalıdır [Mənbə: X.pdf, səh. 3] [1] [Ümumi bilik].";
    const std::string stripped = strip_citation_tags(tagged);
    check(stripped.find("Mənbə") == std::string::npos &&
              stripped.find("Ümumi bilik") == std::string::npos &&
              stripped.find("[1]") != std::string::npos,
          "strip_citation_tags removes labels, keeps [1]");

    // 8) STEP 3: the strict retry must reuse the selected chunks.
    check(strict_retry_mode(true, true) == StrictRetryMode::WithContext,
          "retry with chunks reuses them (domain question)");
    check(strict_retry_mode(true, false) == StrictRetryMode::WithContext,
          "retry with chunks reuses them (general question)");

    // 9) No chunks + a general question: a context-free retry is allowed.
    check(strict_retry_mode(false, false) == StrictRetryMode::WithoutContext,
          "no chunks + general question -> context-free retry");

    // 10) No chunks + livestock/crop/medicine: never a free general answer.
    check(strict_retry_mode(false, true) == StrictRetryMode::Tier4Text,
          "no chunks + domain question -> Tier 4 text, no free answer");

    // 11) STEP 4: a rejected BroadTopic/Diagnosis answer shows 2-3 source
    //     excerpts as bullets, with no inline [Mənbə: ...] label.
    auto make_chunk = [](const std::string& text, const char* file, int page) {
        Chunk c;
        c.text = text;
        c.source = file;
        c.page = page;
        c.kind = "pdf";
        return c;
    };
    std::vector<Chunk> excerpts{
        make_chunk("Timpaniya zamanı qarın şişməsi sürətlə artır və bu halda heyvana\n"
                   "təcili müalicə vermək lazımdır.",
                   "Heyvandarlığın əsasları.pdf", 210),
        make_chunk("Timpaniya zamanı ilk addım heyvanın vəziyyətini yoxlamaq və müalicəyə\n"
                   "dərhal başlamaqdır.",
                   "Heyvandarlığın əsasları.pdf", 211),
        make_chunk("Bu kitabda taxıl becərilməsi və onun agrotexnikası barədə yazılmışdır.",
                   "Bitkiçilik.pdf", 40)};
    std::vector<Chunk> used;
    const std::string bullets = build_excerpt_bullets(
        "Timpaniya zamanı hansı müalicə lazımdır?", excerpts, 3, 400, &used);
    auto bullet_lines = [](const std::string& text) {
        size_t n = 0;
        for (size_t i = 0; i + 1 < text.size(); ++i)
            if (text[i] == '\n' && text[i + 1] == '-') ++n;
        return n;
    };
    check(!bullets.empty() && bullets.find("Qısa çıxarışlar:") == 0 &&
              bullets.find("[Mənbə:") == std::string::npos &&
              bullet_lines(bullets) == 2 && used.size() == 2,
          "rejected answer becomes 2 source-excerpt bullets");

    // 12) The item cap is respected.
    std::vector<Chunk> used1;
    const std::string one =
        build_excerpt_bullets("Timpaniya zamanı hansı müalicə lazımdır?", excerpts, 1, 400,
                              &used1);
    check(bullet_lines(one) == 1 && used1.size() == 1,
          "excerpt bullets respect the item cap");

    // 13) No relevant excerpt -> no bullets (the caller falls back further).
    std::vector<Chunk> none;
    check(build_excerpt_bullets("Timpaniya zamanı hansı müalicə lazımdır?", {}, 3, 400, &none)
                  .empty(),
          "no chunks -> no excerpt bullets");

    std::printf("\n%d/%d passed\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
