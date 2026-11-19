# Query expansion vs short questions: 16 questions x 3 runs

Date: 2026-11-19. Branch `fix/species-guard`. STEP 1 commit **`8123d9c`** ("Skip query
expansion for questions under max_simple_words"), which adds
`az_skip_expansion_for_length()` (`include/dumy/az_quality.h`, next to
`is_title_statement`) and uses it at `src/rag_pipeline.cpp:1450-1455`:

```cpp
const bool skip_expansion =
    short_non_topic ||
    (cfg_.expansion_skip_for_simple &&
     az_skip_expansion_for_length(q, cfg_.expansion_max_simple_words)) ||
    (qtype == QuestionType::General && cfg_.expansion_skip_for_general);
```

No config value and no threshold was changed: `expansion.skip_for_simple: true` and
`max_simple_words: 10` already existed in `config.json` (`config.h:156-158`,
`config.cpp:149-151`) and were simply never read.

## Method

Before = binary built from `30f480c` (`build_id Nov 19 2026 …`, 7045 chunks),
after = `8123d9c` (`build_id Nov 19 2026 22:09:11`). Same server flags each side,
`DUMY_DEBUG_TRACE=1 ./build/Release/dumy.exe --no-browser --no-web`, same index, one
server per side, 16 questions x 3 runs (`test_results/expansion_run.py`,
`_before.json` / `_after.json`, logs `expansion_{before,after}_server.log`).
Verdicts are manual: **yes** = the answer addresses the question from the sources,
**partly** = answers off-target or with a wrong core claim, **no** = Tier-4 refusal or
a clarification question instead of an answer.

## Per-question result

`T4` = species-no-source refusal. Gate cell = `branch score overlap decision`.

| Q | before (3 runs) | before gate | after (3 runs) | after gate | mean lat. |
|---|---|---|---|---|---|
| A1 | ok,ok,ok | lexical 3/5 keep x3 | ok,ok,ok | lexical 3/5 keep x3 | 11.8s -> 3.5s |
| A2 | ok,ok,ok | lexical 3/4 keep (0.643131/0.619235) | ok,ok,ok | lexical **4/4** keep x3 | 9.8s -> 9.1s |
| A3 | ok,ok,ok | lexical 4/5 keep x3 | ok,ok,ok | lexical 4/5 keep x3 | 11.0s -> 12.0s |
| A4 | ok,ok,ok | lexical 4/4 (0.594117/0.613745) | ok,ok,ok | lexical 4/4 0.613745 x3 | 9.1s -> 7.0s |
| A5 | ok,ok,ok | lexical 6/6 (0.628884/0.609228) | ok,ok,ok | lexical 6/6 0.628884 x3 | 13.3s -> 22.2s |
| A6 | ok,ok,ok | lexical 5/5 keep x3 | ok,ok,ok | lexical 5/5 keep x3 | 18.0s -> 22.1s |
| A7 | **T4,T4**,ok | llm_no 1/3 drop x2, lexical 2/3 keep | ok,ok,ok | **lexical 2/3 0.578856 keep x3** | 5.0s -> 24.2s |
| A8 | ok,ok,ok | lexical 2/3 keep x3 | ok,ok,ok | lexical 2/3 0.673485 x3 | 14.0s -> 9.3s |
| A9 | ok,ok,ok | lexical 6/6 keep x3 | ok,ok,ok | lexical 6/6 keep x3 | 9.8s -> 9.0s |
| A10 | T4,T4,T4 | lexical 2/4 keep x3 | T4,T4,T4 | lexical 2/4 keep x3 | 1.8s -> **0.1s** |
| S1 | ok,ok,ok | lexical 2/3 keep x3 | ok,ok,ok | lexical 2/3 keep x3 | 6.9s -> 5.7s |
| S2 | **T4,T4,T4** | llm_no 1/2 drop x3 | ok,ok,ok | **lexical 2/2 0.599237 keep x3** | 2.1s -> 9.7s |
| S3 | **T4**,ok,ok | llm_no drop, llm_yes x2 | ok,ok,ok | llm_yes 0.589961 1/2 keep x3 | 11.2s -> 10.5s |
| S4 | ok,**T4,T4** | lexical 2/2, llm_no 1/2 x2 | ok,ok,ok | **lexical 2/2 0.551996 keep x3** | 4.2s -> 7.5s |
| S5 | ok,ok,ok | **llm_yes 1/2 keep x3** | **T4,T4,T4** | **llm_no 1/2 drop x3** | 15.5s -> 0.3s |
| S6 | ok,ok,**T4** | lexical 2/2 x2, llm_no 1/2 drop | ok,ok,ok | lexical 2/2 keep x3 | 7.2s -> 5.6s |

Expansion ran 48/48 times before and **0/48** after (`expansion queries (…, skipped)` in
every after-run). Questions with a Tier-4 refusal: 12/48 before, 6/48 after; mean
latency 9.4 s -> 9.9 s.

## Answer verdicts, with quotes

**Unchanged and good (yes -> yes): A1, A2, A3, A4, A6, A8, A9, S1.** A2 became clearer:
after r1 *"İnək gündə 2-3 dəfə sağılmalıdır. … gündə 2 dəfə, uyğun sağım texnikası…"*
(before r2 was *"İnək sutkada üç dəfə sağılırdı."*). A6 still answers varroa from the bee
books, 6 sources.

**A7 — fixed (no,no,yes -> yes,yes,yes).** Before r1 was the refusal
*"Bu heyvan növü üçün aydın mənbə tapılmadı (sorğu: inək, dana, mal-qara)…"*; after all
three runs answer identically:
*"Dana ishalı olduqda aşağıdakı addımları görmək vacibdir: 1. **Diaqnoz:** Xəstə heyvanın
klinik əlamətləri (uzunmüddətli ishal, qanda hemoqlobinin miqdarı artması…"*
— the known risk did not materialise; the single-query ranking (0.578856, overlap 2/3)
now passes the lexical branch every time.

**S2, S4, S6 — refusal -> answer, but the content is weak (no -> partly).**
S2 before: *"Bu heyvan növü üçün aydın mənbə tapılmadı (sorğu: toyuq, quş)…"*; after r1:
*"Toyuqlarda qəbizlik, xüsusilə yeni doğulmuş körpələrində daha çox başqa kimi
simptomlarla müşayiət olunacaqdır. Qəbizliyin əlamətləri arasında bala salma və ölü bala
doğmay…"* — about chicks, not laying hens. S4 after r1: *"Camışlarda mastit – buğdaycıq
xəstəliyinə səbəb olan bakteriyaların … toplanmasıdır."* (nonsense core claim; before r1
was also nonsense: *"buxeylanma xəstəsi hesab edilir"*). S6 after r1: *"Quzuların ishalı
hallarında … Quzulara ZOM (zinqa-otlak müalicəsi) verilir."*

**S3 — answer -> worse answer (yes,yes,no -> partly,clarification,partly).** Before r2:
*"Atlarda kolik – bu, atın bağıracığında yaranan bir xəstəlikdir. Kolikin simptomları
arasında şiddətli ağrı…"* (correct: intestinal colic). After r1: *"Kolik atlarda barmaq
bükücü vətərinin cırılmasıdır."* (flexor-tendon rupture, not colic), and r2 returned the
clarification *"at barədə nəyi öyrənmək istəyirsiniz…"*.

**A5 — on-target answer lost (yes(1/3) -> partly(3/3)).** Before r3 was right:
*"Mal-qarada qarın şişməsi (timpaniya) zamanı müalicə tədbirləri aşağıdakılardır:
**Köpük inhibitorları:** Otlaqlarda köpmənin qarşısını almaq üçün…"*. After, all three runs
describe a haematoma instead: r1 *"Qarın boşluğunda hematoma zamanı müalicə tədbirləri,
həm simptomatik, həm də kompleks olanamalıdır…"*.

**S5 — REGRESSION: answers -> refusals in 3/3 runs.** Before r1/r3 answered
(*"Nozematoz arılar arasında yayılıb … ergin bal arılarının orta bağırsak epitel
hüceyrələrində infeksiyaya səbəb olan bir protuzoon parazitdir…"*, 4 and 5 sources), r2 was
a clarification. After, all three runs are the refusal
*"Bu heyvan növü üçün aydın mənbə tapılmadı (sorğu: arı)…"* with
`relevance gate: branch=llm_no score=0.660969 overlap=1/2 key_terms=[arilarda,nozomatoz]
missing=[nozomatoz] decision=drop` and then `species guard: … 0 chunk(s)`.

## STEP 3: STOP

Per the rule ("if any question that answered reliably before now refuses or degrades in
>=2 of 3 runs … STOP"), the change is **not fit to keep as it stands**:

* **S5 `Arılarda nozomatoz`: 2 answers + 1 clarification before -> 3 Tier-4 refusals
  after.** This is the blocking regression.
* **A5: the only on-target answer (1/3) disappeared (0/3)**; all three runs now describe
  an abdominal haematoma instead of tympani treatment.
* **S3: 2 correct answers -> 1 correct-ish**; the new text defines colic as a ruptured
  flexor tendon.

Why S5 fails is measurable, and it is **not** a retrieval miss: the single-query vector
ranking puts the bee/nozematoz pages first - `ari-ailalarinin-…pdf` p.114 **0.6610**,
p.113 0.6135, p.131 0.6076, p.88 0.5815, `10. Arı hastalıkları.pdf` p.51 0.5574
(`test_results/gate/vecrank_s5.txt`). The gate's own `score=0.660969` proves those chunks
were in front of it; the lexical probe misses only `nozomatoz` (the books spell
`nozematoz`), so it falls to the helper model, which says XEYR, and
`pdf_chunks.clear()` (`rag_pipeline.cpp:1598`) throws away a candidate set that contains
the right pages. With expansion the fused list happened to include a chunk the model
accepted; without it, the same check fails deterministically.

So the fix trades one instability (expansion sampling) for a different, now stable,
failure: the gate's yes/no on a spelling mismatch. Both A5 and S3 are generation-side
(the same retrieved pages, a draft that drifts), not gate decisions.

No threshold was tuned and no further code was written.

## Options (proposed only)

1. **Revert `8123d9c`.** Restores S5/A5/S3 behaviour and keeps the old flakiness
   (36/48 vs 42/48 Tier-4-free, and S2/S4/S6/A7 stay unreliable). Zero risk, no gains.
2. **Keep the skip and make the gate non-destructive** (recommended next experiment):
   on `llm_no`, drop only the chunks that fail the lexical probe instead of clearing
   everything (`rag_pipeline.cpp:1591-1601`), then let the species guard decide. Evidence:
   S5's rejected set already ranks the bee pages 1-5 by cosine, and the guard is the
   component that knows the chunks name `arı`; this is 5-8 lines, no threshold change.
   Must be re-measured on all 16 questions, including the four that the skip fixed.
3. **Constrain expansion instead of skipping it**: keep the call but discard alt-queries
   that share no content word with the question and any non-Azerbaijani query (the
   measured garbage: `camel mastitis`, `itlərdə ishal`, `quruşu xəstəlikləri`). Costs the
   0.8-1.8 s call, keeps recall; needs its own 16-question measurement.
4. **Spelling tolerance in the gate probe** (`nozomatoz` vs `nozematoz`) - this is a
   matcher/threshold change, so out of scope here; note that S5's lexical branch fails on
   exactly one vowel and that the fuzzy species matcher already handles such variants.

## Checks

* `./build/Release/dumy_unit_tests.exe` after `8123d9c`: **35/35 passed**, exit 0
  (new case `expansion: <= max_simple_words words is short, one more is not`).
* `tests\run_all.ps1` after `8123d9c`: **87/87 passed, 0 warnings**, `SUITE_EXIT=0`
  (`test_results/expansion_suite_step1.log`).
* No code change after those runs; the measurement only read the server.

## Not verified

* One index, one model pair (gemma3:4b + bge-m3); answer quality is graded by reading the
  draft, not by a fact-checker.
* The 3-run verdict counts are small; the S5 failure is deterministic (3/3 with the same
  score and overlap), the A5/S3 content drift is not proven causal beyond the retrieval
  set change.
