# Document-level species labels (STEP 2)

Date: 2026-11-19. Branch `fix/species-doc-level`.

## What changed

1. **The shared matcher** (`include/dumy/az_quality.h`, `az_species_ending` +
   `az_species_token_matches`): a stem now matches only the bare word, or the word plus
   ONE complete nominal ending (plural / possessive / case: `lar`, `ler`, `larin`,
   `larda`, `in`, `nin`, `ini`, `da`, `a`, `si`, `lik`, ...). Stems shorter than four
   letters (`at`, `mal`, `qaz`, `qus`, `mum`, `ari`, `qoc`) additionally accept a plural
   marker plus a case ending (`arılarda`, `atlar`), nothing else. STEP 1 measured why:
   the old `+7 letters` prefix rule counted `keçirilir` as a goat 432 times, `mümkün`
   as beeswax 1306 times, `quşma`/`qüsür` as poultry, `cücərmə` as chicks
   (`test_results/doc_species.md`, "Measured problem").

2. **The document label** (`DocSpeciesLabel`, `az_doc_species_label`,
   `az_document_species_labels`): every chunk of a source is counted through the same
   shared table and stems; a document is `Single` when the dominant group has
   >= 0.80 of the document's group mentions and >= 50 of them, `Multi` otherwise and
   `None` when it names no species. `RagPipeline::refresh_document_species()` computes
   the map from `store_->all()` at index load time (`src/main.cpp:299`, again after
   ingest at `:333`, after a console `/ingest` at `:177` and after `POST /ingest` in
   `src/http_server.cpp:633`).

3. **The rule** (`filter_chunks_by_species`, third argument = label map):
   * chunk names the asked group -> keep;
   * chunk names only other groups -> drop;
   * chunk names no group -> keep when its document is labelled with the asked group,
     `Multi` or `None`; drop when the document is labelled with a different group.
   With no label map (unit tests, a store that is not loaded) the old chunk-level rule
   is used unchanged, so nothing that passed before changes meaning silently.

The result also carries `by_document`, used only for logging: it tells whether the
question was answered by the document label when no chunk names the species.

## Recomputed measurement (same tool, same rule, corrected matcher)

`./test_results/doc_species_report.exe data/index.bin` (exit 0, `DOCS=23 CHUNKS=6751`),
read with the STEP 1 rule (share >= 0.80, mentions >= 50):

| PDF | chunks | generic | mentions | cattle | sheep | goat | buffalo | horse | poultry | bee | dominant | share | label |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---|---|
| Antropozoonoz xəstəlikləri.pdf | 511 | 185 | 1336 | 251 | 377 | 65 | 25 | 105 | 512 | 1 | poultry | 0.38 | multi-species |
| AQRONOMİYANIN əsasları.pdf | 353 | 299 | 91 | 15 | 4 | 0 | 0 | 4 | 53 | 15 | poultry | 0.58 | multi-species |
| ARIF_YDX.pdf | 43 | 27 | 26 | 0 | 0 | 0 | 0 | 0 | 26 | 0 | poultry | 1.00 | multi-species |
| Famil  Nəsibov Klinik diqanostika.pdf | 223 | 223 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | — | — | none |
| HEYVAN BİOKİMYASI.pdf | 485 | 401 | 243 | 73 | 49 | 9 | 21 | 29 | 46 | 16 | cattle | 0.30 | multi-species |
| Heyvandarlığın əsasları.pdf | 329 | 73 | 2351 | 658 | 638 | 38 | 83 | 249 | 386 | 299 | cattle | 0.28 | multi-species |
| İntensiv və Ekstensiv maldarlıq.pdf | 303 | 91 | 861 | 825 | 8 | 3 | 2 | 2 | 21 | 0 | cattle | 0.96 | specific: cattle |
| Körpə kənd təsərrüfatı hüyvanlarının daxili xəstəlikləri.pdf | 189 | 45 | 759 | 351 | 287 | 24 | 12 | 28 | 43 | 14 | cattle | 0.46 | multi-species |
| KƏND TƏSƏRRÜFATI HEYVANLARININ YEMLƏNDİRİLMƏSİ.pdf | 292 | 96 | 1154 | 455 | 309 | 0 | 178 | 84 | 121 | 7 | cattle | 0.39 | multi-species |
| KƏND TƏSƏRRÜFATI HEYVANLARININ YETİŞDİRİLMƏSİ (SON).pdf | 268 | 38 | 1603 | 567 | 320 | 38 | 253 | 217 | 198 | 10 | cattle | 0.35 | multi-species |
| Patoloji fiziologiyadan təcrübə məşğələləri.pdf | 145 | 131 | 19 | 0 | 1 | 0 | 0 | 8 | 10 | 0 | poultry | 0.53 | multi-species |
| Qaramal naxırlarında-kitab A5.pdf | 277 | 32 | 1583 | 1518 | 3 | 2 | 0 | 3 | 47 | 10 | cattle | 0.96 | specific: cattle |
| Qoyunçuluq.pdf | 409 | 21 | 4402 | 94 | 4114 | 157 | 6 | 13 | 18 | 0 | sheep | 0.93 | specific: sheep |
| SAĞIM və SÜDÜN EMALINDA TEXMOLOGİYALAR və TİNNOVASİYALAR-Monoqrafiya-PDF_compressed.pdf | 529 | 423 | 295 | 265 | 0 | 1 | 0 | 9 | 19 | 1 | cattle | 0.90 | specific: cattle |
| sud  və süd məhsullarının ekspertizası.pdf | 180 | 91 | 266 | 173 | 26 | 24 | 21 | 8 | 13 | 1 | cattle | 0.65 | multi-species |
| T.B. İsgəndərov Baytarlıq cərrahiyyəsi praktikum.pdf | 323 | 210 | 230 | 109 | 16 | 1 | 0 | 96 | 8 | 0 | cattle | 0.47 | multi-species |
| T.B.İsgəndərov  xırda heyvanların xəstəlikləri.pdf | 317 | 282 | 42 | 8 | 4 | 2 | 0 | 1 | 26 | 1 | poultry | 0.62 | multi-species |
| T.B.İsgəndərov BAYTARLIQ ANESTEZIOLOGIYASI VƏ REANIMATOLOGIYASI.pdf | 164 | 152 | 15 | 1 | 0 | 0 | 0 | 0 | 14 | 0 | poultry | 0.93 | multi-species |
| T.B.İsgəndərov Baytarlıq oftalmologiyası.pdf | 85 | 50 | 87 | 37 | 5 | 4 | 12 | 24 | 5 | 0 | cattle | 0.43 | multi-species |
| T.B.İsgəndərov ümumi və xüsusi cərrahiyyə.pdf | 440 | 318 | 293 | 137 | 36 | 4 | 7 | 80 | 27 | 2 | cattle | 0.47 | multi-species |
| YEM HAZIRLANMASININ TEX. VƏ TEXNİKİ TƏMİNATINDA TƏD.-lar VƏ İNNOV-lar- PDF-MONOQRAFİYA-2_compressed.pdf | 564 | 501 | 155 | 71 | 13 | 0 | 1 | 10 | 51 | 9 | cattle | 0.46 | multi-species |
| Yem otları.pdf | 172 | 146 | 35 | 6 | 4 | 0 | 0 | 1 | 4 | 20 | bee | 0.57 | multi-species |
| Şirəli yem bitkiləri - 2013.pdf | 150 | 138 | 15 | 6 | 1 | 0 | 0 | 1 | 5 | 2 | cattle | 0.40 | multi-species |

## Labels: before -> after

| PDF | STEP 1 (old matcher) | STEP 2 (fixed matcher) | why it changed |
|---|---|---|---|
| `Qaramal naxırlarında-kitab A5.pdf` | specific: cattle (0.89) | specific: cattle (0.96) | `keçir-` no longer counts as goat |
| `Qoyunçuluq.pdf` | specific: sheep (0.89) | specific: sheep (0.93) | `keçir-` no longer counts as goat |
| `İntensiv və Ekstensiv maldarlıq.pdf` | specific: cattle (0.91) | specific: cattle (0.96) | noise removed from other columns |
| `T.B.İsgəndərov  xırda heyvanların xəstəlikləri.pdf` | specific: goat (0.82) | **multi-species** (0.62 poultry, 42 mentions) | its 147 "goat" hits were `keçir-`; real goat mentions are 2 |
| `SAĞIM və SÜDÜN EMALINDA ... Monoqrafiya` | multi-species (0.59) | **specific: cattle (0.90)** | goat 155 -> 1 and poultry 18 -> 18: the 0.59 share was noise |
| `Antropozoonoz xəstəlikləri.pdf` | multi (0.31 poultry) | multi (0.38 poultry) | poultry 520 -> 222 |
| `Yem otları.pdf` | multi (0.64 poultry) | multi (0.57 bee, 35 mentions) | `qaz*`/`qus*` noise removed |
| every other document | multi / none | multi / none | share moved by <= 0.1 |

The rule itself was not touched: 0.80 / 50 are the STEP 1 numbers, and after the fix
the gap is even wider - the four labelled documents sit at 0.90-0.96, the highest
unlabelled one at 0.65 (`sud və süd məhsullarının ekspertizası.pdf`). Two documents
would qualify on share alone and are stopped by the 50-mention floor:
`ARIF_YDX.pdf` (1.00, 26 mentions) and `...ANESTEZIOLOGIYASI...` (0.93, 15 mentions).

Load-time log of the built server (`build_id=Nov 19 2026 20:20:33`, `chunks=6751`):

```
doc species labels: 23 document(s), 4 species-specific
  pdfs/Qaramal naxırlarında-kitab A5.pdf -> cattle (1518 mentions, share 0.95)
  pdfs/Qoyunçuluq.pdf -> sheep (4114 mentions, share 0.93)
  pdfs/SAĞIM və SÜDÜN EMALINDA ... Monoqrafiya-PDF_compressed.pdf -> cattle (265 mentions, share 0.89)
  pdfs/İntensiv və Ekstensiv maldarlıq.pdf -> cattle (825 mentions, share 0.95)
```

(The server prints the pre-rounded mean over its own chunk copies; the table above is
the same computation printed with two decimals. `share 0.89` for SAĞIM is 0.8959.)

## Checks at this step

```
build: cmake --build build --config Release --target dumy dumy_unit_tests   exit 0
dumy_unit_tests.exe                                                        28/28 passed
testsun_all.ps1                                                        83/84 passed, 0 warnings
```

The one failure is `t16_pdf_diagnosis` ("Gündəlik süd verimini necə artırmaq olar?"),
reported as `answer ends mid-word`: the 4B draft stopped at `"["`. It is not caused by
this step - the question names no species, so `az_species_in_question()` is empty and
the guard is a no-op (`filter_chunks_by_species` returns the chunks untouched). Two
fresh requests to the same server answered it completely (`800 chars`, `641 chars`,
`sources=1`) with no trailing bracket. Reported as model variance, not worked around.
