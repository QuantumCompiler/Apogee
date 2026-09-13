#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "logger/session.h"

/// Server-owned conversations.
///
/// Every cloud provider Apogee serves is stateless, so a client that wants a
/// conversation either re-sends the whole history on every request or asks the
/// server to keep it. This is the second option: the server holds the
/// transcript, the client sends only its new message, and the server
/// compacts the history before it overflows the model's window.
///
/// **A served session IS a chat session.** The store holds `logger::Session`
/// objects and persists each one through `logger::save` after every completed
/// turn -- the same file, in the same place, as `apogee chat` writes. So a
/// remote client's conversation is resumable from the terminal with
/// `apogee chat --resume <id>`, and nothing about it is a second format.
///
/// Eviction is **from memory only**. An idle session leaves the live set after
/// its TTL so a long-running server does not accumulate every conversation it
/// ever held; the transcript on disk is the user's and stays. A client that
/// sends an evicted id gets a typed 404 and knows to start again.
namespace apogee::httpserver {

struct SessionSummary {
    std::string id;
    std::string backend;
    int turns = 0;
    std::chrono::system_clock::time_point last_active;
};

class SessionStore {
public:
    using Clock = std::function<std::chrono::system_clock::time_point()>;

    /// `clock` is injected so a test can age sessions without sleeping. The
    /// default reads the system clock.
    explicit SessionStore(Clock clock = {});

    /// Mints a session bound to `backend`, persists it, and returns its id.
    /// The id is a chat id: `apogee chat --resume <id>` finds the file.
    [[nodiscard]] std::string create(const std::string& backend,
                                     const logger::InferenceParams& params);

    /// A copy of the live session, or nullopt when there is none by that id.
    [[nodiscard]] std::optional<logger::Session> get(std::string_view id) const;

    /// Stores `session` back after a completed turn and persists it. A
    /// session evicted while its turn was running is revived here: a session
    /// in use is, by definition, not idle.
    void commit(const logger::Session& session);

    /// Ends the live session. The transcript on disk is untouched. False when
    /// there was none.
    bool erase(std::string_view id);

    [[nodiscard]] std::vector<SessionSummary> list() const;

    /// Drops every session idle for longer than `ttl`. A zero or negative TTL
    /// disables eviction. Returns how many were dropped.
    std::size_t evict_idle(std::chrono::minutes ttl);

    [[nodiscard]] std::size_t size() const;

private:
    struct Live {
        logger::Session session;
        std::chrono::system_clock::time_point last_active;
    };

    Clock clock_;
    mutable std::mutex mutex_;
    std::map<std::string, Live, std::less<>> live_;
};

/// `when` as an RFC 3339 UTC timestamp, `2026-09-13T10:04:11Z`.
[[nodiscard]] std::string format_utc(std::chrono::system_clock::time_point when);

}  // namespace apogee::httpserver
