# DUMY — read-only audit

Audit date: 2026-11-17. Repo: `C:\Users\Shadow\Desktop\dumy`, branch `feature/memory-ui`,
working tree clean at audit start (`git status --porcelain` → empty).
Audited binary: `build/Release/dumy.exe`, build id `Nov 15 2026 13:52:05` (newer than the
last source edit, `src/rag_pipeline.cpp` 2026-11-15 13:51), so the running code matches the
audited source.

Nothing in the repo was modified. Two side effects of running the pipeline, both outside git:
`test_results/` was rewritten by `tests/run_all.ps1` (expected, gitignored), and the temporary
memory namespace `data/memory/audit30/` created by my 30-question probe was deleted afterwards.

Method: source reading with file:line, the shipped binaries, the real HTTP API on
`127.0.0.1:8085`, `tests/run_all.ps1`, and a 30-question probe. Everything below is either a
path/line reference or a command output quoted verbatim.

---

## 1. REPO MAP

```
dumy/
├── CMakeLists.txt              CMake C++17 build; vcpkg manifest deps (curl, nlohmann-json, cpp-httplib, poppler-cpp)
├── vcpkg.json                  vcpkg dependency manifest
├── config.json                 runtime config: models, chunking, thresholds, rate limit, CORS, features (218 lines)
├── start_dumy.bat              Windows launcher; sets OLLAMA_MAX_LOADED_MODELS=2, starts server
├── test_models.ps1             ad-hoc model comparison script (12 KB)
├── README.md                   user/deploy documentation, model table, config table (26 KB)
├── PROGRESS.md                 dated engineering log; claims are historical, not verified here
├── CLEANUP_LOG.md              one-off cleanup notes
├── src/                        16 .cpp files, 7,833 lines (see table)
├── include/dumy/               21 headers, 4,608 lines — most pipeline logic lives here (see table)
├── web/                        index.html — the whole chat UI, 71,858 bytes, no framework, no CDN
├── docs/                       13 entries: 01..07 analysis reports, ADD_KNOWLEDGE, DEPLOY, PRIVACY, RESTORE, TRANSLATION_PLAN, web_demo/
├── knowledge/                  common_sense.json (25 curated entries), pending_review.json (2 parked), REVIEW.md
├── tests/                      run_all.ps1 (556 lines), quick.ps1, cases/ (43 JSON), expected/cases.json,
│                               expected/forbidden.json, expected/honest_failure.json, memory/ fixtures,
│                               build_review.py, probe_f6.py, probe_f7.py, variance.py, debug_report.ps1
├── pdfs/                       23 PDFs + README.txt (corpus; gitignored)
├── metnler/                    7 pre-extracted .txt versions of some books
├── data/                       151 entries: index.bin (43.2 MB), manifest.json, memory/, plus ~140 scratch
│   │                           logs/payloads from earlier debugging sessions (*.log, *.json, *.sse, *.out)
│   ├── index.bin               binary vector store (43,195,623 bytes)
│   ├── manifest.json           per-PDF ingest manifest: chunks/pages/scanned_pages/hash/status
│   ├── memory/                 36 client namespaces; "default" holds 1,908 conversations
│   └── web_cache/, probe/, audit_bench/, audit_payloads/, new_general/
├── db/                         empty (files: none — `find db -type f` → nothing)
├── benchmark/                  23 files: per-model answer JSONs, server logs, timings.csv
├── build/                      CMake output (gitignored): Release/dumy.exe (1,601,536 B, Nov 15 13:52),
│                               audit/Release/dumy.exe (Nov 10), Debug/dumy.exe (Oct 13), vcpkg_installed/
├── cmake/                      toolchain file
├── scripts/                    7 files: install_dependencies_{ubuntu.sh,windows.ps1}, pull_models.{sh,ps1},
│                               start_ollama.ps1, bench_one.sh, benchmark.sh, benchmark_models.py
├── test_results/               latest.md from the suite + older CSV/MD runs, payloads/, latency/, variance.md
└── tools/                      cloudflared.exe (gitignored binary)
```

### `src/` line counts

| File | Lines | What it does |
|---|---:|---|
| `rag_pipeline.cpp` | 2237 | The whole answer pipeline: fast paths, routing, retrieval coordination, rerank, draft, self-check, quality gate, trace, session append |
| `http_server.cpp` | 935 | cpp-httplib REST + SSE server; 15 routes; CORS, API-key check, rate limit, upload/ingest |
| `conversation_store.cpp` | 738 | Durable JSON conversations + user facts; atomic writes, per-client namespaces, fact extraction |
| `web_search.cpp` | 577 | SearXNG → DuckDuckGo → Wikipedia search, page download, clean, cache, per-host rate limit |
| `indexer.cpp` | 570 | Resumable PDF ingestion, content-hash manifest, simhash duplicate detection, BM25 build |
| `retriever.cpp` | 484 | Question-type heuristic, LLM query expansion, vector+BM25+RRF hybrid retrieval, web retrieval |
| `ollama_client.cpp` | 428 | Ollama HTTP client: chat, stream chat, embeddings, `/api/ps`, timing capture, thinking-strip |
| `main.cpp` | 375 | Entry point: server / `--console` / `--ingest` / `--reindex`, warm-up, CLI flags |
| `config.cpp` | 370 | config.json load/validate/save, env overrides, `ChatOptions` builder |
| `vector_store.cpp` | 257 | In-memory cosine store + binary persistence ("DUMYVEC1") |
| `html_cleaner.cpp` | 214 | HTML → text for web chunks |
| `reranker.cpp` | 213 | One LLM call scoring chunks 0–10, keep top-N |
| `pdf_loader.cpp` | 176 | poppler-cpp page text extraction; ASCII temp-path workaround for non-ASCII filenames |
| `bm25_index.cpp` | 116 | BM25 scoring over chunks |
| `chunker.cpp` | 96 | Character-window chunking with overlap |
| `session_manager.cpp` | 47 | Per-session in-memory message history (deque, trimmed) |

### `include/dumy/` line counts (where the logic actually sits)

| File | Lines | What it does |
|---|---:|---|
| `prompts.h` | 1296 | SYSTEM_PROMPT, fixed messages, ~40 `is_*` intent predicates, arithmetic parser, prompt builders, `needs_safety_warning` |
| `az_quality.h` | 1269 | AZ folding/word utils, answer hygiene (`clean_answer_text`), repetition/degenerate detectors, feed calculator, agri-keyword gate |
| `common.h` | 383 | Chunk-free helpers: JSON, logging (`log_info`, `log_model_call`), `StageTimer`, UTF-8 utils |
| `config.h` | 214 | Config struct + defaults (incl. `knowledge_enabled=true`) |
| `knowledge_base.h` | 207 | Tier-2 curated knowledge loader/matcher, hot reload by mtime |
| `meta_commands.h` | 174 | Deterministic meta-command classifier ("cavab ver", "davam et", …) |
| `rag_pipeline.h` | 161 | `AnswerResult`, `WarmupInfo`, `RagPipeline` interface |
| `conversation_store.h` | 142 | Store interface + `MemoryRef`/`MemoryFact`/`StoredMessage` |
| `ollama_client.h` | 102 | Client + `ChatOptions`/`ChatTiming`/`RunningModel` |
| `retriever.h` | 96 | `QuestionType`, `RetrievalOutcome`, `StoreResult`, `rrf_fuse` |
| `indexer.h` | 92 | Ingest interface + `IngestReport` |
| `vector_store.h` | 83 | `Chunk` (id/text/source/page/url/title/kind/trusted/embedding) + `IVectorStore` |
| `thread_pool.h` | 82 | Fixed thread pool for parallel web fetches |
| `web_search.h` | 66 | `WebResult`, `WebSearch` |
| `bm25_index.h` | 46 | BM25 index |
| `http_server.h` | 45 | Server wrapper |
| `session_manager.h` | 42 | Session history |
| `pdf_loader.h` | 35 | PDF page loader |
| `reranker.h` | 34 | Reranker |
| `chunker.h` | 20 | Chunker |
| `html_cleaner.h` | 19 | HTML cleaner |

Total: 7,833 lines in `src/`, 4,608 in `include/dumy/`. The "small" `src/rag_pipeline.cpp` number
hides that ~2,500 lines of its logic sit in `prompts.h` and `az_quality.h`.

---

## 2. PIPELINE TRACE

One request = `RagPipeline::answer` (`src/rag_pipeline.cpp:801`) → `answer_impl`
(`:917`) → deterministic hygiene pass back in `answer` (`:809`, `clean_answer_text`).

Order of steps, all in `src/rag_pipeline.cpp` unless stated:

| # | Step | Where | What it does |
|---:|---|---|---|
| 0 | Trim + normalization | `:928-929` `normalize_az_input(q_original)` | Restores ə/ı/ğ/ş/ç/ö/ü for purely-ASCII input via a local dictionary (no model). Original spelling kept in `q_original`. |
| 1 | Language detection | NOT a separate step | No language-detection function exists. Language is implicit: `is_language_task_question` (`:1097`), `looks_like_bare_sentence` (`:1076`), `normalize_smalltalk` in `prompts.h`. Answer language is forced by SYSTEM_PROMPT rule 3 (`prompts.h:22`). |
| 2 | Meta command | `:940` `classify_meta(q)` (`meta_commands.h:139`) | 32-phrase table → AnswerPrevious / Continue / Detail / Shorten / Retry / Protest. Resolves to the last non-smalltalk user turn from `sessions_.history()`, or re-runs the previous question; `Detail`/`Shorten` append an instruction suffix. No model, no retrieval. Empty target → "Hansı suala cavab verim?" and return. |
| 3 | Identity / date | `:978`, `:991` | Canned answers, no retrieval. |
| 4 | Feed calculation | `:1005` `is_feed_calculation_question` (`az_quality.h:931`) → `build_feed_calculation_answer` (`az_quality.h:952`) | Deterministic formula, no model. |
| 5 | Arithmetic | `:1017` `is_arithmetic_question` (`prompts.h:970`) → `az_arithmetic_expression` (`prompts.h:923`) → `eval_simple_arithmetic` (`prompts.h:825`) | Recursive-descent parser: `+ - * / ( )`, unary sign. Unparsable but operator-bearing → `ARITHMETIC_REPHRASE_MESSAGE`. |
| 6 | Small talk | `:1053` `is_smalltalk` | Greeting/thanks/bye replies, no model. |
| 7 | Code request | `:1064` `is_code_request` | Fixed Azerbaijani refusal (`REFUSAL_MESSAGE`), `refused=true`. |
| 8 | Bare ASCII sentence | `:1076` `looks_like_bare_sentence` → `deterministic_sentence_correction` | Spelling correction from the local dictionary. |
| 9 | Ollama health | `:1089` `if (!ollama_ok_)` | `OLLAMA_DOWN_MESSAGE`. |
| 10 | Language task | `:1097` `is_language_task_question` | One model call, no retrieval; letter-coverage retry for "write a sentence with ə ö ü…". |
| 11 | Memory | `:1163-1186` `remember_from_question` / `best_fact_for` | Deterministic; stores a stated fact or answers "mənim neçə … var?" from stored facts. |
| 12 | Statement / fact-check | `:1194` `is_statement_input` | Verdict + 2-sentence justification, then an adversarial second pass; disagreement → "Dəqiq bilmirəm". No retrieval. |
| 13 | Intent / route | `:1266` `retriever_.analyze(q)` (`retriever.cpp:74`) | Keyword heuristic → `SimpleFact` / `BroadTopic` / `Diagnosis` / `Comparison` / `General`. `top_k` from `top_k_for` (`retriever.cpp:118`). |
| 14 | Tier-2 pre-empt | `:1277` `knowledge_.match_combo(q)` | Only entries that declare `combos` may answer before retrieval. |
| 15 | Off-domain branch | `:1286` `!has_agri_keyword(q)` (`az_quality.h:279`) | Curated knowledge (`:1289`) → else general answer, 1 attempt + 1 clean retry, with word-validity/subject/domain-drift acceptance (`:1330-1420`); grammar/procedure/list → `OUT_OF_SCOPE_MESSAGE`. |
| 16 | Follow-up rewrite | `:1432` `is_followup_question(q)` (def `:782`) | ≤8 meaningful words **and** a reference marker → prepends the previous user text to the retrieval query only. |
| 17 | Query expansion | `:1440-1450` `retriever_.expand_query` (`retriever.cpp`) | Helper model emits 3-5 sub-queries (az+en); skipped for general/short messages (`expansion.skip_for_general`, `max_simple_words`). |
| 18 | Retrieval | `:1457-1458` `retrieve_store(queries, top_k)` in `std::async` | Vector cosine + BM25 + RRF (`rrf_fuse`, `retriever.cpp`), returns `max_score` and `score_margin`. Web search (`:1478`) runs in parallel but only if `web_search_enabled`. |
| 19 | min_score guard | `:1497` | `max_score < min_score` (0.5) → PDF chunks dropped. |
| 20 | General-knowledge drop | `:1510` `is_general_knowledge_question` (`prompts.h:1172`) | Question has a "what is/where is" shape and no domain term → PDFs dropped. |
| 21 | Relevance gate | `:1525-1580` | Key-term overlap ≥ `relevance_min_overlap` (0.34) **and** ≥2 matched terms, or cosine ≥ `relevance_check_score` (0.75); otherwise one yes/no helper-model call. `no` → chunks dropped, `pdf_relevance_failed=true`. |
| 22 | Rerank | `:1646` `rerank_or_trim` (`:631`) | Skipped when `score_margin >= rerank_skip_margin` (0.05) or on the fast path; else one LLM scoring call keeping `rerank_top_n`=8, capped at `rerank_max_candidates`=6. |
| 23 | Diversify | `:1636/1641/1647` `diversify` (`:746`) | Max 3 chunks per PDF / 2 per URL, drops cosine>0.95 near-duplicates. |
| 24 | Tier-2 post gate | `:1655` | Only when nothing survived: curated knowledge answers instead of a refusal. |
| 25 | Map-reduce | `:1691-1693` `map_reduce_summarize` (`:670`) | Only on the full path when context > `map_reduce_threshold_tokens` (6000) and >6 chunks. |
| 26 | Context budget | `:1700-1725` | Pops lowest-ranked chunks until the estimate fits `num_ctx` (4096) minus prompt+history+`num_predict`+margin. Observed: "context trimmed to fit num_ctx: 6 -> 2 chunks (limit=2596 est. tokens)". |
| 27 | Draft | `:1767` `ollama_.chat_stream(chat_model_, …)` | temperature 0.25, top_p 0.9, repeat_penalty 1.1, `num_predict` 500. Up to 3 attempts, halving the context on a context-window error. `done_reason=="length"` → one clean regeneration with 300–850 tokens (`:1820-1855`), then cut back to the last complete sentence. |
| 28 | Self-check | `:1886` `build_self_check_user_message(q, draft, !selected.empty())` | Only for BroadTopic/Diagnosis/Comparison. JSON `{verdict, problems}`; FAIL → exactly one correction call at temperature 0, accepted only if word-Jaccard ≥ 0.25 (`:1930-1965`). |
| 29 | Citation validation | `:1972` `enforce_citation_support`, `validate_source_labels` | A `[Mənbə: file, səh. N]` label survives only if the chunk is in context and shares a key entity with the sentence. |
| 30 | Source-list strip | `:1980` `strip_trailing_source_lines` | Removes a model-written "İstifadə olunan mənbələr:" block. |
| 31 | Garbled-word guard | `:1988-2015` `unknown_word_stats` | Unknown-word ratio > `garbled_word_ratio_max` (0.12) → one retry at temp 0.1, else `GARBLED_FALLBACK_MESSAGE`. |
| 32 | Quality gate | `:2035-2100` | Reject on degenerate / repetition loop / off-domain drift / too short (<40 chars). Then **one context-free** strict retry (`:2058`), then extractive fallback (`:2086`), then Tier-2 knowledge, then Tier-4 `helpful_uncertainty` (`:2098`). |
| 33 | Final shaping | `:2125-2137` | Requested sentence count, `max_general_sentences`=5 when no context, `trim_to_last_sentence`. |
| 34 | Provenance fallback | `:2140-2160` | If no `[Mənbə:]` label survived, the best non-web chunk sharing ≥2 key terms is attached to `used_chunks` (sources list only). |
| 35 | Safety flag | `:2175` `needs_safety_warning(q)` (`prompts.h:545`) | Sets `safety_warning` for the UI badge. |
| 36 | Trace | `:2180-2215` | `route`, `route_why`, `arbitration`, `attempts[]`, `expansion`, `retrieval`, `selected`, `prompt` (first 6000 chars), `sampling`, `timings_ms`. Returned only when `debug_trace_enabled` **and** a loopback client sends `"debug":true` (`http_server.cpp:429-432`). |
| 37 | Memory write | `:2231` `sessions_.append(...)`; `http_server.cpp:442` `append_turn` | In-memory history + JSON conversation file. |

Fallback paths, in the order they can fire: Tier-1 PDF context → Tier-2 curated knowledge
(`knowledge/common_sense.json`) → Tier-3 general/off-domain model answer → Tier-4
`helpful_uncertainty` → hard refusal texts (`REFUSAL_MESSAGE`, `OUT_OF_SCOPE_MESSAGE`,
`HONEST_FAILURE_MESSAGE`). "Tier 3 model fallback not implemented" is stated in `PROGRESS.md`
(2026-11-14 entry); the code confirms no third-tier model call.

---

## 3. CAPABILITY CHECKLIST

### a) Intent router (knowledge / calculation / follow-up command / chit-chat) — **IMPLEMENTED+TESTED**
`retriever.cpp:74` `Retriever::analyze` (5 types) plus `prompts.h` predicates for calculation,
meta command, small talk, code request, fact-check, language task. Suite:
`t01_identity PASS`, `t02_arith PASS`, `t03..t07_feed_* PASS`, `step1_* PASS`,
`mem_fastpath PASS` — 83/84 overall. Real-run evidence: 30 probe questions produced
`simple_fact 7, broad_topic 7, general 3, diagnosis 1` (log `Sual tipi:` counts) plus
`arithmetic 3, calculation 6, code_request 1`.
Weakness: the router is a keyword list; `Retriever::analyze`'s agri list (`retriever.cpp:107-116`)
has no `dana`, `toyuq`, `keçi`, `arı`, `quzu`, so "Dana ishalı olduqda nə etmək lazımdır?"
routed `general` while the question is textbook cattle medicine.

### b) Follow-up and meta-command understanding — **IMPLEMENTED+TESTED (understanding), PARTIAL (output)**
`meta_commands.h:139` `classify_meta`, 32 phrases; 6 kinds. Suite: `step1_meta_first PASS
(0.08 s, no model call)`, `step1_followup PASS`, `step1_mixed PASS`, `step1_fragment PASS`.
Probe: all five follow-ups (Q21–Q25) resolved to the prior topic (routes `broad_topic`,
`simple_fact`, `general`), e.g. "davam et" after the milking question returned "sağmal inəklər
gündə 2 dəfə sağılmalıdır". But the *content* is then mangled downstream: "qısa yaz" produced
"…qıcıq verir (dəyirmi) vəzifələrinizi pozursanız…", "yenidən yaz" re-emitted the refusal.

### c) Conversation memory — **IMPLEMENTED+TESTED**
Two layers. Short-term: `SessionManager` keeps `max_history_messages` = **8 messages** per
session (`config.json`), in RAM only (`session_manager.cpp:47`). Long-term:
`ConversationStore` writes `data/memory/<client>/conversations/<id>.json` and `facts.json`
(`conversation_store.h:1-20`), facts capped at `memory_max_facts` = 50, memory injection capped
at 160 tokens/prompt. Suite: `mem_fact_store`, `mem_fact_list`, `mem_fact_inject`,
`mem_step_latency`, `mem_fact_delete`, `mem_disabled`, `mem_no_pdf_pollution (pdf=2)`,
`mem_isolation (cross=404 traversal=404)`, `mem_corrupt_store (get=200)`, `mem_oversized`,
`mem_persist_restart` — all PASS. Observed store size: 36 client namespaces, 1,908 conversations
in `default`.

### d) Calculator / formula tools — **PARTIAL**
Two independent calculators, both rule-based, no expression evaluator for units:
1. `prompts.h:825` `eval_simple_arithmetic` — `+ - * /`, parentheses, unary sign, `×`/`÷`/`−`
   normalization; result formatted to 6 dp and trimmed.
2. `az_quality.h:952` `build_feed_calculation_answer` — `heads × kg/head/day × days`, with
   `daily = weight × rate / 100` when a live weight + % is given; defaults 300 kg and 2.5 %
   are labelled "Fərziyyə".
Supported today: plain arithmetic; total feed from heads+kg/day+days (any word order, "ton");
total feed from heads+weight+%+days. **Not supported**: days-until-feed-runs-out (needs
division), percentage *change* arithmetic (only described in SYSTEM_PROMPT rule 4), unit
conversion, decimals below 1 (see bug B2), any multi-step word problem. Evidence: 9/10 calculation
probes returned a `computed` source; two were wrong or unhelpful (Q17, Q19).

### e) Structured knowledge tables — **PARTIAL (JSON only, no domain tables)**
`knowledge/common_sense.json` — 25 entries, 2 `pending_review.json`. Each entry has
`id, answer, keywords[], combos[][], tags[], safe, confidence`. It is animal-behaviour trivia
(why roosters crow, why goats' pupils are horizontal, why ducks don't get wet), **not** a
disease–symptom–treatment, feed-composition or crop–soil–climate table.
NOT FOUND: any disease/symptom/treatment, feed-composition or crop/soil/climate dataset. Searched
`find . -name "*.csv" -o -name "*.db" -o -name "*.sqlite*"` (only `benchmark/timings.csv` and three
`test_results/*.csv` run logs), `db/` (empty), and every `*.json` outside `data/`.

### f) PDF corpus — **IMPLEMENTED+TESTED** (manifest is ground truth)
23 PDFs, 6,833 pages, 6,751 chunks, **51 scanned/no-text pages**, all `status: ok`
(`data/manifest.json`):

| PDF | Pages | Chunks | Scanned pages |
|---|---:|---:|---:|
| AQRONOMİYANIN əsasları | 353 | 353 | 0 |
| ARIF_YDX | 46 | 43 | 2 |
| Antropozoonoz xəstəlikləri | 520 | 511 | 0 |
| Famil Nəsibov Klinik diqanostika | 217 | 223 | 0 |
| HEYVAN BİOKİMYASI | 492 | 485 | 7 |
| Heyvandarlığın əsasları | 330 | 329 | 0 |
| Körpə k.t. heyvanlarının daxili xəstəlikləri | 185 | 189 | 0 |
| K.T. HEYVANLARININ YEMLƏNDİRİLMƏSİ | 293 | 292 | 1 |
| K.T. HEYVANLARININ YETİŞDİRİLMƏSİ (SON) | 267 | 268 | 0 |
| Patoloji fiziologiyadan təcrübə məşğələləri | 145 | 145 | 0 |
| Qaramal naxırlarında-kitab A5 | 277 | 277 | 0 |
| Qoyunçuluq | 408 | 409 | 1 |
| SAĞIM və SÜDÜN EMALI… MONOQRAFİYA | 533 | 529 | 3 |
| T.B. İsgəndərov Baytarlıq cərrahiyyəsi praktikum | 334 | 323 | 0 |
| T.B.İsgəndərov xırda heyvanların xəstəlikləri | 318 | 317 | 1 |
| T.B.İsgəndərov ANESTEZIOLOGIYA VƏ REANIMATOLOGIYA | 184 | 164 | 22 |
| T.B.İsgəndərov Baytarlıq oftalmologiyası | 82 | 85 | 0 |
| T.B.İsgəndərov ümumi və xüsusi cərrahiyyə | 450 | 440 | 3 |
| YEM HAZIRLANMASININ … MONOQRAFİYA-2 | 565 | 564 | 0 |
| Yem otları | 185 | 172 | 1 |
| sud və süd məhsullarının ekspertizası | 188 | 180 | 7 |
| İntensiv və Ekstensiv maldarlıq | 308 | 303 | 3 |
| Şirəli yem bitkiləri - 2013 | 153 | 150 | 0 |

`ocr_enabled: false` (`config.json`), so those 51 pages are permanently invisible; the biggest
hole is the 22 pages of the anaesthesiology book. `docs/07_known_issues.md` says "roughly 48
pages" — the manifest says 51.

### g) Vector store — **IMPLEMENTED+TESTED**
Embedding model `bge-m3` (`config.json` `models.embed`; confirmed by `/health`
`"embedding_model":"bge-m3"`). Chunking `chunk_size` 650 chars / `chunk_overlap` 100
(`config.json`; used at `indexer.cpp:354`), distinct sources capped 3 chunks, near-dupes
(cosine>0.95) dropped. **6,751 chunks** (`/health` `"chunks":6751`, `data/index.bin` 43.2 MB).
Stored metadata per chunk (`vector_store.h:19-27`): `id, text, source, page, url, title, kind
("pdf"|"web"), trusted, embedding` — **no section/heading, no language, no keywords, no date**.
`db/` is empty despite `docs/07_known_issues.md` claiming "db/ still contains the active JSON
index".

### h) Reranker and relevance gate — **IMPLEMENTED+TESTED**
Thresholds in `config.json`: `rerank_top_n` 8, `rerank_max_candidates` 6, `rerank_min_score` 8.0
(`reranker.cpp`), `rerank_skip_margin` 0.05, `min_score` 0.5, `relevance_min_overlap` 0.34,
`relevance_check_score` 0.75. Gate logic `rag_pipeline.cpp:1525-1580`; skip logic
`rag_pipeline.cpp:641`. In the audit run the gate kept 6/6 chunks on "Dana ishalı" and 12/12 on
timpaniya, but rerank was skipped every time (`rerank skipped: cosine margin 0.168079 >= 0.050000`)
— the 0.05 margin makes the LLM reranker effectively dead on this corpus. No test asserts
reranker quality; `rrf_fuse` is exercised only indirectly.

### i) General-knowledge tier and curated file — **IMPLEMENTED+TESTED (thin)**
Tier 2: `knowledge/common_sense.json`, **25 entries**, all `safe:true`, `confidence:"high"`,
answers 88–385 chars. Matching (`knowledge_base.h:104-160`): fold AZ letters → tokens of ≥4
chars → truncate to 5 → `combos` (AND groups) must all hit, each worth 10 points; `keywords`
add 1 point each via a stricter 5-char stem; keyword-only entries need ≥2 keyword hits
(`F6.3: no single-word matches`) and are only tried after retrieval fails. Hot-reloaded by mtime
(`:70`). Tier 3 (general model answer) exists inside the off-domain branch and after gate failure;
`general_fallback_enabled` is a config flag that the pipeline does not read (only `knowledge_enabled`
is consulted, `rag_pipeline.cpp:1276/1655/2094`).
Suite coverage: `dom02_milkfat`, `dom03_camel_pdf`, `fact09_qosma`, `fact10_sozyar`… all PASS;
`tests/probe_f6.py` and `probe_f7.py` are the dedicated Tier-2 probes (not run by `run_all.ps1`).

### j) Safety layer for medicine dosage / pesticide / animal health — **PARTIAL (disclaimer only)**
`prompts.h:545` `needs_safety_warning` matches ~45 keywords (xəstəlik, doza, antibiot, pestisid,
zəhər, parazit, …) and sets `safety_warning`, surfaced as `notes_ui.safety_warning`
(`http_server.cpp` `notes_json`) and a UI badge. SYSTEM_PROMPT rule 4 tells the model to defer to
a vet. There is **no** dose validation, no cross-check of a stated drug/amount against the
corpus, no pesticide-specific rule, no blocking of a dosage answer. Real output: Q5 returned
"2-3 qr ixtiol, 20-30 qr timpanol və 20-50 qr naftalan nefti 100 ml suda həll edilərək daxilə
verilir" — a drug dose copied from a PDF, with only a static disclaimer attached.
NOT FOUND: dosage table, dose-range parser, contraindication check, poisoning triage rules.

### k) Answer-only output + separate source list — **IMPLEMENTED+TESTED**
`az_quality.h:1201` `clean_answer_text` erases inline tags `[Mənbə: …]`, `[İnternet: …]`,
`[Ümumi bilik]`, `[Xülasə]`, `[Söhbət yaddaşı]`, then line-level `Qeyd:`/`Mənbə:`/`PDF:`/
`Dəyişiklik:`/`Note:`/`Sources:` markers, then preamble stripping ("Elbette,", "Qısa cavab:").
Called on every answer at `rag_pipeline.cpp:809`, with a second belt-and-braces line filter.
Sources travel only in `sources_json()` (`:344`) with `type` ∈ pdf|web|general|computed|memory|
knowledge. Suite: `sanitize_markers PASS (variants=15 bad=0)`, `ui_static_safe PASS`.
Observed in all 30 probe answers: no `Qeyd:`, no inline `[Mənbə:]`.

### l) Web search — **IMPLEMENTED+TESTED (off by default; not exercised)**
`web_search.cpp:577` lines: SearXNG probe (`:258`), SearXNG JSON (`:276`), Wikipedia API
(`:330`), DuckDuckGo HTML (`:384`), page fetch + cache (`web_cache_ttl_hours` 168), per-host
rate limit, thread pool. Runtime state: `config.json` `"web_search_enabled": false` →
`/health` `"web_search_enabled":false`, and every probe log line reads
`web search disabled in config.json (web_search_enabled=false)`. So in the audited configuration
it is **opt-in and never executes**; no test asserts web output (`docs/07_known_issues.md`:
"the test suite does not require it"). The `web_engine: "ddg"` key is honoured at
`web_search.cpp:453` after SearXNG.

### m) Live data (weather, prices, sensors, farm/user profile) — **NOT FOUND**
Searched `grep -rn -i "weather|hava durumu|qiymet|price|sensor|ferma profili|farm_profile"` over
`src/` and `include/` → only sampling `temperature` hits. The only "profile" is the memory fact
store (`data/memory/<client>/facts.json`, free-text facts the user typed). No weather API, no
market-price feed, no sensor input, no farm entity model.

### n) Voice input — **IMPLEMENTED (browser only), FAILS AT RUNTIME (reproduced)**
UI: `web/index.html:1286-1345`, Web Speech API (`window.SpeechRecognition ||
window.webkitSpeechRecognition`), button enabled at `:1397` when the API exists. It is
client-side only — no audio reaches the server (`:550`). Reproduced in the shipped UI on
`http://127.0.0.1:8085/`: `typeof SpeechRecognition === "undefined"`, `typeof
webkitSpeechRecognition === "function"`, mic button enabled — clicking it raised
`error = "network"` and the banner displayed **"Səs tanıma üçün internet lazımdır."**
(`index.html:1329`). Root cause: this Chromium build exposes the API but has no Google speech
backend, so recognition can never start; the code has no fallback (no server STT, no
`mediaDevices` path). The button therefore advertises a feature that always fails on this machine.

### o) Model routing — **IMPLEMENTED+TESTED**
`config.json`: `embed: bge-m3`, `helper: gemma3:4b`, `draft: gemma3:4b`, `selfcheck: gemma3:4b`,
`fallback_model`/`model_fallbacks`: `qwen3:4b-instruct-2507-q4_K_M`.
Roles: draft = the answer (`rag_pipeline.cpp:1767`), helper = expansion + rerank + relevance
yes/no + map-reduce summaries, self-check = the JSON verdict (`:1886`), embed = query/chunk
vectors. Resolution + fallbacks in `RagPipeline::initialize` (`:410`) and `resolve_models`
helpers (`:113-213`). Measured resident set from `/health` after warm-up:
`gemma3:4b` 2,882,482,994 B all on GPU (`size_vram` = `size`), `bge-m3:latest` 1,244,166,879 B
with `size_vram: 0` (CPU). Total ≈ 4.13 GB; on a 6 GB card that leaves ~1.8 GB, which is why only
one chat model may be loaded (`start_dumy.bat` sets `OLLAMA_MAX_LOADED_MODELS=2`, `keep_alive:"-1"`).
Preload: `warmup()` (`:511`) runs before the server reports ready — a 1-token chat call + a tiny
embed call, then verifies both through `/api/ps`; result is exposed at `/health` (`warmup.main_ready`,
`main_resident`, `embed_on_cpu`, load ms). Observed: `main_ready:true, main_resident:true,
embed_on_cpu:true, main_load_ms:8, embed_load_ms:6` (models were already hot).

### p) Logging / debug trace / metrics — **PARTIAL**
Logging: `log_info/log_warn/log_error` + `log_model_call` printing `[timing] <stage> model=…
wall=… load=… prompt_eval=… gen=… tok=P+G done_reason=…` (`common.h:365`), plus a final
`Mərhələ vaxtları (ms): routing, expansion, retrieval, web, rerank+diversify, draft, self_check,
post, TOTAL`. Debug trace: full JSON per answer (`route, route_why, arbitration, attempts,
expansion, retrieval, selected, prompt, sampling, timings_ms`) but gated behind
`debug_trace_enabled` (false) **and** a loopback client sending `"debug":true`.
NOT FOUND: any metrics endpoint or aggregate counters — no refusal rate, no per-route p50/p95,
no Prometheus/CSV export, no per-chunk scores in the trace (`PROGRESS.md` admits "Per-chunk
vector/fused scores skipped"). `log_questions` is false, so questions are not persisted in logs.

### q) API / web layer — **IMPLEMENTED+TESTED (QA gaps)**
Routes (`src/http_server.cpp`): `GET /` (:295), `GET /health` (:306), `GET /documents` (:366),
`GET /debug/sanitize` (:374), `POST /chat` (:389), `POST /chat/stream` SSE (:471), `POST /ingest`
(:606), `POST /upload` (:647), `GET|POST /conversations` (:715,:725), `GET|DELETE
/conversations/{id}` (:736,:769), `GET|DELETE /memory`, `POST /memory/toggle` (:782-817),
`GET /config` (:833). Auth: `check_api_access` (`:154`) enforces `X-API-Key` **only if
`api_key` is non-empty** — it is `""` in `config.json`, so the audited server is unauthenticated.
Rate limit: `rate_limit_per_minute` 20, but `rate_limit_exempt_loopback: true` and
`rate_limit_localhost_per_minute: 0` → unlimited for localhost. CORS: reflects only
`allowed_origins` (localhost:8085 / 127.0.0.1:8085). Body caps 256 KB, question cap 2,000 chars.
UI (`web/index.html`, single file, no CDN — `ui_static_safe PASS`): SSE streaming, conversation
sidebar with search/rename/delete, memory dialog (toggle, list, clear), settings dialog (API key,
voice auto-send, TTS), theme toggle, copy/regenerate, welcome chips from `config.json`,
`/health` status badge, localStorage client id sent as `X-Session-Id` (`:630`).
Gap: `GET /conversations` with no header returned **`"count":1908`** and 275,882 bytes of
every previous conversation (`Content-Length: 275882`) — on an exposed port that is a data leak.

---

## 4. TEST STATUS

### 4.1 `tests\run_all.ps1`

Command (from repo root, shipped binary, no build):

```
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_all.ps1
```

Result (verbatim tail): `Report: test_results\latest.md  |  83/84 passed, 0 warning(s)`,
process exit status **1**.

The single failure:

```
q13                      FAIL   7,57s  pdf=3  PDF sources 3 > 1
```

`tests/cases/general_q13.json` = `{"id":"q13","question":"Arı ailəsi qışa necə hazırlanır?",
…"max_pdf_sources":1,…}` — the answer cited 3 PDFs where the case allows 1.
Passing groups: 16 `t*` cases, 18 `dom*`/`fact*`/`gen*` cases, 15 `q*` cases, 10 `mem_*` cases,
`ui_static_safe`, `sanitize_markers` (variants=15 bad=0), 4 `step1_*`, 6 `welcome_*`.

Caveats found in the runner itself:
- `tests/run_all.ps1` declares **two `param` blocks** (line 2 `param([switch]$Quick)`, line 18
  `param([switch]$ReuseServer)`). PowerShell errors at line 18 —
  `param : The term 'param' is not recognized…` — at the top of every run, and `-ReuseServer`
  can never be bound. The run continues because `$ErrorActionPreference='Stop'` is only set on
  line 20.

### 4.2 30 fresh Azerbaijani questions through the real pipeline

Method: started `build/Release/dumy.exe --no-browser --no-web`, waited for
`/health warmup.main_ready:true` (chunks=6751, gemma3:4b, 43.2 MB index), POSTed each question to
`/chat` with a distinct `X-Session-Id`; follow-ups used a seed question in the same conversation.
Route/source counts are from the API response (`route`, `sources[]`), seconds are wall clock,
latency is the server's own `latency_ms`. "ok?" is my judgement of the returned text:
*yes* = answers the question correctly; *partly* = right topic but truncated/garbled/incomplete;
*no* = wrong, hallucinated, template, or refusal.

| # | Question | Route taken | #sources | Answer ok? | Seconds |
|---:|---|---|---:|---|---:|
| 1 | Mal-qarada qarayara xəstəliyinin əlamətləri nələrdir? | broad_topic | 3 (pdf) | yes | 9.21 |
| 2 | İnək gündə neçə dəfə sağılmalıdır? | simple_fact | 1 (pdf) | no | 10.43 |
| 3 | Qoyunlarda quzu ölü doğumunun səbəbləri nələrdir? | broad_topic | 5 (pdf) | partly | 10.47 |
| 4 | Buğda əkini üçün torpaq necə hazırlanmalıdır? | simple_fact | 1 (pdf) | no | 19.68 |
| 5 | Mal-qarada qarın şişməsi (timpaniya) necə müalicə olunur? | diagnosis | 1 (pdf) | partly | 33.10 |
| 6 | Arılarda varroa xəstəliyi ilə necə mübarizə aparılır? | general | 1 (general) | no | 5.38 |
| 7 | Dana ishalı olduqda nə etmək lazımdır? | general | 0 | no | 19.76 |
| 8 | Süd verimini artırmaq üçün yemləmə necə olmalıdır? | simple_fact | 1 (pdf) | no | 25.99 |
| 9 | Toyuqlarda yumurta istehsalı üçün işıq rejimi necə olmalıdır? | simple_fact | 1 (pdf) | no | 14.82 |
| 10 | Keçilərdə dırnaq xəstəlikləri hansılardır? | broad_topic | 1 (pdf) | partly | 20.40 |
| 11 | 15 x 12 neçədir? | arithmetic | 1 (computed) | yes | 0.01 |
| 12 | 120 baş, gündə 25 kq, 30 gün üçün nə qədər yem lazımdır? | calculation | 1 (computed) | yes | 0.01 |
| 13 | 50 baş qoyun üçün 45 günlük yem hesablayın. | calculation | 1 (computed) | partly | 0.03 |
| 14 | 250 baş inək üçün gündə 3 kq yem, 10 gün - ümumi nə qədər? | calculation | 1 (computed) | yes | 0.01 |
| 15 | 100-dən 37 çıx | arithmetic | 1 (computed) | yes | 0.01 |
| 16 | 20 baş dana, 40 gün, gündə 8 kq yem nə qədər lazımdır? | calculation | 1 (computed) | yes | 0.01 |
| 17 | 500 kq yem 25 baş mal üçün neçə günə çatır? | calculation | 1 (computed) | no | 0.01 |
| 18 | 400 kq inək üçün 2.5% canlı çəki norması ilə gündəlik yem? | simple_fact | 5 (pdf) | partly | 19.31 |
| 19 | 1000 baş toyuq üçün 7 günlük yem, gündə 0.12 kq? | calculation | 1 (computed) | no | 0.02 |
| 20 | 45 + 55 neçədir? | arithmetic | 1 (computed) | yes | 0.01 |
| 21 | cavab ver *(after quzu ölü doğumu)* | broad_topic | 1 (pdf) | partly | 12.18 |
| 22 | davam et *(after inək sağımı)* | simple_fact | 3 (pdf) | yes | 21.43 |
| 23 | ətraflı yaz *(after qarayara)* | broad_topic | 1 (general) | yes | 8.31 |
| 24 | qısa yaz *(after dana ishalı)* | general | 1 (pdf) | no | 6.73 |
| 25 | yenidən yaz *(after varroa)* | general | 1 (general) | no | 6.30 |
| 26 | Python dilində fibonacci funksiyası yaz | code_request | 0 | yes (correct refusal) | 0.02 |
| 27 | Fransanın paytaxtı hansıdır? | general | 1 (general) | yes | 0.34 |
| 28 | Şəkərçörək necə hazırlanır? | general | 1 (general) | partly | 0.04 |
| 29 | 2026-cı ildə dünya çempionu kim oldu? | general | 1 (general) | no | 0.49 |
| 30 | Mənə qısa bir şeir yaz | general | 1 (general) | partly | 1.18 |

Aggregates:
- **Answers usable (yes): 11/30 = 37 %. Partly: 8/30. Wrong or non-answer: 11/30 = 37 %.**
- **Refusal / no-substance rate: 6/30 = 20 %** — 1 hard refusal (`refused=true`, Q26), 2 Tier-4
  clarification templates (Q2, Q4), 3 honest "no info" texts (Q6, Q25, Q28).
- **Median latency 5,820 ms** (server `latency_ms`), mean 7,828 ms, min 0 ms (deterministic
  paths), max **33,084 ms** (Q5). Wall clock median ≈ 5.9 s.
- Domain answers were **all** rejected once by the quality gate: the log contains 9
  `quality gate: reject reasons=[…,repetitive=1,…]` lines for the 10 knowledge questions, and
  the strict retry failed 9/9 times, leaving a 100–260-char extractive sentence or the Tier-4
  template.

---

## 5. KNOWN BUGS (all reproduced)

**B1 — Quality-gate "repetitive" rejects correct drafts; the retry is context-free and invents an answer.**
Trigger: `Dana ishalı olduqda nə etmək lazımdır?`
Log: `quality gate: reject reasons=[degenerate=0,repetitive=1,off_domain=0,too_short=0(708<40)]`
→ `strict_retry model=gemma3:4b … tok=83+400` → `quality gate: strict retry accepted (1190 chars)`.
The strict retry calls `build_strict_answer_message(q)` (`rag_pipeline.cpp:2058`) with **no
context at all**, so the answer became infant-diarrhoea advice: "İshal olunduqda aşağıdakı
addımları etmək lazımdır: 1. **Çəhresləmə:** … Dişat suyunu və limonlu suyu tökmək". This single
mechanism explains Q7, Q8, Q24. 9 of 10 domain questions hit it.

**B2 — Decimal feed amounts are multiplied by 100.**
Trigger: `1000 baş toyuq üçün 7 günlük yem, gündə 0.12 kq nə qədərdir?`
Answer: `Nəticə: 1 000 × 12 × 7 = 84 000 kq (84 ton).` Correct value: 840 kg. `az_fold` converts
`.` to a separator before `feed_detail::parse_inputs` (`az_quality.h:~883`) tokenizes, so `0.12`
becomes `12`.

**B3 — The feed calculator prints a formula that contradicts its own result and its own assumption.**
Trigger: `50 baş qoyun üçün 45 günlük yem hesablayın.`
Answer: `Nəticə: 50 × 8 × 45 = 16 875 kq (17 ton). / Fərziyyə: canlı çəki 300 kq, günlük norma 3%`.
50×8×45 = 18,000, not 16,875; 16,875 corresponds to 7.5 kg/head/day (300 kg × 2.5 %), and the
assumption line claims 3 %. The displayed daily value is rounded (`kg()`), the rate printed is
not the rate used.

**B4 — "How many days will the feed last" is unsupported and answered with a template.**
Trigger: `500 kq yem 25 baş mal üçün neçə günə çatır?`
Answer: `Bu hesablama üçün baş sayı və dövr (gün) lazımdır…` — the question is routed to
`calculation` (`is_feed_calculation_question` accepts digits+baş+yem+gün+neçə) but
`build_feed_calculation_answer` requires heads and days and otherwise returns the prompt.

**B5 — A correct draft can be replaced by a clarifying question.**
Triggers: `İnək gündə neçə dəfə sağılmalıdır?` → `inek barədə nəyi öyrənmək istəyirsiniz:
qidalanma, xəstəlik, yoxsa baxım?`; `Buğda əkini üçün torpaq necə hazırlanmalıdır?` →
`bugda barədə nəyi öyrənmək istəyirsiniz…`. Both carried a PDF source in `sources[]` yet the
text is `helpful_uncertainty` (Tier 4, `rag_pipeline.cpp:899`). The first is a welcome-chip
question from `config.json`, i.e. a demo path that fails on demand.

**B6 — Garbled text passes the quality gate.**
Trigger: `qısa yaz` after the diarrhoea question.
Answer contains `…qıcıq verir (dəyirmi) vəzifələrinizi pozursanız tezlikle müraciət etmək
vacibdir`. `is_degenerate_answer`/`has_repetition_loop` both pass it; the word-validity check
needs ≥10 words and a 12 % unknown ratio (`garbled_word_min_words`, `garbled_word_ratio_max`).

**B7 — General tier fabricates facts.**
Trigger: `2026-cı ildə dünya çempionu kim oldu?` → `2026-cı ildə dünya çempionu - Fransa oldu.`
(0.49 s, accepted). No date-awareness, no "I don't know" for post-cutoff facts; the acceptance
test only checks subject mention, word validity and domain drift.

**B8 — Over-citation of PDFs (the one suite failure).**
Trigger: `Arı ailəsi qışa necə hazırlanır?` → 3 PDF sources, case allows 1 (`general_q13.json`).
Root: the "citation fallback" (`rag_pipeline.cpp:2140-2160`) attaches the best matching chunk,
while `cited_chunks` had already counted labels the model wrote.

**B9 — `tests/run_all.ps1` has two `param` blocks → `-ReuseServer` unusable, error on every run.**
`tests/run_all.ps1:2` and `:18`; output quoted in §4.1.

**B10 — Multi-PDF answers lose context silently: `num_ctx` 4096 trims to 2–3 chunks.**
Trigger: `Mal-qarada qarın şişməsi (timpaniya)…` — `context trimmed to fit num_ctx: 6 -> 2 chunks
(limit=2596 est. tokens)`; the draft then ran out of `num_predict` (`done_reason=length`, 2908
prompt tokens + 500), was regenerated, and cost 33.1 s.

**B11 — Unauthenticated bulk conversation read.**
`curl http://127.0.0.1:8085/conversations` (no `X-API-Key`, no `X-Session-Id`) →
`HTTP/1.1 200 OK`, `Content-Length: 275882`, `"count":1908`. Everyone who can reach the port
reads every stored conversation while `api_key` is empty (the default).

**B12 — Voice input always fails on this machine.**
Click the mic on `http://127.0.0.1:8085/` → banner `Səs tanıma üçün internet lazımdır.`
(`index.html:1329`, `ev.error === "network"`). The button is enabled because
`webkitSpeechRecognition` exists, but recognition never starts.

Minor, observed but not separately ranked: expansion emits nonsense sub-queries
(`in coru necə sıqır`, `dəbir sayı qurban`) that still enter retrieval; `data/memory/default`
holds 1,908 conversations / 36 namespaces with `conversation_retention_days: 0` (never purged);
`db/` is empty while `docs/07_known_issues.md` claims it holds the active JSON index.

---

## 6. GIT STATE

- Branch: **`feature/memory-ui`** (`git rev-parse --abbrev-ref HEAD`).
- Working tree: **clean** (`git status --porcelain` → no output), before and after the audit.
- Last 10 commits (`git log --oneline -10`):

```
5b1b6ca 2026-11-15 F7.1: Tier 2 arbitration (off-domain pre, no-chunk pre, post-gate) + reachability keywords
a06880d 2026-11-15 F6.4: review table, park advice entries
8a869f9 2026-11-15 F6.1/F6.2: unnatural-colour combos, blue entry, action entry; Tier 4 tailoring
a6eb503 2026-11-15 F1: keep qtype stable; combo-only Tier 2
4299f90 2026-11-15 F1: combo-only Tier 2 pre-empt so PDF tier keeps priority
510f632 2026-11-15 F2: treat 'sari' as a colour word
557594e 2026-11-15 F2: combo weighting and suffix-tolerant 4-letter stems
14b2f5d 2026-11-15 F2: cover 'boyayirlar/boyali' phrasings in the colour entry
af8b7d2 2026-11-15 F1/F2: curated Tier 2 knowledge, hot reload, Tier 4 wording
a4bfc15 2026-11-15 E3a: measure the extractive fallback (refusals 33% -> 10%)
```

- Branches (`git branch -a -vv`) and contents relative to `master` (`afbd536`):

| Branch | HEAD | vs master | What it contains |
|---|---|---:|---|
| `feature/memory-ui` *(current)* | `5b1b6ca` | ahead 23, behind 0 | The whole recent line: memory + web-UI redesign, "answer-only" output, debug trace, extractive fallback, Tier-2 knowledge, colour combos, F7.1 arbitration. **Nothing is merged to `master`.** |
| `wip/answers` | `a06880d` | ahead 22, behind 0 | Same line minus the F7.1 commit (parked advice entries, review table). |
| `wip/retrieval` | `a4bfc15` | ahead 14, behind 0 | Stops at the extractive fallback; contains the debug-trace work (D1/D2/E1/E2) but not Tier-2 knowledge. |
| `wip/step1` | `c550d62` | ahead 8, behind 0 | `DUMY_DEBUG_TRACE` alias, best-of-3 latency judging, memory flag fixes, browser voice input, welcome examples. |
| `master` | `afbd536` | baseline | "Add the 15 new-general questions as regression cases" — 23 commits behind the working branch. |
| `cline/7d746`, `cline/910f7`, `cline/d8d72` | `8d5ec1f` | merged, behind 29 | Cline worktree snapshots ("snapshot before Cline work"), no unique work. |
| `cline/fd72e` | `5b1b6ca` | ahead 23 | Duplicate of `feature/memory-ui` in another worktree. |

So the shipping branch is unmerged, and four worktrees (`C:/Users/Shadow/.cline/worktrees/*`) hold
copies of the same history. There is no tag, no release branch, and `master` cannot reproduce the
binary that was audited.

---

## 7. TOP 10 GAPS (ranked by impact on a farmer's answer quality)

1. **The quality gate destroys correct domain answers, and its retry answers without the sources.**
   9 of 10 knowledge questions were rejected as `repetitive`, the context-free strict retry then
   hallucinated (Q7 infant-diarrhoea advice, Q8 baby-feeding advice), and the fallback left one
   sentence. Evidence: §4.2 rows 2–10, 24 and B1. This is the single biggest reason the bot looks
   knowledgeable in the tests and useless in practice.

2. **The feed calculator is wrong on decimals and prints formulas that do not match its results.**
   `0.12 kq` → `12` (Q19: 84 t instead of 840 kg), and `50 × 8 × 45 = 16 875` (B2, B3). Feed
   amounts are the one thing a farmer will act on with money; a 100× error is disqualifying. The
   calculator is also the only *fast* path (0.01 s vs 5–33 s), so it is what people will use.

3. **Top-5 answers are truncated to a single sentence by the extractive fallback.**
   Q9 returned only "…2-3 faiz dəyişir", Q10 only "10-55 %-ni əhatə edir" — technically grounded,
   practically useless. Cause: gate rejection → `extract_best_sentence` (`:2086`) picks the single
   best-overlapping sentence. Evidence: §4.2 rows 9–10, B1/B5.

4. **The retrieval context is cut to 2–3 chunks by `num_ctx` 4096 with `num_predict` 500.**
   `context trimmed to fit num_ctx: 6 -> 2 chunks (limit=2596)` (B10). A 6,751-chunk corpus is
   reduced to two chunks on the questions that matter most (diagnosis), which is why answers are
   thin and why the model drifts. Raising the budget or shrinking `num_predict` for long answers
   would change more answers than any reranker tuning.

5. **No structured veterinary knowledge — no disease/symptom/treatment table, no dose table, no
   feed-composition table.** Everything comes from 6,833 pages of prose PDFs, so a question like
   "timpaniya müalicəsi" returns one scraped sentence with drug doses and only a static
   disclaimer (Q5, §3e, §3j). The 25-entry `common_sense.json` is animal trivia.

6. **Medicine/pesticide safety is a badge, not a check.** `needs_safety_warning` only matches
   keywords; a dosage can be emitted verbatim from a 1980s textbook with no range check, no
   species check, no withdrawal-period warning (§3j, Q5). For a farmer this is the highest-risk
   gap after correctness.

7. **The intent router misses basic livestock vocabulary.** `Retriever::analyze`'s agri list has no
   `dana`, `toyuq`, `keçi`, `arı`, `quzu` (`retriever.cpp:107-116`), so "Dana ishalı…" routed
   `general`, skipped rerank, and went down the general-answer path (Q7). `has_agri_keyword`
   (`az_quality.h:279`) is a *different* list that does contain them — the two disagree.

8. **The general tier invents facts instead of saying "I don't know".** "2026-cı ildə dünya
   çempionu" → "Fransa oldu" (Q29, B7); the acceptance test checks subject mention and word
   validity, not truth. With web search disabled by default (`web_search_enabled:false`), nothing
   can correct it.

9. **Keyword-only retrieval quality: rerank is dead, expansion adds noise, no hybrid tuning.**
   `rerank skipped: cosine margin 0.168079 >= 0.050000` on every domain question, so the LLM
   reranker never runs; expansion instead injects garbage sub-queries (`in coru necə sıqır`) while
   the relevance gate keeps 12/12 chunks on a diagnosis question (`overlap=6/6`). Thresholds are
   config defaults, never measured against a labelled set (no golden-answer file exists).

10. **Operational blind spots: no metrics, unauth read of all conversations, voice input that
    cannot work.** Refusal rate, per-stage latency and route distribution exist only in stdout
    `[timing]` lines (`log_questions:false`, no metrics endpoint, §3p); `GET /conversations`
    returns 1,908 conversations / 275 KB to any client when `api_key` is empty (B11); the mic
    button is enabled and always fails with `error "network"` (B12). None of these change a single
    answer, but each makes the system harder to trust and to operate.

---

### Notes on what was NOT verified

- Not built: no compilation was performed; the audit used `build/Release/dumy.exe`
  (Nov 15 2026 13:52:05), newer than every source file.
- Web search was never exercised: it is disabled in `config.json`, and enabling it would have
  changed the audited configuration. DuckDuckGo/SearXNG/Wikipedia code paths were read, not run.
- Scanned pages were counted from `manifest.json`; no page was re-extracted to confirm them.
- Voice was tested only to the point of the reproduced `network` error; audio capture itself was
  never available in this environment.
- TTS, upload/ingest and the tunnel (`tools/cloudflared.exe`) were not exercised.
- The 30-question "ok?" column is my judgement of the returned Azerbaijani text; the answer texts
  are quoted verbatim in the probe output and in §5 where they are wrong.
