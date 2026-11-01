// session_manager.h - keeps the last N messages per session_id so the bot
// can answer follow-up questions ("o quzu neçə yaşındadır?" etc.).
// Thread-safe: several HTTP requests may touch the same/different sessions.
#pragma once

#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "dumy/ollama_client.h"

namespace dumy {

class SessionManager {
public:
    explicit SessionManager(size_t max_messages_per_session);

    // History for a session (oldest first). Unknown id -> empty.
    std::vector<ChatMessage> history(const std::string& session_id) const;

    // Appends a user/assistant turn and trims to the configured maximum.
    void append(const std::string& session_id, const ChatMessage& user,
                const ChatMessage& assistant);

    // Generates an id when the client did not send one.
    static std::string make_session_id();

    size_t session_count() const;

private:
    struct Session {
        std::deque<ChatMessage> messages;
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::string, Session> sessions_;
    size_t max_messages_;
};

}  // namespace dumy
