#include "backends/sse_parser.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using apogee::backends::SseEvent;
using apogee::backends::SseParser;

namespace {

/// Collects every event a parser emits.
struct Collector {
    std::vector<SseEvent> events;

    [[nodiscard]] apogee::backends::SseEventSink sink() {
        return [this](const SseEvent& event) {
            events.push_back(event);
            return true;
        };
    }
};

constexpr std::string_view kStream =
    "event: message_start\n"
    "data: {\"type\":\"message_start\"}\n"
    "\n"
    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"delta\":{\"text\":\"Hello\"}}\n"
    "\n"
    ": ping\n"
    "\n"
    "event: message_stop\n"
    "data: {\"type\":\"message_stop\"}\n"
    "\n";

std::vector<SseEvent> parse_in_chunks(std::string_view stream, std::size_t chunk_size) {
    Collector collector;
    SseParser parser{collector.sink()};
    for (std::size_t offset = 0; offset < stream.size(); offset += chunk_size) {
        REQUIRE(parser.feed(stream.substr(offset, chunk_size)));
    }
    parser.finish();
    return collector.events;
}

}  // namespace

TEST_CASE("a whole stream parses into its events", "[backends][sse]") {
    const auto events = parse_in_chunks(kStream, kStream.size());

    REQUIRE(events.size() == 3);
    CHECK(events[0].name == "message_start");
    CHECK(events[1].name == "content_block_delta");
    CHECK(events[1].data.find("Hello") != std::string::npos);
    CHECK(events[2].name == "message_stop");
}

TEST_CASE("the same stream parses identically at EVERY chunk size", "[backends][sse][split]") {
    // The heart of the parser, and the bug it exists to prevent: an SSE event
    // does not arrive in one read. A chunk boundary lands mid-frame, mid-line,
    // and mid-token whenever the network feels like it. A parser that assumes
    // "one read == one event" works perfectly on a fast local connection and
    // drops tokens on a slow one -- passing every test written on a
    // developer's machine.
    const auto expected = parse_in_chunks(kStream, kStream.size());

    for (std::size_t chunk = 1; chunk <= kStream.size(); ++chunk) {
        INFO("chunk size " << chunk);
        const auto actual = parse_in_chunks(kStream, chunk);
        REQUIRE(actual.size() == expected.size());
        for (std::size_t i = 0; i < actual.size(); ++i) {
            CHECK(actual[i].name == expected[i].name);
            CHECK(actual[i].data == expected[i].data);
        }
    }
}

TEST_CASE("one byte at a time still yields whole events", "[backends][sse][split]") {
    // Called out separately because it is the pathological case and the one
    // most likely to expose an off-by-one in the carry buffer.
    const auto events = parse_in_chunks(kStream, 1);
    REQUIRE(events.size() == 3);
    CHECK(events[1].data == R"({"type":"content_block_delta","delta":{"text":"Hello"}})");
}

TEST_CASE("comment lines are ignored, not treated as data", "[backends][sse]") {
    // Keep-alive pings arrive as `: ping`. Mistaking one for data injects
    // garbage into the answer.
    Collector collector;
    SseParser parser{collector.sink()};
    parser.feed(": keep-alive\n\n: another\n\ndata: real\n\n");
    parser.finish();

    REQUIRE(collector.events.size() == 1);
    CHECK(collector.events[0].data == "real");
}

TEST_CASE("repeated data fields join with newlines", "[backends][sse]") {
    Collector collector;
    SseParser parser{collector.sink()};
    parser.feed("data: line one\ndata: line two\n\n");
    parser.finish();

    REQUIRE(collector.events.size() == 1);
    CHECK(collector.events[0].data == "line one\nline two");
}

TEST_CASE("CRLF terminators are accepted", "[backends][sse]") {
    // The spec allows both, and a proxy may rewrite one into the other.
    Collector collector;
    SseParser parser{collector.sink()};
    parser.feed("event: e\r\ndata: {\"a\":1}\r\n\r\n");
    parser.finish();

    REQUIRE(collector.events.size() == 1);
    CHECK(collector.events[0].name == "e");
    CHECK(collector.events[0].data == R"({"a":1})");
}

TEST_CASE("a value with no leading space parses the same", "[backends][sse]") {
    Collector collector;
    SseParser parser{collector.sink()};
    parser.feed("data:no-space\n\ndata: with-space\n\n");
    parser.finish();

    REQUIRE(collector.events.size() == 2);
    CHECK(collector.events[0].data == "no-space");
    CHECK(collector.events[1].data == "with-space");
}

TEST_CASE("an unterminated final event is flushed by finish()", "[backends][sse][split]") {
    // A connection that drops mid-stream never sends the closing blank line,
    // and the final event is often the one carrying the stop reason and token
    // usage -- exactly the accounting the caller needs most.
    Collector collector;
    SseParser parser{collector.sink()};
    parser.feed("data: {\"type\":\"message_delta\"}");
    CHECK(collector.events.empty());

    parser.finish();
    REQUIRE(collector.events.size() == 1);
    CHECK(collector.events[0].data == R"({"type":"message_delta"})");
}

TEST_CASE("finish() on an empty parser emits nothing", "[backends][sse]") {
    Collector collector;
    SseParser parser{collector.sink()};
    parser.finish();
    CHECK(collector.events.empty());
}

TEST_CASE("blank lines between events do not emit empty events", "[backends][sse]") {
    Collector collector;
    SseParser parser{collector.sink()};
    parser.feed("\n\n\ndata: x\n\n\n\n");
    parser.finish();
    REQUIRE(collector.events.size() == 1);
    CHECK(collector.events[0].data == "x");
}

TEST_CASE("unknown fields are ignored rather than failing the stream", "[backends][sse]") {
    // A vendor adding a field must not turn a working stream into an outage
    // mid-answer.
    Collector collector;
    SseParser parser{collector.sink()};
    parser.feed("retry: 3000\nfuture-field: whatever\ndata: x\nid: 7\n\n");
    parser.finish();

    REQUIRE(collector.events.size() == 1);
    CHECK(collector.events[0].data == "x");
    CHECK(collector.events[0].id == "7");
}

TEST_CASE("a sink returning false stops the parser", "[backends][sse]") {
    int seen = 0;
    SseParser parser{[&seen](const SseEvent&) {
        ++seen;
        return seen < 2;  // stop after the second
    }};

    CHECK(parser.feed("data: a\n\ndata: b\n\ndata: c\n\n") == false);
    CHECK(seen == 2);
    CHECK(parser.stopped());
    // Further feeding is a no-op.
    CHECK(parser.feed("data: d\n\n") == false);
    CHECK(seen == 2);
}

TEST_CASE("a data payload containing a colon survives intact", "[backends][sse]") {
    // JSON is full of colons; only the FIRST one on a line is the field
    // separator.
    Collector collector;
    SseParser parser{collector.sink()};
    parser.feed(R"(data: {"a":"b:c","d":{"e":1}})"
                "\n\n");
    parser.finish();

    REQUIRE(collector.events.size() == 1);
    CHECK(collector.events[0].data == R"({"a":"b:c","d":{"e":1}})");
}

TEST_CASE("a multi-byte UTF-8 sequence split across chunks is preserved",
          "[backends][sse][split]") {
    // The parser works on bytes, so a split inside a UTF-8 sequence must
    // simply carry over -- the payload is reassembled before anyone decodes it.
    const std::string stream = "data: {\"text\":\"héllo — wörld\"}\n\n";
    for (std::size_t chunk = 1; chunk <= 8; ++chunk) {
        INFO("chunk size " << chunk);
        const auto events = parse_in_chunks(stream, chunk);
        REQUIRE(events.size() == 1);
        CHECK(events[0].data == R"({"text":"héllo — wörld"})");
    }
}
