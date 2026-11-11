#include "dumy/conversation_store.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

#include "dumy/az_quality.h"
#include "dumy/common.h"

namespace dumy {
namespace {

std::string collapse_spaces(const std::string& s) {
    std::string out;
    bool space = false;
    for (char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u == ' ' || u == '\t' || u == '\n' || u == '\r') {
            if (!out.empty()) space = true;
            continue;
        }
        if (space) {
            out.push_back(' ');
            space = false;
        }
        out.push_back(c);
    }
    return out;
}

std::string clamp_chars(const std::string& s, size_t max_chars) {
    if (max_chars == 0) return s;
    size_t chars = 0, cut = s.size();
    for (size_t i = 0; i < s.size(); ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
            if (chars == max_chars) {
                cut = i;
                break;
            }
            ++chars;
        }
    }
    if (cut >= s.size()) return s;
    // Do not cut a UTF-8 sequence in half.
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    return s.substr(0, cut) + "…";
}

bool contains_word(const std::string& padded, const char* word) {
    return padded.find(std::string(" ") + word + " ") != std::string::npos;
}

size_t digit_run(const std::string& s) {
    size_t best = 0, run = 0;
    for (char c : s) {
        if (c >= '0' && c <= '9') {
            ++run;
            best = std::max(best, run);
        } else {
            run = 0;
        }
    }
    return best;
}

bool has_digit(const std::string& s) {
    for (char c : s)
        if (c >= '0' && c <= '9') return true;
    return false;
}

// Sentence-ish split on . ! ? newline and semicolons.
std::vector<std::string> split_clauses(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text) {
        if (c == '.' || c == '!' || c == '?' || c == '\n' || c == ';') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

std::string strip_edges(std::string s) {
    s = trim(s);
    while (!s.empty() && (s.back() == ',' || s.back() == '.' || s.back() == '!' ||
                          s.back() == '?' || s.back() == ';' || s.back() == ' '))
        s.pop_back();
    while (!s.empty() && (s.front() == ',' || s.front() == ';' || s.front() == ' '))
        s.erase(s.begin());
    return s;
}

// Never persist anything that looks like a credential or sensitive detail.
bool looks_sensitive(const std::string& folded) {
    static const char* BAD[] = {
        "sifre",  "parol",   "password", "passwd",  "api key", "api acar",
        "token",  "secret",  "telefon",  "nomre",   "+994",    "pasport",
        "fin kod", "dogum tarixi", "diaqnoz", "xesteliyim", "hamile",
        "derman", "maas",    "kart",     "iban",    "voen",    "ssn"};
    const std::string p = " " + folded + " ";
    for (const char* w : BAD) {
        if (p.find(w) != std::string::npos) return true;
    }
    if (folded.find('@') != std::string::npos) return true;
    if (digit_run(folded) >= 7) return true;  // phone / ID numbers
    return false;
}

bool has_personal_marker(const std::string& folded) {
    const std::string p = " " + folded + " ";
    return contains_word(p, "menim") || contains_word(p, "mende") ||
           contains_word(p, "mene") || contains_word(p, "adim") ||
           contains_word(p, "men");
}

// Shared content stems, used for relevance and for "did the answer use it?".
std::vector<std::string> stems(const std::string& text) {
    const std::string folded = az_fold(text);
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&]() {
        if (cur.size() >= 4) out.push_back(cur.substr(0, std::min<size_t>(5, cur.size())));
        cur.clear();
    };
    for (size_t i = 0; i <= folded.size(); ++i) {
        const char c = i < folded.size() ? folded[i] : ' ';
        const bool letter = (c >= 'a' && c <= 'z');
        if (letter) {
            cur.push_back(c);
        } else {
            flush();
        }
    }
    return out;
}

size_t stem_overlap(const std::string& a, const std::string& b) {
    const auto sa = stems(a);
    const auto sb = stems(b);
    std::set<std::string> sb_set(sb.begin(), sb.end());
    size_t n = 0;
    std::set<std::string> used;
    for (const auto& s : sa) {
        if (sb_set.count(s) && used.insert(s).second) ++n;
    }
    return n;
}

bool write_atomic(const fs::path& path, const std::string& data) {
    std::error_code ec;
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(fs::u8path(tmp.u8string()),
                          std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        out.flush();
        if (!out) return false;
    }
    fs::remove(path, ec);  // Windows rename does not replace an existing file
    ec.clear();
    fs::rename(tmp, path, ec);
    if (ec) {
        std::error_code ec2;
        fs::remove(tmp, ec2);
        return false;
    }
    return true;
}

std::atomic<long long> g_id_counter{0};

}  // namespace

long long mem_now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

nlohmann::json Conversation::to_json(bool with_messages) const {
    nlohmann::json j = {{"id", id},
                        {"title", title},
                        {"created", created},
                        {"updated", updated},
                        {"message_count", messages.size()}};
    if (with_messages) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& m : messages) {
            arr.push_back({{"role", m.role},
                           {"text", m.text},
                           {"route", m.route},
                           {"sources", m.sources},
                           {"latency_ms", m.latency_ms},
                           {"created", m.created}});
        }
        j["messages"] = arr;
    }
    return j;
}

nlohmann::json MemoryFact::to_json() const {
    return {{"id", id}, {"text", text}, {"conversation_id", conversation_id},
            {"created", created}};
}

ConversationStore::ConversationStore(const Config& cfg) : cfg_(cfg) {}

// ---------------------------------------------------------------- ids ----

bool ConversationStore::valid_id(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (char c : id) {
        const unsigned char u = static_cast<unsigned char>(c);
        const bool ok = (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') ||
                        (u >= '0' && u <= '9') || c == '-' || c == '_';
        if (!ok) return false;
    }
    return true;
}

std::string ConversationStore::sanitize_client(const std::string& raw) {
    if (raw.empty()) return "default";
    std::string out;
    for (char c : raw) {
        const unsigned char u = static_cast<unsigned char>(c);
        const bool ok = (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') ||
                        (u >= '0' && u <= '9') || c == '-' || c == '_';
        if (!ok) return "default";  // odd header -> the shared default namespace
        out.push_back(c);
        if (out.size() >= 40) break;
    }
    return out.empty() ? "default" : out;
}

std::string ConversationStore::make_id(const char* prefix) {
    return std::string(prefix) + "-" + std::to_string(mem_now_ms()) + "-" +
           std::to_string(g_id_counter.fetch_add(1));
}

std::string ConversationStore::make_title(const std::string& text, size_t max_chars) {
    std::string t = collapse_spaces(strip_edges(text));
    if (t.empty()) return "Yeni söhbət";
    return clamp_chars(t, max_chars);
}

// -------------------------------------------------------------- paths ----

fs::path ConversationStore::client_dir(const std::string& client) const {
    return cfg_.data_dir() / "memory" / fs::u8path(sanitize_client(client));
}

fs::path ConversationStore::conv_dir(const std::string& client) const {
    return client_dir(client) / "conversations";
}

fs::path ConversationStore::conv_path(const std::string& client,
                                      const std::string& id) const {
    return conv_dir(client) / (id + ".json");
}

fs::path ConversationStore::facts_path(const std::string& client) const {
    return client_dir(client) / "facts.json";
}

fs::path ConversationStore::settings_path(const std::string& /*client*/) const {
    // The on/off switch is a product setting, not per-client data.
    return cfg_.data_dir() / "memory" / "settings.json";
}

// ------------------------------------------------------- enable/toggle ----

bool ConversationStore::enabled() const {
    if (!cfg_.memory_enabled) return false;
    std::lock_guard<std::mutex> lk(mutex_);
    return read_setting_locked("default");
}

void ConversationStore::set_enabled(bool on) {
    std::lock_guard<std::mutex> lk(mutex_);
    // The toggle is global (it is a product switch, not per-conversation data),
    // so it is stored once in the default namespace.
    write_setting_locked("default", on);
}

bool ConversationStore::read_setting_locked(const std::string& client) const {
    // Returns true unless the settings file explicitly stores false.
    const fs::path p = settings_path(client);
    std::string raw;
    if (!fs::exists(p)) return true;
    if (!read_text_file(p, raw) || trim(raw).empty()) return true;
    try {
        const auto j = nlohmann::json::parse(raw);
        if (j.is_object() && j.contains("enabled") && j["enabled"].is_boolean())
            return j["enabled"].get<bool>();
    } catch (const std::exception&) {
        // Corrupt setting -> keep memory on (the config default), never crash.
    }
    return true;
}

void ConversationStore::write_setting_locked(const std::string& client, bool on) {
    write_atomic(settings_path(client),
                 nlohmann::json({{"enabled", on}}).dump());
}

// ------------------------------------------------------ conversations ----

void ConversationStore::purge_old_locked(const std::string& client) const {
    const int days = cfg_.conversation_retention_days;
    if (days <= 0) return;  // 0 = keep forever
    std::error_code ec;
    const fs::path dir = conv_dir(client);
    if (!fs::exists(dir, ec)) return;
    const auto now = fs::file_time_type::clock::now();
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!e.is_regular_file(ec)) continue;
        auto t = fs::last_write_time(e.path(), ec);
        if (ec) continue;
        if (now - t > std::chrono::hours(24 * days)) {
            fs::remove(e.path(), ec);
        }
    }
}

Conversation ConversationStore::create(const std::string& client,
                                       const std::string& title) {
    Conversation c;
    c.id = make_id("c");
    c.title = title.empty() ? "Yeni söhbət" : make_title(title);
    c.created = mem_now_ms();
    c.updated = c.created;
    std::lock_guard<std::mutex> lk(mutex_);
    purge_old_locked(client);
    write_conv_locked(client, c);
    return c;
}

bool ConversationStore::read_conv_locked(const std::string& client,
                                         const std::string& id,
                                         Conversation* out) const {
    if (!out || !valid_id(id)) return false;
    std::string raw;
    if (!read_text_file(conv_path(client, id), raw) || trim(raw).empty()) return false;
    try {
        const auto j = nlohmann::json::parse(raw);
        if (!j.is_object()) return false;
        Conversation c;
        c.id = j.value("id", id);
        c.title = j.value("title", std::string("Söhbət"));
        c.created = j.value("created", 0LL);
        c.updated = j.value("updated", 0LL);
        if (j.contains("messages") && j["messages"].is_array()) {
            for (const auto& m : j["messages"]) {
                if (!m.is_object()) continue;
                StoredMessage sm;
                sm.role = m.value("role", std::string("user"));
                sm.text = m.value("text", std::string());
                sm.route = m.value("route", std::string());
                if (m.contains("sources") && m["sources"].is_array()) {
                    for (const auto& s : m["sources"]) {
                        if (s.is_string()) sm.sources.push_back(s.get<std::string>());
                    }
                }
                sm.latency_ms = m.value("latency_ms", 0LL);
                sm.created = m.value("created", 0LL);
                if (!sm.text.empty()) c.messages.push_back(std::move(sm));
            }
        }
        *out = std::move(c);
        return true;
    } catch (const std::exception& e) {
        log_warn(std::string("conversation store: unreadable ") + id + ": " + e.what());
        return false;  // treated as "not found", the server keeps running
    }
}

bool ConversationStore::write_conv_locked(const std::string& client,
                                          const Conversation& c) {
    if (!valid_id(c.id)) return false;
    return write_atomic(conv_path(client, c.id), c.to_json(true).dump());
}

bool ConversationStore::get(const std::string& client, const std::string& id,
                            Conversation* out) {
    std::lock_guard<std::mutex> lk(mutex_);
    return read_conv_locked(client, id, out);
}

bool ConversationStore::exists(const std::string& client, const std::string& id) {
    std::lock_guard<std::mutex> lk(mutex_);
    std::error_code ec;
    return valid_id(id) && fs::exists(conv_path(client, id), ec);
}

std::vector<Conversation> ConversationStore::list(const std::string& client) {
    std::lock_guard<std::mutex> lk(mutex_);
    purge_old_locked(client);
    std::vector<Conversation> out;
    std::error_code ec;
    const fs::path dir = conv_dir(client);
    if (!fs::exists(dir, ec)) return out;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!e.is_regular_file(ec)) continue;
        const std::string name = e.path().filename().u8string();
        if (name.size() < 6 || !ends_with(name, ".json")) continue;
        Conversation c;
        if (read_conv_locked(client, name.substr(0, name.size() - 5), &c))
            out.push_back(std::move(c));
    }
    std::sort(out.begin(), out.end(),
              [](const Conversation& a, const Conversation& b) {
                  return a.updated > b.updated;
              });
    return out;
}

bool ConversationStore::rename(const std::string& client, const std::string& id,
                               const std::string& title) {
    std::lock_guard<std::mutex> lk(mutex_);
    Conversation c;
    if (!read_conv_locked(client, id, &c)) return false;
    c.title = make_title(title);
    c.updated = mem_now_ms();
    return write_conv_locked(client, c);
}

bool ConversationStore::remove(const std::string& client, const std::string& id) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!valid_id(id)) return false;
    std::error_code ec;
    return fs::remove(conv_path(client, id), ec);
}

void ConversationStore::append_turn(const std::string& client,
                                    const std::string& conv,
                                    const std::string& question,
                                    const std::string& answer,
                                    const std::string& route,
                                    const std::vector<std::string>& sources,
                                    long long latency_ms) {
    if (!valid_id(conv)) return;
    std::lock_guard<std::mutex> lk(mutex_);
    Conversation c;
    if (!read_conv_locked(client, conv, &c)) {
        c.id = conv;
        c.title = "Yeni söhbət";
        c.created = mem_now_ms();
    }
    const long long now = mem_now_ms();
    StoredMessage u;
    u.role = "user";
    u.text = question;
    u.route = route;
    u.created = now;
    c.messages.push_back(std::move(u));

    StoredMessage a;
    a.role = "assistant";
    a.text = answer;
    a.route = route;
    a.sources = sources;
    a.latency_ms = latency_ms;
    a.created = mem_now_ms();
    c.messages.push_back(std::move(a));

    if (c.title.empty() || c.title == "Yeni söhbət") c.title = make_title(question);
    c.updated = now;
    if (c.created == 0) c.created = now;
    write_conv_locked(client, c);
}

std::vector<StoredMessage> ConversationStore::recent(const std::string& client,
                                                     const std::string& conv,
                                                     size_t max_messages) {
    std::lock_guard<std::mutex> lk(mutex_);
    Conversation c;
    if (!read_conv_locked(client, conv, &c)) return {};
    if (max_messages == 0 || c.messages.size() <= max_messages)
        return c.messages;
    return std::vector<StoredMessage>(c.messages.end() -
                                          static_cast<long>(max_messages),
                                      c.messages.end());
}

std::string ConversationStore::last_user_text(const std::string& client,
                                              const std::string& conv) {
    const auto msgs = recent(client, conv, 12);
    for (auto it = msgs.rbegin(); it != msgs.rend(); ++it) {
        if (it->role == "user" && !trim(it->text).empty()) return it->text;
    }
    return {};
}

// --------------------------------------------------------------- facts ----

std::vector<std::string> ConversationStore::extract_facts(const std::string& question) {
    std::vector<std::string> out;
    // A question never states a fact: "mənim neçə qoyunum var?" must not be
    // stored as "mənim neçə qoyunum var".
    if (question.find('?') != std::string::npos) return out;
    for (const auto& raw_clause : split_clauses(question)) {
        std::string clause = collapse_spaces(strip_edges(raw_clause));
        if (clause.size() < 6 || clause.size() > 160) continue;
        // A question is not a statement about the user.
        if (trim(raw_clause).find('?') != std::string::npos) continue;

        const std::string folded = az_fold(clause);
        const std::string p = " " + folded + " ";
        if (looks_sensitive(folded)) continue;
        if (contains_word(p, "nece") || contains_word(p, "ne qeder")) continue;

        bool keep = false;
        if (contains_word(p, "adim")) keep = true;               // "adım Sahdow"
        if (contains_word(p, "menim") || contains_word(p, "mende") ||
            contains_word(p, "men")) {
            if (contains_word(p, "var") || contains_word(p, "isleyirem") ||
                contains_word(p, "mesgulam"))
                keep = true;                                     // "30 qoyunum var"
        }
        if (contains_word(p, "fermam") || contains_word(p, "farmam") ||
            contains_word(p, "teserrufatim") || contains_word(p, "surum"))
            keep = true;                                         // "südçülük fermam var"

        if (!keep) continue;

        // The persisted text must describe the user, not the bot's answer.
        clause = clamp_chars(clause, 120);
        if (clause.empty()) continue;
        bool dup = false;
        for (const auto& seen : out) {
            if (az_fold(seen) == az_fold(clause)) {
                dup = true;
                break;
            }
        }
        if (!dup) out.push_back(clause);
    }
    return out;
}

std::vector<MemoryFact> ConversationStore::load_facts_locked(
    const std::string& client) const {
    std::vector<MemoryFact> out;
    const fs::path p = facts_path(client);
    std::error_code ec;
    if (!fs::exists(p, ec)) return out;
    std::string raw;
    if (!read_text_file(p, raw)) return out;
    if (trim(raw).empty()) return out;
    try {
        const auto j = nlohmann::json::parse(raw);
        if (!j.is_object() || !j.contains("facts") || !j["facts"].is_array())
            return out;
        for (const auto& f : j["facts"]) {
            if (!f.is_object()) continue;
            MemoryFact m;
            m.id = f.value("id", std::string());
            m.text = f.value("text", std::string());
            m.conversation_id = f.value("conversation_id", std::string());
            m.created = f.value("created", 0LL);
            if (m.id.empty() || m.text.empty() || !valid_id(m.id)) continue;
            out.push_back(std::move(m));
        }
    } catch (const std::exception& e) {
        // Corruption safety: keep the bad file for inspection, start empty.
        log_warn(std::string("memory: corrupt facts file, starting empty: ") + e.what());
        std::error_code ec2;
        fs::path bad = p;
        bad += ".corrupt";
        fs::rename(p, bad, ec2);
        out.clear();
    }
    return out;
}

bool ConversationStore::save_facts_locked(const std::string& client,
                                          const std::vector<MemoryFact>& facts) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& f : facts) arr.push_back(f.to_json());
    return write_atomic(facts_path(client),
                        nlohmann::json({{"facts", arr}}).dump());
}

std::vector<MemoryFact> ConversationStore::facts(const std::string& client) {
    if (!cfg_.memory_enabled) return {};
    std::lock_guard<std::mutex> lk(mutex_);
    if (!read_setting_locked(client)) return {};
    return load_facts_locked(client);
}

std::string ConversationStore::remember_from_question(const std::string& client,
                                                      const std::string& question,
                                                      const std::string& conv) {
    if (!cfg_.memory_enabled) return {};
    const auto extracted = extract_facts(question);
    if (extracted.empty()) return {};

    std::lock_guard<std::mutex> lk(mutex_);
    if (!read_setting_locked(client)) return {};
    auto stored = load_facts_locked(client);

    std::string saved;
    const int cap = std::max(1, cfg_.memory_max_facts);
    for (const auto& text : extracted) {
        const std::string folded = az_fold(text);
        bool dup = false;
        for (const auto& f : stored) {
            const std::string existing = az_fold(f.text);
            if (existing == folded || existing.find(folded) != std::string::npos ||
                folded.find(existing) != std::string::npos) {
                dup = true;
                break;
            }
        }
        if (dup) continue;
        MemoryFact f;
        f.id = make_id("f");
        f.text = text;
        f.conversation_id = valid_id(conv) ? conv : std::string();
        f.created = mem_now_ms();
        stored.push_back(f);
        saved = text;
    }
    if (saved.empty()) return {};

    // Cap: oldest facts fall out first.
    while (static_cast<int>(stored.size()) > cap)
        stored.erase(stored.begin());
    save_facts_locked(client, stored);
    return saved;
}

bool ConversationStore::remove_fact(const std::string& client, const std::string& id) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto stored = load_facts_locked(client);
    const auto before = stored.size();
    stored.erase(std::remove_if(stored.begin(), stored.end(),
                                [&](const MemoryFact& f) { return f.id == id; }),
                 stored.end());
    if (stored.size() == before) return false;
    save_facts_locked(client, stored);
    return true;
}

void ConversationStore::clear_facts(const std::string& client) {
    std::lock_guard<std::mutex> lk(mutex_);
    save_facts_locked(client, {});
}

bool ConversationStore::best_fact_for(const std::vector<MemoryFact>& facts,
                                      const std::string& question,
                                      MemoryFact* out) {
    if (!out) return false;
    const std::string folded = az_fold(question);
    const std::string p = " " + folded + " ";
    const bool personal = contains_word(p, "menim") || contains_word(p, "mende") ||
                          contains_word(p, "mene") || contains_word(p, "adim") ||
                          contains_word(p, "men");
    const bool asks_count =
        question.find("neçə") != std::string::npos ||
        question.find("nece") != std::string::npos ||
        folded.find("ne qeder") != std::string::npos ||
        folded.find("sayi") != std::string::npos;
    if (!personal || !asks_count) return false;

    bool found = false;
    size_t best = 0;
    for (const auto& f : facts) {
        if (!has_digit(f.text)) continue;
        const size_t score = stem_overlap(f.text, question);
        if (score == 0) continue;
        if (!found || score > best) {
            *out = f;
            best = score;
            found = true;
        }
    }
    return found;
}

std::string ConversationStore::build_memory_block(
    const std::vector<MemoryFact>& facts, const std::string& question,
    size_t max_tokens, double chars_per_token) {
    if (facts.empty() || max_tokens == 0) return {};
    struct Scored {
        const MemoryFact* f;
        size_t score;
    };
    std::vector<Scored> scored;
    for (const auto& f : facts)
        scored.push_back({&f, stem_overlap(f.text, question)});
    std::stable_sort(scored.begin(), scored.end(),
                     [](const Scored& a, const Scored& b) {
                         return a.score > b.score;
                     });

    const std::string folded = az_fold(question);
    const std::string p = " " + folded + " ";
    const bool personal = contains_word(p, "menim") || contains_word(p, "mende") ||
                          contains_word(p, "mene") || contains_word(p, "adim") ||
                          contains_word(p, "men");

    std::string block =
        "[Söhbət yaddaşı - istifadəçinin özü haqqında əvvəl dedikləri]\n";
    size_t used = 0;
    int taken = 0;
    for (const auto& s : scored) {
        if (taken >= 3) break;
        if (s.score == 0 && !personal) continue;
        if (s.score == 0 && taken >= 1) break;  // only the newest bleed-in
        std::string line = "- " + s.f->text + "\n";
        const size_t cost = static_cast<size_t>(
            std::ceil(static_cast<double>(line.size()) /
                      (chars_per_token > 0.5 ? chars_per_token : 2.6)));
        if (used + cost > max_tokens) break;
        block += line;
        used += cost;
        ++taken;
    }
    if (taken == 0) return {};
    return block;
}

}  // namespace dumy
