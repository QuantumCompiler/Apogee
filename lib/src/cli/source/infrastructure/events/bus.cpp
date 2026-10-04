#include "events/bus.h"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <utility>

namespace apogee::events {

std::string utc_now() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

// ---------------------------------------------------------------------------
// Subscriber
// ---------------------------------------------------------------------------

bool Subscriber::offer(Event event) {
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        if (closed_) {
            return false;
        }
        if (queue_.size() >= kQueueDepth) {
            // Drop rather than block: the emitter is a model turn or a session
            // store, and neither may wait on a client that stopped reading.
            ++dropped_;
            return false;
        }
        queue_.push_back(std::move(event));
    }
    ready_.notify_one();
    return true;
}

std::optional<Event> Subscriber::wait_for(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock{mutex_};
    ready_.wait_for(lock, timeout, [this] { return closed_ || !queue_.empty(); });
    if (queue_.empty()) {
        return std::nullopt;
    }
    Event event = std::move(queue_.front());
    queue_.pop_front();
    return event;
}

void Subscriber::close() {
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        closed_ = true;
    }
    ready_.notify_all();
}

bool Subscriber::closed() const {
    const std::lock_guard<std::mutex> lock{mutex_};
    return closed_;
}

std::size_t Subscriber::dropped() const {
    const std::lock_guard<std::mutex> lock{mutex_};
    return dropped_;
}

// ---------------------------------------------------------------------------
// Bus
// ---------------------------------------------------------------------------

std::shared_ptr<Subscriber> Bus::subscribe() {
    auto subscriber = std::make_shared<Subscriber>();
    const std::lock_guard<std::mutex> lock{mutex_};
    subscribers_.push_back(subscriber);
    return subscriber;
}

void Bus::unsubscribe(const std::shared_ptr<Subscriber>& subscriber) {
    if (subscriber == nullptr) {
        return;
    }
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        std::erase(subscribers_, subscriber);
    }
    subscriber->close();
}

void Bus::publish(Event event) {
    if (event.time.empty()) {
        event.time = utc_now();
    }
    // The list is copied under the lock and delivered outside it: a
    // subscriber's own mutex is taken by offer(), and holding both would make
    // unsubscribe-from-inside-a-consumer a deadlock.
    std::vector<std::shared_ptr<Subscriber>> targets;
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        targets = subscribers_;
    }
    for (const std::shared_ptr<Subscriber>& subscriber : targets) {
        (void)subscriber->offer(event);
    }
}

void Bus::close_all() {
    std::vector<std::shared_ptr<Subscriber>> targets;
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        targets.swap(subscribers_);
    }
    for (const std::shared_ptr<Subscriber>& subscriber : targets) {
        subscriber->close();
    }
}

std::size_t Bus::subscriber_count() const {
    const std::lock_guard<std::mutex> lock{mutex_};
    return subscribers_.size();
}

// ---------------------------------------------------------------------------
// Subscription
// ---------------------------------------------------------------------------

Subscription::Subscription(Bus& bus, std::shared_ptr<Subscriber> subscriber)
    : bus_{&bus}, subscriber_{std::move(subscriber)} {}

Subscription::~Subscription() {
    if (bus_ != nullptr && subscriber_ != nullptr) {
        bus_->unsubscribe(subscriber_);
    }
}

Subscription::Subscription(Subscription&& other) noexcept
    : bus_{other.bus_}, subscriber_{std::move(other.subscriber_)} {
    other.bus_ = nullptr;
}

Subscription& Subscription::operator=(Subscription&& other) noexcept {
    if (this != &other) {
        if (bus_ != nullptr && subscriber_ != nullptr) {
            bus_->unsubscribe(subscriber_);
        }
        bus_ = other.bus_;
        subscriber_ = std::move(other.subscriber_);
        other.bus_ = nullptr;
    }
    return *this;
}

Subscription subscribe(Bus& bus) {
    return Subscription{bus, bus.subscribe()};
}

Bus& default_bus() {
    static Bus bus;
    return bus;
}

void emit(std::string_view type, nlohmann::json data) {
    Event event;
    event.type = std::string{type};
    event.data = std::move(data);
    default_bus().publish(std::move(event));
}

}  // namespace apogee::events
