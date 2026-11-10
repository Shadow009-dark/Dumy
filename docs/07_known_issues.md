# DUMY — known issues (current state)

Last updated: 2026-11-10, after the production pass (phases 1–8).

## Model / hardware limits (cannot be fixed in code)

- **One 4B chat model on 6 GB VRAM.** `gemma3:4b` uses ~2.9 GB and `bge-m3`
  ~1.2 GB. Loading a second chat model evicts the first (measured), so the
  helper/self-check/draft roles all use the same model and only one chat model
  may be resident. Multi-model quality gains are not available on this GPU.
- **`OLLAMA_MAX_LOADED_MODELS=2`** is deliberate: `bge-m3` must stay resident on
  CPU next to the chat model. Setting it to 1 evicts the embedding model and
  adds seconds to every retrieval.
- **Answer style.** A 4B model still produces occasional awkward Azerbaijani
  (`15 x 12 edə 180 edir`) and may add unrequested explanation. The quality gate
  rejects loops, empty/clarification-only answers and domain drift, not style.
- **Long inputs.** Oversized questions are trimmed to one chunk and answered;
  the answer can be vague because the model sees little context. No raw HTTP
  400/token error is shown any more, but quality on such inputs is limited.

## Data / retrieval

- **Scanned PDF pages.** Roughly 48 pages have too little text and are skipped
  (`ocr_enabled=false`). Installing Tesseract + `aze.traineddata` would be the
  next quality gain; it is not bundled.
- **Rerank/expansion** are skipped for general/short messages by design. For
  domain questions the local reranker can still add 1–3 s.
- **Feed calculator** handles explicit head count + kg/day + days in any order
  (`90 000 kq` for 120 × 25 × 30). If the daily ration is missing it uses a
  clearly labelled assumption (25 kg/head/day); it is not a nutrition model.
- **ASCII normalization** is dictionary/rule based. Rare words, proper names and
  dialect spellings may stay unchanged; numbers, units and valid Azerbaijani
  words are left intact. A bare ASCII sentence is corrected locally, so it is
  fast but limited to the dictionary's vocabulary.

## Web search

- **DuckDuckGo HTML** is key-free but unofficial: markup or anti-bot changes
  can return zero results. It fails fast (1 s connect / 3 s total) and falls
  back to Wikipedia, then to PDFs/general knowledge. The `[İnternet]` label is
  only added when web text was actually used.
- `web_kill_switch` in `config.json` disables all web access.
- Web answers depend on the machine having internet access; the test suite does
  not require it.

## API / deployment

- **`api_key` is empty by default** so the local UI keeps working. Set
  `DUMY_API_KEY` (environment) or `api_key` in `config.json` before exposing
  the server. Never commit a real key.
- Rate limiting is an in-memory fixed one-minute window per IP. It resets on
  restart and is not shared between processes.
- `/upload` and `/ingest` are loopback-only. A reverse proxy must therefore run
  on the same machine; remote reindexing/upload is intentionally unavailable.
- CORS reflects only `allowed_origins` (default localhost:8085). A website on a
  tunnel domain must be added there explicitly.
- The server itself is plain HTTP; TLS must be terminated by the tunnel or
  reverse proxy.

## Repository

- `data_backup_20261013_221930/` (~42 MB) is untracked and ignored; it awaits a
  decision. `db/` still contains the active JSON index; only the unused Chroma
  prototype files were removed.
