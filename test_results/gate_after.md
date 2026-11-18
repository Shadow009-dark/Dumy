# Quality gate after the fix (STEP 6)

Date: 2026-11-18. Branch `fix/answer-quality`, HEAD after G5 (amended).
Server: `build\Release\dumy.exe --no-browser --no-web`, `/health` → `chunks=6751`,
`chat_model=gemma3:4b`, `embedding_model=bge-m3`, `config.json` unmodified
(`quality_check.min_answer_chars=40`, `debug_trace_enabled=false`).

## Method

Same ten questions, same order, same fresh `X-Session-Id` per question as
`test_results/gate_diagnosis.md`, and the same capture: one `POST /chat/stream` per
question — the concatenated `delta` events are the raw draft, the `done` event is the
final answer — with the server log sliced per request (byte offset before/after) so
the `quality gate:` / `relevance gate:` lines belong to the same execution.
Config was not touched for this run (no debug trace), so the after numbers need no
caveat about a changed config.

Reproduce: `python %TEMP%\gate_step6.py %TEMP%\gate_after_server.log`.

## Before / after

| # | Question | Route (before → after) | Draft chars | Gate before | Gate after | Final chars (before → after) | PDF files (before → after) | Seconds (before → after) | Judgement |
|---:|---|---|---:|---|---|---|---|---|---|
| 1 | Mal-qarada qarayara xəstəliyinin əlamətləri nələrdir? | broad_topic → broad_topic | 463 → 718 | pass | pass | 305 → 511 | 3 → 1 | 7.4 → 9.5 | yes |
| 2 | İnək gündə neçə dəfə sağılmalıdır? | simple_fact → simple_fact | 619 → 748 | **REJECT** `4gram>=3` `qaramal naxirlarinda kitab a5` ×4 | pass | 211 → 376 | 1 → 1 | 8.4 → 8.4 | partly |
| 3 | Qoyunlarda quzu ölü doğumunun səbəbləri nələrdir? | broad_topic → broad_topic | 840 → 814 | pass | pass | 580 → 450 | 3 → 2 | 9.1 → 13.9 | partly |
| 4 | Buğda əkini üçün torpaq necə hazırlanmalıdır? | simple_fact → simple_fact | 791 → 1219 | **REJECT** `4gram>=3` `menbe aqronomiyanin esaslari pdf` ×5, `word>=6x6` `aqronomiyanin` ×6 | pass | 944 → 1331 | 1 → 1 | 16.3 → 24.5 | yes |
| 5 | Mal-qarada qarın şişməsi (timpaniya) necə müalicə olunur? | diagnosis → diagnosis | 1273 → 1165 | pass | pass | 1084 → 716 | 3 → 1 | 22.0 → 21.2 | partly |
| 6 | Arılarda varroa xəstəliyi ilə necə mübarizə aparılır? | **general → diagnosis** | 193 → 613 | pass (refusal) | pass | 193 → 316 | 1 → 1 | 5.1 → 7.1 | partly |
| 7 | Dana ishalı olduqda nə etmək lazımdır? | **general → simple_fact** | 701 → 649 | pass (free answer) | pass | 444 → 397 | 2 → 0 | 11.2 → 13.2 | partly |
| 8 | Süd verimini artırmaq üçün yemləmə necə olmalıdır? | simple_fact → simple_fact | 1019 → 1104 | **REJECT** `4gram>=3` `qaramal naxirlarinda kitab a5` ×5 | pass | 923 → 687 | 0 → 2 | 23.5 → 21.6 | yes |
| 9 | Toyuqlarda yumurta istehsalı üçün işıq rejimi necə olmalıdır? | simple_fact → simple_fact | 687 → 650 | pass | pass | 447 → 360 | 4 → 1 | 7.4 → 7.3 | yes |
| 10 | Keçilərdə dırnaq xəstəlikləri hansılardır? | broad_topic → broad_topic | 851 → 833 | **REJECT** `4gram>=3` `ve xususi cerrahiyye pdf` ×3 | pass | 85 → 564 | 1 → 1 | 19.2 → 10.1 | partly |

`Gate after` is the absence of a `quality gate: reject reasons=[…]` line for that
request; `PDF files` counts distinct source files in the `sources` array.

## Per-question judgement (quoting the returned answer)

**1 — yes.** "Qarayara kəskin gedişli infeksion xəstəlik olub, septisemiya, orqanizmin
ağır intoksikasiyası, daxili bədən temperaturasının kəskin yüksəlməsi, şişkinliklər və
karbunkulların əmələ gəlməsi…" — the symptoms the question asks for, one book.

**2 — partly.** The fact is right ("inəklərin südünü düzgün şəkildə əldə etmək üçün,
onlara gündə 2 dəfə saqqıldamaq lazımdır") but the sentence is mangled:
`saqqıldamaq`, `saqqıldaqlamaq` are not words. The gate no longer destroys the answer,
the model's phrasing is poor.

**3 — partly.** Starts with "Qoyunlarda quzu ölü doğumunun səbəbləri ilə bağlı
məlumatlar PDF-də göstərilir: ." — a meta lead-in with a dangling colon, left over from
a citation label that `clean_answer_text` removed. The body is on topic.

**4 — yes.** "torpaq şumlanaraq xırdalanır və səthi hamarlanır…" then the seedbed
sequence; 1331 characters, the longest before-and-after answer, and the one the gate
used to replace with an unrelated generic list.

**5 — partly.** The treatment list is correct in substance but the bullets end in
prepositions: "**Köpüklü timpaniyaya qarşı vasitələr:** Kömük əleyhinə vasitələrlə .",
"**Yemlərin tənzimi:**". That is the model truncating each bullet, not the gate.

**6 — partly.** Was the refusal "Bu mövzuda dəqiq məlumatım yoxdur…" (193 chars, route
`general`); now `diagnosis` and a real answer: "varroa xəstəliyi ilə mübarizə üçün
Tibazid yemə qatılması… Hər kq canlı kütləyə 10 mqr dozada, 2-3 ay müddətində Tubazid
yemə qatılaraq heyvanlara verilir." Two defects remain: `Tibazid`/`Tubazid` spelling,
and the inapplicable closing sentence "Hər halda xəstələr öldürülür və zərərsizləşdirilir".

**7 — partly.** Routing is fixed (`general` → `simple_fact`, `relevance gate: … overlap=3/3
decision=keep`), and the answer is a numbered clinical checklist ("Kliniki müayinə: Danaın
vəziyyətini tamamilə yoxlayın…"). It still has 0 PDF files in this run and invented words
(`qıcıqdanıqlıq`, `bəlövha`), so it is a generic answer, not a grounded one. In an earlier
run of the same binary this question returned the Tier-4 text instead, so retrieval for it
is still unstable.

**8 — yes.** "Süd verimliliyini artırmaq üçün südlük qaramalların yemlənməsi strateji
formada aparılmalıdır…", 2 books — previously the gate replaced it with the
infant-feeding text that `AUDIT.md` B1 quotes.

**9 — yes.** Coherent light-regime explanation (short days → less feeding time → lower
laying), 1 book.

**10 — partly.** 85 → 564 characters, so the gate no longer reduces it to one sentence;
but the content is written for cattle ("qaramalı dırnaqların uzun müddət bağlı və fəal
hərəktsiz şəraitdə saxlanması") and applied to goats.

Totals: **yes 4, partly 6, no 0** (before: 4 of 10 answers destroyed by the gate, 1
refusal, and Q7 answered from the model's own memory).

## Refusal rate

| | Before | After |
|---|---:|---:|
| Explicit refusal / "I don't know" text | 1/10 (Q6, "Bu mövzuda dəqiq məlumatım yoxdur…") | 0/10 |
| Answer destroyed by the gate (extractive one-liner or unrelated retry) | 4/10 (Q2, Q4, Q8, Q10) | 0/10 |
| Answer with at least one PDF source | 9/10 (Q8 had none) | 9/10 (Q7 had none) |

The pre-fix audit run of the same questions hit the gate **9/10** times, so the before
numbers above (4/10) are the conservative end of a sampling-dependent range.

## Suite

`powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_all.ps1` →

```
Report: test_results\latest.md  |  84/84 passed, 0 warning(s)
EXIT=0
```

Unit tests: `build\Release\dumy_unit_tests.exe` → `16/16 passed`.

## What is still wrong (not the gate)

1. Q3's "…məlumatlar PDF-də göstərilir: ." and Q2's "saxlanıla bilər ." show that
   removing an inline label can leave a dangling colon or a space before the full stop —
   `clean_answer_text` needs to repair the surrounding punctuation.
2. Q7 has no sources and invented vocabulary; the relevance gate keeps the chunks
   (`overlap=3/3`) but the draft drifts. Needs a grounded-answer check, not a gate fix.
3. Q10 applies cattle surgery text to goats, and Q3 leads with a retrieval meta-sentence;
   both are model behaviour on a small corpus, unchanged by these steps.
