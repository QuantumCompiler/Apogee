#pragma once

#include <nlohmann/json.hpp>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// The process-wide publish/subscribe hub for lifecycle events.
///
/// **A leaf package.** It includes nothing else from the project, so any
/// subsystem -- the session store, a backend loading a model, the job
/// registry -- can publish without an include cycle, and `tests/layering.cmake`
/// holds it to that. The HTTP control plane subscribes and streams what it
/// hears to remote clients over `GET /v1/admin/events`; in a process with no
/// subscriber -- a one-shot `complete` -- publishing is a mutex and an empty
/// list, so emitters call it unconditionally.
///
/// **Delivery is advisory, never load-bearing.** A subscriber holds a bounded
/// queue; publishing never blocks, and a subscriber that has fallen 256 events
/// behind drops the next one rather than stalling the emitter. Lifecycle
/// events describe what happened -- they are not how anything is made to
/// happen -- so a dropped frame under burst is acceptable and a stalled model
/// turn is not.
namespace apogee::events {

/// The event-type catalog, as `GET /v1/admin/events` names them. Documented for
/// client authors in `documentation/reference/http-api.md`; keep the two in step.
inline constexpr std::string_view kSessionCreated = "session.created";
inline constexpr std::string_view kSessionEvicted = "session.evicted";
inline constexpr std::string_view kModelLoadStarted = "model.load.started";
inline constexpr std::string_view kModelLoadCompleted = "model.load.completed";
inline constexpr std::string_view kAgentRunStarted = "agent.run.started";
inline constexpr std::string_view kAgentRunCompleted = "agent.run.completed";
inline constexpr std::string_view kJobStarted = "admin.job.started";
inline constexpr std::string_view kJobProgress = "admin.job.progress";
inline constexpr std::string_view kJobCompleted = "admin.job.completed";
inline constexpr std::string_view kJobFailed = "admin.job.failed";
inline constexpr std::string_view kJobCancelled = "admin.job.cancelled";

/// One lifecycle event. `data` carries the event-specific fields and is
/// serialized verbatim.
struct Event {
    std::string type;
    /// RFC 3339 UTC, stamped by `publish` when left empty.
    std::string time;
    nlohmann::json data = nlohmann::json::object();
};

/// Per-subscriber queue depth: generous enough that a client draining at a
/// normal pace never drops, small enough to bound memory per subscriber.
inline constexpr std::size_t kQueueDepth = 256;

/// One subscriber's bounded queue. Thread-safe; the bus offers, the consumer
/// waits.
class Subscriber {
public:
    /// Queues `event`. False when the queue is full (the event is dropped and
    /// counted) or the subscriber is closed.
    bool offer(Event event);

    /// The next event, or nullopt after `timeout` with nothing queued, or
    /// immediately once closed with an empty queue.
    [[nodiscard]] std::optional<Event> wait_for(std::chrono::milliseconds timeout);

    /// Wakes any waiter and refuses further events. Idempotent.
    void close();

    [[nodiscard]] bool closed() const;

    /// Events dropped because the queue was full.
    [[nodiscard]] std::size_t dropped() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<Event> queue_;
    bool closed_ = false;
    std::size_t dropped_ = 0;
};

class Bus {
public:
    /// Registers a new subscriber. Pair with `unsubscribe`, or hold a
    /// `Subscription`, which does it on destruction.
    [[nodiscard]] std::shared_ptr<Subscriber> subscribe();

    /// Removes and closes `subscriber`. Safe to call twice.
    void unsubscribe(const std::shared_ptr<Subscriber>& subscriber);

    /// Fans `event` out to every subscriber without blocking, stamping `time`
    /// when the caller left it empty.
    void publish(Event event);

    /// Closes every subscriber -- server shutdown.
    void close_all();

    [[nodiscard]] std::size_t subscriber_count() const;

private:
    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<Subscriber>> subscribers_;
};

/// An RAII subscription: unsubscribes when it goes out of scope, which is what
/// a streaming handler wants -- the client disconnecting must not leave a
/// queue filling up forever.
class Subscription {
public:
    Subscription(Bus& bus, std::shared_ptr<Subscriber> subscriber);
    ~Subscription();

    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    Subscription(Subscription&& other) noexcept;
    Subscription& operator=(Subscription&& other) noexcept;

    [[nodiscard]] Subscriber& subscriber() noexcept {
        return *subscriber_;
    }

private:
    Bus* bus_;
    std::shared_ptr<Subscriber> subscriber_;
};

[[nodiscard]] Subscription subscribe(Bus& bus);

/// The process-wide bus emitters publish to and the control plane reads.
[[nodiscard]] Bus& default_bus();

/// `default_bus().publish({type, {}, data})`.
void emit(std::string_view type, nlohmann::json data = nlohmann::json::object());

/// The current time as RFC 3339 UTC, `2026-09-13T10:04:11Z`.
[[nodiscard]] std::string utc_now();

}  // namespace apogee::events
