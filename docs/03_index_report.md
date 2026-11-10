# PDF Index Report

**Date**: 2026-11-05
**Source**: `data/manifest.json` (generated after last ingestion)

---

## Summary

| Metric | Value |
|--------|-------|
| Total PDFs | 23 |
| Total Chunks | 6,751 |
| Total Pages | ~6,878 |
| Total Scanned Pages | 48 |
| Embedding Model | `bge-m3` |
| Chunk Size | 650 tokens |
| Chunk Overlap | 100 tokens |
| Index File | `data/index.bin` (43 MB) |

---

## PDF Index Table

| # | PDF Filename | Chunks | Pages | Scanned Pages | Status |
|---|--------------|--------|-------|---------------|--------|
| 1 | AQRONOMİYANIN əsasları.pdf | 353 | 353 | 0 | ✅ ok |
| 2 | ARIF_YDX.pdf | 43 | 46 | 2 | ✅ ok |
| 3 | Antropozoonoz xəstəlikləri.pdf | 511 | 520 | 0 | ✅ ok |
| 4 | Famil Nəsibov Klinik diqanostika.pdf | 223 | 217 | 0 | ✅ ok |
| 5 | HEYVAN BİOKİMYASI.pdf | 485 | 492 | 7 | ✅ ok |
| 6 | Heyvandarlığın əsasları.pdf | 329 | 330 | 0 | ✅ ok |
| 7 | Körpə kənd təsərrüfatı hüyvanlarının daxili xəstəlikləri.pdf | 189 | 185 | 0 | ✅ ok |
| 8 | KƏND TƏSƏRRÜFATI HEYVANLARININ YEMLƏNDİRİLMƏSİ.pdf | 292 | 293 | 1 | ✅ ok |
| 9 | KƏND TƏSƏRRÜFATI HEYVANLARININ YETİŞDİRİLMƏSİ (SON).pdf | 268 | 267 | 0 | ✅ ok |
| 10 | Patoloji fiziologiyadan təcrübə məşğələləri.pdf | 145 | 145 | 0 | ✅ ok |
| 11 | Qaramal naxırlarında-kitab A5.pdf | 277 | 277 | 0 | ✅ ok |
| 12 | Qoyunçuluq.pdf | 409 | 408 | 1 | ✅ ok |
| 13 | SAĞIM və SÜDÜN EMALINDA TEXMOLOGİYALAR... | 529 | 533 | 3 | ✅ ok |
| 14 | T.B. İsgəndərov Baytarlıq cərrahiyyəsi praktikum.pdf | 323 | 334 | 0 | ✅ ok |
| 15 | T.B.İsgəndərov xırda heyvanların xəstəlikləri.pdf | 317 | 318 | 1 | ✅ ok |
| 16 | T.B.İsgəndərov BAYTARLIQ ANESTEZIOLOGIYASI... | 164 | 184 | 22 | ✅ ok |
| 17 | T.B.İsgəndərov Baytarlıq oftalmologiyası.pdf | 85 | 82 | 0 | ✅ ok |
| 18 | T.B.İsgəndərov ümumi və xüsusi cərrahiyyə.pdf | 440 | 450 | 3 | ✅ ok |
| 19 | YEM HAZIRLANMASININ TEX. VƏ TEXNİKİ TƏMİNATINDA... | 564 | 565 | 0 | ✅ ok |
| 20 | Yem otları.pdf | 172 | 185 | 1 | ✅ ok |
| 21 | sud və süd məhsullarının ekspertizası.pdf | 180 | 188 | 7 | ✅ ok |
| 22 | İntensiv və Ekstensiv maldarlıq.pdf | 303 | 308 | 3 | ✅ ok |
| 23 | Şirəli yem bitkiləri - 2013.pdf | 150 | 153 | 0 | ✅ ok |
| **TOTAL** | | **6,751** | **~6,878** | **48** | |

---

## Scanned Pages Note

- **48 pages total** across 10 PDFs are scanned (image-based, not extractable text)
- Largest scanned portion: `T.B.İsgəndərov BAYTARLIQ ANESTEZIOLOGIYASI VƏ REANIMATOLOGIYASI.pdf` — 22 scanned pages
- OCR is **disabled** by default (`ocr_enabled: false` in config.json)
- Scanned pages contribute **0 searchable text** — chunks from these pages may be empty or garbage
- Recommendation: Enable OCR (`ocr_enabled: true`) and install Tesseract with `aze+eng` languages if these PDFs are critical

---

## Knowledge Base Coverage Assessment

| Domain | Coverage | Notes |
|--------|----------|-------|
| Livestock diseases (cattle, sheep, goats) | ✅ Excellent | 10+ PDFs, ~3000 chunks |
| Veterinary surgery/anesthesia | ✅ Good | 3 PDFs by İsgəndərov |
| Animal nutrition / feed | ✅ Good | 4 PDFs (yem, yem otları,喂养) |
| Milk/dairy technology | ✅ Good | 2 PDFs (sağım, süd ekspertizası) |
| Agronomy / crop production | ⚠️ Limited | 1 PDF (AQRONOMİYANIN əsasları) |
| **Crop diseases (tomato, wheat, etc.)** | ❌ **Missing** | **No PDFs on plant pathology** |
| Forage crops (şirəli yem bitkiləri) | ✅ Good | 1 dedicated PDF |

**Critical Gap**: The tomato question ("Pomidor yarpaqları niyə saralır?") has **zero relevant chunks** in the index. The knowledge base is overwhelmingly veterinary/livestock with almost no crop disease content.