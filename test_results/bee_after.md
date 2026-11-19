# Bee corpus: text check, ingest, and live re-measurement (STEP 1-3)

Date: 2026-11-19. Branch `fix/species-guard`, HEAD `28d2685`. No C++ source file was
changed in this task; only the test case and the test runner (see STEP 3). The 0-byte
`Respublikamızda yayılan arı cinsləri ... .txt` was not touched or ingested.

## STEP 1 - text extractability and hashes

`sha256sum "/c/Users/Shadow/Desktop/lazimi rusus/"*.pdf` (exit 0):

```
516b2aa1f3c6ed4bf12a46363dca569dc9bfa840275a30a0c39d323f42fdfbd0  ari-ailalarinin-vaziyyati-ziyanverici-va-xastaliklari.pdf
8bfef6efa589b2de9e1bf858cd9c346036114919024a3774c61838d465813566  Arıçılıq_təlimat.pdf
2d565866f8ca8c3982115d521cecced93cb5a3def509b6a3c96d5ac776635aa8  10. Arı hastalıkları.pdf
```

Per-page text (`pdftotext -enc UTF-8`, one page per form feed; characters counted
after removing whitespace, script `test_results/bee_extract/pages.py`):

| PDF | pages | chars | min / median / max per page | pages with <30 chars |
|---|---:|---:|---|---|
| `ari-ailalarinin-vaziyyati-ziyanverici-va-xastaliklari.pdf` | 158 | 239 402 | 76 / 1 476 / 4 807 | none |
| `Arıçılıq_təlimat.pdf` | 54 | 171 099 | 0 / 3 415 / 4 421 | 1, 54 |
| `10. Arı hastalıkları.pdf` | 57 | 24 540 | 0 / 479 / 1 164 | 2, 8, 9, 10, 17, 18, 22, 23, 32, 36, 42, 48, 51, 54, 56, 57 |

Details:

* `ari-ailalarinin-...pdf` is a full text layer on every page (min 76 chars, no empty
  page); first line `İxtisasın adı: Arıçı ... Bakı - 2017`.
* `Arıçılıq_təlimat.pdf` - the "page 1" hit is the leading form feed before the title
  page text (`head:` starts with `\fArıçılığa yeni başlayanlar üçün TƏ L İ M AT`); page
  54 is the back cover. poppler-cpp (what the app uses) reports 53 pages, 2 low-text.
* `10. Arı hastalıkları.pdf` is a presentation deck (`ARI HASTALIKLARI - ASSIST. PROF.
  DR. HİLAL ÇAPAR AKYÜZ`). 41 of 57 pages carry a text layer; the 16 listed pages are
  image-only slides (0 chars) - e.g. the varroa section p.43-50 is readable, only p.48
  is an image. Their content is therefore not searchable, but the disease sections are.

Verdict: **none of the three is a scan.** Every PDF has an extractable text layer on
all content pages; the app's own ingest recorded the same picture in
`data/manifest.json`: `scanned_pages` = 16, 2 and 0 respectively
(its own 8-byte hash `6cedbaf4e5a36391`, `58c6ceaa756ccdef`, `eb6d8b8f906f4f9c`).

## STEP 2 - ingest and document labels

The three PDFs were copied into `pdfs/` (the folder `pdf_folder` in `config.json` names,
README.md:133-135), then:

```
./build/Release/dumy.exe --ingest      # exit 0
Ingest finished in 2.66 min: 1 indexed, 25 skipped, 0 duplicates, 0 failed, +159 chunks
```

(The first, timed-out attempt had already indexed the first two books; the re-run
resumed from the manifest: `2026-11-19 21:25:57  +159 chunk`, third book.) Index is now
**26 documents / 7 045 chunks** (`/health` `"chunks":7045`, server started 21:26).

Per-book species counts, measured with the throwaway
`test_results/doc_species_report.cpp` (compiled against the current
`include/dumy/az_quality.h`, i.e. the same stems and matcher as the pipeline):

| book | chunks | generic | species mentions | bee hits | bee share | label |
|---|---:|---:|---:|---:|---:|---|
| `10. Arı hastalıkları.pdf` | 39 | 4 | 178 | 178 | **1.000** | Single bee |
| `Arıçılıq_təlimat.pdf` | 96 | 0 | 1 369 | 1 360 | **0.993** | Single bee |
| `ari-ailalarinin-vaziyyati-ziyanverici-va-xastaliklari.pdf` | 159 | 9 | 1 666 | 1 599 | **0.960** | Single bee |

Server startup log (`test_results/bee_after_server.log:9`):

```
doc species labels: 26 document(s), 7 species-specific
  pdfs/10. Arı hastalıkları.pdf -> bee (178 mentions, share 1.00)
  pdfs/Arıçılıq_təlimat.pdf -> bee (1360 mentions, share 0.99)
  pdfs/Qaramal naxırlarında-kitab A5.pdf -> cattle (1518 mentions, share 0.95)
  pdfs/Qoyunçuluq.pdf -> sheep (4114 mentions, share 0.93)
  pdfs/SAĞIM və SÜDÜN EMALINDA ... .pdf -> cattle (265 mentions, share 0.89)
  pdfs/ari-ailalarinin-vaziyyati-ziyanverici-va-xastaliklari.pdf -> bee (1599 mentions, share 0.95)
  pdfs/İntensiv və Ekstensiv maldarlıq.pdf -> cattle (825 mentions, share 0.95)
```

All three bee labels are correct (bee is the dominant group by name and by share, all
above the unchanged 0.80 share / 50-mention rule at `include/dumy/az_quality.h:559-560`),
so **no stem or threshold was edited** (`az_species_stems()` `az_quality.h:421`).
The corpus had no Single-bee document before this task, which is why A6 used to fall
into the "named only" branch (`az_quality.h:652-666`).

## STEP 3 - live cases

### Test-case change

`tests/cases/dom06_varroa.json` (whole file):

```json
{"id":"dom06_varroa","question":"Arılarda varroa xəstəliyi ilə necə mübarizə aparılır?","qtype_in":["broad_topic","simple_fact","diagnosis","general"],"expect_contains":["varroa"],"expect_not_contains":["vərəm","Vərəm"],"expect_source_contains_any":["Arı hastalıkları","Arıçılıq","ari-ailalarinin"],"max_garbage_ratio":0.18,"max_seconds":45}
```

* `expect_not_contains: vərəm` - the old failure (TB chapter cited for bees).
* `expect_source_contains_any` - **new runner field**, because `run_all.ps1` could only
  count distinct PDF files (`min/max_pdf_sources`, run_all.ps1:278-284) and could not
  require a particular book. Added at `tests/run_all.ps1:288-303`; it fails the case
  unless at least one cited PDF file name contains one of the fragments. No other
  assertion or threshold was changed.
* `max_pdf_sources: 0` was removed (it existed to force the Tier-4 answer); requiring a
  bee book source is the new expectation.

### Answers before / after

Before = `test_results/doc_level_after2.json` (previous task, without bee books),
after = `test_results/bee_after.json` (same 9 questions, same server settings,
`DUMY_DEBUG_TRACE=1 ./build/Release/dumy.exe --no-browser --no-web`, gemma3:4b,
bge-m3, 7045 chunks).

| Q | question | before | after |
|---|---|---|---|
| A1 | Mal-qarada qarayara əlamətləri | answer, 2 PDF srcs (Antropozoonoz p.21, 36) | **unchanged**, same 2 srcs |
| A6 | Arılarda varroa ilə mübarizə | answer, 1 src `Körpə ... xəstəlikləri.pdf` p.171 | answer, **5 srcs from 2 bee books** |
| A10 | Keçilərdə dırnaq xəstəlikləri | Tier-4, 0 srcs | **Tier-4, 0 srcs** (unchanged) |
| S1 | Qoyunlarda dırnaq çürüməsi | answer, 1 src `Heyvandarlığın əsasları.pdf` p.207 | **unchanged**, same src |
| S2 | Toyuqlarda qəbizlik | Tier-4 | Tier-4 (unchanged) |
| S3 | Atlarda kolik | clarification, 0 srcs | **Tier-4** (flip, see below) |
| S4 | Camışda mastit | answer, 3 srcs (YEMLƏNDİRMƏ p.201, Heyvandarlığın əsasları p.263/265) | **Tier-4** (flip, see below) |
| S5 | Arılarda nozematoz | Tier-4 | answer, **3 srcs from a bee book** (improvement) |
| S6 | Quzu ishalı | answer, 4 srcs | **Tier-4** (flip, see below) |

A6 after (12.4 s, `tier4=False`, sources `10. Arı hastalıkları.pdf` p.54, 48, 42 and
`ari-ailalarinin-...pdf` p.100, 114; guard log: `kept 5 chunk(s) for bee (chunk names
it), dropped 0`):

> Varroosis arı koloniyaları üçün ciddi bir təhdiddir. Mübarizə tədbirləri əsasən
> varroonun yayılmasının qarşısını almaq və infeksiya olan arıları təmizləmək üzrədir.
> Varroa ilə mübarizədə ilk növbədə kolonilərin qarantinə alınması, hərəkətə
> qoyulmaması və təmizliyin təmin edilib şübhə doğdurmamağı vacibdir. Həmçinin,
> arıların göbələkə meşəyə çıxmamaq üçün kovanlara yavrulu petek və işçi arı
> verilməməlidir.
> Əlavə olaraq, yağmacılığın qarşısını almaq üçün uçuş deli üzərində qoruyucu tədbirlər
> görülməlidir. Varroa ilə mübarizədə tütün dumanı da effektiv vasitə hesab olunur.
> Həmçinin viruslu ifliclə mübarizə aparılarkən arılarının sağlamlığını qorumaq üçün
> profilaktik tədbirlər görmək vacibdir.

Before A6 (for contrast - this is the bug the bee books fix):

> Arilarda varroa xəstəliyinə müvafiq olaraq mübarizə aparmaq üçün göstərir ki, varroa
> varioa qurdudur ... Bu xəstəliyin müalicəsi üçün serumlar yeridilir. Yara yerindən
> 10-15 sm aralı 0,5%-li novokainlə dairəvi keçirici blokada aparılır ... **Böv zəhəri**
> ilə zəhərləndikdə heyvanlara yuxarıda qeyd olunan serumdan inyeksiya olunur ...

S5 after (8.2 s, 3 srcs from `ari-ailalarinin-...pdf` p.112, 131, 113):

> Nozematoz, ergin bal arılarının orta bağırsaklarında Nosema apis paraziti tərəfindən
> törədildiyi bir xəstəlikdir. ... Müalicəyə dair məlumdur ki, nistatin və yoddan istifadə
> edilərək müalicə aparıla bilər. Nistatin toz kimi ya şərbətlə birgə verilib, yod isə
> çiləməklə ya şərbətlə birgə verilir.

S1 after (6.0 s, unchanged, `kept 1 chunk(s) for sheep (chunk names it), dropped 5`):

> Qoyunlarda dırnaq çürüməsi, yəni nekrobakterioz xəstəliyi axsama, müalicə olunmazsa
> qoyun dizi üstə yeriyir. Dırnağın yumşaq sahəsində və dırnaq arası sahədə yaralar əmələ
> gəlir. Müalicə kimi dırnaq buynuzunda olan ölmüş hüceyrələr götürülür və 3%-li hidrogen
> neroksid, marqansovka ilə işlənir.

A1 after (10.3 s, `kept 2 chunk(s) for cattle (by document label, no chunk names it)`):

> Qarayara kəskin gedişli infeksion xəstəlik olub, septisemiya, orqanizmin ağır
> intoksikasiyası, daxili bədən temperaturasının kəskin yüksəlməsi, şişkinliklər və
> karbunkulların əmələ gəlməsi ... ilə səciyyələnir.

Tier-4 answers (A10, S3, S4, S6) are the fixed text
`az_quality.h:688` - e.g. S4: *"Bu heyvan növü üçün aydın mənbə tapılmadı (sorğu:
camış). Kitablarda bu növ üçün uyğun bölmə yoxdur, ona görə cavabı başqa heyvan növündən
götürmürəm. ..."*

### Turkish words leaking into the Azerbaijani answers

Counter `test_results/bee_trcount.py`: tokens are compared against Turkish-only
spellings whose Azerbaijani equivalent differs (hastalık/xəstəlik, tedavi/müalicə,
teşhis/diaqnoz, mücadele/mübarizə, belirti/əlamət, korunma/qorunma, önlem/tədbir,
uygulama/tətbiq, değil/deyil, yavru/balalar, çerçeve/çərçivə, petek/pətək, duman/tüstü,
ilaç/dərman, kullanıl-/istifadə, yapılır/edilir, olarak/kimi, için/üçün, ile/ilə,
virüs/virus, enfeksiyon/infeksiya, bulaşma/yoluxma, deli/dəlik). Shared words (`arilar`,
`kovan`, `tütün`, `bal`, `virus`) are not counted.

**4 hits, all in A6: `yavrulu`, `petek`, `deli`, `dumanı`** (A6 draws on the Turkish
deck `10. Arı hastalıkları.pdf`). A1, A10, S1-S6: **0**. The answer language itself is
Azerbaijani; the leak is lexical, in the domain words the Turkish slides supply.

### Checks run

* `./build/Release/dumy_unit_tests.exe` -> `34/34 passed`, `UNIT_EXIT=0`.
* `powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_all.ps1` ->
  **`87/87 passed, 0 warning(s)`, `SUITE_EXIT=0`** (`test_results/bee_suite.log`, report
  `test_results/latest.md`). The previously red case now passes:
  `dom06_varroa  PASS  21,45s  pdf=2`, citing
  `10. Arı hastalıkları.pdf` and `ari-ailalarinin-vaziyyati-ziyanverici-va-xastaliklari.pdf`.
* The new assertion was also checked against the saved response directly: the three real
  fragments give `problems(0)`, an impossible fragment gives
  `no PDF source matching any of: ZZZ-not-a-book` - so the field is wired, not vacuous.
* No source file changed, so no rebuild was needed; `--ingest` ran on the committed
  binary (`build_id Nov 19 2026 20:50:21`).

## Side effects (reported, not fixed)

Three simple-fact questions flipped **out** of answering into Tier-4 even though nothing
in the species rule or thresholds changed, because the 294 new chunks change retrieval
and the LLM relevance gate now rejects the retrieved chunks before the species guard
sees them (deterministic - S4 and S6 were re-run twice each, identical result):

| Q | gate log (after) |
|---|---|
| S4 | `relevance gate: branch=llm_no score=0.551996 overlap=1/2 ... decision=drop` -> `species guard: ... 0 chunk(s)` |
| S6 | `relevance gate: branch=llm_no score=0.523726 overlap=1/2 ... decision=drop` -> `0 chunk(s)` |
| S3 | `relevance gate: branch=llm_no score=0.555717 overlap=1/2 ... decision=drop` -> `0 chunk(s)` |

S6 lost 4 PDF sources and S4 lost the 3 buffalo chunks they had before the ingest; S3
turned from a clarification question into Tier-4. These are retrieval/relevance-gate
effects of the larger corpus, not the species guard (its log line reports `0 chunk(s)`
in all three). Per the brief they are reported here and not "fixed".

## Not verified

* No browser/UI check; all answers come from `/chat` JSON.
* The A6 text is on-topic but partly garbled ("göbələkə meşəyə çıxmamaq", "uçuş deli");
  it was not corrected - the answer is quoted as produced.
* The 16 image-only pages of the Turkish deck have no searchable text (OCR is off,
  `config.json: ocr_enabled: false`), so whatever they show is simply absent.
* `pdfs/` and `data/` are git-ignored (`.gitignore:7`, `:10`), so the three new PDFs and
  the rebuilt index exist only on this machine; a fresh checkout plus `--ingest` would
  have to be given the files again. Nothing was committed in this task.
