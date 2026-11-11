// conversation_store.h - durable conversations and long-term user facts.
//
// Layout (all under <data_folder>/memory/<client>/):
//   conversations/<id>.json   one file per conversation (atomic writes)
//   facts.json                short facts the user stated about themselves
//   settings.json             {"enabled": true|false} - the UI toggle
//
// Design rules:
//   * every write goes to <file>.tmp and is renamed over the target, so a
//     crash never leaves a half-written store;
//   * a corrupted or missing file is treated as empty (the server must never
//     crash because of user data), and a corrupt facts file is moved aside;
//   * <client> is the X-Session-Id namespace (only relevant when an API key is
//     configured); every lookup is scoped to it, so one client can never read
//     another client's data;
//   * ids are validated ([A-Za-z0-9_-]{1,64}) before touching the filesystem.
#pragma once

#include <ctime>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "dumy/config.h"

namespace dumy {

struct MemoryRef {
    std::string client;        // namespace, "default" when no header is sent
    std::string conversation;  // conversation id ("" = not persisted)
};

struct StoredMessage {
    std::string role;      // "user" | "assistant"
    std::string text;
    std::string route;     // qtype shown in the UI ("fact_check", ...)
    std::vector<std::string> sources;  // "file.pdf səh. 12"
    long long latency_ms = 0;
    long long created = 0;
};

struct Conversation {
    std::string id;
    std::string title;
    long long created = 0;
    long long updated = 0;
    std::vector<StoredMessage> messages;

    nlohmann::json to_json(bool with_messages) const;
};

struct MemoryFact {
    std::string id;
    std::string text;
    std::string conversation_id;
    long long created = 0;

    nlohmann::json to_json() const;
};

class ConversationStore {
public:
    explicit ConversationStore(const Config& cfg);

    // --- ids -------------------------------------------------------------
    static bool valid_id(const std::string& id);
    static std::string sanitize_client(const std::string& raw);
    static std::string make_id(const char* prefix);
    // First user message -> a short title (no model call).
    static std::string make_title(const std::string& text, size_t max_chars = 40);

    // --- enable / disable ------------------------------------------------
    bool config_enabled() const { return cfg_.memory_enabled; }
    bool enabled() const;  // config flag AND the persisted UI toggle
    void set_enabled(bool on);

    // --- conversations ---------------------------------------------------
    Conversation create(const std::string& client, const std::string& title);
    bool get(const std::string& client, const std::string& id, Conversation* out);
    bool exists(const std::string& client, const std::string& id);
    std::vector<Conversation> list(const std::string& client);  // newest first
    bool rename(const std::string& client, const std::string& id,
                const std::string& title);
    bool remove(const std::string& client, const std::string& id);

    void append_turn(const std::string& client, const std::string& conv,
                     const std::string& question, const std::string& answer,
                     const std::string& route,
                     const std::vector<std::string>& sources, long long latency_ms);
    std::vector<StoredMessage> recent(const std::string& client,
                                      const std::string& conv,
                                      size_t max_messages);
    std::string last_user_text(const std::string& client,
                               const std::string& conv);

    // --- long-term facts -------------------------------------------------
    std::vector<MemoryFact> facts(const std::string& client);
    // Extracts + validates + dedupes + caps. Returns the stored text or "".
    std::string remember_from_question(const std::string& client,
                                       const std::string& question,
                                       const std::string& conv);
    bool remove_fact(const std::string& client, const std::string& id);
    void clear_facts(const std::string& client);

    // Deterministic extraction (public for the tests / pipeline reuse).
    static std::vector<std::string> extract_facts(const std::string& question);

    // Prompt block limited to `max_tokens`; empty when nothing is relevant.
    static std::string build_memory_block(const std::vector<MemoryFact>& facts,
                                          const std::string& question,
                                          size_t max_tokens,
                                          double chars_per_token);
    // The fact that directly answers "mənim neçə ... var?" (if any).
    static bool best_fact_for(const std::vector<MemoryFact>& facts,
                              const std::string& question, MemoryFact* out);

private:
    fs::path client_dir(const std::string& client) const;
    fs::path conv_dir(const std::string& client) const;
    fs::path conv_path(const std::string& client, const std::string& id) const;
    fs::path facts_path(const std::string& client) const;
    fs::path settings_path(const std::string& client) const;

    std::vector<MemoryFact> load_facts_locked(const std::string& client) const;
    bool save_facts_locked(const std::string& client,
                           const std::vector<MemoryFact>& facts);
    bool read_conv_locked(const std::string& client, const std::string& id,
                          Conversation* out) const;
    bool write_conv_locked(const std::string& client, const Conversation& c);
    bool read_setting_locked(const std::string& client) const;
    void write_setting_locked(const std::string& client, bool on);
    void purge_old_locked(const std::string& client) const;

    const Config& cfg_;
    mutable std::mutex mutex_;
};

long long mem_now_ms();

}  // namespace dumy
