# Species guard — after (STEP 5)

Date: 2026-11-18. Branch `fix/species-guard`, HEAD `6772f4e` (STEP 4) plus this report.
Binary `build\Release\dumy.exe`, rebuilt from that commit, `/health` ->
`build_id=Nov 18 2026 19:03:52`, `chunks=6751`, `chat_model=gemma3:4b`,
`embedding_model=bge-m3`, pid 14136. `config.json` is unmodified
(`debug_trace_enabled=false`); the measurement server was started with
`DUMY_DEBUG_TRACE=1`, which is the documented test override
(`src/config.cpp:91-96`).

Method: one `POST /chat` with `{"debug":true}` per question, a fresh
`session_id` each time, and the server log sliced by byte offset around each
request so the `relevance gate` / `species guard` / `word check` lines belong to
that request. Raw capture: `test_results/species_after.json` (gitignored), log:
`test_results/species_after_server.log`.

"Before" quotes for the ten audit questions come from `test_results/gate_after.md`
(the G6 run, before STEP 2); before quotes for Q10/Q7/S1-S6 come from
`test_results/species_diagnosis.md` (STEP 1). Two different samples exist for
Q7/Q10 because the pipeline is stochastic; the STEP 1 run is used as the baseline.

## Verdict summary

| Tag | Question | Before | After |
|---|---|---|---|
| A1 | Mal-qarada qarayara xəstəliyinin əlamətləri nələrdir? | yes | **no** |
| A2 | İnək gündə neçə dəfə sağılmalıdır? | partly | partly |
| A3 | Qoyunlarda quzu ölü doğumunun səbəbləri nələrdir? | partly | **yes** |
| A4 | Buğda əkini üçün torpaq necə hazırlanmalıdır? | yes | yes |
| A5 | Mal-qarada qarın şişməsi (timpaniya) necə müalicə olunur? | partly | partly |
| A6 | Arılarda varroa xəstəliyi ilə necə mübarizə aparılır? | partly | partly |
| A7 | Dana ishalı olduqda nə etmək lazımdır? | partly | partly |
| A8 | Süd verimini artırmaq üçün yemləmə necə olmalıdır? | yes | yes |
| A9 | Toyuqlarda yumurta istehsalı üçün işıq rejimi necə olmalıdır? | yes | yes |
| A10 | Keçilərdə dırnaq xəstəlikləri hansılardır? | no | partly |
| S1 | Qoyunlarda dırnaq çürüməsi | partly | **yes** |
| S2 | Toyuqlarda qəbizlik | no | no (honest refusal) |
| S3 | Atlarda kolik | partly | partly |
| S4 | Camışda mastit | no | no (honest refusal) |
| S5 | Arılarda nozematoz | no | no (honest refusal) |
| S6 | Quzu ishalı | no | no (honest refusal) |

Totals: before **yes 4 / partly 7 / no 5**; after **yes 5 / partly 6 / no 5**.
The four `no` answers that became honest refusals (S2, S4, S5, S6) are the
intended Tier-4 behaviour; A1 is the one new regression.

## Per-question before / after (quoted)

### A1 — Mal-qarada qarayara xəstəliyinin əlamətləri nələrdir?

Before — yes, real symptoms from one book:

> "Qarayara kəskin gedişli infeksion xəstəlik olub, septisemiya, orqanizmin ağır intoksikasiyası, daxili bədən temperaturasının kəskin yüksəlməsi, şişkinliklər və karbunkulların əmələ gəlməsi…" (`gate_after.md`)

After — **no**: the four retrieved chunks name no cattle, so the guard refuses:

> "Bu heyvan növü üçün aydın mənbə tapılmadı (sorğu: inək, dana, mal-qara). Kitablarda bu növ üçün uyğun bölmə yoxdur, ona görə cavabı başqa heyvan növündən götürmürəm. Sualı dəqiqləşdirin (xəstəliyin adı, heyvanın yaşı) və ya baytara müraciət edin."

Log: `species guard: no source for cattle (inək, dana, mal-qara) - 4 chunk(s) name only other species or none - context dropped`.

This is the rule as written (no chunk matches -> Tier-4), but it costs a good
answer: the chunks were generic, not another animal's, so the "don't apply
another species" reason does not actually apply here. See Limitations.

### A2 — İnək gündə neçə dəfə sağılmalıdır?

Before — partly (right fact, mangled words):

> "inəklərin südünü düzgün şəkildə əldə etmək üçün, onlara gündə 2 dəfə saqqıldamaq lazımdır"

After — partly: the fact is right, still no citation, still awkward wording:

> "Süd məhsuldarlığını artırmaq üçün inəkləri gündə 2-3 dəfə sağımaq daha faydalıdır. Lakin, bu, inəyin sağlamlığına da ciddi təhdid ola bilər…"

Selected 6, route `simple_fact`, `species guard` did not need to drop anything
(log has no `dropped` line for this request).

### A3 — Qoyunlarda quzu ölü doğumunun səbəbləri nələrdir?

Before — partly, meta lead-in with a dangling colon:

> "Qoyunlarda quzu ölü doğumunun səbəbləri ilə bağlı məlumatlar PDF-də göstərilir: ."

After — **yes**: the dangling `: .` is gone and the answer is on topic:

> "Qoyunlarda quzu ölü doğumunun səbəbləri ilə bağlı məlumatlar PDF-də göstərilir. Əsas səbəblərdən biri – qoyunun yaşı…"

### A4 — Buğda əkini üçün torpaq necə hazırlanmalıdır?

Before — yes. After — yes:

> "Buğda əkini üçün torpağı hazırlamaq zamanı bir sıra amillərə diqqət yetirmək lazımdır…"

### A5 — Mal-qarada qarın şişməsi (timpaniya) necə müalicə olunur?

Before — partly (bullets end in prepositions). After — partly; 2 chunks naming
only other species were dropped, but the treatment text still drifts into
"reinfuziya" / blood:

> "Qarın şişməsi (tympaniya) zamanı mal-qarada ilk növbədə qan axınını müayinə etmək və onun səbəbini müəyyən etmək vacibdir…"

Log: `species guard: kept 4 chunk(s) for cattle, dropped 2 naming only other species`.

### A6 — Arılarda varroa xəstəliyi ilə necə mübarizə aparılır?

Before — partly (was a refusal, then a real but misspelled answer). After —
partly; 2 non-bee chunks dropped, but the content is bee-sting first aid, not
varroa:

> "Arilarda varroa xəstəliyi ilə mübarizə üçün Tibb Baytarlığında istifadə edilən üsullara baxaq. Arı zəhəri ilə zəhərlənmiş heyvanlara parenteral kalsium-xlorid…"

Log: `species guard: kept 2 chunk(s) for bee, dropped 2 naming only other species`.

### A7 — Dana ishalı olduqda nə etmək lazımdır?

Before — partly (generic answer with invented words). After — partly: 5 cattle
chunks kept, 1 other-species chunk dropped, word ratio clean (`1/77 = 0.013`),
but the draft was cut by `num_predict` and returns no source:

> "Dana ishalı olduqda aşağıdakı addımları görmək vacibdir:\n1. **Diaqnoz:** …\n4."

`sources: []`. The STEP 3 rule did not fire because the ratio is below
`kUncitedUnknownRatioMax = 0.035` (`include/dumy/az_quality.h:994-1002`); it is
uncited but not garbled, which is outside that rule.

### A8 — Süd verimini artırmaq üçün yemləmə necə olmalıdır?

Before — yes. After — yes:

> "Süd verimliliyinə nail çatmaq üçün südlük qaramallarda aşağıdakı yemləmə sistemlərindən istifadə edilməlidir: …"

### A9 — Toyuqlarda yumurta istehsalı üçün işıq rejimi necə olmalıdır?

Before — yes. After — yes, 6 poultry chunks kept:

> "Toyuqlarda yumurta istehsalı üçün işığın rejimi quşun fəsli ilə bağlı dəyişir…"

### A10 — Keçilərdə dırnaq xəstəlikləri hansılardır?

Before — **no**: cattle chapter applied to goats. The STEP 1 draft opened:

> "Qaramal dırnaq altlığının, dırnaq yumşaqlığının, dırnaq tacının dırnaqarası yarığın əzələləri… [Mənbə: T.B.İsgəndərov ümumi və xüsusi cərrahiyyə.pdf, səh. 434]"

After — partly, and the cattle sentence is gone. The guard kept 2 of 4 chunks
and dropped the two naming only cattle; the final answer never says "qaramal":

> "Keçilərdə dırnaq xəstəliklərinin ağır və xronik forması sadə formaların vaxtında müalicə olunmayıb onların xarici mikroflora ilə (koklar, viruslar, göbələklər və s) sirayətlənərək mürəkkəbləşməsindən tədricən baş verir…"

Log: `species guard: kept 2 chunk(s) for goat, dropped 2 naming only other species`.
The kept chunks are `T.B.İsgəndərov ümumi və xüsusi cərrahiyyə.pdf` p.436
(generic) and p.437 (names `keçi` and `inək`, verified in `data/index.bin` with
`test_results/chunk_lookup.py`). The explicit cattle chunk p.434 ("Qaramal
dırnaq altlığının…") is no longer in the context.

### S1 — Qoyunlarda dırnaq çürüməsi

Before — partly: 1 sheep chunk against 5 cattle-only chunks, and the answer
mixed cattle claims ("inəklərin 10-55%-i") into a sheep answer. After — **yes**,
2 sheep chunks kept, 4 cattle-only dropped:

> "Qoyunlarda dırnaq çürüməsi (dirnaq xəstəliyi) – … Nekrobakterioz kimi başqa ad altında da ifadə olunur. … Müalicə üçün Yara 3%-li hidrogen neroksid, marqansovka ilə işlənir…"

Log: `species guard: kept 2 chunk(s) for sheep, dropped 4 naming only other species`.

### S2 — Toyuqlarda qəbizlik

Before — no: 586 chars of model prose (chicken constipation treated like human
constipation, "1,5-2 litr su içmək"), no matching chunk. After — honest refusal:

> "Bu heyvan növü üçün aydın mənbə tapılmadı (sorğu: toyuq, quş). …"

### S3 — Atlarda kolik

Before — partly: 6/6 horse chunks but `unknown=7/50 ratio=0.140000` and a garbled
retry. After — partly: the garbled retry was accepted (`garbled retry accepted
(unknown=1/29)`), answer is about nerve block rather than colic treatment:

> "Kolik atlarda axsaqlığın itməsi ilə bağlıdır. Bu hallarada incik siniri blokadaya alınır…"

### S4 — Camışda mastit

Before — no: an invented 313-char mastitis answer, 0 selected chunks. After —
honest refusal:

> "Bu heyvan növü üçün aydın mənbə tapılmadı (sorğu: camış). …"

### S5 — Arılarda nozematoz

Before — no: an invented bee-disease answer ("arıların sürünün hızır şəkildə
silinməsi"). After — honest refusal:

> "Bu heyvan növü üçün aydın mənbə tapılmadı (sorğu: arı). …"

### S6 — Quzu ishalı

Before — no: 83-char one-liner ("əczaçıların ticarətında satılan antibiotiklər ilə
müalicə olunacaqdır"). After — honest refusal:

> "Bu heyvan növü üçün aydın mənbə tapılmadı (sorğu: qoyun, quzu). …"

## Selected chunks for the eight species questions

Counts before are STEP 1 (`species_diagnosis.md`, same counter); after is this
run (`trace.selected` length). Q10/Q7/S1-S6 because these are the only questions
with a STEP 1 baseline in the same unit.

| Tag | Selected before | Selected after | Guard action after |
|---|---:|---:|---|
| Q10 | 5 | 2 | kept 2 goat/generic, dropped 2 cattle-only |
| Q7 | 0 | 5 | kept 5 cattle, dropped 1 other-species |
| S1 | 6 | 2 | kept 2 sheep, dropped 4 cattle-only |
| S2 | 0 | 0 | no source -> Tier 4 |
| S3 | 6 | 6 | all horse, nothing dropped |
| S4 | 0 | 0 | no source -> Tier 4 |
| S5 | 0 | 0 | no source -> Tier 4 |
| S6 | 0 | 0 | no source -> Tier 4 |

## Refusal rate

| | Before (16 questions) | After (16 questions) |
|---|---:|---:|
| Explicit refusal / "no information" text | 1/16 (A6) | **5/16** (A1, S2, S4, S5, S6) |
| Answers saying "no clear source for this species" | 0/16 | **5/16** |
| — of the six new species questions | 0/6 | **4/6** (S2, S4, S5, S6) |
| Answers with at least one PDF source | 15/16 (only Q7 had 0) | 9/16 (A1, A7, S2-S6 have 0) |

The increase is the guard working: four questions that previously got invented
or off-species prose now decline. Counted from the answer text and
`species_no_source` in `test_results/species_after.json`; the REST `refused`
flag is false for all 16 (it marks the code-refusal branch, not Tier 4). One of the five (A1) is a cattle question that
lost a previously good answer, because its retrieved chunks were generic rather
than cattle-named.

## Q10 acceptance check

Requirement: "Q10 must no longer apply cattle surgery text to goats."

- Before: 3 of 5 selected chunks were cattle-only (p.434, p.436, `İntensiv və
  Ekstensiv maldarlıq` p.286) and the draft opened with "Qaramal dırnaq
  altlığının…" (`species_diagnosis.md`, Q10 draft).
- After: the two cattle-only chunks are dropped
  (`species guard: kept 2 chunk(s) for goat, dropped 2 naming only other
  species`), and the answer contains no "qaramal" and no cattle-chapter opener.
- Residual: the two kept chunks are still from the cattle surgery book
  (`T.B.İsgəndərov ümumi və xüsusi cərrahiyyə.pdf` p.436/437); p.437 names
  `keçi` in its own text. The rule says generic chunks stay
  (`include/dumy/az_quality.h:495-524`), so this is the specified outcome, not a
  missed drop.

Verdict: **partly -> yes on the named defect**, with the residual above stated
plainly.

## STEP 3 / Q7 check on this run

A7 selected 5 chunks, all cattle, and the final answer carries
`unknown=1/77 ratio=0.012987`, i.e. below `kUncitedUnknownRatioMax=0.035`
(`include/dumy/az_quality.h:1002`), so the uncited-invented-word rule correctly
did not fire. The answer is nevertheless uncited and truncated mid-list
(`sources: []`, ends "4."); that is the model hitting `num_predict`, not the
species guard, and is recorded here as a remaining defect rather than fixed in
STEP 5.

## Limitations

- **A1 over-refusal.** `Mal-qarada qarayara…` names cattle; the 4 retrieved
  chunks name no species at all. The rule as written treats "no chunk matches
  the species" as "no source" even when the chunks are generic. A narrower rule
  (refuse only when every chunk names a *different* group, and answer from
  generic chunks otherwise) would keep A1; it was not implemented because the
  requested rule is explicit.
- **Q10 still uses the cattle surgery book.** Only the cattle-named chunks are
  dropped; generic chunks from that book stay by design.
- **A7 truncation.** Uncited, `num_predict`-cut answer; outside the STEP 3
  ratio rule.
- **One sample per question.** The pipeline is stochastic (temperature 0.25);
  Q7/Q10 vary between runs. The before/after pairs are single samples, not
  averages.
- No test in this step: STEP 5 is measurement only. Suite state on this commit:
  `tests/run_all.ps1` 84/84 passed, `dumy_unit_tests.exe` 28/28 passed.

## Reproduce

```
build\Release\dumy.exe --no-browser --no-web          # with DUMY_DEBUG_TRACE=1
python test_results\species_after_run.py              # -> test_results\species_after.json
python test_results\chunk_lookup.py                   # page 436/437 species tokens
```
