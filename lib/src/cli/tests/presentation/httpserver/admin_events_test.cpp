#include "httpserver/admin_events.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "events/bus.h"
#include "httpserver/shutdown.h"

/// The lifecycle stream: named events, heartbeats, and the ways it ends.
namespace {

using apogee::events::Bus;
using apogee::events::Event;
using apogee::httpserver::event_stream;
using apogee::httpserver::EventStreamOptions;
using apogee::httpserver::HttpResponse;

/// Splits an SSE body on blank lines.
std::vector<std::string> frames(const std::string& body) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start < body.size()) {
        const std::size_t end = body.find("\n\n", start);
        REQUIRE(end != std::string::npos);
        out.push_back(body.substr(start, end - start));
        start = end + 2;
    }
    return out;
}

}  // namespace

TEST_CASE("an event is a named frame carrying the whole event", "[httpserver][events]") {
    Event event;
    event.type = "session.created";
    event.time = "2026-09-13T00:00:00Z";
    event.data = nlohmann::json{{"session_id", "abc"}};
    const std::string frame = apogee::httpserver::sse_named_event(event);
    CHECK(frame.rfind("event: session.created\ndata: ", 0) == 0);
    CHECK(frame.substr(frame.size() - 2) == "\n\n");
    const std::string payload = frame.substr(frame.find("data: ") + 6);
    const nlohmann::json json = nlohmann::json::parse(payload.substr(0, payload.size() - 2));
    CHECK(json["type"] == "session.created");
    CHECK(json["time"] == "2026-09-13T00:00:00Z");
    CHECK(json["data"]["session_id"] == "abc");
}

TEST_CASE(
    "the stream opens, delivers published events, heartbeats, and ends when the client "
    "leaves",
    "[httpserver][events]") {
    apogee::httpserver::clear_shutdown();
    Bus bus;
    EventStreamOptions options;
    options.heartbeat = std::chrono::milliseconds{30};
    options.poll = std::chrono::milliseconds{5};
    const HttpResponse response = event_stream(bus, options);
    CHECK(response.content_type == "text/event-stream");
    CHECK(response.headers.at("X-Accel-Buffering") == "no");
    REQUIRE(response.streamed());

    // A publisher on another thread, once the subscription exists.
    std::thread publisher{[&bus] {
        while (bus.subscriber_count() == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        Event event;
        event.type = "agent.run.started";
        bus.publish(event);
    }};

    // The client: reads the opening comment, the event, then at least one
    // heartbeat, then hangs up. Bounded by a deadline as well, so a stream
    // that never heartbeats FAILS this test rather than hanging it -- which
    // is what the first version did under exactly that mutation.
    std::string body;
    int writes = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    response.stream([&](std::string_view piece) {
        body += piece;
        ++writes;
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        return body.find(": heartbeat") == std::string::npos || writes < 3;
    });
    publisher.join();
    CHECK(body.find(": heartbeat") != std::string::npos);

    const std::vector<std::string> received = frames(body);
    REQUIRE(received.size() >= 3);
    CHECK(received[0] == ": apogee event stream open");
    CHECK(received[1].rfind("event: agent.run.started", 0) == 0);
    CHECK(received.back() == ": heartbeat");
    // The client leaving unsubscribed it.
    CHECK(bus.subscriber_count() == 0);
}

TEST_CASE("the stream ends the moment an event write fails, not at the next heartbeat",
          "[httpserver][events]") {
    // A client that hangs up while an event is being written must end the
    // stream right there. The heartbeat path ends it too, but 25 seconds
    // later -- and that path is not the one a departing client hits.
    apogee::httpserver::clear_shutdown();
    Bus bus;
    EventStreamOptions options;
    options.heartbeat = std::chrono::seconds{60};  // never within this test
    options.poll = std::chrono::milliseconds{5};
    const HttpResponse response = event_stream(bus, options);

    std::thread publisher{[&bus] {
        while (bus.subscriber_count() == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        Event first;
        first.type = "first";
        bus.publish(first);
        std::this_thread::sleep_for(std::chrono::milliseconds{60});
        Event second;
        second.type = "second";
        bus.publish(second);
    }};

    // The client: accepts the opening comment, hangs up ON the first event.
    // Should the stream ignore that and keep going, the third write is where
    // it gets told to stop -- so a wrong implementation fails rather than
    // hangs, and the body shows the event that should never have been sent.
    std::string body;
    int writes = 0;
    response.stream([&](std::string_view piece) {
        body += piece;
        ++writes;
        if (writes >= 3) {
            apogee::httpserver::request_shutdown();
        }
        return writes < 2;
    });
    publisher.join();
    apogee::httpserver::clear_shutdown();

    CHECK(body.find("event: first") != std::string::npos);
    CHECK(body.find("event: second") == std::string::npos);
    CHECK(writes == 2);
    CHECK(bus.subscriber_count() == 0);
}

TEST_CASE("the stream ends when the server is stopping", "[httpserver][events]") {
    Bus bus;
    EventStreamOptions options;
    options.heartbeat = std::chrono::seconds{60};  // never within this test
    options.poll = std::chrono::milliseconds{5};
    const HttpResponse response = event_stream(bus, options);

    apogee::httpserver::clear_shutdown();
    std::thread stopper{[] {
        std::this_thread::sleep_for(std::chrono::milliseconds{30});
        apogee::httpserver::request_shutdown();
    }};
    const auto started = std::chrono::steady_clock::now();
    std::string body;
    response.stream([&body, started](std::string_view piece) {
        body += piece;
        // Bounded: a stream deaf to shutdown must fail, not hang.
        return std::chrono::steady_clock::now() - started < std::chrono::seconds{3};
    });
    stopper.join();
    apogee::httpserver::clear_shutdown();
    // Returned promptly on the flag, not after the heartbeat.
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds{5});
    CHECK(body == ": apogee event stream open\n\n");
    CHECK(bus.subscriber_count() == 0);
}
