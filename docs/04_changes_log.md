# Changes Log

**Project**: DUMY
**Format**: Date | File | Change | Reason

---

## 2026-11-05 (Stage 0 Documentation)

| File | Change | Reason |
|------|--------|--------|
| docs/01_audit_report.md | Created | Audit report with feature status, top problems, fix plan |
| docs/02_stage0_findings.md | Created | Model roles, timings, config values, tomato test analysis |
| docs/03_index_report.md | Created | PDF index table (23 PDFs, 6751 chunks, scanned pages note) |
| docs/04_changes_log.md | Created | This file |
| docs/05_test_plan_and_results.md | Created | Test questions with expected behavior, results table |
| docs/06_roadmap.md | Created | Stages 1-5 with status |
| docs/07_known_issues.md | Created | Open problems list |

---

## Pre-Stage 0 Changes (from code review)

| Date | File | Change | Reason |
|------|------|--------|--------|
| 2026-10-xx | config.json | Added `diagnosis_keywords` array | Stage 1: configurable diagnosis classification |
| 2026-10-xx | config.json | Added `stopwords` array | Stage 1: key-term extraction stopwords |
| 2026-10-xx | config.json | Added `quality_check` object | Stage 1: answer quality gate config |
| 2026-10-xx | config.json | Added `keyterm_relevance` object | Stage 1: key-term relevance check config |
| 2026-10-xx | config.json | Added `expansion` object | Stage 1: query expansion control config |
| 2026-10-xx | config.json | Set `web_fetch_timeout: 1` | Stage 1: web search fail-fast |
| 2026-10-xx | config.json | Set `num_ctx: 4096` | Reduce VRAM pressure |
| 2026-10-xx | config.json | Set `context_budget_tokens: 1500` | Trigger map-reduce earlier |
| 2026-10-xx | config.json | Models block: draft=qwen3:8b, helper=qwen2.5:7b | Use larger model for draft, helper for expansion |
| 2026-10-xx | include/dumy/config.h | Added config struct fields for all Stage 1 items | Type-safe config loading |
| 2026-10-xx | src/config.cpp | Implemented loading of all Stage 1 config fields | Parse new config sections |
| 2026-10-xx | src/retriever.cpp | `analyze()` uses `cfg_.diagnosis_keywords` | Config-driven diagnosis classification |
| 2026-10-xx | src/retriever.cpp | `expand_query()` checks `expansion_skip_for_simple/general` | Skip expansion for simple/general questions |
| 2026-10-xx | src/rag_pipeline.cpp | Quality check logic added (partial) | Retry once if answer too short |
| 2026-10-xx | src/rag_pipeline.cpp | Key-term relevance check logic added (partial) | Pre-LLM relevance gate |
| 2026-10-xx | src/rag_pipeline.cpp | Web search timeout handling (partial) | Fail-fast on SearXNG |
| 2026-10-xx | src/web_search.cpp | Timeout enforcement (needs verification) | Respect `web_fetch_timeout` |
| 2026-10-xx | include/dumy/prompts.h | SYSTEM_PROMPT updated (long version) | Project requirements S8 |
| 2026-10-xx | include/dumy/prompts.h | Added `is_off_topic()`, `is_arithmetic_question()` | Fast-path refusal |
| 2026-10-xx | include/dumy/prompts.h | Added `is_smalltalk()` | No-LLM greeting path |
| 2026-10-xx | src/main.cpp | Added CLI args: --no-web, --no-browser, --chat-model | Testing flexibility |

---

## Deleted / Removed Items

| Date | File/Feature | Reason |
|------|--------------|--------|
| — | None yet | Stage 0 only |

---

## To Be Updated After Each Step

- This file (04_changes_log.md)
- docs/05_test_plan_and_results.md (test results)
- docs/06_roadmap.md (stage status)