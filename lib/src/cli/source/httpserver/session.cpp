#include "httpserver/session.h"

#include <ctime>
#include <iomanip>
#include <sstream>
#include <utility>

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

SessionStore::SessionStore(Clock clock)
    : clock_{clock ? std::move(clock) : [] { return std::chrono::system_clock::now(); }} {}

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

    const std::lock_guard<std::mutex> lock{mutex_};
    const std::string id = session.chat_id;
    live_[id] = Live{std::move(session), clock_()};
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
    const std::lock_guard<std::mutex> lock{mutex_};
    const auto it = live_.find(id);
    if (it == live_.end()) {
        return false;
    }
    live_.erase(it);
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
    const std::lock_guard<std::mutex> lock{mutex_};
    std::size_t evicted = 0;
    for (auto it = live_.begin(); it != live_.end();) {
        if (now - it->second.last_active > ttl) {
            it = live_.erase(it);
            ++evicted;
        } else {
            ++it;
        }
    }
    return evicted;
}

std::size_t SessionStore::size() const {
    const std::lock_guard<std::mutex> lock{mutex_};
    return live_.size();
}

}  // namespace apogee::httpserver
