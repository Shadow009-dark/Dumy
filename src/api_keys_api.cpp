#include "dumy/api_keys_api.h"

#include <httplib.h>

#include <string>

#include "dumy/common.h"
#include "dumy/config.h"
#include "dumy/key_manager.h"

namespace dumy {
namespace {

bool is_local(const std::string& addr) {
    return addr == "127.0.0.1" || addr == "::1" || addr == "localhost" ||
           starts_with(addr, "127.");
}

// Provider names / pasted keys may carry invalid UTF-8; never let a bad byte
// turn a JSON reply into a thrown exception.
std::string dump_json(const nlohmann::json& body) {
    return body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string allowed_origin(const Config& cfg, const httplib::Request& req) {
    const std::string origin = req.get_header_value("Origin");
    if (origin.empty()) return std::string();
    for (const auto& allowed : cfg.allowed_origins) {
        if (allowed == "*" || allowed == origin) return origin;
    }
    return std::string();
}

void reply(const Config& cfg, const httplib::Request& req, httplib::Response& res,
           int status, const nlohmann::json& body) {
    const std::string origin = allowed_origin(cfg, req);
    if (!origin.empty()) {
        res.set_header("Access-Control-Allow-Origin", origin);
        res.set_header("Vary", "Origin");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, X-API-Key");
    }
    res.status = status;
    res.set_header("Cache-Control", "no-store");
    res.set_content(dump_json(body), "application/json; charset=utf-8");
}

void error(const Config& cfg, const httplib::Request& req, httplib::Response& res,
           int status, const std::string& message) {
    reply(cfg, req, res, status, {{"ok", false}, {"error", message}});
}

// Local machine + (when configured) the server API key. Remote clients are
// refused even if they know the key: this panel writes credentials.
bool may_manage(const Config& cfg, const httplib::Request& req, httplib::Response& res) {
    if (!is_local(req.remote_addr)) {
        error(cfg, req, res, 403, "API açarları yalnız bu kompüterdən idarə olunur");
        return false;
    }
    if (!cfg.api_key.empty() && req.get_header_value("X-API-Key") != cfg.api_key) {
        error(cfg, req, res, 401, "API açarı tələb olunur (X-API-Key başlığı)");
        return false;
    }
    return true;
}

bool parse_object(const Config& cfg, const httplib::Request& req, httplib::Response& res,
                  nlohmann::json& out) {
    if (req.body.empty()) {
        error(cfg, req, res, 400, "sorğu gövdəsi boşdur");
        return false;
    }
    out = nlohmann::json::parse(req.body, nullptr, false);
    if (out.is_discarded() || !out.is_object()) {
        error(cfg, req, res, 400, "sorğu JSON obyekti olmalıdır");
        return false;
    }
    return true;
}

std::string text_field(const nlohmann::json& body, const char* field) {
    const auto it = body.find(field);
    if (it != body.end() && it->is_string()) return it->get<std::string>();
    return std::string();
}

}  // namespace

void register_api_key_routes(httplib::Server& svr, KeyManager& keys, const Config& cfg) {
    for (const char* path : {"/api/keys", "/api/keys/activate"}) {
        svr.Options(path, [&cfg](const httplib::Request& req, httplib::Response& res) {
            const std::string origin = allowed_origin(cfg, req);
            if (!origin.empty()) {
                res.set_header("Access-Control-Allow-Origin", origin);
                res.set_header("Vary", "Origin");
                res.set_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
                res.set_header("Access-Control-Allow-Headers", "Content-Type, X-API-Key");
            }
            res.status = 204;
        });
    }

    // --- masked list --------------------------------------------------------
    svr.Get("/api/keys", [&keys, &cfg](const httplib::Request& req,
                                       httplib::Response& res) {
        if (!may_manage(cfg, req, res)) return;
        reply(cfg, req, res, 200, keys.masked_json());
    });

    // --- add ----------------------------------------------------------------
    svr.Post("/api/keys", [&keys, &cfg](const httplib::Request& req,
                                        httplib::Response& res) {
        if (!may_manage(cfg, req, res)) return;

        nlohmann::json body;
        if (!parse_object(cfg, req, res, body)) return;

        const std::string service_name = text_field(body, "service");
        const auto service = key_service_from_name(service_name);
        if (!service) {
            return error(cfg, req, res, 400,
                         "service \"ai_api\" və ya \"weather_api\" olmalıdır");
        }

        const std::string provider = text_field(body, "provider");
        const std::string key = text_field(body, "key");
        if (provider.empty()) return error(cfg, req, res, 400, "provider tələb olunur");
        if (key.empty()) return error(cfg, req, res, 400, "key tələb olunur");

        long long limit = 0;
        const auto limit_it = body.find("limit");
        if (limit_it != body.end() && !limit_it->is_null()) {
            if (limit_it->is_number_unsigned()) {
                limit = static_cast<long long>(limit_it->get<unsigned long long>());
            } else if (limit_it->is_number_integer()) {
                limit = limit_it->get<long long>();
            } else {
                return error(cfg, req, res, 400, "limit tam ədəd olmalıdır");
            }
            if (limit < 0) return error(cfg, req, res, 400, "limit mənfi ola bilməz");
        }

        std::size_t index = 0;
        std::string err;
        if (!keys.add_key(*service, provider, key, limit, &index, &err)) {
            return error(cfg, req, res, 409, err);
        }
        reply(cfg, req, res, 201,
              {{"ok", true}, {"service", key_service_name(*service)}, {"index", index}});
    });

    // --- delete -------------------------------------------------------------
    svr.Delete("/api/keys", [&keys, &cfg](const httplib::Request& req,
                                          httplib::Response& res) {
        if (!may_manage(cfg, req, res)) return;

        nlohmann::json body;
        if (!parse_object(cfg, req, res, body)) return;

        const auto service = key_service_from_name(text_field(body, "service"));
        if (!service) {
            return error(cfg, req, res, 400,
                         "service \"ai_api\" və ya \"weather_api\" olmalıdır");
        }

        const auto index_it = body.find("index");
        if (index_it == body.end() || !index_it->is_number_integer() ||
            index_it->get<long long>() < 0) {
            return error(cfg, req, res, 400, "index mənfi olmayan tam ədəd olmalıdır");
        }
        const auto index = static_cast<std::size_t>(index_it->get<long long>());
        if (index >= keys.key_count(*service)) {
            return error(cfg, req, res, 404, "açar indeksi tapılmadı");
        }

        std::string err;
        if (!keys.delete_key(*service, index, &err)) {
            return error(cfg, req, res, 400, err);
        }
        reply(cfg, req, res, 200, {{"ok", true}});
    });

    // --- activate -----------------------------------------------------------
    svr.Post("/api/keys/activate", [&keys, &cfg](const httplib::Request& req,
                                                 httplib::Response& res) {
        if (!may_manage(cfg, req, res)) return;

        nlohmann::json body;
        if (!parse_object(cfg, req, res, body)) return;

        const auto service = key_service_from_name(text_field(body, "service"));
        if (!service) {
            return error(cfg, req, res, 400,
                         "service \"ai_api\" və ya \"weather_api\" olmalıdır");
        }

        const auto index_it = body.find("index");
        if (index_it == body.end() || !index_it->is_number_integer() ||
            index_it->get<long long>() < 0) {
            return error(cfg, req, res, 400, "index mənfi olmayan tam ədəd olmalıdır");
        }
        const auto index = static_cast<std::size_t>(index_it->get<long long>());
        if (index >= keys.key_count(*service)) {
            return error(cfg, req, res, 404, "açar indeksi tapılmadı");
        }

        std::string err;
        if (!keys.activate_key(*service, index, &err)) {
            return error(cfg, req, res, 400, err);
        }
        reply(cfg, req, res, 200, {{"ok", true}, {"index", index}});
    });
}

}  // namespace dumy
