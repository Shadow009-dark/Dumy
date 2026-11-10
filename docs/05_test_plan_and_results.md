# Test Plan and Results

**Date**: 2026-11-05
**Server**: `http://127.0.0.1:8085`
**Test Method**: `curl.exe -X POST -H "Content-Type: application/json" -d @test_request.json http://127.0.0.1:8085/chat`
**Note**: Use UTF-8 JSON file for Azerbaijani text (not inline in cmd)

---

## Test Questions

| # | Question | Category | Expected Behavior |
|---|----------|----------|-------------------|
| 1 | Qoyunlarda qabzlığı necə müalicə etmək olar? | Diagnosis (livestock) | Answer from PDF sources, safety warning appended |
| 2 | Südün keyfiyyəti necə yoxlanılır? | Simple fact (dairy) | Answer from PDF sources, no safety warning |
| 3 | Şirəli yem bitkiləri hansılardır? | Broad topic (forage) | Map-reduce answer listing forage plants from PDF |
| 4 | İnəklərin sağımı zamanı hansı qaydalara əməl olunmalıdır? | Simple fact (milking) | Answer from PDF sources |
| 5 | Pomidor yarpaqları niyə saralır? | Diagnosis (crop) — **NOT IN SOURCES** | "Verilən mənbələrdə bu suala cavab tapılmadı" + safety warning |
| 6 | Mars-da neçə nəfər yaşayır? | Off-topic (space) | Polite refusal, **no LLM call** |
| 7 | 15 vurulsun 12 neçə edir? | Arithmetic | Polite refusal, **no LLM call** |
| 8 | Adın nədir və səni kim yaradıb? | Identity | Short answer: "Mən DUMY-yəm. Mənə DUMY Team hazırlamışdır." — no invented creator |

---

## Results Table (to be filled after STEP 3)

| # | Question | Status | Answer Length | Time (ms) | Max Cosine | Chunks Used | Safety Warning | Notes |
|---|----------|--------|---------------|-----------|------------|-------------|----------------|-------|
| 1 | Qoyunlarda qabzlığı... | ⏳ Pending | — | — | — | — | — | |
| 2 | Südün keyfiyyəti... | ⏳ Pending | — | — | — | — | — | |
| 3 | Şirəli yem bitkiləri... | ⏳ Pending | — | — | — | — | — | |
| 4 | İnəklərin sağımı... | ⏳ Pending | — | — | — | — | — | |
| 5 | Pomidor yarpaqları... | ⏳ Pending | — | — | — | — | — | |
| 6 | Mars-da neçə nəfər... | ⏳ Pending | — | — | — | — | — | |
| 7 | 15 vurulsun 12... | ⏳ Pending | — | — | — | — | — | |
| 8 | Adın nədir... | ⏳ Pending | — | — | — | — | — | |

---

## Test Request Template (UTF-8 JSON file)

```json
{
  "session_id": "test-001",
  "message": "QUESTION_HERE"
}
```

Save as `test_request.json` with UTF-8 encoding before each curl call.

---

## Raw Data to Capture Per Test

- Full JSON response (`answer`, `sources`, `qtype`, `timings`)
- Console/server log output (stage timings, model load events)
- Cosine similarity scores from retriever (top 3 chunks)
- Whether LLM was called (check logs for `[timing] draft` lines)

---

## Pass/Fail Criteria

| # | Pass Condition |
|---|----------------|
| 1 | Answer contains treatment info from PDF, safety warning present |
| 2 | Answer from PDF (milk quality standards), no safety warning |
| 3 | Lists multiple forage plants from "Şirəli yem bitkiləri" PDF |
| 4 | Answer from PDF (milking hygiene rules) |
| 5 | Returns exact not-found message + safety warning (diagnosis keyword matched) |
| 6 | Returns refusal message, **no `[timing] draft` in logs** |
| 7 | Returns refusal message, **no `[timing] draft` in logs** |
| 8 | Returns identity answer, no invented creator name |