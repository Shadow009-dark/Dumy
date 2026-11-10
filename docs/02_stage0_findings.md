# Stage 0 Findings

**Date**: 2026-11-05
**Source**: Code review, config.json, logs, test runs

---

## Model Roles

| Role | Model | Purpose |
|------|-------|---------|
| Embedding | `bge-m3` | Vector embeddings for PDF chunks and queries |
| Draft/Chat | `qwen3:8b` (config: `models.draft`) | Main answer generation |
| Helper | `qwen2.5:7b` (config: `models.helper`) | Query expansion, reranking, self-check |
| Self-check | `qwen2.5:7b` (config: `models.selfcheck`) | Answer validation/correction |
| Fallback | `llama3.1:8b` | Used when draft model unavailable |

**Note**: `qwen2.5:3b` is NOT used as the main model (config shows `qwen3:8b` for draft).

---

## Timings (from logs)

- **Model loading/swapping**: ~76% of total request latency
- Typical breakdown (single question):
  - Model loading: 1500-3000ms (when model not resident)
  - Embedding: 200-400ms
  - Retrieval (vector + BM25): 100-200ms
  - Reranking: 500-1500ms (LLM call)
  - Draft generation: 2000-4000ms
  - Self-check: 1000-2000ms
  - **Total**: 5-10 seconds (cold), 2-4 seconds (warm)

---

## Key Configuration Values

| Parameter | Value | Source |
|-----------|-------|--------|
| `num_ctx` | 4096 | config.json |
| Prompt size | ~2050 tokens | Estimated from context budget |
| `chunk_size` | 650 tokens | config.json |
| `chunk_overlap` | 100 tokens | config.json |
| `top_k_simple` | 6 | config.json |
| `top_k_broad` | 12 | config.json |
| `rerank_top_n` | 8 | config.json |
| `context_budget_tokens` | 1500 | config.json (note: config.h default 4500) |
| `web_fetch_timeout` | 1 second | config.json |
| `quality_check.min_answer_chars` | 80 | config.json |
| `keyterm_relevance.min_matched_terms` | 1 | config.json |
| `keyterm_relevance.prefix_length` | 5 | config.json |
| `expansion.max_simple_words` | 10 | config.json |

---

## Tomato Test Case: "Pomidor yarpaqları niyə saralır?"

### Expected Behavior
- Question contains "niyə saralır" (why yellowing) → should be classified as **diagnosis**
- Safety warning should be appended
- Answer should be "Verilən mənbələrdə bu suala cavab tapılmadı" (not found in sources) since no tomato PDFs exist

### Actual Behavior (Before Fixes)
1. **Misclassification**: Classified as `general` (not `diagnosis`)
   - Root cause: `diagnosis_keywords` in config.json had encoding issues (e.g., `niyÉ™ saralÄ±r` instead of `niyə saralır`)
   - The `contains_any` check failed to match the UTF-8 question text

2. **No quality check**: Model returned raw bad output with stray citations like `[Mənbə: ...]` in the middle of sentences, or citation-only answers

3. **No key-term relevance check**: Retrieved chunks were about livestock (cattle, sheep) — no "pomidor" (tomato) or "yarpaq" (leaf) terms in any chunk
   - System still called LLM with irrelevant context
   - LLM hallucinated or gave generic answer

4. **Web search**: SearXNG was down, request hung (no fail-fast)

---

## Root Causes Summary

| Problem | Root Cause |
|---------|------------|
| Question misclassified as general | `diagnosis_keywords` in config.json had corrupted UTF-8 (mojibake) |
| No quality check on answers | Feature configured but not fully implemented/enforced in pipeline |
| No key-term relevance check | Feature configured but not implemented in retriever/pipeline |
| Web search hangs | `web_fetch_timeout`=1s in config but not enforced in `web_search.cpp` |
| Query expansion runs for simple questions | `expansion.skip_for_simple`=true in config but not checked in `retriever.cpp::expand_query` |
| Knowledge base gaps | 23 PDFs are livestock/veterinary focused; no crop disease PDFs |
| Scanned pages not searchable | `ocr_enabled`=false by default; no Tesseract integration |

---

## Unverified Items

- Exact prompt token count (estimated ~2050)
- Whether SYSTEM_PROMPT was shortened (current version in prompts.h is long)
- Whether "bad quote" (stray citation artifact) was removed from SYSTEM_PROMPT
- Debug log completeness across all stages