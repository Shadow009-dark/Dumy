# DUMY – Progress Log

Local C++17 RAG chatbot (agriculture + general Q&A, Azerbaijani), Ollama-only,
MSVC + vcpkg (curl, nlohmann-json, cpp-httplib, poppler).

## STATUS: COMPLETE (2026-10-14) – all spec sections done, all tests pass

### DONE (earlier sessions)
- Full CMake project: config, ollama_client, pdf_loader, vector_store, bm25,
  indexer (resumable per-file + per-PDF table + shingle dup detection),
  retriever (hybrid cosine+BM25+RRF, expansion, web parallel), reranker,
  rag_pipeline (draft -> self-check, map-reduce), http_server (REST + SSE +
  CORS + upload + ingest), browser UI in web/, start_dumy.bat, README, OCR hook.
- 23 PDFs indexed: 6751 chunks, 0 duplicates, 51 low-text pages (none mostly
  scanned), all files status "ok".

### DONE (this session, spec sections in parentheses)
- [x] §2 state check; data/ backup -> data_backup_20261013_221930; full ingest
      re-run (all 23 already up to date, per-PDF table printed).
- [x] Error handler no longer relabels body-less 500s as "404 not found";
      every >=400 response logged with method+path (http_server.cpp).
- [x] §6 quality: [Xülasə] ban (prompt + strip + summaries cite real sources),
      self-check returns ONLY JSON {"final":...} with critique post-validation,
      map-reduce prompt forbids meta-commentary, request_timeout 300,
      num_predict 800, UTF-8 sanitizer, boundary-aware truncation.
- [x] §5 model routing: config "models" {embed,helper,draft,selfcheck} +
      fallback_model + model_fallbacks, graceful warn-once fallbacks,
      think:false in every Ollama call + strip_thinking, per-stage timing log
      (expansion/retrieval/web/rerank/draft/self_check/total).
- [x] Pulled: qwen2.5:3b (helper/selfcheck), qwen3:8b (benchmark candidate).
- [x] §3 UI/API: exact AZ texts (title "DUMY – Heyvandarlıq köməkçisi",
      "Sualınızı yazın...", Göndər/Yeni söhbət/Dayandır/Sənədlər/Yenidən skan et,
      "Düşünürəm...", "Yoxlanılır...", "Mənbələr", "Ümumi bilik", "İnternet",
      "Sənəd yüklə"), SSE status event for self-check, /health live Ollama
      probe (2 s cap, 5 s cache -> ~90 ms), /upload never-overwrite suffix,
      /ingest 409 guard + flag released on exceptions, OPTIONS 204,
      --no-browser flag, start_dumy.bat port check (verified BUSY_DETECTED).
- [x] §4 retrieval: max 3 chunks/PDF + 2/URL, near-dup cosine >0.95 dropped,
      expansion retry + pair-flattening (qwen2.5:3b answers [[az,en],...]),
      Wikipedia fallback: query simplification ("Telefon nədir?"->"Telefon"),
      429 backoff (1.5 s + 4 s), max 3 wiki queries, honest error text.
- [x] Rerank: scores by models.draft (helper scores were noise), prompt forces
      full id=1..N arrays, keep best rerank_top_n + retrieval-order fill.
- [x] Label honesty: fix_url_labels + validate_source_labels -> invented
      citations become [Ümumi bilik].
- [x] §5 benchmark (benchmark/timings.csv + JSONs): qwen2.5:7b wins
      (112*/84/36/0/82 s; *cold) vs qwen3:8b (168/159/77/0/141) vs
      llama3.1:8b (321 timeout/335 timeout/101/0/122). Default stays
      qwen2.5:7b; user makes the final call after reading benchmark/*.json.
- [x] §7 tests all pass (results in test_results/, times in README §8):
      T1 52 s/3 books, T2 53–121 s/5 sub-queries/4–6 books/never "not found",
      T3a 52–58 s PDF+note, T3b 39–53 s Wikipedia [İnternet:], T4 0 s refusal,
      follow-up pair 65 s resolves subject, SSE 800 deltas + status:selfcheck
      + done, uploads (valid/dup-suffix/fake-magic/traversal), 409 race,
      OPTIONS 204, GET /documents, /health 90 ms, browser UI end-to-end.
- [x] §8: README config table fixed (models block, num_ctx 8192,
      request_timeout 300, context_budget 5892 = 8192-900-600-800),
      measured results + benchmark table + GPU splits + honest limitations
      (README §12). Final build: 0 warnings.

### NOT DONE / deliberately skipped
- OCR: hook only (ocr_enabled false, run_ocr stub) – needs Tesseract install.
- SearXNG: not running on this machine; Wikipedia fallback in use (README §7
  has the docker instructions; recommended next step).
- llamacpp rerank latency (14–24 s on broad/diagnosis) accepted for score
  quality; documented in README §12.

### Next steps (for a future session)
1. Start SearXNG (docker, README §7) and re-test Q3-style web questions.
2. Optionally `ollama pull llama3.2:3b` as an even smaller helper.
3. Read benchmark/*.json and set models.draft if you prefer qwen3:8b quality
   over speed.
4. Tesseract + aze.traineddata if the 51 low-text pages matter (README §9).

## Reference: how to run
- Double-click start_dumy.bat (port check included) or:
  build\Release\dumy.exe [--no-browser|--console|--ingest|--chat-model X]
- Server: http://127.0.0.1:8085/  |  tests use curl.exe + UTF-8 JSON files.
- Backup of the index exists: data_backup_20261013_221930/
