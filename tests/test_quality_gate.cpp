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

    // 14) STEP 5: one shared agriculture list covers the livestock words the
    //     retriever's private copy missed.
    check(has_agri_keyword("Dana ishalı olduqda nə etmək lazımdır?") &&
              has_agri_keyword("Buzov hansı yemlə bəslənir?") &&
              has_agri_keyword("Toyuq yumurtası necə saxlanmalıdır?") &&
              has_agri_keyword("Keçi südü") && has_agri_keyword("Arı ailəsi qışa hazırlanır") &&
              has_agri_keyword("Quzu əti") && has_agri_keyword("At bəslənilməsi") &&
              has_agri_keyword("Camış südü") && has_agri_keyword("Qaramal naxırları") &&
              has_agri_keyword("Mal-qara bəslənilməsi") && has_agri_keyword("Ördək bəsləmək") &&
              has_agri_keyword("Hinduşka yemi") &&
              has_agri_keyword("Arılarda varroa xəstəliyi ilə necə mübarizə aparılır?") &&
              has_agri_keyword("Quşlar hansı yemlə bəslənir?") &&
              has_agri_keyword("Atlar üçün yem"),
          "shared agri list covers the livestock vocabulary");

    // 15) ...and still keeps everyday questions out of the PDF path.
    check(!has_agri_keyword("Telefon necə işləyir?") &&
              !has_agri_keyword("2026-cı ildə dünya çempionu kim oldu?"),
          "shared agri list ignores general questions");

    // ---- STEP 2 (species guard): 3 matches, 3 other-species, 2 generic ----
    auto chunk = [](const std::string& text, const char* file) {
        Chunk c;
        c.text = text;
        c.source = file;
        c.page = 1;
        c.kind = "pdf";
        return c;
    };
    const Chunk goat_chunk = chunk(
        "Keçilərdə dırnaq xəstəlikləri: dırnaq altlığının iltihabı və buynuz "
        "hissəsinin çatlaması keçilərdə də rast gəlinir.", "Keçi.pdf");
    const Chunk cattle_chunk = chunk(
        "Qaramal dırnaq altlığının, dırnaq yumaşqlığının və dırnaq tacının "
        "xəstəlikləri mal-qarada geniş yayılmışdır.", "İri buynuzlu.pdf");
    const Chunk generic_chunk = chunk(
        "Dırnaq xəstəliklərinin ağır və xronik forması vaxtında müalicə "
        "olunmayanda ortaya çıxır.", "Cerrahiyye.pdf");
    const Chunk sheep_chunk = chunk(
        "Qoyunlarda dırnaq çürüməsi nəmli pəyərdə saxlanma ilə bağlıdır, "
        "quzularda da təsadüf olunur.", "Qoyunçuluq.pdf");
    const Chunk horse_chunk = chunk(
        "Atların bağırsaq koliki qarın ağrısı ilə özünü göstərir.", "Atlar.pdf");

    // 16) match: the cattle chunk is dropped, the goat one stays.
    {
        const SpeciesFilterResult r = filter_chunks_by_species(
            az_species_in_question("Keçilərdə dırnaq xəstəlikləri hansılardır?"),
            {cattle_chunk, goat_chunk});
        check(r.matched && r.kept.size() == 1 && r.dropped == 1 &&
                  r.kept[0].source == "Keçi.pdf",
              "species: goat question drops the cattle chunk");
    }

    // 17) match: a generic chunk stays next to the matching one.
    {
        const SpeciesFilterResult r = filter_chunks_by_species(
            az_species_in_question("Dana ishalı olduqda nə etmək lazımdır?"),
            {generic_chunk, cattle_chunk});
        check(r.matched && r.kept.size() == 2 && r.dropped == 0,
              "species: generic chunks stay when a chunk matches");
    }

    // 18) match: a chunk naming the target and another group is usable.
    {
        const SpeciesFilterResult r = filter_chunks_by_species(
            az_species_in_question("Quzu ishalı"), {sheep_chunk, horse_chunk});
        check(r.matched && r.kept.size() == 1 && r.kept[0].source == "Qoyunçuluq.pdf" &&
                  r.dropped == 1,
              "species: sheep/lamb question keeps the sheep chunk");
    }

    // 19) other species only -> no match, nothing kept.
    {
        const SpeciesFilterResult r = filter_chunks_by_species(
            az_species_in_question("Camışda mastit"), {cattle_chunk, sheep_chunk});
        check(!r.matched && r.kept.empty(),
              "species: buffalo question keeps no cattle/sheep chunk");
    }

    // 20) other species only, single chunk.
    {
        const SpeciesFilterResult r = filter_chunks_by_species(
            az_species_in_question("Toyuqlarda qəbizlik"), {horse_chunk});
        check(!r.matched && r.kept.empty(),
              "species: poultry question keeps no horse chunk");
    }

    // 21) other species only, with a generic chunk in the mix.
    {
        const SpeciesFilterResult r = filter_chunks_by_species(
            az_species_in_question("Arılarda nozematoz"), {generic_chunk, sheep_chunk});
        check(!r.matched && r.kept.empty(),
              "species: bee question with only generic/other chunks gets no context");
    }

    // 22) generic: generic-only chunks never count as a species match.
    {
        const SpeciesFilterResult r = filter_chunks_by_species(
            az_species_in_question("Keçilərdə dırnaq xəstəlikləri"), {generic_chunk});
        check(!r.matched && r.kept.empty(),
              "species: generic-only chunks do not answer a species question");
    }

    // 23) generic: a question with no species is not filtered at all.
    {
        const SpeciesFilterResult r = filter_chunks_by_species(
            az_species_in_question("Buğda əkini üçün torpaq necə hazırlanmalıdır?"),
            {cattle_chunk, horse_chunk, generic_chunk});
        check(r.kept.size() == 3 && r.dropped == 0,
              "species: question without a species keeps every chunk");
    }

    // The Tier-4 text names the species that was asked.
    {
        const SpeciesQuery sq = az_species_in_question("Camışda mastit");
        check(sq.name() == "buffalo" &&
                  species_no_source_text(sq.label()).find("camış") != std::string::npos,
              "species: Tier-4 text names the asked species");
    }

    // ---- STEP 4 (cosmetics): dangling " ." / " :" after citations --------
    {
        const std::string cleaned = clean_answer_text(
            "Cavab: dırnaqlar quru saxlanmalıdır [Mənbə: Qoyunçuluq.pdf, səh. 7] .");
        check(cleaned.find(" .") == std::string::npos &&
                  cleaned.find("[Mənbə") == std::string::npos && !cleaned.empty() &&
                  cleaned.back() == '.',
              "citations: a label before the full stop leaves no ' .'");
    }
    {
        const std::string cleaned = clean_answer_text(
            "Tədbirlər [Mənbə: Qoyunçuluq.pdf, səh. 9] : su və yem.");
        check(cleaned.find(" :") == std::string::npos &&
                  cleaned.find(": su") != std::string::npos,
              "citations: a label before a colon leaves no ' :'");
    }
    {
        const std::string kept = strip_dangling_punctuation("3.5 kq və səh. 5.");
        check(kept == "3.5 kq və səh. 5.",
              "cosmetics: decimals and 'səh. 5' are not punctuation gaps");
    }

    // ---- STEP 3 (document-level species label) ---------------------------
    // A generic chunk (names no animal) inherits the label of its book. Three
    // synthetic books, labelled by az_document_species_labels() itself so the
    // test does not restate the thresholds.
    const Chunk no_species = chunk(
        "Dırnaq altlığının iltihabı vaxtında müalicə olunmazsa ağırlaşır.", "Cərrahiyyə.pdf");
    std::vector<Chunk> corpus;
    for (int i = 0; i < 60; ++i)
        corpus.push_back(chunk("Qaramalın bəslənməsi və sağım qaydaları.", "Qaramal.pdf"));
    for (int i = 0; i < 60; ++i)
        corpus.push_back(chunk("Qoyunlar və keçilər bir yerdə saxlanılır.", "Qarışıq.pdf"));
    for (int i = 0; i < 60; ++i)
        corpus.push_back(chunk("Buğda əkini üçün torpaq hazırlanır.", "Buğda.pdf"));
    // ...and the chunk under test belongs to the cattle book.
    const Chunk cattle_book = chunk(
        "Dırnaq altlığının iltihabı vaxtında müalicə olunmazsa ağırlaşır.", "Qaramal.pdf");
    const Chunk mixed_book = chunk(
        "Dırnaq altlığının iltihabı vaxtında müalicə olunmazsa ağırlaşır.", "Qarışıq.pdf");
    const auto labels = az_document_species_labels(corpus);

    // 24) The label is computed from the book, not given per chunk.
    {
        const auto cattle = labels.find("Qaramal.pdf");
        const auto mixed = labels.find("Qarışıq.pdf");
        const auto plant = labels.find("Buğda.pdf");
        check(cattle != labels.end() && cattle->second.kind == DocSpeciesKind::Single &&
                  cattle->second.name() == "cattle" &&
                  mixed != labels.end() && mixed->second.kind == DocSpeciesKind::Multi &&
                  plant != labels.end() && plant->second.kind == DocSpeciesKind::None,
              "doc label: cattle book / mixed book / plant book");
    }

    // 25) A generic chunk from ANOTHER species' book is dropped for a goat
    //     question, even though it names no animal at all.
    {
        const SpeciesFilterResult r = filter_chunks_by_species(
            az_species_in_question("Keçilərdə dırnaq xəstəlikləri"),
            {cattle_book}, &labels);
        check(!r.matched && r.kept.empty() && r.dropped == 1 && !r.by_document,
              "doc label: generic chunk from a cattle book is dropped for goats");
    }

    // 26) The same generic chunk is kept for a cattle question that names the
    //     species in no chunk (the qarayara case: by document label).
    {
        const SpeciesFilterResult r = filter_chunks_by_species(
            az_species_in_question("Mal-qarada qarayara xəstəliyinin əlamətləri nələrdir?"),
            {cattle_book}, &labels);
        check(!r.matched && r.kept.size() == 1 && r.by_document,
              "doc label: cattle question keeps a generic chunk of a cattle book");
    }

    // 27) NO document is labelled Single for the questioned species (goats),
    //     so a generic chunk of a multi-species book is dropped too: it cannot
    //     be attributed to the goat. This is the A6 rule - the TB pages of
    //     multi-species books answered "Arılarda varroa" with cattle
    //     vaccinations (test_results/doc_level_after.md A6).
    {
        const SpeciesFilterResult r = filter_chunks_by_species(
            az_species_in_question("Keçilərdə dırnaq xəstəlikləri"),
            {cattle_book, mixed_book}, &labels);
        check(!r.matched && r.kept.empty() && r.dropped == 2 && r.named_only &&
                  !r.by_document,
              "doc label: no single goat book drops generic chunks of every book");
    }

    // 28) The matcher no longer reads verbs and compounds as animals: STEP 1
    //     measured 2225 of 2762 keci* tokens as keçir- and 1306 of 1382 mum*
    //     as mümkün (test_results/doc_species.md).
    {
        check(az_species_groups_in("Keçirilir və mümkün qədər cücərmə olunur").empty() &&
                  az_species_groups_in("Keçilərdə və arılarda").size() == 2,
              "stems: keçir/mümkün/cücərmə are not species, keçilərdə/arılarda are");
    }

    // 29) ...but a chunk that NAMES the questioned species needs no single-book
    //     label to survive (S4 buffalo: the camış chunk is the whole evidence).
    {
        const Chunk buffalo_book = chunk(
            "Camışlarda mastit süd vəzisinin iltihabı kimi özünü göstərir.", "Qarışıq.pdf");
        const SpeciesFilterResult r = filter_chunks_by_species(
            az_species_in_question("Camışlarda mastit necə müalicə olunur?"),
            {mixed_book, buffalo_book}, &labels);
        check(r.matched && r.kept.size() == 1 && r.kept[0].source == "Qarışıq.pdf" &&
                  r.dropped == 1 && !r.by_document && r.named_only,
              "doc label: a chunk naming the species survives without a single book");
    }

    // 30) Query expansion is skipped for a short question - at most
    //     expansion.max_simple_words words. The helper model samples different
    //     alternative queries on every run ("Camışda mastit" -> "camel
    //     mastitis" vs "qamış qəlbəyin iltihabı"), the retrievers fuse them and
    //     the relevance gate flips between identical runs
    //     (test_results/gate_regression.md). The word count is az_words(), i.e.
    //     the same tokenizer the quality helpers use.
    {
        const std::string ten = "bir iki uc dord bes alti yeddi sekkiz doqquz on";
        check(az_skip_expansion_for_length("Camışda mastit", 10) &&
                  az_skip_expansion_for_length("Dana ishalı olduqda nə etmək lazımdır?", 10) &&
                  az_skip_expansion_for_length(ten, 10) &&
                  !az_skip_expansion_for_length(ten + " onbir", 10) &&
                  !az_skip_expansion_for_length("Camışda mastit", 0),
              "expansion: <= max_simple_words words is short, one more is not");
    }

    std::printf("\n%d/%d passed\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
