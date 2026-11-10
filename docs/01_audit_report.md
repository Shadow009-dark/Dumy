# DUMY Audit Report

**Project**: DUMY - Azerbaijani RAG Chatbot for Agriculture/Veterinary
**Date**: 2026-11-05
**Status**: Stage 1 in progress

---

## Feature Status Table

| Feature | Status | Notes |
|---------|--------|-------|
| PDF ingestion & indexing | ✅ Done | 23 PDFs, 6751 chunks indexed |
| Hybrid retrieval (BM25 + vector) | ✅ Done | RRF fusion implemented |
| Query expansion (LLM-based) | ✅ Done | Uses helper model (qwen2.5:7b) |
| Question classification | ✅ Done | 5 types: simple_fact, broad_topic, diagnosis, comparison, general |
| Diagnosis keyword classification | ✅ Done | Config-driven (`diagnosis_keywords` in config.json) |
| Reranking (cross-encoder via LLM) | ✅ Done | Top-N reranking with helper model |
| Map-reduce for broad topics | ✅ Done | Splits into sub-questions, aggregates |
| Self-check / self-correction | ✅ Done | One retry with stricter prompt |
| Web search (SearXNG) | ⚠️ Partial | Enabled but SearXNG often down |
| Answer quality check | ⚠️ Partial | Config exists, implementation needs verification |
| Key-term relevance check | ⚠️ Partial | Config exists, implementation needs verification |
| Query expansion skip for simple | ⚠️ Partial | Config exists, implementation needs verification |
| Web search fail-fast | ⚠️ Partial | Config has 1s timeout, implementation needs verification |
| Safety warnings (diagnosis) | ✅ Done | Appended to health-related answers |
| Off-topic detection | ✅ Done | Polite refusal for non-agri questions |
| Arithmetic detection | ✅ Done | Polite refusal |
| Small-talk fast path | ✅ Done | No LLM call for greetings |
| Identity questions | ✅ Done | Returns bot name, no invented creator |
| Streaming (SSE) | ✅ Done | /chat/stream endpoint |
| Session history | ✅ Done | 8 messages by default |

---

## Top Problems (as of Stage 0)

1. **Model loading dominates latency** — 76% of request time is Ollama model loading/swapping on 6GB VRAM
2. **Question misclassification** — "Pomidor yarpaqları niyə saralır?" classified as `general` instead of `diagnosis`, so no safety warning, no focused retrieval
3. **No quality gate on answers** — Model returns citations-only or very short answers; no retry
4. **No key-term relevance check** — Retrieves chunks that don't contain the answer's main terms (e.g., tomato question returned livestock chunks)
5. **Web search hangs** — SearXNG often unreachable; no fail-fast timeout enforcement
6. **Query expansion runs unnecessarily** — Even for simple questions, wasting helper model calls
7. **Knowledge base gaps** — 23 PDFs are mostly livestock/veterinary; no crop disease PDFs (tomato, etc.)
8. **Scanned PDF pages** — Some PDFs are scanned images; OCR disabled by default

---

## Fix Plan (Stage 1)

| Item | Description | Config Key | Status |
|------|-------------|------------|--------|
| a. Diagnosis keywords in config | Already present in config.json | `diagnosis_keywords` | ✅ Done |
| b. Answer quality check | Retry once if answer < 80 chars or citation-only; exempt "not found" | `quality_check.enabled`, `quality_check.min_answer_chars` | ⚠️ Partial |
| c. Key-term relevance check | Extract content words from question, prefix-match against chunks; return not-found if no match | `keyterm_relevance.enabled`, `keyterm_relevance.min_matched_terms`, `keyterm_relevance.prefix_length` | ⚠️ Partial |
| d. Web search fail-fast | Timeout ≤ 1s or skip if disabled | `web_search_enabled`, `web_fetch_timeout` | ⚠️ Partial |
| e. Skip expansion for simple questions | Config-controlled; don't use qwen2.5:3b as main model | `expansion.skip_for_simple`, `expansion.skip_for_general`, `expansion.max_simple_words` | ⚠️ Partial |

---

## Unverified Items (marked as such in this report)

- Exact implementation status of quality check, key-term relevance, web fail-fast, expansion skip in `rag_pipeline.cpp` and `retriever.cpp`
- Whether the "bad quote" (stray citation format) was removed from SYSTEM_PROMPT
- Debug log completeness
- Short SYSTEM_PROMPT vs original long version