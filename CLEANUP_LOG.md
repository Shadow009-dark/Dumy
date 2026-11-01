# CLEANUP_LOG

Every removed item, with the reason. Protected items (PDFs, index/embeddings,
Ollama models, .git, README, user docs, `test_models.ps1`, latest `test_results`
run) are never listed here because they were not touched.

## 2026-10-25 — retrieval-parity baseline artifact (temporary)

| Item | Reason |
|---|---|
| `build/retriever.cpp.bak`, `build/reranker.cpp.bak` | Temporary copies used to restore the two `opts.num_ctx = cfg_.num_ctx;` lines after building the comparison binary. Deleted immediately after the restore; verified `grep "opts.num_ctx = cfg_.num_ctx"` finds both lines again and no `BASELINE` marker remains. |
| baseline `dumy.exe` (built with those two lines removed) | Temporary measurement artifact, authorised for this comparison only. It was overwritten by the next `cmake --build build --config Release`, which rebuilt from the restored sources. No baseline binary exists any more. |

Retained on purpose (measurement evidence, not clutter):
`test_results/latency/base_*`, `new_*`, `acc_*`, `base_topk.log`, `new_topk.log`,
`v_1000.log`, `v_1200.log`, `v1000_*`, `v1200_*`.
