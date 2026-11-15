# Adding curated knowledge (Tier 2)

`knowledge/common_sense.json` is the curated supplement. It answers **before**
the model runs: matching is deterministic, stem-based on folded Azerbaijani
tokens, and the stored text is returned verbatim (a few milliseconds).

The file is hot-reloaded: `KnowledgeBase` remembers the file's mtime and
re-reads it on the next question when it changes. No restart is needed.

## Format

```json
{
  "version": 1,
  "entries": [
    {
      "id": "unique_snake_case_id",
      "keywords": ["cuce", "benovseyi"],
      "combos": [["cuce", "toyuq"], ["benovseyi", "mavi"]],
      "answer": "Qısa, təsdiqlənmiş Azərbaycan dilində cavab.",
      "tags": ["heyvan", "rəng"],
      "safe": true,
      "confidence": "high"
    }
  ]
}
```

- `keywords` — any one match is enough (folded, first 5 letters compared).
- `combos` — optional; **every** inner list must match at least one token.
  Use it for "animal word × colour word" style questions.
- `answer` — 1-3 sentences, no source tags, no markdown.
- `safe` — keep `true`; only reviewed, uncontroversial facts belong here.
- `confidence` — `high` for reviewed facts.

## Examples

```json
{
  "id": "egg_incubation",
  "keywords": ["inkubasiya", "kurtum"],
  "answer": "Toyuq yumurtasının inkubasiya müddəti adətən 21 gün olur."
}
```

```json
{
  "id": "animal_unnatural_color",
  "combos": [["cuce", "toyuq", "quzu"], ["benovseyi", "mavi", "cehrayi"]],
  "answer": "Təbii cücələr belə rəngdə olmur; çox güman ki, boyayıblar."
}
```

Answers from this file appear with the UI chip **Əlavə bilik** and
`source.type = "knowledge"`, never as a PDF citation.
