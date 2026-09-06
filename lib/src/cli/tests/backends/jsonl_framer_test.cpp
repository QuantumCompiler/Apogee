#include "backends/jsonl_framer.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

/// The line framer, tested directly rather than only through the replay suite.
///
/// It is worth its own file because it is where the vendor-CLI family's worst
/// bug class lives, and because a pure component can be pushed into states a
/// fixture never reaches — a newline as the very last byte of a chunk, a
/// buffer that must not grow without bound, a stream that ends mid-line.
namespace {

using apogee::backends::JsonlFramer;

[[nodiscard]] std::vector<std::string> feed_all(std::string_view bytes, std::size_t chunk) {
    std::vector<std::string> lines;
    JsonlFramer framer;
    const auto collect = [&lines](std::string_view line) { lines.emplace_back(line); };
    for (std::size_t offset = 0; offset < bytes.size(); offset += chunk) {
        framer.feed(bytes.substr(offset, std::min(chunk, bytes.size() - offset)), collect);
    }
    framer.flush(collect);
    return lines;
}

}  // namespace

TEST_CASE("lines survive any chunk boundary", "[backends][framer]") {
    // The seam that matters: a boundary between every pair of bytes.
    constexpr std::string_view kInput = "{\"a\":1}\n{\"b\":2}\n{\"c\":3}\n";
    const std::vector<std::string> expected{R"({"a":1})", R"({"b":2})", R"({"c":3})"};

    for (const std::size_t chunk :
         {std::size_t{1}, std::size_t{2}, std::size_t{3}, std::size_t{5}, std::size_t{100}}) {
        INFO("chunk " << chunk);
        CHECK(feed_all(kInput, chunk) == expected);
    }
}

TEST_CASE("a newline landing exactly on a chunk boundary is not lost", "[backends][framer]") {
    // The specific off-by-one a hand-rolled framer gets wrong: the chunk ends
    // WITH the newline, so the next chunk starts a fresh line.
    JsonlFramer framer;
    std::vector<std::string> lines;
    const auto collect = [&lines](std::string_view line) { lines.emplace_back(line); };

    framer.feed("{\"a\":1}\n", collect);
    CHECK(lines.size() == 1);
    CHECK(framer.pending() == 0);

    framer.feed("{\"b\":2}\n", collect);
    CHECK(lines.size() == 2);
    CHECK(lines[1] == R"({"b":2})");
}

TEST_CASE("a partial line is held until it completes", "[backends][framer]") {
    JsonlFramer framer;
    std::vector<std::string> lines;
    const auto collect = [&lines](std::string_view line) { lines.emplace_back(line); };

    framer.feed("{\"long\":\"val", collect);
    CHECK(lines.empty());
    CHECK(framer.pending() > 0);

    framer.feed("ue\"}\n", collect);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == R"({"long":"value"})");
    CHECK(framer.pending() == 0);
}

TEST_CASE("the buffer does not grow across completed lines", "[backends][framer]") {
    // A framer that appends without ever erasing works perfectly and leaks a
    // session's entire output. Over a long chat that is real memory.
    JsonlFramer framer;
    const auto ignore = [](std::string_view) {};

    for (int i = 0; i < 1000; ++i) {
        framer.feed("{\"n\":" + std::to_string(i) + "}\n", ignore);
    }
    CHECK(framer.pending() == 0);
}

TEST_CASE("a stream ending mid-line yields it on flush", "[backends][framer]") {
    // A child that exits without a trailing newline still had a real last
    // event, and that event is often `result` -- where the session id and the
    // cost accounting live.
    const std::vector<std::string> lines = feed_all("{\"a\":1}\n{\"b\":2}", 3);
    REQUIRE(lines.size() == 2);
    CHECK(lines[1] == R"({"b":2})");
}

TEST_CASE("flush on an empty buffer emits nothing", "[backends][framer]") {
    JsonlFramer framer;
    int calls = 0;
    framer.flush([&calls](std::string_view) { ++calls; });
    CHECK(calls == 0);
}

TEST_CASE("CRLF is normalised away", "[backends][framer]") {
    // A trailing \r reaching the JSON parser makes a valid object fail -- on
    // Windows only, which is the worst place to discover it.
    const std::vector<std::string> lines = feed_all("{\"a\":1}\r\n{\"b\":2}\r\n", 4);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == R"({"a":1})");
    CHECK(lines[1] == R"({"b":2})");
}

TEST_CASE("blank lines are skipped", "[backends][framer]") {
    const std::vector<std::string> lines = feed_all("{\"a\":1}\n\n\n{\"b\":2}\n", 1);
    CHECK(lines.size() == 2);
}

TEST_CASE("reset drops a half-line from a dead child", "[backends][framer]") {
    // When a child is replaced, its partial last line must not prefix the new
    // child's first event -- which would corrupt exactly one object, the
    // `init` that carries the new session id.
    JsonlFramer framer;
    std::vector<std::string> lines;
    const auto collect = [&lines](std::string_view line) { lines.emplace_back(line); };

    framer.feed("{\"half\":", collect);
    REQUIRE(framer.pending() > 0);
    framer.reset();
    CHECK(framer.pending() == 0);

    framer.feed("{\"whole\":1}\n", collect);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == R"({"whole":1})");
}

TEST_CASE("only lines that could be JSON objects are offered", "[backends][framer]") {
    using apogee::backends::looks_like_json_object;

    CHECK(looks_like_json_object(R"({"a":1})"));
    CHECK(looks_like_json_object("   {\"a\":1}"));
    CHECK(looks_like_json_object("\t{}"));

    CHECK_FALSE(looks_like_json_object("Warning: something happened"));
    CHECK_FALSE(looks_like_json_object("[1,2,3]"));
    CHECK_FALSE(looks_like_json_object(""));
    CHECK_FALSE(looks_like_json_object("   "));
}

TEST_CASE("a very long line is handled whole", "[backends][framer]") {
    // A real `init` event exceeds 64 KiB with MCP servers configured, so any
    // fixed-size line assumption breaks on the FIRST event of a real session.
    std::string big = R"({"type":"system","subtype":"init","pad":")";
    big.append(100000, 'x');
    big += "\"}\n";

    const std::vector<std::string> lines = feed_all(big, 4096);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].size() > 100000);
}
