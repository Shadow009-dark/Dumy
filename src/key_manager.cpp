#include "dumy/key_manager.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace dumy {
namespace {

const char* const kActive = "active";
const char* const kExhausted = "exhausted";

std::string trim(const std::string& value) {
    std::size_t begin = 0;
    std::size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(begin, end - begin);
}

long long read_integer(const nlohmann::json& node, const char* field, long long fallback) {
    const auto it = node.find(field);
    if (it == node.end() || it->is_null()) return fallback;
    if (it->is_number_unsigned()) return static_cast<long long>(it->get<unsigned long long>());
    if (it->is_number_integer()) return it->get<long long>();
    if (it->is_number_float()) return static_cast<long long>(it->get<double>());
    if (it->is_string()) {
        try {
            return std::stoll(it->get<std::string>());
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

std::string read_string(const nlohmann::json& node, const char* field,
                        const std::string& fallback = std::string()) {
    const auto it = node.find(field);
    if (it != node.end() && it->is_string()) return it->get<std::string>();
    return fallback;
}

// Atomic replace: a crash or a full disk can never leave half a key store.
bool replace_file(const std::filesystem::path& from, const std::filesystem::path& to) {
#ifdef _WIN32
    return ::MoveFileExW(from.c_str(), to.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    return !ec;
#endif
}

bool has(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

std::string key_service_name(KeyService service) {
    switch (service) {
        case KeyService::ai_api: return "ai_api";
        case KeyService::weather_api: return "weather_api";
    }
    return "ai_api";
}

std::optional<KeyService> key_service_from_name(const std::string& name) {
    std::string lowered;
    lowered.reserve(name.size());
    for (char ch : name) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    if (lowered == "ai_api" || lowered == "ai") return KeyService::ai_api;
    if (lowered == "weather_api" || lowered == "weather") return KeyService::weather_api;
    return std::nullopt;
}

std::string mask_api_key(const std::string& key) {
    if (key.empty()) return std::string();
    if (key.size() <= 8) return std::string(key.size(), '*');
    return key.substr(0, 4) + "..." + key.substr(key.size() - 4);
}

bool ApiKeyEntry::placeholder() const {
    if (key.empty()) return true;
    if (key.rfind("YOUR_", 0) == 0) return true;
    if (key.rfind("REPLACE", 0) == 0) return true;
    if (has(key, "...") || has(key, "<") || has(key, "your-key")) return true;
    return false;
}

KeyManager::KeyManager(std::filesystem::path config_path) : path_(std::move(config_path)) {}

KeyManager::Bucket KeyManager::parse_bucket(const nlohmann::json& node) {
    Bucket bucket;
    const long long active = read_integer(node, "active_index", 0);
    if (active > 0) bucket.active_index = static_cast<std::size_t>(active);

    const auto keys = node.find("keys");
    if (keys != node.end() && keys->is_array()) {
        bucket.keys.reserve(keys->size());
        for (const auto& item : *keys) {
            if (!item.is_object()) continue;
            ApiKeyEntry entry;
            entry.provider = trim(read_string(item, "provider"));
            entry.key = trim(read_string(item, "key"));
            entry.status = read_string(item, "status", kActive);
            if (entry.status != kExhausted) entry.status = kActive;
            entry.request_count = std::max<long long>(0, read_integer(item, "request_count", 0));
            entry.limit = std::max<long long>(0, read_integer(item, "limit", 0));
            if (entry.provider.empty() && entry.key.empty()) continue;
            bucket.keys.push_back(std::move(entry));
        }
    }

    bucket.active_index = bucket.keys.empty() ? 0 : bucket.active_index % bucket.keys.size();
    return bucket;
}

nlohmann::json KeyManager::bucket_to_json(const Bucket& bucket) {
    nlohmann::json keys = nlohmann::json::array();
    for (const auto& entry : bucket.keys) {
        keys.push_back({{"provider", entry.provider},
                        {"key", entry.key},
                        {"status", entry.status},
                        {"request_count", entry.request_count},
                        {"limit", entry.limit}});
    }
    return {{"active_index", bucket.active_index}, {"keys", std::move(keys)}};
}

bool KeyManager::load(std::string* err) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::error_code ec;
    if (!std::filesystem::exists(path_, ec)) {
        document_ = nlohmann::json::object();
        buckets_ = {};
        std::string save_err;
        if (!save_locked(&save_err)) {
            if (err) *err = save_err;
            return false;
        }
        return true;
    }

    std::ifstream in(path_, std::ios::binary);
    if (!in.is_open()) {
        if (err) *err = "açar faylı açılmadı: " + path_.string();
        return false;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    if (!in.good() && !in.eof()) {
        if (err) *err = "açar faylı oxunmadı: " + path_.string();
        return false;
    }

    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(buffer.str());
    } catch (const std::exception& ex) {
        // Never overwrite a store we could not parse.
        if (err) *err = std::string("api_keys.json səhvdir: ") + ex.what();
        return false;
    }
    if (!parsed.is_object()) {
        if (err) *err = "api_keys.json kökü obyekt olmalıdır";
        return false;
    }

    document_ = std::move(parsed);
    buckets_ = {};
    for (const KeyService service : {KeyService::ai_api, KeyService::weather_api}) {
        const auto node = document_.find(key_service_name(service));
        if (node != document_.end() && node->is_object()) {
            buckets_[slot(service)] = parse_bucket(*node);
        }
    }
    return true;
}

bool KeyManager::save_locked(std::string* err) {
    if (!document_.is_object()) document_ = nlohmann::json::object();
    for (const KeyService service : {KeyService::ai_api, KeyService::weather_api}) {
        document_[key_service_name(service)] = bucket_to_json(buckets_[slot(service)]);
    }

    std::error_code ec;
    const auto parent = path_.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            if (err) *err = "config qovluğu yaradılmadı: " + ec.message();
            return false;
        }
    }

    std::filesystem::path temp = path_;
    temp += ".tmp";

    const std::string payload = document_.dump(2) + "\n";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            if (err) *err = "müvəqqəti fayl yazılmadı: " + temp.string();
            return false;
        }
        out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        out.flush();
        if (!out.good()) {
            out.close();
            std::filesystem::remove(temp, ec);
            if (err) *err = "açar faylı yazılarkən xəta: " + temp.string();
            return false;
        }
    }

    if (!replace_file(temp, path_)) {
        std::filesystem::remove(path_, ec);
        if (!replace_file(temp, path_)) {
            std::filesystem::remove(temp, ec);
            if (err) *err = "açar faylı əvəz olunmadı: " + path_.string();
            return false;
        }
    }
    return true;
}

bool KeyManager::save(std::string* err) {
    std::lock_guard<std::mutex> lock(mutex_);
    return save_locked(err);
}

KeyLease KeyManager::resolve_locked(KeyService service) {
    Bucket& bucket = buckets_[slot(service)];
    KeyLease lease;
    const std::string name = key_service_name(service);

    if (bucket.keys.empty()) {
        lease.error = name + " üçün açar yoxdur";
        return lease;
    }

    const std::size_t count = bucket.keys.size();
    const std::size_t start = bucket.active_index % count;
    for (std::size_t offset = 0; offset < count; ++offset) {
        const std::size_t index = (start + offset) % count;
        const ApiKeyEntry& entry = bucket.keys[index];
        if (!entry.usable()) continue;

        if (bucket.active_index != index) {
            bucket.active_index = index;
            std::string save_err;
            if (!save_locked(&save_err)) {
                std::fprintf(stderr, "[dumy] key rotation not persisted: %s\n",
                             save_err.c_str());
            }
        }
        lease.ok = true;
        lease.index = index;
        lease.provider = entry.provider;
        lease.key = entry.key;
        return lease;
    }

    lease.error = name + " açarları bitib (exhausted / limit / boş)";
    return lease;
}

KeyLease KeyManager::get_next_key(KeyService service) {
    std::lock_guard<std::mutex> lock(mutex_);
    return resolve_locked(service);
}

bool KeyManager::increment_usage(KeyService service) {
    std::lock_guard<std::mutex> lock(mutex_);
    Bucket& bucket = buckets_[slot(service)];
    if (bucket.keys.empty()) return false;

    const std::size_t index = bucket.active_index % bucket.keys.size();
    ++bucket.keys[index].request_count;
    std::string save_err;
    if (!save_locked(&save_err)) {
        std::fprintf(stderr, "[dumy] usage counter not persisted: %s\n", save_err.c_str());
    }
    return true;
}

bool KeyManager::mark_key_exhausted(KeyService service) {
    std::lock_guard<std::mutex> lock(mutex_);
    Bucket& bucket = buckets_[slot(service)];
    if (bucket.keys.empty()) return false;

    const std::size_t count = bucket.keys.size();
    const std::size_t current = bucket.active_index % count;
    bucket.keys[current].status = kExhausted;

    std::size_t next = current;
    for (std::size_t offset = 1; offset <= count; ++offset) {
        const std::size_t index = (current + offset) % count;
        if (bucket.keys[index].usable()) {
            next = index;
            break;
        }
    }
    bucket.active_index = next;

    std::string save_err;
    if (!save_locked(&save_err)) {
        std::fprintf(stderr, "[dumy] key exhaustion not persisted: %s\n", save_err.c_str());
    }
    return true;
}

bool KeyManager::add_key(KeyService service, std::string provider, std::string key,
                         long long limit, std::size_t* out_index, std::string* err) {
    provider = trim(provider);
    key = trim(key);
    if (provider.empty() || key.empty()) {
        if (err) *err = "provider və key tələb olunur";
        return false;
    }
    if (limit < 0) {
        if (err) *err = "limit mənfi ola bilməz";
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    Bucket& bucket = buckets_[slot(service)];
    if (bucket.keys.size() >= kMaxKeysPerService) {
        if (err) *err = "bir servis üçün maksimum " +
                        std::to_string(kMaxKeysPerService) + " açar";
        return false;
    }
    for (const auto& entry : bucket.keys) {
        if (entry.provider == provider && entry.key == key) {
            if (err) *err = "bu açar artıq " + provider + " üçün əlavə olunub";
            return false;
        }
    }

    ApiKeyEntry entry;
    entry.provider = provider;
    entry.key = key;
    entry.status = kActive;
    entry.request_count = 0;
    entry.limit = limit;
    bucket.keys.push_back(std::move(entry));

    const std::size_t index = bucket.keys.size() - 1;
    const std::size_t current = bucket.active_index % bucket.keys.size();
    if (!bucket.keys[current].usable()) bucket.active_index = index;

    std::string save_err;
    if (!save_locked(&save_err)) {
        if (err) *err = save_err;
        return false;
    }
    if (out_index) *out_index = index;
    return true;
}

bool KeyManager::delete_key(KeyService service, std::size_t index, std::string* err) {
    std::lock_guard<std::mutex> lock(mutex_);
    Bucket& bucket = buckets_[slot(service)];
    if (index >= bucket.keys.size()) {
        if (err) *err = "açar indeksi aralıqdan kənardır";
        return false;
    }

    bucket.keys.erase(bucket.keys.begin() + static_cast<std::ptrdiff_t>(index));
    if (bucket.keys.empty()) {
        bucket.active_index = 0;
    } else {
        if (index < bucket.active_index) {
            --bucket.active_index;
        } else if (index == bucket.active_index && bucket.active_index >= bucket.keys.size()) {
            --bucket.active_index;
        }
        const std::size_t count = bucket.keys.size();
        if (!bucket.keys[bucket.active_index].usable()) {
            for (std::size_t offset = 0; offset < count; ++offset) {
                const std::size_t candidate = (bucket.active_index + offset) % count;
                if (bucket.keys[candidate].usable()) {
                    bucket.active_index = candidate;
                    break;
                }
            }
        }
    }

    std::string save_err;
    if (!save_locked(&save_err)) {
        if (err) *err = save_err;
        return false;
    }
    return true;
}

bool KeyManager::activate_key(KeyService service, std::size_t index, std::string* err) {
    std::lock_guard<std::mutex> lock(mutex_);
    Bucket& bucket = buckets_[slot(service)];
    if (index >= bucket.keys.size()) {
        if (err) *err = "açar indeksi aralıqdan kənardır";
        return false;
    }

    ApiKeyEntry& entry = bucket.keys[index];
    if (entry.placeholder()) {
        if (err) *err = "placeholder açar aktivləşdirilə bilməz";
        return false;
    }
    if (entry.quota_reached()) {
        if (err) {
            *err = "açar limitinə çatıb (" + std::to_string(entry.request_count) + " / " +
                   std::to_string(entry.limit) + ")";
        }
        return false;
    }

    entry.status = kActive;
    bucket.active_index = index;

    std::string save_err;
    if (!save_locked(&save_err)) {
        if (err) *err = save_err;
        return false;
    }
    return true;
}

std::size_t KeyManager::key_count(KeyService service) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return buckets_[slot(service)].keys.size();
}

std::size_t KeyManager::active_index(KeyService service) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return buckets_[slot(service)].active_index;
}

nlohmann::json KeyManager::masked_json() const {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json out = nlohmann::json::object();

    for (const KeyService service : {KeyService::ai_api, KeyService::weather_api}) {
        const Bucket& bucket = buckets_[slot(service)];
        nlohmann::json keys = nlohmann::json::array();
        for (std::size_t i = 0; i < bucket.keys.size(); ++i) {
            const ApiKeyEntry& entry = bucket.keys[i];
            const bool active = i == bucket.active_index;
            const char* status = entry.status.c_str();
            if (!entry.placeholder() && entry.quota_reached() && !entry.exhausted()) {
                status = "quota_reached";
            } else if (entry.placeholder() && !entry.exhausted()) {
                status = "unconfigured";
            }
            keys.push_back({{"provider", entry.provider},
                            {"key", entry.placeholder() ? std::string()
                                                        : mask_api_key(entry.key)},
                            {"status", status},
                            {"request_count", entry.request_count},
                            {"limit", entry.limit},
                            {"active", active},
                            {"usable", entry.usable()}});
        }
        out[key_service_name(service)] = {{"active_index", bucket.active_index},
                                          {"keys", std::move(keys)}};
    }
    return out;
}

}  // namespace dumy
