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

    std::printf("\n%d/%d passed\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
