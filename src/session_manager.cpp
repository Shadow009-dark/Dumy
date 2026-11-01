#include "dumy/session_manager.h"

#include <atomic>
#include <chrono>

#include "dumy/common.h"

namespace dumy {

SessionManager::SessionManager(size_t max_messages_per_session)
    : max_messages_(max_messages_per_session == 0 ? 8 : max_messages_per_session) {}

std::vector<ChatMessage> SessionManager::history(const std::string& session_id) const {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) return {};
    return std::vector<ChatMessage>(it->second.messages.begin(), it->second.messages.end());
}

void SessionManager::append(const std::string& session_id, const ChatMessage& user,
                            const ChatMessage& assistant) {
    if (session_id.empty()) return;
    std::lock_guard<std::mutex> lk(mutex_);
    auto& s = sessions_[session_id];
    s.messages.push_back(user);
    s.messages.push_back(assistant);
    // Keep only the newest N messages (always an even user/assistant count).
    while (s.messages.size() > max_messages_) s.messages.pop_front();
    while (s.messages.size() > 2 && s.messages.front().role != "user")
        s.messages.pop_front();  // never start history with an assistant turn
}

std::string SessionManager::make_session_id() {
    static std::atomic<int> counter{0};
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count();
    return "s-" + std::to_string(ms) + "-" +
           std::to_string(counter.fetch_add(1));
}

size_t SessionManager::session_count() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return sessions_.size();
}

}  // namespace dumy
