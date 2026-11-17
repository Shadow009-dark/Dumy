# Quality-gate diagnosis (STEP 1)

Date: 2026-11-17. Branch `fix/answer-quality` (from `feature/memory-ui`, `5b1b6ca`).
Binary: `build/Release/dumy.exe`, build id `Nov 15 2026 13:52:05` — no code changed for this step.
Server: `dumy.exe --no-browser --no-web`, `/health` → `chunks=6751`, `gemma3:4b`, `bge-m3`.
`config.json` was switched to `"debug_trace_enabled": true` for the trace run and restored to
`false` afterwards (documented, not committed).

## Method

Two passes over the same ten questions, each in a fresh `X-Session-Id`:

- **Pass A (authoritative, self-consistent)** — one `POST /chat/stream` per question; the
  concatenated `delta` events give the raw draft, the `done` event gives route/sources/latency/
  final answer, and the server log is sliced per request (byte offset before/after) so the
  `quality gate:` decision belongs to the *same* pipeline execution as the draft.
- **Pass B** — `POST /chat` with `{"debug":true}` (loopback only) to read `trace.attempts`,
  `trace.timings_ms` and `trace.selected`. Used only for the route/timing column.

The detector's rules were re-implemented 1:1 from `include/dumy/az_quality.h:1140-1180` on the
same primitives (`common.h:186` `az_fold`, `common.h:220` `az_words`) so the matched span can be
printed; every span below is reproduced verbatim from the draft text.

## Result table (pass A)

| # | Question | Route | Draft chars | Gate | Detector fired | Exact repeated span | Final chars | Sources | Seconds |
|---:|---|---|---:|---|---|---|---:|---:|---:|
| 1 | Mal-qarada qarayara xəstəliyinin əlamətləri nələrdir? | broad_topic | 463 | pass | — | — | 305 | 3 | 7.4 |
| 2 | İnək gündə neçə dəfə sağılmalıdır? | simple_fact | 619 | **REJECT** | repetitive | `4gram>=3` → `qaramal naxirlarinda kitab a5` ×4 (also `menbe qaramal naxirlarinda kitab` ×3) | 211 | 1 | 8.4 |
| 3 | Qoyunlarda quzu ölü doğumunun səbəbləri nələrdir? | broad_topic | 840 | pass | — | — | 580 | 3 | 9.1 |
| 4 | Buğda əkini üçün torpaq necə hazırlanmalıdır? | simple_fact | 791 | **REJECT** | repetitive | `4gram>=3` → `menbe aqronomiyanin esaslari pdf` ×5, `aqronomiyanin esaslari pdf seh` ×5; `word>=6x6` → `aqronomiyanin` ×6, `esaslari` ×6 | 944 | 1 | 16.3 |
| 5 | Mal-qarada qarın şişməsi (timpaniya) necə müalicə olunur? | diagnosis | 1273 | pass | — | — | 1084 | 3 | 22.0 |
| 6 | Arılarda varroa xəstəliyi ilə necə mübarizə aparılır? | general | 193 | pass | — | — (off-domain branch: no draft attempts in trace) | 193 | 1 | 5.1 |
| 7 | Dana ishalı olduqda nə etmək lazımdır? | general | 701 | pass | — | — | 444 | 2 | 11.2 |
| 8 | Süd verimini artırmaq üçün yemləmə necə olmalıdır? | simple_fact | 1019 | **REJECT** | repetitive | `4gram>=3` → `qaramal naxirlarinda kitab a5` ×5, `naxirlarinda kitab a5 pdf` ×5; `sentence>=2` → `menbe qaramal naxirlarinda kitab a5` ×2 | 923 | 0 | 23.5 |
| 9 | Toyuqlarda yumurta istehsalı üçün işıq rejimi necə olmalıdır? | simple_fact | 687 | pass | — | — | 447 | 4 | 7.4 |
| 10 | Keçilərdə dırnaq xəstəlikləri hansılardır? | broad_topic | 851 | **REJECT** | repetitive | `4gram>=3` → `ve xususi cerrahiyye pdf` ×3, `xususi cerrahiyye pdf seh` ×3; `sentence>=2` → `isgenderov umuni ve xususi cerrahiyye` ×3 | 85 | 1 | 19.2 |

What happened after each rejection (from the same request's log):

| # | Rejected draft | After the gate |
|---:|---|---|
| 2 | 602 chars, opens "İnəklər gündə 2 dəfə sağılmalıdır [Mənbə: Qaramal naxırlarında-kitab A5.pdf, …" | `strict_retry` 1.4 s → 72 tokens → rejected → `extractive fallback used (256 chars)` → final answer is a chunk excerpt that starts `SAĞIM GİGİYENASI VƏ YELİN SAĞLAMLIĞI` |
| 4 | 835 chars, opens "Payız buğdayının toxumları 5-6 sm dərinliyə basdırılmalıdır [Mənbə: AQRONOMİ…" | `strict_retry` 6.9 s with **no context** → accepted (1141 chars) → a generic numbered list that is not tied to any source |
| 8 | 856 chars, opens "Süd verimliliyinin artırılması üçün yemləmə sistemlərinin seçimi və optimal …" | `strict_retry` 6.3 s with **no context** → accepted (1098 chars) → the infant-feeding text quoted in `AUDIT.md` B1, now with 0 sources |
| 10 | 699 chars, opens "Keçilərdə dırnaq xəstəlikləri əsasən mekaniki zədələmə, uzun müddət bağlı …" | `strict_retry` 2.7 s → 146 tokens → rejected → `extractive fallback used (100 chars)` → final answer is one sentence: "Dırnaq xəstəlikləri bəzi təsərrüfatlarda naxırın ümumi sayının 10-55 %-ni əhatə edir." |

On this run **4 of 10** domain answers were destroyed by the gate (211, 944, 923 and 85 chars
instead of 600–1300). In the pre-fix audit run of the same ten questions the same binary hit the
gate **9 of 10** times (`AUDIT.md` §4.2, log lines `quality gate: reject … repetitive=1`), so the
false-rejection rate is sampling-dependent and large, not an edge case.

## Why `repetitive=1` fires on a correct 700-char draft

`has_repetition_loop` is called on `final_answer` at `src/rag_pipeline.cpp:2035`, i.e. **before**
the inline citation labels are removed: `clean_answer_text`, which erases `[Mənbə: …]`,
`[İnternet: …]`, `[Ümumi bilik]`, runs only afterwards, in `RagPipeline::answer`
(`src/rag_pipeline.cpp:809`), because `answer_impl` returns first. So the detector tokenizes the
answer *including* every `[Mənbə: <PDF file name>, səh. N]` label. A correct veterinary or
agronomy answer cites the same book five times, which repeats the same four-word sequence — the
file name itself, folded to ASCII (`Qaramal naxırlarında-kitab A5.pdf` → `qaramal naxirlarinda
kitab a5`) — exactly three or more times. That satisfies rule 1 (`4gram>=3`) by construction, and
rule 2 (`word>=6x6`) counts the label's own words (`aqronomiyanin`, `esaslari` six times each in
Q4); rule 3 (`sentence>=2`) additionally treats the label fragment between two citations as a
"sentence", because folding turns `]`, `,` and `.` into spaces and the fragment is still ≥30
characters. It is not a too-small n-gram window and it is not a byte-vs-character bug —
`az_words` folds to ASCII first (`common.h:220`), so the 6-character rule really counts letters.
The detector is measuring citation metadata as if it were prose: a 700-character answer with one
source cited repeatedly is structurally "repetitive" under the current rules, which is precisely
the class of answer the pipeline wants to produce.

## Secondary observation

Rule 3 fires at **two** occurrences, so a legitimate numbered list that repeats a lead-in phrase
("… cədvəl 14-də göstərildiyi kimi") is rejected as a loop. A real generation loop repeats the
same sentence three or more times, so the threshold is one iteration too tight.

## Reproduce

```
build\Release\dumy.exe --no-browser --no-web        # or leave the default config
# per question: POST /chat/stream, concat delta.text, read the done event,
# and slice the server log between the request's byte offsets
```

Raw captures (drafts, gate log lines, spans, answers) were kept outside the repo in
`%TEMP%\gate_step1.json` and `%TEMP%\gate_step1b.json`.
