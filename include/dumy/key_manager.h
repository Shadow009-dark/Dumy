// key_manager.h - local API key store for external providers.
//
// Additive module: DUMY's own Ollama path is untouched. This keeps the user's
// external provider keys (OpenRouter / Gemini / OpenWeatherMap) in
// config/api_keys.json, rotates them when one is rate limited or out of quota,
// and exposes a masked view for the UI.
//
// Thread-safe: the HTTP server is multi-threaded, so every public member locks.
#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace dumy {

enum class KeyService { ai_api = 0, weather_api = 1 };

std::string key_service_name(KeyService service);

// Accepts "ai_api" / "weather_api" (case-insensitive).
std::optional<KeyService> key_service_from_name(const std::string& name);

// "sk-or-v1-0123456789abcdef" -> "sk-o...cdef"; short keys are fully starred.
std::string mask_api_key(const std::string& key);

struct ApiKeyEntry {
    std::string provider;
    std::string key;
    std::string status = "active";  // "active" | "exhausted"
    long long request_count = 0;
    long long limit = 0;  // <= 0 means no local quota

    bool exhausted() const { return status == "exhausted"; }
    bool placeholder() const;
    bool quota_reached() const { return limit > 0 && request_count >= limit; }
    bool usable() const { return !exhausted() && !quota_reached() && !placeholder(); }
};

struct KeyLease {
    bool ok = false;
    std::size_t index = 0;
    std::string provider;
    std::string key;
    std::string error;
};

class KeyManager {
public:
    explicit KeyManager(std::filesystem::path config_path);

    KeyManager(const KeyManager&) = delete;
    KeyManager& operator=(const KeyManager&) = delete;

    // Reads config/api_keys.json, creating the skeleton file when missing.
    // An unparseable file is reported and left untouched.
    bool load(std::string* err = nullptr);
    bool save(std::string* err = nullptr);

    // Active usable key; exhausted / over-quota / placeholder entries are
    // skipped and the cursor is advanced (and persisted) when it moves.
    KeyLease get_next_key(KeyService service);

    // One request against the active key, persisted.
    bool increment_usage(KeyService service);

    // Retires the active key (429 / 401 / 402 / 403) and moves to the next.
    bool mark_key_exhausted(KeyService service);

    bool add_key(KeyService service, std::string provider, std::string key, long long limit,
                 std::size_t* out_index, std::string* err);
    bool delete_key(KeyService service, std::size_t index, std::string* err);
    bool activate_key(KeyService service, std::size_t index, std::string* err);

    std::size_t key_count(KeyService service) const;
    std::size_t active_index(KeyService service) const;

    // UI view: api_keys.json layout with the key masked, plus active/usable.
    nlohmann::json masked_json() const;

    const std::filesystem::path& path() const { return path_; }

    static constexpr std::size_t kMaxKeysPerService = 32;

private:
    struct Bucket {
        std::size_t active_index = 0;
        std::vector<ApiKeyEntry> keys;
    };

    static std::size_t slot(KeyService service) {
        return static_cast<std::size_t>(service);
    }
    static Bucket parse_bucket(const nlohmann::json& node);
    static nlohmann::json bucket_to_json(const Bucket& bucket);

    // Both require mutex_ to be held.
    KeyLease resolve_locked(KeyService service);
    bool save_locked(std::string* err);

    mutable std::mutex mutex_;
    std::filesystem::path path_;
    std::array<Bucket, 2> buckets_{};
    nlohmann::json document_ = nlohmann::json::object();
};

}  // namespace dumy
