// weather_tool.cpp - deterministic weather intent + OpenWeatherMap lookup.
//
// Design notes (why regex instead of a helper model):
//   * zero latency (no extra model call before the draft),
//   * works when Ollama is down or offline,
//   * unit-testable without a server, a model or the network.
// A 4B helper model is not reliable enough to classify "sabah Gəncədə yağış
// yağacaq?" and it would add a second model load per question.
#include "dumy/weather_tool.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

#include <curl/curl.h>

#include "dumy/common.h"

namespace dumy {
namespace {

// --- String helpers --------------------------------------------------------
size_t write_to_string(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t n = size * nmemb;
    static_cast<std::string*>(userdata)->append(ptr, n);
    return n;
}

// A byte is "word" if it is ASCII alnum or part of a UTF-8 sequence.
bool word_byte(unsigned char c) {
    return std::isalnum(c) != 0 || (c & 0x80) != 0;
}

// True when `needle` starts a word in `hay`, so "yağış" matches "yağışlı" but
// "hava" does not match "havalandırma" (that case is excluded explicitly).
bool starts_word(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return false;
    size_t pos = hay.find(needle);
    while (pos != std::string::npos) {
        if (pos == 0 || !word_byte(static_cast<unsigned char>(hay[pos - 1]))) return true;
        pos = hay.find(needle, pos + 1);
    }
    return false;
}

bool contains_any(const std::string& hay, const std::vector<std::string>& needles) {
    for (const auto& n : needles) {
        if (starts_word(hay, n)) return true;
    }
    return false;
}

std::string fmt_num(double v, int prec = 1) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(prec) << v;
    std::string s = os.str();
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s;
}

std::string local_time_string(long long unix_ts) {
    std::time_t t = static_cast<std::time_t>(unix_ts);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::ostringstream os;
    os << std::put_time(&tm_buf, "%d.%m.%Y %H:%M");
    return os.str();
}

// "2026-11-20 15:00:00" -> "20.11 15:00"
std::string short_dt_txt(const std::string& dt_txt) {
    if (dt_txt.size() < 16) return dt_txt;
    return dt_txt.substr(8, 2) + "." + dt_txt.substr(5, 2) + " " + dt_txt.substr(11, 5);
}

std::string json_str(const nlohmann::json& j, const char* key,
                     const std::string& fallback = std::string()) {
    if (j.contains(key) && j[key].is_string()) return j[key].get<std::string>();
    return fallback;
}

double json_num(const nlohmann::json& j, const char* key, double fallback = 0.0) {
    if (j.contains(key) && j[key].is_number()) return j[key].get<double>();
    return fallback;
}

// Known Azerbaijani places -> OpenWeatherMap spelling. `aliases` is a
// pipe-separated list of lower-case spellings (Azerbaijani + typed without
// diacritics + Latin). Matching is longest-first, so "Qubadlı" wins over "Quba".
struct CityEntry {
    const char* api;
    const char* aliases;
};

const CityEntry kCities[] = {
    {"Baku", "bakı|baki"},
    {"Ganja", "gəncə|gence|ganja"},
    {"Sumqayit", "sumqayıt|sumqayit"},
    {"Mingachevir", "mingəçevir|mingecevir"},
    {"Shirvan", "şirvan|sirvan|shirvan"},
    {"Nakhchivan", "naxçıvan|naxcivan|nahchivan"},
    {"Sheki", "şəki|seki|sheki"},
    {"Lankaran", "lənkəran|lenkeran|lankaran"},
    {"Yevlakh", "yevlax|yevlakh"},
    {"Khankendi", "xankəndi|xankendi"},
    {"Shusha", "şuşa|susa|shusha"},
    {"Shamakhi", "şamaxı|samaxi|shamakhi"},
    {"Quba", "quba|quba"},
    {"Qusar", "qusar"},
    {"Khachmaz", "xaçmaz|xacmaz"},
    {"Zaqatala", "zaqatala"},
    {"Qakh", "qax"},
    {"Balakan", "balakən|balaken"},
    {"Oghuz", "oğuz|oguz"},
    {"Goychay", "göyçay|goycay"},
    {"Agdash", "ağdaş|agdash"},
    {"Ujar", "ucar|ujar"},
    {"Kurdamir", "kürdəmir|kurdemir|kurdamir"},
    {"Imishli", "i̇mişli|imişli|imishli"},
    {"Sabirabad", "sabirabad"},
    {"Saatli", "saatlı|saatli"},
    {"Hajigabul", "hacıqabul|haciqabul"},
    {"Salyan", "salyan"},
    {"Neftchala", "neftçala|neftcala"},
    {"Bilasuvar", "biləsuvar|bilesuvar"},
    {"Jalilabad", "cəlilabad|celilabad"},
    {"Masalli", "masallı|masalli"},
    {"Astara", "astara"},
    {"Lerik", "lerik"},
    {"Yardimli", "yardımlı|yardimli"},
    {"Fuzuli", "füzuli|fuzuli"},
    {"Jabrayil", "cəbrayıl|cebrayil"},
    {"Zangilan", "zəngilan|zengilan"},
    {"Gubadli", "qubadlı|qubadli"},
    {"Lachin", "laçın|lacin"},
    {"Kalbajar", "kəlbəcər|kelbecer"},
    {"Agdam", "ağdam|agdam"},
    {"Barda", "bərdə|berde"},
    {"Tartar", "tərtər|terter"},
    {"Goranboy", "goranboy"},
    {"Samukh", "samux"},
    {"Shamkir", "şəmkir|semkir|shamkir"},
    {"Tovuz", "tovuz"},
    {"Gazakh", "qazax|gazakh"},
    {"Agstafa", "ağstafa|agstafa"},
    {"Dashkasan", "daşkəsən|daskesen"},
    {"Gadabay", "gədəbəy|gedebey"},
    {"Khizi", "xızı|xizi"},
    {"Siyazan", "siyəzən|siyezen"},
    {"Shabran", "şabran|sabran"},
    {"Gobustan", "qobustan"},
    {"Absheron", "abşeron|abseron"},
    {"Khirdalan", "xırdalan|xirdalan"},
    {"Naftalan", "naftalan"},
    {"Goygol", "göygöl|goygol"},
    {"Agjabadi", "ağcabədi|agcabedi"},
    {"Beylagan", "beyləqan|beyleqan"},
    {"Khojaly", "xocalı|xocali"},
    {"Khojavend", "xocavənd|xocavend"},
    {"Ordubad", "ordubad"},
    {"Julfa", "cülfa|culfa"},
    {"Shahbuz", "şahbuz|sahbuz"},
    {"Sharur", "şərur|serur"},
    {"Sadarak", "sədərək|sederek"},
};

// Without an API key a weather answer is impossible. Such a failure must not
// swallow an agronomy question that merely tripped a keyword, so it is only
// reported when the question really asks about weather: it names a place or
// asks for a forecast.
bool explicit_weather_request(const std::string& question) {
    const std::string q = to_lower_utf8(question);
    if (q.find("proqnoz") != std::string::npos) return true;
    if (q.find("prognoz") != std::string::npos) return true;
    if (q.find("sabah") != std::string::npos) return true;
    for (const auto& e : kCities) {
        std::stringstream ss(e.aliases);
        std::string alias;
        while (std::getline(ss, alias, '|')) {
            if (!alias.empty() && q.find(alias) != std::string::npos) return true;
        }
    }
    return false;
}

}  // namespace

WeatherTool::WeatherTool(const Config& cfg) : cfg_(cfg) {}

nlohmann::json WeatherResult::to_json() const {
    return nlohmann::json{{"requested", requested}, {"ok", ok},
                          {"forecast", forecast},   {"cache_hit", cache_hit},
                          {"http_code", http_code}, {"city", city},
                          {"city_display", city_display},
                          {"error", error},         {"message", message},
                          {"block", block}};
}

// ---------------------------------------------------------------------------
// Intent detection
// ---------------------------------------------------------------------------
bool WeatherTool::is_weather_question(const std::string& question) {
    const std::string q = to_lower_utf8(question);
    if (q.empty()) return false;

    // "havalandırma" (ventilation) is farm equipment, not weather.
    const bool vent = q.find("havalandır") != std::string::npos ||
                      q.find("havalandir") != std::string::npos;
    if (!vent && starts_word(q, "hava")) return true;  // hava / havanın / havada

    if (contains_any(q, {"proqnoz", "prognoz"})) return true;
    if (contains_any(q, {"temperatur", "temperatura"})) return true;
    if (contains_any(q, {"yağış", "yagis", "yağiş", "yagish"})) return true;
    if (contains_any(q, {"külək", "kulek", "küləkl"})) return true;
    if (contains_any(q, {"bulud", "buludlu"})) return true;
    if (contains_any(q, {"şaxta", "saxta", "soyuq olacaq", "isti olacaq"})) return true;
    if (contains_any(q, {"qarlı", "qar yağ", "qar olacaq", "dolu yağ"})) return true;
    // "neçə dərəcədir?" - but "elmi dərəcə" is an academic degree and a bare
    // "dərəcə" is agronomy ("torpağın dərəcəsi"), so only the question form.
    if (q.find("elmi dərəcə") == std::string::npos && q.find("elmi derece") == std::string::npos &&
        contains_any(q, {"neçə dərəcə", "nece derece", "dərəcə isti", "derece isti",
                         "dərəcə soyuq", "derece soyuq"}))
        return true;
    return false;
}

bool WeatherTool::wants_forecast(const std::string& question) {
    const std::string q = to_lower_utf8(question);
    if (contains_any(q, {"sabah", "sabahkı", "sabahki"})) return true;
    if (contains_any(q, {"proqnoz", "prognoz"})) return true;
    if (q.find("gələn həftə") != std::string::npos ||
        q.find("gelen hefte") != std::string::npos)
        return true;
    if (contains_any(q, {"yağacaq", "yagacaq", "yağarmı", "yagarmi", "olacaq"})) return true;
    return false;
}

// ---------------------------------------------------------------------------
// City extraction
// ---------------------------------------------------------------------------
std::string WeatherTool::to_api_city(const std::string& city) {
    const std::string c = to_lower_utf8(trim(city));
    if (c.empty()) return std::string();
    std::string best;
    size_t best_len = 0;
    for (const auto& e : kCities) {
        std::stringstream ss(e.aliases);
        std::string alias;
        while (        std::getline(ss, alias, '|')) {
            if (alias.empty()) continue;
            // The API spelling is accepted too ("baku" as typed in English).
            if (c != alias && c != to_lower_utf8(e.api)) continue;
            if (alias.size() > best_len) {
                best_len = alias.size();
                best = e.api;
            }
        }
    }
    return best.empty() ? trim(city) : best;
}

std::string WeatherTool::extract_city(const std::string& question, const std::string& fallback) {
    const std::string q = to_lower_utf8(question);
    std::string best;
    size_t best_len = 0;
    for (const auto& e : kCities) {
        std::stringstream ss(e.aliases);
        std::string alias;
        while (std::getline(ss, alias, '|')) {
            if (alias.size() <= best_len) continue;  // longest match wins
            if (alias.empty() || q.find(alias) == std::string::npos) continue;
            best_len = alias.size();
            best = e.api;
        }
    }
    if (!best.empty()) return best;
    // "Baku" typed in Latin letters still names the capital.
    for (const auto& e : kCities) {
        const std::string api = to_lower_utf8(e.api);
        if (api.size() > best_len && api.size() > 2 && q.find(api) != std::string::npos) {
            best_len = api.size();
            best = e.api;
        }
    }
    if (!best.empty()) return best;
    return to_api_city(fallback);
}

// ---------------------------------------------------------------------------
// Prompt blocks
// ---------------------------------------------------------------------------
std::string WeatherTool::format_current(const nlohmann::json& j, const std::string& api_city) {
    if (!j.is_object() || !j.contains("main") || !j["main"].is_object()) return std::string();
    const auto& main = j["main"];
    std::string city = json_str(j, "name", api_city);
    const std::string country = j.contains("sys") && j["sys"].is_object()
                                    ? json_str(j["sys"], "country")
                                    : std::string();
    if (!country.empty()) city += ", " + country;

    std::string desc;
    if (j.contains("weather") && j["weather"].is_array() && !j["weather"].empty())
        desc = json_str(j["weather"][0], "description");

    std::ostringstream os;
    os << "CANLI HAVA MƏLUMATI (OpenWeatherMap";
    if (j.contains("dt") && j["dt"].is_number())
        os << "; " << local_time_string(j["dt"].get<long long>());
    os << "):\n";
    os << "Şəhər: " << city << "\n";
    os << "Temperatur: " << fmt_num(json_num(main, "temp")) << "°C";
    if (main.contains("feels_like"))
        os << " (hiss edilir: " << fmt_num(json_num(main, "feels_like")) << "°C)";
    os << "\n";
    if (!desc.empty()) os << "Hava: " << desc << "\n";
    if (main.contains("humidity")) os << "Rütubət: " << fmt_num(json_num(main, "humidity"), 0) << "%\n";
    if (main.contains("pressure")) os << "Təzyiq: " << fmt_num(json_num(main, "pressure"), 0) << " hPa\n";
    if (j.contains("wind") && j["wind"].is_object())
        os << "Külək: " << fmt_num(json_num(j["wind"], "speed")) << " m/s\n";
    return os.str();
}

std::string WeatherTool::format_forecast(const nlohmann::json& j, const std::string& api_city) {
    if (!j.is_object() || !j.contains("list") || !j["list"].is_array() || j["list"].empty())
        return std::string();
    std::string city = api_city;
    if (j.contains("city") && j["city"].is_object()) {
        city = json_str(j["city"], "name", api_city);
        const std::string country = json_str(j["city"], "country");
        if (!country.empty()) city += ", " + country;
    }

    std::ostringstream os;
    os << "CANLI HAVA PROQNOZU (OpenWeatherMap; 3 saatlıq intervallarla):\n";
    os << "Şəhər: " << city << "\n";
    int n = 0;
    for (const auto& item : j["list"]) {
        if (++n > 8) break;
        if (!item.is_object()) continue;
        std::string when = json_str(item, "dt_txt");
        if (!when.empty()) when = short_dt_txt(when);
        std::string desc;
        if (item.contains("weather") && item["weather"].is_array() && !item["weather"].empty())
            desc = json_str(item["weather"][0], "description");
        os << "- " << when << ": ";
        if (item.contains("main") && item["main"].is_object())
            os << fmt_num(json_num(item["main"], "temp")) << "°C";
        if (!desc.empty()) os << ", " << desc;
        if (item.contains("wind") && item["wind"].is_object())
            os << ", külək " << fmt_num(json_num(item["wind"], "speed")) << " m/s";
        if (item.contains("pop") && item["pop"].is_number())
            os << ", yağıntı ehtimalı " << fmt_num(json_num(item, "pop") * 100.0, 0) << "%";
        os << "\n";
    }
    return os.str();
}

std::string WeatherTool::inject_weather(const std::string& user_message, const WeatherResult& r) {
    if (!r.ok || r.block.empty()) return user_message;
    return r.block + "\n\n" + user_message;
}

std::string WeatherTool::system_suffix(const WeatherResult& r) {
    if (!r.ok || r.block.empty()) return std::string();
    return "\n\nHAVA ALƏTİ: Bu sorğuda sənə CANLI HAVA MƏLUMATI verilib "
           "(OpenWeatherMap, real vaxt). Hava ilə bağlı hissəni YALNIZ bu "
           "məlumatdan cavablandır: temperatur, hava şəraiti, rütubət, külək və "
           "yağıntı ehtimalını oradan götür, rəqəm uydurma, [Mənbə: ...] "
           "etiketi yazma, [Ümumi bilik] yazma. Bu məlumat dəyişkəndir - lazım "
           "olduqda istifadəçiyə onun canlı olduğunu bildir.";
}

std::string WeatherTool::no_key_message() {
    return "Hava məlumatını əldə edə bilmirəm, çünki Hava API açarı təyin "
           "edilməyib. Sağ üstdəki \"API açarları\" panelindən OpenWeatherMap "
           "açarı əlavə edin.";
}

std::string WeatherTool::no_city_message() {
    return "Hava məlumatı üçün şəhər adını yazın (məsələn: \"Bakıda hava "
           "necədir?\").";
}

// ---------------------------------------------------------------------------
// URL helper
// ---------------------------------------------------------------------------
std::string weather_url_encode(const std::string& s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// HTTP
// ---------------------------------------------------------------------------
bool WeatherTool::fetch(const std::string& url, std::string& body, int& http_code,
                        std::string& err) const {
    body.clear();
    http_code = 0;
    CURL* raw = curl_easy_init();
    if (!raw) {
        err = "curl_easy_init failed";
        return false;
    }
    std::unique_ptr<CURL, void (*)(CURL*)> curl(raw, curl_easy_cleanup);
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, write_to_string);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "DUMY/1.0 (weather tool)");

    const CURLcode rc = curl_easy_perform(curl.get());
    long code = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &code);
    http_code = static_cast<int>(code);
    if (rc != CURLE_OK) {
        err = std::string("transport: ") + curl_easy_strerror(rc);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// resolve(): cache -> key lease -> provider -> formatted block
// ---------------------------------------------------------------------------
WeatherResult WeatherTool::resolve(const std::string& question) const {
    WeatherResult r;
    r.requested = is_weather_question(question);
    if (!r.requested) return r;
    if (!cfg_.weather_enabled) {  // rule out: feature switched off in config.json
        r.requested = false;
        return r;
    }

    r.forecast = wants_forecast(question);
    r.city = extract_city(question, cfg_.weather_default_city);
    r.city_display = r.city;
    if (r.city.empty()) {
        r.error = "no_city";
        r.message = no_city_message();
        return r;
    }

    // --- 10-minute in-memory cache ----------------------------------------
    const std::string cache_key = r.city + (r.forecast ? "|f" : "|c");
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = cache_.find(cache_key);
        if (it != cache_.end() && cfg_.weather_cache_minutes > 0 &&
            now - it->second.at <
                std::chrono::minutes(cfg_.weather_cache_minutes)) {
            WeatherResult cached = it->second.result;
            cached.cache_hit = true;
            return cached;
        }
    }

    if (!keys_) {
        r.error = "no_key";
        if (!explicit_weather_request(question)) {  // not a weather question -> RAG
            r.requested = false;
            return r;
        }
        r.message = no_key_message();
        return r;
    }

    const std::string base = "https://api.openweathermap.org/data/2.5/";
    const std::string units = cfg_.weather_units.empty() ? "metric" : cfg_.weather_units;
    const std::string lang = cfg_.weather_lang.empty() ? "az" : cfg_.weather_lang;

    // Rotation: a 429/401/402/403 retires the key and the next one is tried.
    std::string last_err;
    const int max_attempts = 4;
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        KeyLease lease = keys_->get_next_key(KeyService::weather_api);
        if (!lease.ok) {
            // The store's own error text ("weather_api üçün açar yoxdur") is
            // for the log; the user gets the actionable message.
            r.error = "no_key";
            if (!explicit_weather_request(question)) {  // not weather -> let RAG answer
                r.requested = false;
                return r;
            }
            r.message = no_key_message();
            break;
        }
        std::string url = base + (r.forecast ? "forecast" : "weather") +
                          "?q=" + weather_url_encode(r.city) +
                          "&appid=" + weather_url_encode(lease.key) +
                          "&units=" + weather_url_encode(units) +
                          "&lang=" + weather_url_encode(lang);
        if (r.forecast) url += "&cnt=8";

        std::string body;
        std::string err;
        const bool transport_ok = fetch(url, body, r.http_code, err);
        if (!transport_ok) {
            r.error = "transport";
            r.message = "Hava xidmətinə qoşulmaq mümkün olmadı: " + err;
            break;
        }

        if (r.http_code == 200) {
            nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
            if (j.is_discarded()) {
                r.error = "parse";
                r.message = "Hava xidmətinin cavabı oxuna bilmədi.";
                break;
            }
            const std::string block =
                r.forecast ? format_forecast(j, r.city) : format_current(j, r.city);
            if (block.empty()) {
                r.error = "parse";
                r.message = "Hava məlumatı bu şəhər üçün boş gəldi.";
                break;
            }
            keys_->increment_usage(KeyService::weather_api);
            r.ok = true;
            r.http_code = 200;
            r.data = j;
            r.block = block;
            if (cfg_.weather_cache_minutes > 0) {
                std::lock_guard<std::mutex> lock(cache_mutex_);
                cache_[cache_key] = CacheEntry{r, now};
            }
            return r;
        }

        if (r.http_code == 404) {
            // Unknown city: the key is fine, do NOT retire it.
            r.error = "city_not_found";
            r.message = "Bu şəhər üzrə hava məlumatı tapılmadı.";
            break;
        }
        if (r.http_code == 401 || r.http_code == 402 || r.http_code == 403 ||
            r.http_code == 429) {
            last_err = "http_" + std::to_string(r.http_code);
            keys_->mark_key_exhausted(KeyService::weather_api);
            continue;  // next key
        }
        r.error = "http_" + std::to_string(r.http_code);
        r.message = "Hava xidməti xəta qaytardı (HTTP " +
                    std::to_string(r.http_code) + ").";
        break;
    }

    if (!r.ok && r.message.empty()) {
        r.error = last_err.empty() ? "no_key" : last_err;
        r.message = "Hava məlumatını əldə edə bilmirəm: bütün OpenWeatherMap "
                    "açarları tükənib. Panelda yeni açar əlavə edin.";
    }
    return r;
}

}  // namespace dumy
