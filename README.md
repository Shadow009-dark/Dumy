# DUMY — Local AI Agriculture Chatbot (C++17 / Ollama / RAG)

A completely **local** AI assistant that answers questions about agriculture, farming,
livestock (sheep, cattle, poultry, bees…), animal diseases, feeding, crops, equipment
and farm management — plus brief general everyday questions ("Telefon nədir?").

- **Main language: Azerbaijani.** The bot answers in Azerbaijani, or in the user's
  language when they write in something else.
- **No API keys, no accounts, no cloud.** The only AI is a local Ollama server
  (`http://localhost:11434`).
- **Internet search is optional** (key-free: DuckDuckGo Lite + Wikipedia fallback;
  `web_search_enabled` in `config.json`). If it is off or down the bot keeps working
  with PDFs and its own knowledge — and *tells* the user.
- **Politely refuses programming tasks** in Azerbaijani, stays helpful for everything else.
- Ready to be connected to a website later: REST API + SSE streaming + CORS + sessions +
  thread safety.

---

## 1. What's inside (architecture)

```
DUMY/
├─ CMakeLists.txt          build (C++17, MSVC/GCC/Clang)
├─ vcpkg.json              Windows dependencies (curl, nlohmann-json, cpp-httplib, poppler)
├─ config.json             ALL settings
├─ pdfs/                   ← put your PDF files here
├─ data/                   index cache, manifest, web cache (auto-created)
├─ cmake/FindPopplerCpp.cmake
├─ scripts/                install_dependencies + pull_models (Windows & Ubuntu)
├─ include/dumy/  src/     modules:
│    config          loads config.json, resolves paths
│    pdf_loader      poppler-cpp text extraction (+ OCR extension point)
│    chunker         500-800 token chunks, 100-token overlap, metadata
│    ollama_client   /api/chat (streaming!), /api/embed, /api/tags — libcurl
│    vector_store    IVectorStore interface + in-memory/binary-persisted impl (swap for Qdrant later)
│    bm25_index      BM25 keyword index (thread-safe)
│    web_search      SearXNG primary, Wikipedia fallback, cache, rate limit, thread pool
│    html_cleaner    strips scripts/styles/menus/tags/entities
│    retriever       hybrid cosine+BM25 with Reciprocal Rank Fusion + LLM query expansion
│    reranker        LLM scores every chunk 0-10, keeps the best 8-12, dedup
│    indexer         incremental ingestion (file-hash manifest), embeddings
│    rag_pipeline    the full quality pipeline (see §5)
│    session_manager last N messages per session_id (thread-safe)
│    http_server     /chat, /chat/stream (SSE), /ingest, /health + CORS
└─ main.cpp              console mode (--console) and server mode
```

**Data flow:** PDFs → poppler → per-page chunks (source file + page number) →
`bge-m3` embeddings → `data/index.bin` (binary) + BM25 rebuilt in memory.

## 2. Prerequisites

| | Windows | Ubuntu/Debian |
|---|---|---|
| Compiler | Visual Studio 2022+ (C++ workload) | `g++` 9+ |
| CMake | 3.16+ (bundled with VS) | `apt install cmake` |
| HTTP/JSON/PDF libs | **vcpkg** (manifest — automatic) | apt (see script) |
| Ollama | [ollama.com](https://ollama.com) (already running = OK) | same |

### Install dependencies

**Windows (PowerShell):**
```powershell
# What the script checks/installs:
winget install Microsoft.VisualStudio.2022.Community   # with "Desktop development with C++"
# vcpkg comes bundled with Visual Studio (VC/vcpkg) — nothing to install.
powershell -ExecutionPolicy Bypass -File scripts\install_dependencies_windows.ps1
```

**Ubuntu/Debian:**
```bash
bash scripts/install_dependencies_ubuntu.sh
# equivalent to:
sudo apt update
sudo apt install -y cmake g++ pkg-config libcurl4-openssl-dev \
    nlohmann-json3-dev libcpp-httplib-dev libpoppler-cpp-dev
```

### Pull the models (once)

```bash
bash scripts/pull_models.sh          # Linux/macOS
powershell -File scripts\pull_models.ps1   # Windows
```
which runs:
```bash
ollama pull bge-m3                        # embeddings, CPU only
ollama pull gemma3:4b                     # draft + helper + self-check (single model)
ollama pull qwen3:4b-instruct-2507-q4_K_M # fallback when gemma3:4b is missing
# optional / experiments (one chat model at a time on 6 GB):
ollama pull qwen3.5:4b
ollama pull qwen2.5:7b
```
If a configured model is missing the app falls back gracefully:
`models.draft` → `fallback_model` → `model_fallbacks` → first installed model;
`models.helper` / `models.selfcheck` → first installed small model → draft model
(one warning is logged, nothing crashes).

### VRAM tips (RTX 4050, 6 GB)

- `start_dumy.bat` sets `OLLAMA_MAX_LOADED_MODELS=2` (one chat model + bge-m3 on
  CPU), `OLLAMA_KEEP_ALIVE=-1` and `OLLAMA_FLASH_ATTENTION=1`. Setting it to 1
  evicts the chat model whenever bge-m3 is used and causes a reload per question.
- `keep_alive: "-1"` keeps both models resident between questions.
- Watch the GPU/CPU split with `ollama ps` while a question is generating.
- gemma3:4b is the only chat model: draft, helper (expansion/rerank/summary) and
  self-check all use it, so no second model ever needs to be loaded.

## 3. Build & run

### Fastest way on Windows

Double-click **`start_dumy.bat`** in the project root — it starts the server and the
browser opens `http://127.0.0.1:8085/` automatically (the chat UI).

### Windows (Developer PowerShell / cmd)
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_TOOLCHAIN_FILE="C:/Program Files/Microsoft Visual Studio/2022/Community/VC/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
```
(On this machine the bundled VS 2026 paths were used — the configure step prints what it found.)

### Ubuntu
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### Run
```bash
# 1) first: index the PDFs in pdfs/ (and the project root if legacy PDFs live there)
./build/Release/dumy.exe --ingest          # Windows
./build/dumy --ingest                      # Linux

# 2a) console chat (no website needed) — test right away
dumy --console

# 2b) or the REST server + browser chat UI (default)
dumy
# opens http://127.0.0.1:8085/ in your browser automatically
```

Other flags: `--config <path>`, `--port <n>`, `--chat-model <name>`, `--no-web`,
`--no-browser` (don't auto-open the chat page — used for tests),
`--ingest --force` (re-index everything), `--help`.

> **Note:** all PDFs live in `pdfs/` (scanned recursively). Startup re-scans
> automatically — incremental, only new/changed files are re-embedded, and the index
> is saved after every file so an interrupted ingest resumes without losses.
> `start_dumy.bat` checks the port first: if something already listens on 8085 it
> only opens `http://127.0.0.1:8085/` and does **not** start a second instance.

## 4. Configuration (`config.json`)

| Key | Default | Meaning |
|---|---|---|
| `ollama_url` | `http://localhost:11434` | local Ollama |
| `models.embed` | `bge-m3` | embeddings |
| `models.helper` | `gemma3:4b` | expansion / rerank / map-reduce summaries |
| `models.draft` | `gemma3:4b` | the main answer model |
| `models.selfcheck` | `gemma3:4b` | second-pass fact check (broad/diagnosis/comparison) |
| `fallback_model` | `qwen3:4b-instruct-2507-q4_K_M` | used when `models.draft` isn't installed |
| `model_fallbacks` | `qwen3:4b-instruct-2507-q4_K_M` | further draft fallbacks |
| `chunk_size` | `650` | tokens per chunk (500–800 range) |
| `chunk_overlap` | `100` | tokens of overlap |
| `top_k_simple` | `6` | chunks for simple questions |
| `top_k_broad` | `12` | chunks for broad questions (map-reduce kicks in) |
| `pdf_folder` | `pdfs` | where you drop PDFs (scanned recursively) |
| `pdf_extra_folders` | `[]` | extra folders scanned at top level (legacy) |
| `data_folder` | `data` | index + caches |
| `host` / `port` | `127.0.0.1` / `8085` | REST server |
| `web_search_enabled` | `false` | enabled only after the fail-fast check passes |
| `searxng_url` | `http://localhost:8080` | legacy SearXNG (optional) |
| `search_languages` | `["az","en"]` | search in both, answer in Azerbaijani |
| `web_top_results` / `web_max_pages` | `8` / `6` | search + download limits |
| `web_fetch_timeout` | `10` | seconds per page |
| `web_cache_ttl_hours` | `168` | page/search cache lifetime |
| `trusted_domains` | fao.org, woah.org, merckvetmanual.com, .edu, .gov, gov.az, edu.az, usda.gov, europa.eu | preferred; others marked “təsdiqlənməmiş mənbə” |
| `fast_mode` | `false` | skips rerank + self-check for simple questions (latency) |
| `request_timeout` | `300` | seconds per Ollama call |
| `max_history_messages` | `8` | remembered per session |
| `rerank_top_n` | `8` | chunks kept after reranking |
| `context_budget_tokens` | `0` | `0` = auto: `num_ctx` − real system prompt − history − `num_predict_draft` − safety margin. Above the map-reduce threshold, long contexts are summarized. |
| `num_ctx` | `4096` | Ollama context window (6 GB VRAM friendly) |
| `num_predict_draft` | `500` | cap on answer length (tokens); one regeneration with a larger budget if the model hits it |
| `keep_alive` | `"-1"` | Ollama never unloads the model |
| `auto_ingest` | `true` | index new PDFs at startup |
| `ocr_enabled` / `ocr_languages` | `false` / `aze+eng` | OCR extension point (Tesseract, not built in v1) |
| `web_folder` | `web` | chat UI files (falls back to the copy embedded in the binary) |
| `open_browser` | `true` | open the chat page automatically on server start |
| `allow_upload_from_remote` | `false` | PDF upload restricted to localhost |
| `upload_max_mb` | `200` | max uploaded PDF size |

### Retrieval diagnostics (`DUMY_DUMP_TOPK`)

Set `DUMY_DUMP_TOPK=1` in the environment before starting the server to log,
for every request, the exact post-rerank context chunks and the draft prompt
size:

```
[topk] n=8 | [Mənbə: Qoyunçuluq.pdf, səh. 44] | [Mənbə: Yem otları.pdf, səh. 12] | ...
[ctx]  prompt_eval_count=4610 num_ctx=8192 num_predict=1000 fits=1
```

`fits=1` means `prompt_eval_count + num_predict` still fits inside `num_ctx`.
Use it to compare retrieval between builds; it is off by default and changes
no behaviour.

## 5. Quality pipeline (what happens for every question)

1. **Classify** the question: simple fact / broad topic / diagnosis / comparison / general
   → chooses `top_k` (5–6 vs 15–20).
2. **Query expansion** — the LLM produces 3–5 specific sub-queries in Azerbaijani *and*
   English ("all animal diseases" → "qoyun xəstəlikləri", "mal xəstəlikləri",
   "poultry diseases", "infectious livestock diseases"…).
3. **Parallel retrieval**: PDF hybrid search **and** web search run simultaneously.
   - Hybrid = cosine vector search + BM25 keywords, merged with **Reciprocal Rank Fusion**.
   - Web = SearXNG (or Wikipedia fallback) → download top 5–8 pages → strip HTML →
     chunk → embed → same hybrid ranking → merged into one context with `[İnternet: url]` labels.
4. **Rerank** — one LLM call scores every candidate 0–10, keeps the best 8–12,
   removes duplicates. *(skipped by `fast_mode` for simple questions)*
5. **Draft answer** — streamed to the client, built only from selected chunks, every
   claim labelled `[Mənbə: file, səh. X]` / `[İnternet: url]` / `[Ümumi bilik]`.
6. **Self-check** — a second LLM call finds unsupported claims, invented numbers and
   source conflicts (PDFs and trusted domains win), fixes and finalizes.
   *(skipped by `fast_mode` for simple questions)*
7. **Map-reduce** — if the context exceeds `context_budget_tokens`, chunk groups are
   summarized first, then the final answer is built from the summaries.
8. **Never "not found"** — with no relevant sources the bot uses its own knowledge,
   says so explicitly (`[Ümumi bilik]` + a note), and the sources list always ends with
   "İstifadə olunan mənbələr".
9. **Safety** — disease/treatment/dose/pesticide questions automatically append:
   *"Dəqiq diaqnoz və müalicə üçün mütləq baytar həkimə / aqronoma müraciət edin."*
10. **Thinking removed** — qwen3 `<think>...</think>` blocks are stripped; only the final
    answer is shown.

## 6. Browser chat UI + REST API (website-ready)

### Browser UI (`http://127.0.0.1:8085/`)

Opening the root URL gives a full chat page served by the C++ server itself — 100%
local, no CDN, works offline:

- live token streaming (SSE via `POST /chat/stream`), a "Düşünürəm... X san"
  indicator, a "Yoxlanılır..." indicator while the self-check pass runs (SSE
  `status` event), and the final self-checked answer replaces the draft when the
  `done` event arrives
- safe markdown rendering (HTML is escaped first — no injection from model output)
- collapsible source list per answer: PDF (file + page), Internet (clickable links,
  trusted/untrusted marked), Ümumi bilik
- session kept in `localStorage` → follow-up questions work; "Yeni söhbət" resets
- status badge from `/health` (Ollama up/down, model, chunk count, web on/off)
- responsive layout, dark/light theme following the system, Enter = send,
  Shift+Enter = new line, "Dayandır" aborts the stream
- **Sənədlər panel**: indexed PDFs with chunk/page counts, "Yenidən skan et"
  (`POST /ingest`) and drag-and-drop PDF upload (`POST /upload`)

The UI is `web/index.html` (edit it to customize; the binary embeds a copy as a
fallback so it works from any working directory).

### REST API

CORS is enabled on all routes, sessions are kept server-side (`session_id`).

### `POST /chat`
```bash
curl -s http://127.0.0.1:8085/chat \
  -H "Content-Type: application/json" \
  -d '{"session_id":"my-session","message":"Qoyunların xəstəlikləri haqqında yaz"}'
```
Response:
```json
{
  "session_id": "my-session",
  "answer": "...cavab [Mənbə: ..., səh. 12] ...\n\nİstifadə olunan mənbələr:\n- ...",
  "sources": [{"label":"[Mənbə: Qoyunçuluq.pdf, səh. 12]","type":"pdf","page":12,...}],
  "qtype": "broad_topic", "refused": false, "web_used": true,
  "map_reduce": false, "own_knowledge": false, "warning": ""
}
```

### `POST /chat/stream` (Server-Sent Events)
```bash
curl -N http://127.0.0.1:8085/chat/stream \
  -H "Content-Type: application/json" \
  -d '{"message":"Telefon nədir?"}'
```
```
data: {"type":"delta","text":"Telefon"}
data: {"type":"delta","text":" — bu gün ..."}
data: {"type":"done","session_id":"s-...","answer":"<tam cavab>","sources":[...], ...}
```
Render `delta` events live; the `done` event carries the authoritative (self-checked)
answer + sources.

### `POST /ingest`
```bash
curl -s -X POST http://127.0.0.1:8085/ingest -d '{"force":false}'
# -> {"status":"ok","files_indexed":2,"chunks_added":311,...}
```

### `GET /documents`
```bash
curl -s http://127.0.0.1:8085/documents
# -> {"documents":[{"file":"Qoyunçuluq.pdf","chunks":212,"pages":180,
#      "scanned_pages":3,"status":"ok","duplicate_of":""}, ...]}
```

### `POST /upload` (multipart PDF → `pdfs/`)
```bash
curl -s -X POST http://127.0.0.1:8085/upload -F "file=@newbook.pdf"
# -> {"status":"ok","file":"newbook.pdf","size":1234567,...}
# then POST /ingest to index it (the UI does both automatically)
```
Only `%PDF`-headed files are accepted, names are sanitized (no path traversal),
size limited by `upload_max_mb`, and by default only localhost may upload
(`allow_upload_from_remote: false`).

### `GET /health`
```json
{"status":"ok","ollama":true,"chat_model":"gemma3:4b","helper_model":"gemma3:4b",
 "embedding_model":"bge-m3","web_search_enabled":false,"sessions":0,"chunks":6751,
 "pdf_support":true}
```
A live Ollama probe with a hard 2 s timeout (cached 5 s) backs the `ollama`
field, so the status badge can never hang when Ollama is down.

## 7. Web search setup (SearXNG, key-free)

Web search works **without any key** — with a local SearXNG as the main source and a
built-in Wikipedia fallback when SearXNG isn't running.

```bash
docker run -d -p 8080:8080 --name searxng searxng/searxng
```

Then enable the JSON format (required — the bot calls `format=json`):

1. `docker exec -it searxng sh` → `vi /etc/searxng/settings.yml`
2. find the `search:` block and make it:
   ```yaml
   search:
     safe_search: 0
     autocomplete: "duckduckgo"
     formats:
       - html
       - json        # ← required for DUMY
   ```
3. `docker restart searxng`
4. Check: `http://localhost:8080/search?q=test&format=json` should return JSON.

If SearXNG is down, DUMY automatically falls back to the Wikipedia API (az/en/ru) and
logs `SearXNG not reachable ... falling back to Wikipedia`. If *everything* is offline
the answer still comes from PDFs + model knowledge, with an explicit note:
*"Qeyd: bu dəfə internet axtarışı işləmədi ..."*.

## 8. Test cases (expected behaviour)

| # | You type | You should get |
|---|---|---|
| 1 | `Qoyunların xəstəlikləri haqqında yaz` | Answer built from your PDFs with **page citations**: `[Mənbə: 10_08_..._Qoyunçuluq.pdf, səh. X]`, ending with "İstifadə olunan mənbələr", plus the veterinarian warning. |
| 2 | `Heyvanların bütün xəstəlikləri haqqında yaz` | **Broad question**: query expansion fires (sub-queries for sheep/cattle/poultry…), `top_k=18`, possibly map-reduce, organized multi-section answer by animal/type. **Never "not found".** |
| 3 | `Süd sağma aparatı nədir?` / `Telefon nədir?` | Not (or barely) in your PDFs → answer from web + own knowledge with `[İnternet: https://...]` and/or `[Ümumi bilik]` labels; a note if the internet was down. |
| 4 | `Mənə Python kodu yaz` | Immediate polite refusal in Azerbaijani: *"Məni kənd təsərrüfatı və ümumi suallar üçün hazırlamışlar — kod yazmaq ... mənim xaricimdir..."* with an offer to help with other topics. |

Run them in console mode (`dumy --console`) or via `POST /chat`.

### Historical measurements (RTX 4050 6 GB, qwen2.5:7b — superseded by gemma3:4b)

| Test | Question | Time | Result |
|---|---|---|---|
| 1 | Qoyunların xəstəlikləri haqqında yaz | 52 s | PDF page citations from 3 books (Qoyunçuluq.pdf, Heyvandarlığın əsasları.pdf, Körpə ... daxili xəstəlikləri.pdf), safety warning, no `[Xülasə]` |
| 2 | Heyvanların bütün xəstəlikləri haqqında yaz | 53–121 s | query expansion fires (original + 4–5 sub-queries), top_k=12, 4–6 different books cited, **never "not found"** |
| 3a | Süd sağma aparatı nədir? | 52–58 s | answered from the SAĞIM monograph with `[Mənbə: ..., səh. X]` (+ web when Wikipedia answers) |
| 3b | Telefon nədir? | 39–53 s | Wikipedia fallback (`[İnternet: https://az.wikipedia.org/wiki/Telefon]`) after simplifying "nədir?" queries |
| 4 | Mənə Python kodu yaz | 0 s | polite Azerbaijani refusal, `"refused":true`, no LLM call |
| + | dependent follow-up ("Onun əlamətləri hansılardır?" in the same session) | 65 s | resolves the previous subject correctly |
| + | SSE stream | — | 800 `delta` events, `status:selfcheck` event (UI shows **Yoxlanılır...**), authoritative `done` |
| + | uploads / traversal / fake PDF / 409 / OPTIONS | — | 200 + numeric suffix `" (1).pdf"` / 400 / 400 into `pdfs/` / 409 / 204 |

Per-stage timings (from the server log, milliseconds):
`expansion ~3–5 s | retrieval ~3–6 s | web ~3–6 s | rerank ~14–24 s |
draft 16–74 s | self_check ~8–10 s` — everything except the draft runs on
the small/assistant path or is skipped where the spec allows it.

### Draft-model benchmark (5 identical Azerbaijani questions)

| Model | Q1 sheep dis. | Q2 all diseases | Q3 milking | Q4 code | Q5 diarrhoea | Verdict |
|---|---|---|---|---|---|---|
| **qwen2.5:7b** (current default) | 112 s* | **84 s** | **36 s** | 0 s | **82 s** | only model meeting the targets (simple <40 s, broad <90 s); 24%/76% CPU/GPU split |
| qwen3:8b | 168 s | 159 s | 77 s | 0 s | 141 s | better structured answers, but ~2× slower (36%/64% split) |
| llama3.1:8b | **321 s (timeout)** | **335 s (timeout)** | 101 s | 0 s | 122 s | unusable as draft: 32%/68% split, two answers exceeded the 300 s request timeout |

\* includes the cold model load. Answers for all three models are saved under
`benchmark/` (`benchmark/timings.csv` + `<model>_<question>.json`) — read them
yourself before overriding `models.draft`; this is my pick, the final decision
is yours. Rerun any time with `bash scripts/benchmark.sh`.

### GPU notes measured with `ollama ps`

- qwen2.5:7b 5.4 GB → **24% CPU / 76% GPU** (fits almost entirely in VRAM)
- qwen3:8b 6.6 GB → 36% CPU / 64% GPU (partial offload = slower)
- llama3.1:8b 6.2 GB → 32% CPU / 68% GPU (too slow for a 300 s budget)
- qwen2.5:3b 2.4 GB → 100% GPU
- The draft model and the helper cannot both stay resident in 6 GB; Ollama
  swaps them (keep_alive `30m` keeps the *weights* on disk-cache warm). Set
  `OLLAMA_MAX_LOADED_MODELS=1` + `OLLAMA_FLASH_ATTENTION=1` (see §2).

## 9. Ingest reports, duplicates and OCR

Every ingest prints a per-file table (file, pages, chunks, scanned pages, status) and
is **resumable**: the index and manifest are saved after each file, so a stopped run
continues where it left off.

- **Duplicates** — a 64-bit **5-word-shingle simhash** over the extracted text detects
  near-identical PDFs (single-word hashing gave false positives for different books
  by the same author). Flagged files are skipped and reported (`status:
  "duplicate"` + which file they duplicate). At retrieval time a second guard runs:
  exact duplicate chunk texts are dropped and near-duplicate embeddings
  (cosine > 0.95) are removed, with at most **3 chunks per PDF / 2 per web page**
  in the final context, so the same text is never cited twice.
  **Current library: 0 duplicates.** The two similarly named
  "KƏND TƏSƏRRÜFATI HEYVANLARININ Y..." books are genuinely different
  (*yemləndirilməsi* = feeding vs *yetişdirilməsi* = rearing) and both stay indexed.
- **Scanned PDFs** — pages with <40 words are counted per file. Files that are mostly
  scanned show `status: "mostly_scanned"`. **Current library: 51 low-text pages
  across 11 of 23 PDFs (worst: Baytarlıq anesteziologiya ~22/184 pages), no file is
  mostly scanned, all 23 are `status: "ok"`.**

### Enabling OCR (Tesseract)

1. Install Tesseract:
   - Windows: `winget install UB-Mannheim.TesseractOCR` (adds `tesseract.exe` to PATH),
     then install the Azerbaijani data: download `aze.traineddata` from the
     tessdata_fast repo into `C:\Program Files\Tesseract-OCR\tessdata\`.
   - Ubuntu: `sudo apt install tesseract-ocr tesseract-ocr-aze`.
2. In `src/pdf_loader.cpp` implement `run_ocr()` (poppler `page::render()` →
   `TessBaseAPI::SetImage/GetUTF8Text`), link `tesseract`.
3. Set `"ocr_enabled": true` in `config.json` — scanned pages are then routed to OCR
   during ingest instead of being skipped.

## 10. Troubleshooting

| Symptom | Fix |
|---|---|
| `Ollama serveri işə düşməyib` | Start the Ollama app (Windows) or `ollama serve`. `curl http://localhost:11434/api/version` must answer. |
| `model not found` | `ollama pull qwen3:8b` etc. (see §2). Or rely on `model_fallbacks`. |
| Ingest fails: embedding error | `ollama pull bge-m3` — embeddings are mandatory for indexing. |
| `PDF support is disabled` | Install poppler-cpp (`libpoppler-cpp-dev` / vcpkg `poppler`) and re-configure CMake. |
| PDF not readable | Password-protected or scanned. See §9 for OCR. |
| Web answers say "internet axtarışı işləmədi" | Start SearXNG (§7) or check the network; Wikipedia fallback needs internet. |
| `could not bind 127.0.0.1:8085` | Another program uses the port → `"port": 8086` in `config.json` or `dumy --port 8086`. |
| Slow first run | First ingest embeds every chunk (minutes). Later runs are incremental. |
| Answers too slow | `"fast_mode": true` (skips rerank+self-check for simple questions), or a smaller `chat_model`. |
| Build: `nlohmann/json not found` | vcpkg manifest not used → pass `-DCMAKE_TOOLCHAIN_FILE=.../vcpkg.cmake`, or allow fetch: `-DDUMY_FETCH_MISSING=ON`. |

## 11. Extension points (already interfaces)

- `IVectorStore` → implement Qdrant/SQLite-vec backends without touching the pipeline.
- OCR hook in `pdf_loader.cpp` (§9).
- The REST contract is website-ready: add auth/reverse-proxy in front, keep
  `session_id` in the browser, stream via `/chat/stream`.

## 12. Honest limitations (current state)

1. **SearXNG is not running on this machine**, so every question uses the
   Wikipedia fallback. Wikipedia rate-limits bursts (HTTP 429) — the code backs
   off and retries twice, then continues with PDFs + own knowledge and says so.
   For full-strength web search start SearXNG once (§7); then all 5–8 result
   slots come from it and Wikipedia is only a backup.
2. **Key-free means Wikipedia-only fallback** (az/en) — no Google/Bing by design.
3. **Reranking uses `models.draft`, not the helper.** qwen2.5:3b produced noise
   scores on long Azerbaijani medical texts (10,2,2,1… and later 3,2,0… on the
   same question), which destroyed valid context. The draft model scores
   reliably and is loaded right before drafting anyway, so there is no extra
   model swap — the cost is 14–24 s on broad/diagnosis questions (the helper
   still does expansion, map-reduce summaries and the self-check).
4. **Query expansion occasionally needs one retry**: qwen2.5:3b sometimes
   answers with an array of pairs `[["az","en"],…]` instead of flat strings —
   the extractor flattens both forms and retries with a sharper reminder.
5. **OCR is an extension point, not a built feature** (`ocr_enabled: false`):
   51 low-text pages are counted but not OCR'd until Tesseract is installed
   (§9) and `run_ocr()` is implemented.
6. **No authentication** — the server binds 127.0.0.1 and upload is
   localhost-only. Do not expose it to a network without a reverse proxy.
7. **Azerbaijani grammar** from 3–7B models occasionally wobbles; the self-check
   (broad/diagnosis/comparison only) fixes unsupported claims and invented
   numbers, not style. `models.draft` is swappable — see the benchmark above.
8. **Benchmark caveats**: qwen3:8b produced the most structured answers but
   missed every time target on 6 GB VRAM; llama3.1:8b timed out twice. Read
   `benchmark/*.json` before changing the default.
