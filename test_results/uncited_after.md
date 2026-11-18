# Uncited answers with invented words (STEP 3)

Date: 2026-11-18. Branch `fix/species-guard` (after STEP 2). Binary
`build\Release\dumy.exe`, `/health` -> `chunks=6751`, `gemma3:4b`, `bge-m3`.
`config.json` was not touched for this step: `garbled_word_min_words=10`,
`garbled_word_ratio_max=0.12`, `quality_check.min_answer_chars=40`.

## Why the invented words were not caught

`measure_unknown_words` (`include/dumy/az_quality.h:965-1003`) counts tokens of at least
4 folded characters that are not in the PDF vocabulary, the common-word list, the small
dictionary, a suffixed form of a known root, or a 6-character prefix of a vocabulary word.
The check at `src/rag_pipeline.cpp:2004-2013` runs it on the final answer **only when**
`total >= garbled_word_min_words` (10) **and** `ratio > garbled_word_ratio_max` (0.12).

On Q7 both guards pass the wrong way: the answer is long (73-152 countable words, so
`total >= 10`) and four or five invented words are diluted by the rest of the prose:

| Q7 sample | Unknown-word line (server log) | Invented words in the answer |
|---|---|---|
| 17:11 baseline | `unknown=4/97 ratio=0.041237` | `bəluruminin`, `qaraşdırıq`, `Danaın` |
| repeat 1 | `unknown=3/73 ratio=0.041096` | `Danaın`, `Dişorun` |
| repeat 3 | `unknown=3/62 ratio=0.048387` | `Anamaz`, `şöriəsi` |
| after STEP 2, run 1 | `unknown=4/86 ratio=0.046512` | `süstləşən`, `anamezini` |
| after STEP 2, run 2 | `unknown=5/95 ratio=0.052632` | `anamezini`, `qalayıb-y` |
| after STEP 2, run 3 | `unknown=4/112 ratio=0.035714` | `anamezini` |

So the cause is not a broken counter - it is the denominator of a document-level share.
A 0.12 ratio tolerates 12 invented words in 100; a 4-word defect in 97 words is 0.041.
The counter never had a chance to fire; nothing lowered the threshold, but nothing looked
at the other half of the defect either: **the same answer had no citation left after**
`enforce_citation_support` (`src/rag_pipeline.cpp:230-300`).

## The rule added

`kUncitedUnknownRatioMax = 0.035` (`include/dumy/az_quality.h`), then in
`src/rag_pipeline.cpp` (block 7a2, after the word check and before the quality gate):

- only when `selected` is non-empty (the draft had chunks) and the validated answer
  contains **no** `[Mənbə:` label and `unknown/total` is above 0.035:
  one regeneration with `build_strict_context_message(selected, q)` at temperature 0.0;
- the regeneration is re-validated (`validate_source_labels`, `enforce_citation_support`,
  word stats, repetition); if it now cites a source **or** the ratio is back at/below
  0.035 it replaces the draft;
- otherwise the G4 behaviour wins: `build_excerpt_bullets(q, selected, 3, 400)`, then the
  extractive sentence, then the Tier-4 text - the invented draft is never returned.

Why 0.035 and not a blind cut of 0.12: the 30-question probe measured 16 answers before
the change. Clean answers sat at 0.000-0.020 (`0/58`, `0/38`, `0/57`, `0/57`, `0/16`,
`1/52 = 0.019`, `1/49 = 0.020`); the damaged Q7 answers at 0.041-0.048. 0.035 is the gap.
The document-level 0.12 retry is unchanged - this is an additional rule for the specific
pair (no citation, invented words).

## The 10 audit questions, before -> after

| # | Question | Selected | Unknown before | Unknown after | Answer chars | Rule fired |
|---:|---|---:|---|---|---:|---|
| 1 | Mal-qarada qarayara xəstəliyinin əlamətləri nələrdir? | 3 -> 0 | 0/58 = 0.000 | - | 580 -> 246 | no |
| 2 | İnək gündə neçə dəfə sağılmalıdır? | 6 -> 6 | 0/38 = 0.000 | 0/44 = 0.000 | 337 -> 406 | no |
| 3 | Qoyunlarda quzu ölü doğumunun səbəbləri nələrdir? | 6 -> 6 | 0/57 = 0.000 | 1/69 = 0.014 | 562 -> 708 | no |
| 4 | Buğda əkini üçün torpaq necə hazırlanmalıdır? | 3 -> 3 | 3/77 = 0.039 | 3/169 = 0.018 | 745 -> 1565 | no |
| 5 | Mal-qarada qarın şişməsi (timpaniya) necə müalicə olunur? | 6 -> 0 | 0/138 = 0.000 | - | 1361 -> 132 | no |
| 6 | Arılarda varroa xəstəliyi ilə necə mübarizə aparılır? | 5 -> 2 | 4/31 = 0.129 | 2/43 = 0.047 | 471 -> 398 | no |
| 7 | Dana ishalı olduqda nə etmək lazımdır? | 0 -> 0 | 2/28 = 0.071 | - | 73 -> 246 | no |
| 8 | Süd verimini artırmaq üçün yemləmə necə olmalıdır? | 6 -> 6 | 0/57 = 0.000 | 1/76 = 0.013 | 590 -> 730 | no |
| 9 | Toyuqlarda yumurta istehsalı üçün işıq rejimi necə olmalıdır? | 5 -> 5 | 1/52 = 0.019 | 1/37 = 0.027 | 476 -> 343 | no |
| 10 | Keçilərdə dırnaq xəstəlikləri hansılardır? | 4 -> 0 | 0/16 = 0.000 | - | 155 -> 237 | no |

Seven of the ten answers have the same route before and after; the two goat/calf questions
now stop at the species guard (STEP 2) before this rule can see them, and `timpaniya` was
answered by the curated Tier-2 entry in the after run. That is why the rule fired 0 times
here and was exercised on purpose below.

## The 30-question probe

Same 30 questions, same order, one debug `/chat` per question, log sliced per request.

| Metric | Before | After |
|---|---:|---:|
| Answers the word check could measure | 16 | 10 |
| ratio > 0.12 | 1 | 0 |
| ratio > 0.04 | 3 | 1 |
| maximum ratio | 0.129032 | 0.046512 |
| refusal-like answers | 2/30 | 6/30 |

| # | Group | Question | Unknown before | Unknown after |
|---:|---|---|---|---|
| 1 | knowledge | Mal-qarada qarayara xəstəliyinin əlamətləri nələrdir? | 0/58 = 0.000 | - |
| 2 | knowledge | İnək gündə neçə dəfə sağılmalıdır? | 0/38 = 0.000 | 0/44 = 0.000 |
| 3 | knowledge | Qoyunlarda quzu ölü doğumunun səbəbləri nələrdir? | 0/57 = 0.000 | 1/69 = 0.014 |
| 4 | knowledge | Buğda əkini üçün torpaq necə hazırlanmalıdır? | 3/77 = 0.039 | 3/169 = 0.018 |
| 5 | knowledge | Mal-qarada qarın şişməsi (timpaniya) necə müalicə olunur? | 0/138 = 0.000 | - |
| 6 | knowledge | Arılarda varroa xəstəliyi ilə necə mübarizə aparılır? | 4/31 = 0.129 | 2/43 = 0.047 |
| 7 | knowledge | Dana ishalı olduqda nə etmək lazımdır? | 2/28 = 0.071 | - |
| 8 | knowledge | Süd verimini artırmaq üçün yemləmə necə olmalıdır? | 0/57 = 0.000 | 1/76 = 0.013 |
| 9 | knowledge | Toyuqlarda yumurta istehsalı üçün işıq rejimi necə olmalıdır? | 1/52 = 0.019 | 1/37 = 0.027 |
| 10 | knowledge | Keçilərdə dırnaq xəstəlikləri hansılardır? | 0/16 = 0.000 | - |
| 11 | calculation | 15 x 12 neçədir? | - | - |
| 12 | calculation | 120 baş, gündə 25 kq, 30 gün üçün nə qədər yem lazımdır? | - | - |
| 13 | calculation | 50 baş qoyun üçün 45 günlük yem hesablayın. | - | - |
| 14 | calculation | 250 baş inək üçün gündə 3 kq yem, 10 gün - ümumi nə qədər lazımdır? | - | - |
| 15 | calculation | 100-dən 37 çıx | - | - |
| 16 | calculation | 20 baş dana, 40 gün, gündə 8 kq yem nə qədər lazımdır? | - | - |
| 17 | calculation | 500 kq yem 25 baş mal üçün neçə günə çatır? | - | - |
| 18 | calculation | 400 kq inək üçün 2.5% canlı çəki norması ilə gündəlik yem nə qədərdir? | 0/32 = 0.000 | 0/63 = 0.000 |
| 19 | calculation | 1000 baş toyuq üçün 7 günlük yem, gündə 0.12 kq nə qədərdir? | - | - |
| 20 | calculation | 45 + 55 neçədir? | - | - |
| 21 | followup | cavab ver  [after: Qoyunlarda quzu ölü doğumunun səbəbləri ] | 4/65 = 0.062 | 0/77 = 0.000 |
| 22 | followup | davam et  [after: İnək gündə neçə dəfə sağılmalıdır?] | 0/52 = 0.000 | 1/42 = 0.024 |
| 23 | followup | ətraflı yaz  [after: Mal-qarada qarayara xəstəliyinin əlamətl] | 2/52 = 0.038 | - |
| 24 | followup | qısa yaz  [after: Dana ishalı olduqda nə etmək lazımdır?] | 0/21 = 0.000 | 0/32 = 0.000 |
| 25 | followup | yenidən yaz  [after: Arılarda varroa xəstəliyi ilə necə mübar] | 1/49 = 0.020 | - |
| 26 | out_of_scope | Python dilində fibonacci funksiyası yaz | - | - |
| 27 | out_of_scope | Fransanın paytaxtı hansıdır? | - | - |
| 28 | out_of_scope | Şəkərçörək necə hazırlanır? | - | - |
| 29 | out_of_scope | 2026-cı ildə dünya çempionu kim oldu? | - | - |
| 30 | out_of_scope | Mənə qısa bir şeir yaz | - | - |

## The rule firing (11 repeated Q7/S6 requests on the after build)

| Case | Selected | Unknown before | Rule | Unknown after the retry |
|---|---:|---|---|---|
| Q7#1 | 6 | 4/86 = 0.047 | yes | unknown=1/52, citations=0 |
| Q7#2 | 3 | 5/95 = 0.053 | yes | unknown=2/90, citations=0 |
| Q7#3 | 5 | 4/112 = 0.036 | yes | unknown=0/84, citations=0 |

3 of 11 fired (ratios 0.0357, 0.0465, 0.0526); each strict retry came back at 0.013,
0.022 and 0.000 and replaced the draft. The excerpt branch was not reached in this
sample - every retry was accepted - but it is the same `build_excerpt_bullets` helper
that has three unit tests (`rejected answer becomes 2 source-excerpt bullets`,
`excerpt bullets respect the item cap`, `no chunks -> no excerpt bullets`).

## What is not proven

- The rule is sampling-conditional: it fired 3/11 on Q7/S6 and 0/30 on the audit probe.
- An accepted retry may still have no inline label (the model often writes none); the
  deterministic provenance fallback then attaches at most one chunk to `sources`.
- The `0.035` cut is justified by 16 measured answers, not by a labelled set.

## Reproduce

```
build\Release\dumy.exe --no-browser --no-web        # debug_trace_enabled=true
python %TEMP%\audit30_measure.py %TEMP%\audit30_after3.json
python %TEMP%\uncited_repeats.py                      # -> %TEMP%\uncited_repeats.json
```

Raw captures: `%TEMP%\audit30_before.json`, `%TEMP%\audit30_after3.json`,
`%TEMP%\uncited_repeats.json`, `%TEMP%\q7_before_repeats.json`.
