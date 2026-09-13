#include "events/bus.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <thread>

/// The lifecycle bus: advisory delivery that never blocks a publisher.
namespace {

using apogee::events::Bus;
using apogee::events::Event;
using apogee::events::kQueueDepth;
using apogee::events::Subscriber;
using apogee::events::Subscription;

Event named(std::string type) {
    Event event;
    event.type = std::move(type);
    return event;
}

}  // namespace

TEST_CASE("a subscriber receives what is published after it subscribed, stamped", "[events][bus]") {
    Bus bus;
    bus.publish(named("before"));  // nobody listening: a no-op, not a queue
    Subscription subscription = apogee::events::subscribe(bus);
    CHECK(bus.subscriber_count() == 1);

    Event with_data;
    with_data.type = "session.created";
    with_data.data = nlohmann::json{{"session_id", "abc"}};
    bus.publish(with_data);

    const auto received = subscription.subscriber().wait_for(std::chrono::milliseconds{50});
    REQUIRE(received.has_value());
    CHECK(received->type == "session.created");
    CHECK(received->data["session_id"] == "abc");
    // Stamped by publish, RFC 3339 UTC.
    CHECK(received->time.size() == 20);
    CHECK(received->time.back() == 'Z');
    // Nothing else: the pre-subscription event never existed for this subscriber.
    CHECK_FALSE(subscription.subscriber().wait_for(std::chrono::milliseconds{10}).has_value());
}

TEST_CASE("a slow subscriber drops rather than blocking the publisher", "[events][bus]") {
    Bus bus;
    Subscription subscription = apogee::events::subscribe(bus);
    // Never drained: the queue fills, then every further event is dropped and
    // counted -- and publish returns immediately every time.
    const auto started = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < kQueueDepth + 10; ++i) {
        bus.publish(named("burst"));
    }
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds{1});
    CHECK(subscription.subscriber().dropped() == 10);
    // The first kQueueDepth are still there, in order.
    std::size_t drained = 0;
    while (subscription.subscriber().wait_for(std::chrono::milliseconds{1}).has_value()) {
        ++drained;
    }
    CHECK(drained == kQueueDepth);
}

TEST_CASE("unsubscribing closes the queue and wakes a waiter", "[events][bus]") {
    Bus bus;
    auto subscriber = bus.subscribe();
    std::thread waiter{[subscriber] {
        // Closed with nothing queued: returns nullopt before the timeout.
        (void)subscriber->wait_for(std::chrono::seconds{10});
    }};
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    const auto started = std::chrono::steady_clock::now();
    bus.unsubscribe(subscriber);
    waiter.join();
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds{5});
    CHECK(subscriber->closed());
    CHECK(bus.subscriber_count() == 0);
    CHECK_FALSE(subscriber->offer(named("late")));
    // A second unsubscribe is harmless.
    bus.unsubscribe(subscriber);
}

TEST_CASE("close_all ends every subscription at once", "[events][bus]") {
    Bus bus;
    auto first = bus.subscribe();
    auto second = bus.subscribe();
    bus.close_all();
    CHECK(first->closed());
    CHECK(second->closed());
    CHECK(bus.subscriber_count() == 0);
}

TEST_CASE("the RAII subscription unsubscribes on scope exit", "[events][bus]") {
    Bus bus;
    {
        const Subscription subscription = apogee::events::subscribe(bus);
        CHECK(bus.subscriber_count() == 1);
    }
    CHECK(bus.subscriber_count() == 0);
}

TEST_CASE("the process-wide bus is one object and emit publishes to it", "[events][bus]") {
    Bus& bus = apogee::events::default_bus();
    CHECK(&bus == &apogee::events::default_bus());
    Subscription subscription = apogee::events::subscribe(bus);
    apogee::events::emit(apogee::events::kModelLoadStarted, nlohmann::json{{"model", "m"}});
    const auto received = subscription.subscriber().wait_for(std::chrono::milliseconds{50});
    REQUIRE(received.has_value());
    CHECK(received->type == "model.load.started");
    CHECK(received->data["model"] == "m");
}
