# Document-level species measurement (STEP 1)

Date: 2026-11-19. Branch `fix/species-doc-level`, from `fix/species-guard` (HEAD 48eed8f).
No source file was changed for this step: the counts below come from a throwaway
measurement tool, `test_results/doc_species_report.cpp` (git-ignored, built with the
project's MSVC toolchain), which `#include`s `include/dumy/az_quality.h` and therefore
uses exactly the shared table and stems the pipeline uses - `az_species_groups()`
(`az_quality.h:402`), `az_species_stems()` (`:421`) and `az_species_token_matches()`
(`:436`). Nothing is hand-labelled: the label column is a rule applied to the counts.

## Method

`doc_species_report.exe data/index.bin` reads the persisted index (`DUMYVEC1`, u32
version, u64 count, then per record u32 metadata length + UTF-8 JSON + u32 embedding
dimension + dim float32) and, for every chunk, folds the text with `az_fold` and walks
`az_words()`, incrementing one counter per (word, group) hit through the shared
`az_species_stems()` / `az_species_token_matches()`. A chunk that hits no group is
`generic`; a chunk that hits a group "names" it.

Command (exit 0):

```
./test_results/doc_species_report.exe data/index.bin > test_results/doc_species_raw.tsv
DOCS=23 CHUNKS=6751
```

`chunks` = chunks of that PDF, `generic` = chunks naming no group, `mentions` = token
hits over all groups, the seven group columns are token hits per group.

## Per-PDF counts

| PDF | chunks | generic | mentions | cattle | sheep | goat | buffalo | horse | poultry | bee | dominant | share | label |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---|---|
| Antropozoonoz xəstəlikləri.pdf | 511 | 101 | 1680 | 256 | 390 | 382 | 26 | 105 | 520 | 1 | poultry | 0.31 | multi-species |
| AQRONOMİYANIN əsasları.pdf | 353 | 155 | 516 | 25 | 5 | 127 | 0 | 4 | 340 | 15 | poultry | 0.66 | multi-species |
| ARIF_YDX.pdf | 43 | 22 | 35 | 0 | 0 | 9 | 0 | 0 | 26 | 0 | poultry | 0.74 | multi-species |
| Famil  Nəsibov Klinik diqanostika.pdf | 223 | 223 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | — | — | none |
| HEYVAN BİOKİMYASI.pdf | 485 | 346 | 319 | 75 | 56 | 89 | 21 | 29 | 43 | 6 | goat | 0.28 | multi-species |
| Heyvandarlığın əsasları.pdf | 329 | 44 | 2592 | 645 | 675 | 207 | 88 | 259 | 402 | 316 | sheep | 0.26 | multi-species |
| İntensiv və Ekstensiv maldarlıq.pdf | 303 | 80 | 919 | 837 | 9 | 52 | 2 | 2 | 17 | 0 | cattle | 0.91 | specific: cattle |
| Körpə kənd təsərrüfatı hüyvanlarının daxili xəstəlikləri.pdf | 189 | 36 | 829 | 360 | 289 | 92 | 12 | 29 | 33 | 14 | cattle | 0.43 | multi-species |
| KƏND TƏSƏRRÜFATI HEYVANLARININ YEMLƏNDİRİLMƏSİ.pdf | 292 | 93 | 1243 | 462 | 324 | 21 | 212 | 92 | 125 | 7 | cattle | 0.37 | multi-species |
| KƏND TƏSƏRRÜFATI HEYVANLARININ YETİŞDİRİLMƏSİ (SON).pdf | 268 | 26 | 1772 | 546 | 387 | 132 | 273 | 230 | 193 | 11 | cattle | 0.31 | multi-species |
| Patoloji fiziologiyadan təcrübə məşğələləri.pdf | 145 | 106 | 50 | 0 | 1 | 31 | 0 | 8 | 10 | 0 | goat | 0.62 | multi-species |
| Qaramal naxırlarında-kitab A5.pdf | 277 | 22 | 1732 | 1546 | 3 | 127 | 0 | 3 | 42 | 11 | cattle | 0.89 | specific: cattle |
| Qoyunçuluq.pdf | 409 | 9 | 4739 | 85 | 4223 | 391 | 6 | 13 | 21 | 0 | sheep | 0.89 | specific: sheep |
| SAĞIM və SÜDÜN EMALINDA TEXMOLOGİYALAR və TİNNOVASİYALAR-Monoqrafiya-PDF_compressed.pdf | 529 | 332 | 446 | 263 | 0 | 155 | 0 | 9 | 18 | 1 | cattle | 0.59 | multi-species |
| sud  və süd məhsullarının ekspertizası.pdf | 180 | 76 | 308 | 163 | 26 | 79 | 21 | 8 | 10 | 1 | cattle | 0.53 | multi-species |
| T.B. İsgəndərov Baytarlıq cərrahiyyəsi praktikum.pdf | 323 | 190 | 271 | 109 | 9 | 48 | 0 | 98 | 7 | 0 | cattle | 0.40 | multi-species |
| T.B.İsgəndərov  xırda heyvanların xəstəlikləri.pdf | 317 | 204 | 180 | 8 | 4 | 147 | 0 | 1 | 19 | 1 | goat | 0.82 | specific: goat |
| T.B.İsgəndərov BAYTARLIQ ANESTEZIOLOGIYASI VƏ REANIMATOLOGIYASI.pdf | 164 | 134 | 38 | 1 | 0 | 28 | 0 | 0 | 9 | 0 | goat | 0.74 | multi-species |
| T.B.İsgəndərov Baytarlıq oftalmologiyası.pdf | 85 | 40 | 113 | 38 | 5 | 29 | 12 | 24 | 5 | 0 | cattle | 0.34 | multi-species |
| T.B.İsgəndərov ümumi və xüsusi cərrahiyyə.pdf | 440 | 261 | 391 | 138 | 32 | 105 | 7 | 80 | 27 | 2 | cattle | 0.35 | multi-species |
| YEM HAZIRLANMASININ TEX. VƏ TEXNİKİ TƏMİNATINDA TƏD.-lar VƏ İNNOV-lar- PDF-MONOQRAFİYA-2_compressed.pdf | 564 | 440 | 240 | 71 | 15 | 87 | 1 | 10 | 47 | 9 | goat | 0.36 | multi-species |
| Yem otları.pdf | 172 | 59 | 192 | 6 | 4 | 35 | 0 | 1 | 123 | 23 | poultry | 0.64 | multi-species |
| Şirəli yem bitkiləri - 2013.pdf | 150 | 69 | 185 | 7 | 1 | 39 | 0 | 1 | 135 | 2 | poultry | 0.73 | multi-species |
## Label rule (applied to the numbers, not chosen per document)

A document is **species-specific** when its dominant group has both

* **share >= 0.80** of all group mentions in the document, and
* **at least 50 mentions**, so a handful of words in a short document cannot label it.

Otherwise it is **multi-species**. A document with no group mention at all gets the
label **none** (it can attribute nothing, so it must not be used to exclude anything).

Why 0.80 and 50, from the table above: the four labelled documents sit at 0.82-0.91
with 180-4739 mentions, and the highest *unlabelled* document is 0.74 with 38 mentions
(`...ANESTEZIOLOGIYASI...`), 0.74 with 35 mentions (`ARIF_YDX.pdf`) and 0.73 with 185
mentions (`Şirəli yem bitkiləri - 2013.pdf`). 0.80 is inside that gap between 0.74 and
0.82. The 50-mention floor is what keeps `ARIF_YDX.pdf` (35 mentions) and
`ANESTEZIOLOGIYASI` (38 mentions) out even if their share rose above 0.80 on four
words. Both numbers are read off the measured distribution above; none of them is
fitted to a question.

Result of the rule on this corpus:

| label | PDFs |
|---|---|
| specific: cattle | `Qaramal naxırlarında-kitab A5.pdf` (0.89, 1546), `İntensiv və Ekstensiv maldarlıq.pdf` (0.91, 837) |
| specific: sheep | `Qoyunçuluq.pdf` (0.89, 4223) |
| specific: goat | `T.B.İsgəndərov  xırda heyvanların xəstəlikləri.pdf` (0.82, 147) |
| multi-species | the other 19 documents |
| none | `Famil  Nəsibov Klinik diqanostika.pdf` (0 group mentions in 223 chunks) |

## Measured problem: the stems also match non-species words

The same shared table is what the guard uses, so this had to be measured before any
label is trusted. Counting every token that starts with a group stem in the corpus:

| stem | tokens starting with it | what they really are |
|---|---:|---|
| `keci` | 2762 | `keçirilir` 432, `keçir` 391, `keçid` 171, `keçirilməsi` 141 ... goat words are 435 (`keçi`, `keçilərin`, `keçilər`, ...) |
| `mum` | 1382 | `mümkün` 607 + `mümkündür` 699 = 1306; `mumlar` 6, `mumun` 5 |
| `qus` | 1193 | `quşlar` 380, but `qusma` (vomiting) 156, `qüsurları` 49, `qüsur` 41 |
| `qaz` | 635 | `qazı` 107, `qazlı` (gassy) 53, `qazax` 45, `qazanılmış` 24 |
| `cuce` | 635 | `cücərmə` 111 + `cücərtilər` 97 + `cücərir` 65 ... = germination, not chicks |
| `ari` | 511 | `arılar` 76 but `arıqlama` 74, `arıqlayır` 56, `arıq` 54, `aritmiya` 28 |
| `dana` | 58 | `danaqıran` 8, `danadışilər` 7 (plant names) |

Root cause: `az_species_token_matches` (`az_quality.h:436-441`) accepts **any** prefix
of a 4+ letter stem up to 7 extra letters, so `keçi` matches `keçir-` (the verb
`keçmək`), `mum` matches `mümkün`, `quş` matches `qusma`. Consequently the `goat`,
`poultry` and `bee` columns above are dominated by non-species words and cannot be
used as they stand: 2225 of the 2762 `keci*` tokens (80.5%) are not about goats, and
1306 of 1382 `mum*` tokens are `mümkün`.

What this means for the labels:

* The two cattle documents and `Qoyunçuluq.pdf` are safe: their dominant counts are
  made of real forms (`qaramalın` 264, `qoyunların` 980, `buzovların` 242, ...), and
  the goat contamination only adds to a group that is not dominant there.
* `xırda heyvanların xəstəlikləri.pdf` is labelled `specific: goat` at 0.82, but 147
  goat mentions include `keçir-` forms; the label is **provisional** until the matcher
  is corrected.
* The `poultry`-dominant and `goat`-dominant columns of the multi-species documents
  are noise-driven, and `mümkün` alone makes every document in the corpus "mention
  bees" - so no document can currently be labelled `specific: bee`.

## Consequence for STEP 2

A document label computed from these stems would inherit the same false friends
(`mümkün` -> bee, `keçirilir` -> goat), so STEP 2 must first make the shared matcher
accept only a **complete nominal inflection** after a stem (`keçilərdə`, `qoyunların`)
and no arbitrary continuation (`keçirilir`, `mümkün`), then recompute this table with
the same rule. STEP 2's report shows the recomputed counts and which labels change;
the rule and the 0.80 / 50 thresholds stay as measured here.
