#include "httpserver/session.h"

#include <nlohmann/json.hpp>

#include <ctime>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

namespace apogee::httpserver {

std::string format_utc(std::chrono::system_clock::time_point when) {
    const std::time_t as_time = std::chrono::system_clock::to_time_t(when);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &as_time);
#else
    gmtime_r(&as_time, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

SessionStore::SessionStore(Clock clock, events::Bus* bus)
    : clock_{clock ? std::move(clock) : [] { return std::chrono::system_clock::now(); }},
      bus_{bus != nullptr ? bus : &events::default_bus()} {}

std::string SessionStore::create(const std::string& backend,
                                 const logger::InferenceParams& params) {
    logger::Session session;
    session.chat_id = logger::new_chat_id();
    session.backend = backend;
    session.params = params;
    if (!params.system_prompt.empty()) {
        session.messages.push_back(harness::ChatMessage::system(params.system_prompt));
    }
    // Persisted at birth, so the id handed to the client already names a
    // file. An empty transcript is a legitimate session -- `apogee chat`
    // saves one too when the user quits before asking anything.
    logger::save(session);

    const std::string id = session.chat_id;
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        live_[id] = Live{std::move(session), clock_()};
    }
    bus_->publish(events::Event{std::string{events::kSessionCreated},
                                {},
                                nlohmann::json{{"session_id", id}, {"model", backend}}});
    return id;
}

std::optional<logger::Session> SessionStore::get(std::string_view id) const {
    const std::lock_guard<std::mutex> lock{mutex_};
    const auto it = live_.find(id);
    if (it == live_.end()) {
        return std::nullopt;
    }
    return it->second.session;
}

void SessionStore::commit(const logger::Session& session) {
    // Persist after EVERY turn, before touching the live set: a crash between
    // the two leaves the transcript complete on disk, which is the property
    // that matters. The same rule `apogee chat` follows, for the same reason.
    logger::save(session);

    const std::lock_guard<std::mutex> lock{mutex_};
    live_[session.chat_id] = Live{session, clock_()};
}

bool SessionStore::erase(std::string_view id) {
    nlohmann::json data;
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        const auto it = live_.find(id);
        if (it == live_.end()) {
            return false;
        }
        data = nlohmann::json{{"session_id", it->first},
                              {"model", it->second.session.backend},
                              {"turns", it->second.session.turns},
                              {"reason", "deleted"}};
        live_.erase(it);
    }
    bus_->publish(events::Event{std::string{events::kSessionEvicted}, {}, std::move(data)});
    return true;
}

std::vector<SessionSummary> SessionStore::list() const {
    const std::lock_guard<std::mutex> lock{mutex_};
    std::vector<SessionSummary> out;
    out.reserve(live_.size());
    for (const auto& [id, live] : live_) {
        SessionSummary summary;
        summary.id = id;
        summary.backend = live.session.backend;
        summary.turns = live.session.turns;
        summary.last_active = live.last_active;
        out.push_back(std::move(summary));
    }
    return out;
}

std::size_t SessionStore::evict_idle(std::chrono::minutes ttl) {
    if (ttl.count() <= 0) {
        return 0;
    }
    const auto now = clock_();
    std::vector<nlohmann::json> evicted_data;
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        for (auto it = live_.begin(); it != live_.end();) {
            if (now - it->second.last_active > ttl) {
                evicted_data.push_back(nlohmann::json{{"session_id", it->first},
                                                      {"model", it->second.session.backend},
                                                      {"turns", it->second.session.turns},
                                                      {"reason", "ttl"}});
                it = live_.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (nlohmann::json& data : evicted_data) {
        bus_->publish(events::Event{std::string{events::kSessionEvicted}, {}, std::move(data)});
    }
    return evicted_data.size();
}

std::size_t SessionStore::size() const {
    const std::lock_guard<std::mutex> lock{mutex_};
    return live_.size();
}

}  // namespace apogee::httpserver
