// pdf_loader.cpp - poppler-cpp based page text extraction.
//
// Windows note: some poppler builds open files using the ANSI code page, so
// non-ASCII paths (e.g. "Qoyunçuluq.pdf") can fail. When a path contains
// non-ASCII bytes we transparently copy the file to an ASCII temp path and
// load that instead.
#include "dumy/pdf_loader.h"

#include <cctype>
#include <cstdio>
#ifndef _WIN32
#include <unistd.h>
#endif

#if DUMY_HAS_POPPLER
#include <poppler/cpp/poppler-document.h>
#include <poppler/cpp/poppler-page.h>
#endif

namespace dumy {
namespace {

bool has_non_ascii(const std::string& s) {
    for (unsigned char c : s)
        if (c >= 0x80) return true;
    return false;
}

}  // namespace

bool pdf_support_enabled() {
#if DUMY_HAS_POPPLER
    return true;
#else
    return false;
#endif
}

bool extract_pdf_pages(const fs::path& path, std::vector<PdfPage>& out, std::string* err) {
    out.clear();
    if (!pdf_support_enabled()) {
        if (err) {
            *err = "PDF support is disabled: the binary was built without poppler-cpp "
                   "(install libpoppler-cpp-dev / vcpkg 'poppler' and rebuild)";
        }
        return false;
    }
    if (!fs::exists(path)) {
        if (err) *err = "file not found: " + path.u8string();
        return false;
    }

#if DUMY_HAS_POPPLER
    fs::path load_path = path;
    fs::path temp_copy;
    const std::string u8 = path.u8string();

    if (has_non_ascii(u8)) {
        // Copy to an ASCII-only temp file so poppler can always open it.
        char tmpl[] = "dumy_pdf_XXXXXX";
#ifdef _WIN32
        fs::path tmp_dir = fs::temp_directory_path();
        temp_copy = tmp_dir / "dumy_pdf_tmp.pdf";
        std::error_code ec;
        fs::copy_file(path, temp_copy, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            if (err) *err = "cannot copy to temp file: " + ec.message();
            return false;
        }
#else
        int fd = mkstemp(tmpl);
        if (fd < 0) {
            if (err) *err = "cannot create temp file";
            return false;
        }
        close(fd);
        temp_copy = tmpl;
        std::error_code ec;
        fs::copy_file(path, temp_copy, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            if (err) *err = "cannot copy to temp file: " + ec.message();
            return false;
        }
#endif
        load_path = temp_copy;
    }

    std::string poppler_name = load_path.u8string();
    std::unique_ptr<poppler::document> doc(poppler::document::load_from_file(poppler_name));
    if (!doc) {
        if (!temp_copy.empty()) {
            std::error_code ec;
            fs::remove(temp_copy, ec);
        }
        if (err) {
            *err = "poppler could not open the PDF (damaged or encrypted?): " +
                   path.u8string();
        }
        return false;
    }
    if (doc->is_locked()) {
        if (!temp_copy.empty()) {
            std::error_code ec;
            fs::remove(temp_copy, ec);
        }
        if (err) *err = "PDF is password-protected: " + path.u8string();
        return false;
    }

    const int pages = doc->pages();
    if (pages <= 0) {
        if (err) *err = "PDF has no pages (or all pages are images): " + path.u8string();
        if (!temp_copy.empty()) {
            std::error_code ec;
            fs::remove(temp_copy, ec);
        }
        return false;
    }

    out.reserve(static_cast<size_t>(pages));
    for (int i = 1; i <= pages; ++i) {
        poppler::page* p = doc->create_page(i);
        if (!p) continue;
        // poppler-cpp returns a ustring; convert to UTF-8 bytes.
        const poppler::byte_array utf8 = p->text().to_utf8();
        delete p;
        PdfPage pp;
        pp.page = i;
        pp.text.assign(utf8.begin(), utf8.end());
        out.push_back(std::move(pp));
    }

    if (!temp_copy.empty()) {
        std::error_code ec;
        fs::remove(temp_copy, ec);
    }

    if (out.empty()) {
        if (err) *err = "no extractable text (probably a scanned PDF): " + path.u8string();
        return false;
    }
    return true;
#else
    // Unreachable: pdf_support_enabled() returned false above.
    return false;
#endif
}

// ---------------------------------------------------------------------------
// OCR extension point (optional, not built in v1)
// ---------------------------------------------------------------------------
bool page_looks_scanned(const std::string& page_text) {
    // Fewer than ~50 visible characters on a whole page means the page is
    // most likely an image that needs OCR.
    size_t visible = 0;
    for (unsigned char c : page_text)
        if (!std::isspace(c)) ++visible;
    return visible < 50;
}

bool ocr_available() {
    // Extension point: when compiled with -DDUMY_WITH_TESSERACT and linked
    // against tesseract, return true here.
    return false;
}

bool run_ocr(const fs::path&, int, const std::string&, std::string& out, std::string* err) {
    out.clear();
    if (err) {
        *err = "OCR is not built in (v1). Compile with Tesseract support "
               "(see README, 'OCR extension point').";
    }
    return false;
}

}  // namespace dumy
