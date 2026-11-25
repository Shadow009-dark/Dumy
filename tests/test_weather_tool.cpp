// test_weather_tool.cpp - unit checks for the internal weather tool.
// No server, no model, no network: intent, city mapping, prompt blocks and the
// no-key fallback are fixed strings (built as dumy_unit_tests).
#include <cstdio>
#include <string>

#include <nlohmann/json.hpp>

#include "dumy/weather_tool.h"

namespace {

int g_failed = 0;
int g_total = 0;

void check(bool ok, const char* name) {
    ++g_total;
    if (!ok) ++g_failed;
    std::printf("%-58s %s\n", name, ok ? "PASS" : "FAIL");
}

bool has(const std::string& hay, const char* needle) {
    return hay.find(needle) != std::string::npos;
}

}  // namespace

int main() {
    using namespace dumy;

    // --- Intent detection --------------------------------------------------
    check(WeatherTool::is_weather_question("Bakıda hava necədir?"), "intent: Bakıda hava necədir");
    check(WeatherTool::is_weather_question("bakida hava necedir"), "intent: typed without diacritics");
    check(WeatherTool::is_weather_question("sabah Gəncədə yağış yağacaq?"), "intent: yağış yağacaq");
    check(WeatherTool::is_weather_question("Lənkəranda temperatur neçə dərəcədir?"),
          "intent: temperatur / dərəcə");
    check(WeatherTool::is_weather_question("külək olacaq?"), "intent: külək");
    check(!WeatherTool::is_weather_question("İnək gündə neçə dəfə sağılmalıdır?"),
          "intent: dairy question is not weather");
    check(!WeatherTool::is_weather_question("tövlədə havalandırma sistemi necə qurulur?"),
          "intent: havalandırma is not hava");
    check(!WeatherTool::is_weather_question("qarğıdalı nə vaxt əkilir?"),
          "intent: qarğıdalı (corn) is not qar");
    check(!WeatherTool::is_weather_question("elmi dərəcə necə alınır?"),
          "intent: elmi dərəcə is not weather");

    // --- Current vs forecast mode -----------------------------------------
    check(WeatherTool::wants_forecast("sabah hava necə olacaq?"), "mode: sabah -> forecast");
    check(WeatherTool::wants_forecast("hava proqnozu nədir?"), "mode: proqnoz -> forecast");
    check(WeatherTool::wants_forecast("Bakıda sabah yağış yağacaq?"), "mode: yağacaq -> forecast");
    check(!WeatherTool::wants_forecast("Bakıda hava necədir?"), "mode: current weather");

    // --- City extraction (AZ -> OpenWeatherMap Latin) ----------------------
    check(WeatherTool::extract_city("bakida hava necedir", "baku") == "Baku",
          "city: bakida -> Baku");
    check(WeatherTool::extract_city("sabah Gəncədə yağış yağacaq?", "baku") == "Ganja",
          "city: Gəncədə -> Ganja");
    check(WeatherTool::extract_city("Lənkəranda hava necədir?", "baku") == "Lankaran",
          "city: Lənkəranda -> Lankaran");
    check(WeatherTool::extract_city("Qubadlıda hava", "baku") == "Gubadli",
          "city: longest match (Qubadlı not Quba)");
    check(WeatherTool::extract_city("hava necədir?", "baku") == "Baku",
          "city: fallback default_city");
    check(WeatherTool::extract_city("hava necədir?", "").empty(),
          "city: no city and no fallback -> empty");

    // --- Current-weather block --------------------------------------------
    const nlohmann::json cur = {
        {"name", "Baku"},
        {"sys", {{"country", "AZ"}}},
        {"dt", 1700000000},
        {"main", {{"temp", 12.44}, {"feels_like", 11.0}, {"humidity", 70}, {"pressure", 1012}}},
        {"weather", {{{"description", "az buludlu"}}}},
        {"wind", {{"speed", 3.2}}}};
    const std::string block = WeatherTool::format_current(cur, "Baku");
    check(has(block, "CANLI HAVA MƏLUMATI"), "format: current has header");
    check(has(block, "12.4°C"), "format: current temperature");
    check(has(block, "az buludlu"), "format: current description");
    check(has(block, "Rütubət: 70%"), "format: current humidity");
    check(has(block, "Baku, AZ"), "format: current city + country");

    // --- Forecast block ----------------------------------------------------
    const nlohmann::json fc = {
        {"city", {{"name", "Ganja"}, {"country", "AZ"}}},
        {"list",
         {{{"dt_txt", "2026-11-21 15:00:00"},
           {"main", {{"temp", 9.0}}},
           {"weather", {{{"description", "yüngül yağış"}}}},
           {"wind", {{"speed", 2.0}}},
           {"pop", 0.3}},
          {{"dt_txt", "2026-11-21 18:00:00"},
           {"main", {{"temp", 7.5}}},
           {"weather", {{{"description", "buludlu"}}}},
           {"wind", {{"speed", 4.0}}},
           {"pop", 0.1}}}}};
    const std::string fblock = WeatherTool::format_forecast(fc, "Ganja");
    check(has(fblock, "CANLI HAVA PROQNOZU"), "format: forecast has header");
    check(has(fblock, "21.11 15:00"), "format: forecast slot time");
    check(has(fblock, "yağıntı ehtimalı 30%"), "format: forecast precipitation chance");
    check(has(fblock, "Ganja, AZ"), "format: forecast city + country");

    // --- Purity: a malformed payload must not produce a block --------------
    check(WeatherTool::format_current(nlohmann::json::object(), "Baku").empty(),
          "format: empty payload -> empty block");
    check(WeatherTool::format_forecast(nlohmann::json::object(), "Baku").empty(),
          "format: empty forecast -> empty block");

    // --- Injection ---------------------------------------------------------
    WeatherResult ok;
    ok.requested = true;
    ok.ok = true;
    ok.block = block;
    const std::string msg = WeatherTool::inject_weather("SUAL: hava necədir?", ok);
    check(msg.rfind("CANLI HAVA MƏLUMATI", 0) == 0, "inject: block is prepended");
    check(has(msg, "SUAL: hava necədir?"), "inject: original message kept");

    WeatherResult failed;
    const std::string plain = "SUAL: hava necədir?";
    check(WeatherTool::inject_weather(plain, failed) == plain,
          "inject: unchanged when no data");
    check(WeatherTool::system_suffix(failed).empty(), "suffix: empty when no data");
    check(!WeatherTool::system_suffix(ok).empty(), "suffix: present with live data");

    // --- No-key fallback ---------------------------------------------------
    check(has(WeatherTool::no_key_message(), "Hava API açarı"),
          "message: no key names the missing weather key");

    Config cfg;  // defaults: weather.enabled = true, default_city = baku
    WeatherTool tool(cfg);
    const WeatherResult no_key = tool.resolve("Bakıda hava necədir?");
    check(no_key.requested && !no_key.ok && no_key.error == "no_key",
          "resolve: no KeyManager -> no_key error");
    check(has(no_key.message, "Hava API açarı"), "resolve: no_key user message");
    check(!tool.resolve("İnək gündə neçə dəfə sağılmalıdır?").requested,
          "resolve: non-weather question ignored");

    std::printf("\n%d/%d passed\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}
