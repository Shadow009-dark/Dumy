// pdf_loader.h - PDF text extraction (poppler-cpp) with an OCR extension point.
#pragma once

#include <string>
#include <vector>

#include "dumy/common.h"

namespace dumy {

struct PdfPage {
    int page = 0;        // 1-based page number
    std::string text;    // extracted (not yet normalized) text
};

// True when this binary was compiled with poppler-cpp.
bool pdf_support_enabled();

// Extracts text page by page. Returns false and fills `err` when the file
// cannot be read (damaged/encrypted/not a PDF) or PDF support is disabled.
bool extract_pdf_pages(const fs::path& path,
                       std::vector<PdfPage>& out,
                       std::string* err);

// --- OCR extension point (Tesseract) ---------------------------------------
// v1 ships this as a stub: scanned pages with (almost) no extractable text
// are reported so a future build can route them to OCR. Enable with
// -DDUMY_WITH_TESSERACT and implement run_ocr() in pdf_loader.cpp.
// `config.ocr_enabled` gates the behaviour at runtime.
bool page_looks_scanned(const std::string& page_text);
bool ocr_available();
bool run_ocr(const fs::path& pdf_path, int page, const std::string& lang,
             std::string& out, std::string* err);

}  // namespace dumy
