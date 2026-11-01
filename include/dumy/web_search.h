// web_search.h - key-free web search + page download for the RAG pipeline.
//
// Primary source : local SearXNG  (http://localhost:8080/search?q=...&format=json)
// Fallback       : Wikipedia API (az, ru, en)
// Pages are downloaded with libcurl (10 s timeout), stripped with
// html_cleaner, cached on disk and fetched in parallel through a thread pool.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "dumy/config.h"
#include "dumy/thread_pool.h"

namespace dumy {

struct WebResult {
    std::string title;
    std::string url;
    std::string snippet;
    std::string engine;  // "searxng" | "wikipedia-az" | "wikipedia-en" | ...
};

struct WebPage {
    WebResult result;
    std::string text;    // cleaned page text (may be long)
    bool ok = false;
    std::string error;
};

class WebSearch {
public:
    explicit WebSearch(const Config& cfg);

    // Queries every language/engine, merges and deduplicates by URL.
    // Returns an empty vector (never throws) when everything is down;
    // `err` then describes why.
    std::vector<WebResult> search(const std::vector<std::string>& queries,
                                  std::string* err) const;

    // Downloads up to `max_pages` pages in parallel (cached on disk).
    std::vector<WebPage> fetch_pages(const std::vector<WebResult>& results,
                                     size_t max_pages) const;

    bool is_trusted(const std::string& url) const;
    bool searxng_available() const;

private:
    std::vector<WebResult> search_searxng(const std::string& q) const;
    std::vector<WebResult> search_wikipedia(const std::string& q,
                                            const std::string& lang,
                                            std::string* err_out = nullptr) const;
    std::string fetch_url(const std::string& url, std::string* content_type,
                          std::string* err) const;

    const Config& cfg_;
    mutable std::unique_ptr<ThreadPool> pool_;
};

// Percent-encodes a query string for URLs (UTF-8 safe).
std::string url_encode(const std::string& s);

}  // namespace dumy
