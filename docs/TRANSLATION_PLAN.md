# DUMY — Translation Plan (downloaded agronomy books → Azerbaijani)

Documentation only. **No translation code is written here.** This plan describes how to
translate the downloaded agronomy / livestock books into Azerbaijani using **local models
only** (Ollama, no cloud, no paid APIs), while keeping the page numbers that the RAG
citations depend on (`[Mənbə: <file>, səh. N]`).

Measured baseline on the target machine (RTX 4050 6 GB, `OLLAMA_MAX_LOADED_MODELS=1`):

| Item | Value |
|---|---|
| Draft model (qwen2.5:7b, Q4_K_M) | ≈ 18 tokens/s generation, ≈ 1 500 tokens/s prompt eval |
| Small model (qwen2.5:3b, Q4_K_M) | ≈ 80 tokens/s generation |
| Embedding model (bge-m3) | ≈ 30–60 ms per short text once resident |
| Corpus | 23 PDFs, ≈ 6 900 pages, 6 751 chunks indexed |
| Model load/swap | ≈ 3–13 s each (single-model limit ⇒ every model switch reloads) |

> All numbers below are estimates derived from those measurements; treat them as planning
> figures (±40 %), not guarantees.

---

## 1. Why a separate offline pipeline

The chat runtime must stay small and fast. Translating ~6 900 pages is a one-off batch job
measured in **hours**, so it must run **outside** the request path, write plain files, and be
**resumable**. The live index is only rebuilt after a translated book passes the quality
gate.

---

## 2. Pipeline overview

```
PDF ──(1) extract + page markers──► page-tagged text
    ──(2) language detect / skip already-AZ──► to-translate pages
    ──(3) chunk into translation units (≤ N tokens)──► units
    ──(4) glossary match + prompt build──► prompts
    ──(5) local Ollama translate──► AZ units
    ──(6) quality checks (back-translation, glossary, numbers)──► accept / retry / flag
    ──(7) rebuild page-tagged AZ text + re-index into RAG──► searchable AZ book
```

Each stage writes its output to disk so the job can be resumed after a crash or a reboot.

---

## 3. Stage 1 — PDF text extraction with page numbers preserved

**Citations depend on page numbers; losing them makes the book useless for RAG.**

Rules:
- Extract **page by page**, keeping a hard boundary between pages. Never let a paragraph
  span two pages without recording both page numbers.
- Emit a machine-readable format with an explicit page marker, e.g. a lightweight
  page-tagged text file:
  ```
  <<<PAGE 161>>>
  ...text of page 161...
  <<<PAGE 162>>>
  ...text of page 162...
  ```
  (A JSONL variant — one object per page `{ "page": 161, "text": "..." }` — is equally fine
  and easier to diff. Pick one and freeze it.)
- Reuse the **existing `pdf_loader` (poppler-cpp)** — it already extracts per page and is
  already a project dependency. Do **not** add a second PDF library.
- Record the extractor's own page numbering and **verify it against the PDF's printed page
  number** on a sample. Offsets of 1–2 pages are common when a book has a cover / TOC; store
  the offset per file so citations match the *printed* page a farmer would look up.
- Flag **low-text pages** exactly like the current indexer does (`scanned_pages`): pages with
  almost no extractable text are likely scans and need OCR or manual handling. Do not
  silently translate an empty page.
- Keep the **original file name** in the output path/metadata: it becomes the citation label.

**Acceptance for this stage:** for each source PDF, `page_count` matches, page 1 and the last
page are non-empty (unless genuinely scanned), and a random sample of 5 pages is visually
identical to the PDF text.

---

## 4. Stage 2 — Language detection and skip rules

- Detect the source language per page (the corpus mixes **Azerbaijani (Latin)**, **Russian
  (Cyrillic)** and some **Turkish**).
- **Skip pages already in Azerbaijani** — never translate AZ→AZ. A cheap heuristic + a short
  model check is enough: if the Latin-letter ratio is high and the text is not Russian/Turkish
  (look for `ə`, `ı`, and characteristic AZ endings), mark it `already_az` and copy it
  verbatim.
- Mark Russian/Turkish pages for full translation to AZ.
- Store the detected language per page so QA can compare like with like.

---

## 5. Stage 3 — Chunking for translation

Translation chunks are **not** the same as RAG chunks; keep two separate passes.

- Boundaries: never split mid-sentence. Prefer paragraph → sentence → clause.
- Size: **~6–10 sentences or ≤ 900 source tokens** per unit. Larger units improve coherence
  but a 6 GB GPU forces smaller `num_ctx`, so keep units well inside the context window
  (target `num_ctx` 4 096 for the translation model, leaving room for prompt + glossary).
- Carry a **1–2 sentence overlap** into the next unit for coreference, but mark overlap text so
  it is de-duplicated when the units are reassembled.
- Keep a table `unit_id → (file, page_start, page_end)` so the AZ text can be rebuilt back
  into the page-tagged file with page markers intact.

---

## 6. Stage 4 — Glossary (agronomy / livestock terms)

A **glossary is mandatory** for this domain: a generic model will otherwise render the same
term three different ways across a book, which wrecks retrieval and reading.

- Seed a bilingual file (`source term → canonical Azerbaijani term`) covering at least:
  - anatomy/physiology: `рубец→işgə` (rumen), `лактика→laktasiya`, `жвачка→geyvəmə`;
  - diseases: `ящур→dabaq`, `бруцеллёз→brusellyoz`, `мастит→mastit`;
  - nutrition/feed: `комбикорм→qarışıq yem`, `силос→silos`, `протеин→zülal`;
  - agronomy: `севооборот→əkin növbəliliyi`, `вспашка→şumlama`, `удобрение→gübrə`;
  - chemicals/doses: drug, pesticide and vaccine names — **keep the international name and the
    numeric dose unchanged**, only translate the surrounding text.
- Sources for the glossary: the already-Azerbaijani books in `pdfs/` (e.g. *Heyvandarlığın
  əsasları*, *KƏND TƏSƏRRÜFATI HEYVANLARININ YEMLƏNDİRİLMƏSİ*), plus fixed terminology from
  FAO/WHO-style Azerbaijani texts. Build it **by hand + assisted extraction**, then freeze it.
- Inject the *relevant subset* of the glossary into each prompt (only terms present in that
  unit) to keep context small. After each unit, run a **glossary-compliance check** and
  re-translate units that violate it.
- Grow the glossary as new terms appear; never let two spellings of the same term both ship.

---

## 7. Stage 5 — Translation with local models

- Model choice: **qwen2.5:7b** for quality, **qwen2.5:3b** for a fast first pass (see the time
  table). Consider a **two-pass** approach only if quality demands it: 3b draft → 7b refine —
  it roughly doubles the time, so gate it behind the QA score.
- Request shape (Ollama `/api/chat`): low temperature (`0.1–0.2`), `think:false`, a strict
  system prompt that says *translate, do not summarise, do not add, keep numbers/doses/units
  identical, keep the given glossary, output only the translation*.
- **Numbers, doses, dates and proper names are copied verbatim.** A number changed in
  translation is a safety bug in a veterinary text.
- Keep `keep_alive` long and, because of the single-model limit, **batch by model** (do all
  translation with one model before switching) to avoid reloading on every unit.
- Persist after **every unit** (append-only output), so a crash/reboot costs at most one unit.
- Log per-unit tokens, wall time and the model's load time (same pattern as the chat stage
  timings) so throughput regressions are visible.

---

## 8. Stage 6 — Quality checks (the safety net)

Run these per unit and score them; keep the scores in a sidecar file:

1. **Number/dose equality** — every number, unit, percentage and drug dose in the source must
   appear unchanged in the translation. Any diff ⇒ fail.
2. **Glossary compliance** — forbidden source terms must not survive; required AZ terms must
   appear.
3. **Length ratio** — AZ/`source` word ratio within a sane band (e.g. 0.85–1.25). Outside it ⇒
   either summarised or padded.
4. **Back-translation spot check** — translate a **random 5–10 %** of units AZ→source with
   the small model and compare key terms/numbers to the original. Full back-translation of
   everything is too slow to be worth it.
5. **Formatting** — the unit's page markers and paragraph breaks survive.
6. **Refusal/echo detection** — reject outputs that contain meta-text ("here is the
   translation…"), the prompt itself, or an empty/duplicated body.

Failed units go to a **retry queue** (one retry with a stricter prompt, then human review).
Nothing enters the RAG index until it passes checks 1–3 and 6.

---

## 9. Stage 7 — Re-index for RAG

Only after a book passes QA:

- Rebuild the **page-tagged AZ text**: concatenate accepted units in order, restore
  `<<<PAGE N>>>` markers, then run the **existing** chunker (`chunk_size 650`,
  `chunk_overlap 100`) so citation granularity matches the current corpus.
- Chunk metadata must point at the **translated file name** and page number, so labels read
  `[Mənbə: <Azerbaijani title>.pdf, səh. N]`.
- Store the translated PDF/text under the normal `pdfs/` + `data/` flow and let the existing
  ingest (`dumy --ingest`) embed it with `bge-m3`. **Do not reuse the source book's vectors** —
  meaning changes across languages.
- Keep the **source-language file indexed too** for questions asked in Russian/Turkish; mark
  translated and source as a linked pair to avoid double-citing the same passage.
- Re-run the project's answer-quality test after indexing to confirm retrieval still answers
  10/10 and citations still resolve to real pages.

---

## 10. Time estimate on 6 GB VRAM (single model loaded)

Approximate, assuming ~6 900 pages × ~350 words ≈ 2.4 M words ≈ **3.5–4 M output tokens**
(Azerbaijani is agglutinative, so output tokens run higher than English).

| Approach | Throughput (gen) | Raw translate | +QA/retries (~25 %) | +extraction/index | Realistic total |
|---|---|---|---|---|---|
| qwen2.5:7b, one pass | ≈ 18 tok/s | ≈ 55–70 h | ≈ 70–90 h | +1–2 h | **≈ 3–4 days** |
| qwen2.5:3b, one pass | ≈ 80 tok/s | ≈ 12–15 h | ≈ 16–20 h | +1–2 h | **≈ 17–22 h** |
| 3b draft → 7b refine | mixed | ≈ 70–85 h | ≈ 90–110 h | +1–2 h | **≈ 4–5 days** |

Notes that move these numbers:
- Because only **one model** is loaded at a time, model switching adds **minutes per switch**,
  not per unit — so **batch strictly by model and by pass**.
- A 6 GB card is the real ceiling: a 7B Q4 model at `num_ctx` 8 192 partly offloads to CPU and
  drops to ~18 tok/s. Larger context ⇒ slower, so keep translation `num_ctx` modest (4 096).
- Overnight batches are the practical unit of work: start a pass, let it run for hours,
  resume from the append-only output.
- **Realistic recommendation:** translate with **qwen2.5:3b first**, QA, and only send the
  flagged low-score units to qwen2.5:7b. That lands near **~1 day** of wall-clock for the
  full corpus with quality close to the 7b-only run.

---

## 11. Is a Python helper script justified for this offline step?

**Recommendation: no — prefer a small offline C++ mode (e.g. `dumy --translate`) and keep the
toolchain single-language.**

Reasons:
- The project has **zero Python today**; adding it introduces a second ecosystem, a second
  dependency file and a second way for Azerbaijani UTF-8 to break. The stack is locked to
  C++17 + Ollama, and a one-off offline tool is still part of that stack.
- Everything this job needs already exists in C++: **poppler-cpp** (`pdf_loader`) for
  page-tagged extraction, **libcurl** for the Ollama HTTP calls (`ollama_client`), the
  chunker, and the RAG indexer. A Python helper would **re-implement** PDF extraction and the
  Ollama client for no benefit.
- The batch job is I/O- and model-bound, not language-bound — C++ adds no meaningful
  development cost here and avoids duplication.

A Python script would only be defensible for a **throwaway QA probe** (e.g. eyeballing PDF
extraction quality or plotting throughput), run once and deleted. Even that is optional, since
the same can be done with `curl` + the existing server logs. **Keep the offline translator in
C++.**

---

## 12. Failure modes and rollback

- **Crash / power loss** → resume from the append-only per-unit output; never re-translate
  finished units.
- **Bad model output** → retry queue with a stricter prompt; after two failures, flag the unit
  for manual review. A single bad unit must not block a whole book.
- **Wrong page offset** → detected by the printed-page check in Stage 1; fix the per-file
  offset and re-emit markers **before** indexing.
- **Quality regression in chat answers** → keep the source-language books indexed and the
  previous `data/index.bin` backed up; re-indexing is reversible by restoring the backup and
  re-running `dumy --ingest`.
- **Never** publish a translated book that failed the number/dose check — in a veterinary
  text, a wrong dose is worse than no translation.

---

## 13. Execution checklist

- [ ] Freeze the page-tagged text format (`<<<PAGE N>>>` or per-page JSONL).
- [ ] Extract every PDF with poppler, verify page counts and printed-page offsets.
- [ ] Detect per-page language; mark AZ pages as `already_az`.
- [ ] Build and freeze the agronomy glossary (v1); add the compliance checker.
- [ ] Chunk into translation units with 1–2 sentence overlap; persist `unit → pages`.
- [ ] Translate all units with **qwen2.5:3b** (batched, resumable, `think:false`, temp 0.1).
- [ ] QA: numbers, glossary, length ratio, 5–10 % back-translation, formatting, refusal.
- [ ] Send low-score units to **qwen2.5:7b** refine; re-QA.
- [ ] Rebuild page-tagged AZ text → existing chunker → `dumy --ingest` (bge-m3).
- [ ] Re-run the 10-question answer-quality test; confirm citations resolve to real pages.
