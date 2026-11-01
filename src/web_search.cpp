// web_search.cpp - SearXNG primary + Wikipedia fallback, page download with
// caching, per-host rate limiting and a parallel thread pool.
#include "dumy/web_search.h"

#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <unordered_set>

#include "dumy/common.h"
#include "dumy/html_cleaner.h"

namespace dumy {
namespace {

using Clock = std::chrono::steady_clock;

// --- tiny HTTP GET (libcurl) ----------------------------------------------
size_t write_body(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t n = size * nmemb;
    static_cast<std::string*>(userdata)->append(ptr, n);
    return n;
}

bool http_get(const std::string& url, int timeout_sec, std::string& body,
              std::string* content_type, std::string* err) {
    CURL* raw = curl_easy_init();
    if (!raw) {
        if (err) *err = "curl_easy_init failed";
        return false;
    }
    std::unique_ptr<CURL, void (*)(CURL*)> curl(raw, curl_easy_cleanup);
    body.clear();

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, write_body);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, static_cast<long>(timeout_sec));
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT,
                     "DUMY/1.0 (local agriculture research assistant)");

    CURLcode rc = curl_easy_perform(curl.get());
    long code = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &code);
    if (content_type) {
        char* ct = nullptr;
        if (curl_easy_getinfo(curl.get(), CURLINFO_CONTENT_TYPE, &ct) == CURLE_OK && ct)
            *content_type = ct;
    }
    if (rc != CURLE_OK) {
        if (err) *err = std::string("download failed: ") + curl_easy_strerror(rc);
        return false;
    }
    if (code < 200 || code >= 300) {
        if (err) *err = "HTTP " + std::to_string(code);
        return false;
    }
    return true;
}

// --- per-host politeness (rate limiting) ----------------------------------
std::mutex& rate_mutex() {
    static std::mutex m;
    return m;
}
std::map<std::string, Clock::time_point>& rate_map() {
    static std::map<std::string, Clock::time_point> m;
    return m;
}
std::string host_of(const std::string& url) {
    size_t p = url.find("://");
    if (p == std::string::npos) return url;
    size_t start = p + 3;
    size_t end = url.find('/', start);
    if (end == std::string::npos) end = url.size();
    return url.substr(start, end - start);
}
void be_polite(const std::string& url) {
    const std::string host = host_of(url);
    std::lock_guard<std::mutex> lk(rate_mutex());
    auto& t = rate_map()[host];
    auto now = Clock::now();
    if (t > now) std::this_thread::sleep_for(t - now);
    t = Clock::now() + std::chrono::milliseconds(300);
}

// --- disk cache ------------------------------------------------------------
std::string cache_path(const Config& cfg, const std::string& prefix, const std::string& key) {
    return (cfg.data_dir() / "web_cache" /
            (prefix + hash_hex(fnv1a64(key.data(), key.size())) + ".json"))
        .u8string();
}

long long now_unix() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool cache_read(const Config& cfg, const std::string& prefix, const std::string& key,
                nlohmann::json& out) {
    std::string raw;
    if (!read_text_file(fs::u8path(cache_path(cfg, prefix, key)), raw)) return false;
    try {
        out = nlohmann::json::parse(raw);
    } catch (...) {
        return false;
    }
    long long t = out.value("fetched_unix", 0LL);
    if (now_unix() - t > static_cast<long long>(cfg.web_cache_ttl_hours) * 3600)
        return false;
    return true;
}

void cache_write(const Config& cfg, const std::string& prefix, const std::string& key,
                 nlohmann::json payload) {
    payload["fetched_unix"] = now_unix();
    write_text_file(fs::u8path(cache_path(cfg, prefix, key)), payload.dump());
}

bool looks_like_binary(const std::string& body) {
    size_t check = std::min<size_t>(body.size(), 2048);
    for (size_t i = 0; i < check; ++i) {
        unsigned char c = static_cast<unsigned char>(body[i]);
        if (c == 0) return true;
    }
    return false;
}

bool url_is_download(const std::string& url) {
    static const char* exts[] = {".pdf", ".doc", ".docx", ".xls", ".xlsx", ".ppt",
                                 ".pptx", ".zip", ".rar", ".7z", ".jpg", ".jpeg",
                                 ".png", ".gif", ".svg", ".mp3", ".mp4", ".exe",
                                 ".apk", ".csv", ".epub"};
    std::string u = to_lower_utf8(url);
    size_t q = u.find('?');
    if (q != std::string::npos) u = u.substr(0, q);
    for (const char* e : exts)
        if (ends_with(u, e)) return true;
    return false;
}

}  // namespace

std::string url_encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else if (c == ' ') {
            out.push_back('+');
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}

WebSearch::WebSearch(const Config& cfg) : cfg_(cfg) {
    pool_ = std::make_unique<ThreadPool>(4);
}

bool WebSearch::is_trusted(const std::string& url) const {
    const std::string u = to_lower_utf8(url);
    for (const auto& d : cfg_.trusted_domains) {
        std::string dl = to_lower_utf8(d);
        if (dl.empty()) continue;
        if (dl[0] == '.') {
            if (u.size() > dl.size() &&
                u.compare(u.size() - dl.size(), dl.size(), dl) == 0)
                return true;
            // also match before a path separator: ".edu/" style hosts
            size_t pos = u.find(dl);
            while (pos != std::string::npos) {
                char after = (pos + dl.size() < u.size()) ? u[pos + dl.size()] : '/';
                if (after == '/' || after == ':' || after == '?') return true;
                pos = u.find(dl, pos + 1);
            }
        } else if (u.find(dl) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool WebSearch::searxng_available() const {
    if (cfg_.searxng_url.empty()) return false;
    static std::mutex m;
    static bool checked = false;
    static bool available = false;
    std::lock_guard<std::mutex> lk(m);
    if (checked) return available;
    std::string url = cfg_.searxng_url + "/search?q=dumy%20probe&format=json";
    std::string body, err;
    available = http_get(url, 3, body, nullptr, &err);
    checked = true;
    if (!available) {
        log_warn("SearXNG not reachable at " + cfg_.searxng_url +
                 " - falling back to Wikipedia (" + err + ")");
    }
    return available;
}

std::vector<WebResult> WebSearch::search_searxng(const std::string& q) const {
    std::vector<WebResult> out;
    if (!searxng_available()) return out;

    const std::string url = cfg_.searxng_url + "/search?q=" + url_encode(q) +
                            "&format=json&language=" +
                            (cfg_.search_languages.empty() ? "az"
                                                           : cfg_.search_languages[0]);
    nlohmann::json cached;
    std::string body, err;
    if (cache_read(cfg_, "search_", q + "|" + cfg_.searxng_url, cached)) {
        body = cached.value("body", std::string());
    } else {
        be_polite(url);
        if (!http_get(url, 10, body, nullptr, &err)) {
            log_warn("SearXNG search failed: " + err);
            return out;
        }
        cache_write(cfg_, "search_", q + "|" + cfg_.searxng_url,
                    nlohmann::json{{"body", body}});
    }

    try {
        auto j = nlohmann::json::parse(body);
        for (const auto& r : j.value("results", nlohmann::json::array())) {
            WebResult wr;
            wr.title = r.value("title", std::string());
            wr.url = r.value("url", std::string());
            wr.snippet = r.value("content", std::string());
            wr.engine = "searxng";
            if (!wr.url.empty()) out.push_back(std::move(wr));
            if (out.size() >= static_cast<size_t>(cfg_.web_top_results)) break;
        }
    } catch (const std::exception& e) {
        log_warn(std::string("SearXNG JSON parse error: ") + e.what());
    }
    return out;
}

// "Telefon nədir?" -> "Telefon": Wikipedia finds the actual article this way,
// while SearXNG keeps the natural question.
std::string simplify_wiki_query(std::string q) {
    static const char* suffixes[] = {" nədir?", " nedir?", " nədir", " nedir",
                                     " nə deməkdir?", " haqqında yaz", " haqqında", "?"};
    for (const char* suf : suffixes) {
        const size_t n = std::string(suf).size();
        while (q.size() > n && q.compare(q.size() - n, n, suf) == 0) {
            q.erase(q.size() - n);
            while (!q.empty() && q.back() == ' ') q.pop_back();
        }
    }
    return trim(q);
}

std::vector<WebResult> WebSearch::search_wikipedia(const std::string& q,
                                                   const std::string& lang,
                                                   std::string* err_out) const {
    std::vector<WebResult> out;
    const std::string url = "https://" + lang +
                            ".wikipedia.org/w/api.php?action=query&list=search&srsearch=" +
                            url_encode(q) + "&format=json&srlimit=6&utf8=1";

    nlohmann::json cached;
    std::string body, err;
    if (cache_read(cfg_, "search_", q + "|wiki-" + lang, cached)) {
        body = cached.value("body", std::string());
    } else {
        be_polite(url);
        bool got = http_get(url, 10, body, nullptr, &err);
        // Wikipedia rate-limits bursts (HTTP 429): back off twice, then give
        // up for this call - the pipeline continues with PDFs + own knowledge.
        const int backoff_ms[] = {1500, 4000};
        for (int attempt = 0; !got && err.find("429") != std::string::npos &&
                              attempt < 2; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms[attempt]));
            be_polite(url);
            err.clear();
            got = http_get(url, 10, body, nullptr, &err);
        }
        if (!got) {
            log_warn("Wikipedia search failed (" + lang + "): " + err);
            if (err_out) *err_out = err;
            return out;
        }
        cache_write(cfg_, "search_", q + "|wiki-" + lang,
                    nlohmann::json{{"body", body}});
    }

    try {
        auto j = nlohmann::json::parse(body);
        for (const auto& r :
             j.value("query", nlohmann::json::object()).value("search", nlohmann::json::array())) {
            WebResult wr;
            wr.title = r.value("title", std::string());
            std::string snip = r.value("snippet", std::string());
            wr.snippet = trim(html_to_text(snip));
            std::string t = wr.title;
            std::replace(t.begin(), t.end(), ' ', '_');
            wr.url = "https://" + lang + ".wikipedia.org/wiki/" + url_encode(t);
            wr.engine = "wikipedia-" + lang;
            if (!wr.url.empty()) out.push_back(std::move(wr));
        }
    } catch (const std::exception& e) {
        log_warn(std::string("Wikipedia JSON parse error: ") + e.what());
    }
    return out;
}

std::vector<WebResult> WebSearch::search(const std::vector<std::string>& queries,
                                          std::string* err) const {
    std::vector<WebResult> merged;
    std::unordered_set<std::string> seen;
    size_t searx_hits = 0;

    for (const auto& q : queries) {
        if (q.empty()) continue;
        auto rs = search_searxng(q);
        searx_hits += rs.size();
        for (auto& r : rs) {
            if (seen.insert(r.url).second) merged.push_back(std::move(r));
        }
    }

    // Fallback: whenever SearXNG gave nothing (down, no JSON format, no hits),
    // ask Wikipedia in the configured languages. Kept deliberately gentle:
    // at most 3 sub-queries, one (retried) request each, so a burst of
    // questions does not trip Wikipedia's anonymous rate limit (HTTP 429).
    if (merged.empty()) {
        std::string wiki_err;
        int wiki_calls = 0;
        for (const auto& q : queries) {
            if (q.empty()) continue;
            if (++wiki_calls > 3) break;
            const std::string wq = simplify_wiki_query(q);
            if (wq.empty()) continue;
            for (const auto& lang : cfg_.search_languages) {
                std::string werr;
                for (auto& r : search_wikipedia(wq, lang, &werr)) {
                    if (seen.insert(r.url).second) merged.push_back(std::move(r));
                }
                if (!werr.empty()) wiki_err = werr;
                if (merged.size() >= static_cast<size_t>(cfg_.web_top_results)) break;
            }
            if (merged.size() >= static_cast<size_t>(cfg_.web_top_results)) break;
            if (!wiki_err.empty() && wiki_err.find("429") != std::string::npos) {
                break;  // rate limited: stop hammering Wikipedia for now
            }
        }
        if (!merged.empty()) {
            log_info("Web search answered from Wikipedia fallback (" +
                     std::to_string(merged.size()) + " results)");
        } else if (err) {
            *err = "web search returned no results (SearXNG down/unconfigured" +
                   std::string("; Wikipedia: ") +
                   (wiki_err.empty() ? std::string("no matches") : wiki_err) + ")";
        }
    }
    (void)searx_hits;
    if (merged.size() > static_cast<size_t>(cfg_.web_top_results) * 2)
        merged.resize(static_cast<size_t>(cfg_.web_top_results) * 2);
    return merged;
}

std::string WebSearch::fetch_url(const std::string& url, std::string* content_type,
                                 std::string* err) const {
    std::string body;
    be_polite(url);
    if (!http_get(url, cfg_.web_fetch_timeout, body, content_type, err)) return "";
    if (looks_like_binary(body)) {
        if (err) *err = "binary content";
        return "";
    }
    return body;
}

std::vector<WebPage> WebSearch::fetch_pages(const std::vector<WebResult>& results,
                                            size_t max_pages) const {
    std::vector<WebResult> todo;
    std::unordered_set<std::string> seen;
    for (const auto& r : results) {
        if (r.url.empty() || url_is_download(r.url)) continue;
        if (!seen.insert(r.url).second) continue;
        todo.push_back(r);
        if (todo.size() >= max_pages) break;
    }

    std::vector<std::future<WebPage>> futures;
    futures.reserve(todo.size());
    for (const auto& r : todo) {
        futures.push_back(pool_->submit([this, r] {
            WebPage page;
            page.result = r;

            // 1) disk cache
            nlohmann::json cached;
            if (cache_read(cfg_, "page_", r.url, cached)) {
                page.text = cached.value("text", std::string());
                if (!page.text.empty()) {
                    page.ok = true;
                    return page;
                }
            }

            // 2) download
            std::string ctype, err;
            std::string body = fetch_url(r.url, &ctype, &err);
            if (body.empty()) {
                page.error = err.empty() ? "empty response" : err;
                return page;
            }
            // 3) clean (works for both HTML and plain text)
            std::string text = html_to_text(body);
            if (text.size() < 400) {
                page.error = "page too short (" + std::to_string(text.size()) +
                             " chars) - possibly not a content page";
                return page;
            }
            page.text = std::move(text);
            page.ok = true;
            cache_write(cfg_, "page_", r.url, nlohmann::json{{"text", page.text}});
            return page;
        }));
    }

    std::vector<WebPage> pages;
    for (auto& f : futures) {
        try {
            WebPage p = f.get();
            if (p.ok) pages.push_back(std::move(p));
        } catch (const std::exception& e) {
            log_warn(std::string("page fetch task failed: ") + e.what());
        }
    }
    return pages;
}

}  // namespace dumy
