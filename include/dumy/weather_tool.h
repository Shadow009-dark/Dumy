// weather_tool.h - internal weather "tool call" for the chat pipeline.
//
// Additive module: there is NO public /api/weather route. When a question is
// about the weather, the pipeline calls WeatherTool::resolve() itself (regex
// intent + deterministic city extraction), gets the live data from
// OpenWeatherMap with key rotation through KeyManager, and injects the result
// into the prompt as a "CANLI HAVA MƏLUMATI" block.
//
// Thread-safe: the HTTP server answers on many threads, so the 10-minute
// in-memory cache is mutex protected.
#pragma once

#include <chrono>
#include <map>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "dumy/config.h"
#include "dumy/key_manager.h"

namespace dumy {

// Everything one weather lookup produced. `block` is the text injected into
// the prompt; `message` is the user-facing text used when the lookup failed.
struct WeatherResult {
    bool requested = false;   // the question asks about the weather
    bool ok = false;          // live data was obtained
    bool forecast = false;    // "/forecast?cnt=8" instead of "/weather"
    bool cache_hit = false;   // served from the 10-minute cache
    int http_code = 0;        // provider HTTP status (0 = no request made)
    std::string city;         // API form, e.g. "Baku"
    std::string city_display; // what the user wrote ("Gəncə")
    std::string block;        // "CANLI HAVA MƏLUMATI (...)" - empty when !ok
    std::string error;        // "" | no_city | no_key | city_not_found | http_XXX | parse
    std::string message;      // user-facing Azerbaijani text for !ok
    nlohmann::json data = nlohmann::json::object();  // trimmed provider payload

    nlohmann::json to_json() const;
};

class WeatherTool {
public:
    explicit WeatherTool(const Config& cfg);

    // Attached by HttpServer once config/api_keys.json is loaded. Without it
    // the tool reports "no_key" instead of calling the provider.
    void set_key_manager(KeyManager* keys) { keys_ = keys; }
    bool enabled() const { return cfg_.weather_enabled; }

    // --- Intent (deterministic, no model, no network) ----------------------
    static bool is_weather_question(const std::string& question);
    static bool wants_forecast(const std::string& question);

    // --- City (deterministic AZ -> Latin mapping) --------------------------
    // Returns the OpenWeatherMap spelling ("Baku"); `fallback` is used when the
    // question names no known city.
    static std::string extract_city(const std::string& question,
                                    const std::string& fallback = "");
    static std::string to_api_city(const std::string& city);

    // --- Prompt building (pure, unit-testable) ----------------------------
    static std::string format_current(const nlohmann::json& j, const std::string& api_city);
    static std::string format_forecast(const nlohmann::json& j, const std::string& api_city);
    // Prepends the live-weather block to an already built user message.
    static std::string inject_weather(const std::string& user_message, const WeatherResult& r);
    // Extra rule appended to the system message for this one request only.
    static std::string system_suffix(const WeatherResult& r);

    static std::string no_key_message();
    static std::string no_city_message();

    // Full lookup: cache -> key lease -> provider -> formatted block.
    WeatherResult resolve(const std::string& question) const;

private:
    struct CacheEntry {
        WeatherResult result;
        std::chrono::steady_clock::time_point at = std::chrono::steady_clock::now();
    };

    // Minimal GET; returns false on a transport error (err filled).
    bool fetch(const std::string& url, std::string& body, int& http_code,
               std::string& err) const;

    mutable std::mutex cache_mutex_;
    mutable std::map<std::string, CacheEntry> cache_;  // "<city>|c" / "<city>|f"
    const Config& cfg_;
    KeyManager* keys_ = nullptr;
};

// Percent-encodes a query value (spaces, non-ASCII) for the provider URL.
std::string weather_url_encode(const std::string& s);

}  // namespace dumy
