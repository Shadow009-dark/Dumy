// api_keys_api.h - additive /api/keys routes for the local key panel.
//
// Kept in its own translation unit so http_server.cpp only needs one include
// and one call. Routes are loopback-only on purpose: external provider keys
// must never be managed from another machine.
#pragma once

namespace httplib {
class Server;
}

namespace dumy {

class KeyManager;
struct Config;

// GET    /api/keys           -> masked key pools
// POST   /api/keys           {"service","provider","key","limit"}
// DELETE /api/keys           {"service","index"}
// POST   /api/keys/activate  {"service","index"}
void register_api_key_routes(httplib::Server& svr, KeyManager& keys, const Config& cfg);

}  // namespace dumy
