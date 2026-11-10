# Roadmap

**Project**: DUMY
**Last Updated**: 2026-11-05

---

## Stage 0: Documentation & Audit ✅ **DONE**

- [x] Create docs/ folder with 7 markdown files
- [x] Audit report with feature status, problems, fix plan
- [x] Stage 0 findings (model roles, timings, tomato test analysis)
- [x] PDF index report (23 PDFs, 6751 chunks)
- [x] Changes log
- [x] Test plan with 8 questions
- [x] Roadmap (this file)
- [x] Known issues list

---

## Stage 1: Core Quality Gates 🔄 **IN PROGRESS**

| Item | Description | Status |
|------|-------------|--------|
| 1a | `diagnosis_keywords` in config.json (UTF-8 fixed) | ✅ Done (config has it, needs UTF-8 fix) |
| 1b | Answer quality check: retry once if <80 chars or citation-only; exempt not-found | ⚠️ Partial (config exists, impl needs verify) |
| 1c | Key-term relevance check: extract content words, prefix-match chunks; return not-found if no match | ⚠️ Partial (config exists, impl needs verify) |
| 1d | Web search fail-fast: timeout ≤1s or skip if disabled | ⚠️ Partial (config has 1s, impl needs verify) |
| 1e | Skip query expansion for simple/general questions; don't use qwen2.5:3b as main | ⚠️ Partial (config exists, impl needs verify) |
| 1f | Fix `diagnosis_keywords` UTF-8 encoding in config.json | ❌ Todo |
| 1g | Verify debug log covers all stages | ❌ Todo |
| 1h | Shorten SYSTEM_PROMPT, remove bad quote | ❌ Todo |

**Stage 1 Exit Criteria**: All 5 items (a-e) implemented, built, and verified via tests.

---

## Stage 2: Retrieval Quality 📋 **TODO**

| Item | Description |
|------|-------------|
| 2a | Add crop disease PDFs (tomato, wheat, cotton, etc.) |
| 2b | Enable OCR for scanned pages (Tesseract + aze+eng) |
| 2c | Improve chunking for tables/structured data |
| 2d | Add metadata filtering (animal type, crop type) |
| 2e | Implement query routing to domain-specific indexes |

---

## Stage 3: Latency Optimization 📋 **TODO**

| Item | Description |
|------|-------------|
| 3a | Keep models resident (increase `keep_alive`, preload at startup) |
| 3b | Speculative decoding (draft model → verify) |
| 3c | Async pipeline: parallel retrieval + web search |
| 3d | Cache embeddings for common queries |
| 3e | Reduce `num_ctx` further if quality allows |

---

## Stage 4: Evaluation & Monitoring 📋 **TODO**

| Item | Description |
|------|-------------|
| 4a | Automated test suite (golden Q&A pairs) |
| 4b | RAGAS / custom metrics (faithfulness, relevance, recall) |
| 4c | Production logging dashboard (latency, error rates, model loads) |
| 4d | A/B testing framework for prompt/model changes |

---

## Stage 5: Production Hardening 📋 **TODO**

| Item | Description |
|------|-------------|
| 5a | Rate limiting, auth, HTTPS |
| 5b | Multi-user sessions with isolation |
| 5c | PDF upload → auto-ingest → reindex |
| 5d | Backup/restore for index.bin |
| 5e | Health checks, graceful shutdown |
| 5f | Docker / systemd deployment |

---

## Status Legend

- ✅ Done
- 🔄 In Progress
- ⚠️ Partial
- ❌ Todo
- 📋 Planned (not started)