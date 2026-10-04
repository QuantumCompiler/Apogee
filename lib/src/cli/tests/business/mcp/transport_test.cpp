#include "mcp/transport.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

#include "support/fake_child.h"

/// The stdio transport over a fake child: framing across chunk boundaries,
/// the bounded stderr tail, and the sink that keeps stderr off the terminal.
namespace {

using apogee::mcp::StderrTail;
using apogee::mcp::StdioTransport;
using apogee::testing::FakeChild;

}  // namespace

TEST_CASE("the stderr tail keeps the last eight lines and tees every byte", "[mcp][transport]") {
    std::string teed;
    StderrTail tail{[&teed](std::string_view bytes) { teed += bytes; }};
    for (int i = 0; i < 200; ++i) {
        tail.write("noise line " + std::to_string(i) + "\n");
    }
    tail.write("  partial without newline");
    const std::string joined = tail.tail();
    CHECK(joined.find("noise line 0 ") == std::string::npos);
    CHECK(joined.find("noise line 199") != std::string::npos);
    CHECK(joined.find("partial without newline") != std::string::npos);
    // Exactly eight complete lines plus the partial, joined on one line.
    CHECK(joined.find('\n') == std::string::npos);
    std::size_t bars = 0;
    for (const char c : joined) {
        bars += c == '|' ? 1 : 0;
    }
    CHECK(bars == 8);
    CHECK(teed.find("noise line 0\n") != std::string::npos);  // the tee dropped nothing
    CHECK(apogee::mcp::stderr_note("") == "");
    CHECK(apogee::mcp::stderr_note("a | b") == " -- stderr: a | b");

    // Blank lines are not retained; whitespace is trimmed.
    StderrTail tidy;
    tidy.write("\n\n   spaced   \n\n");
    CHECK(tidy.tail() == "spaced");
}

TEST_CASE("the transport frames lines across chunk boundaries and flushes at EOF",
          "[mcp][transport]") {
    auto child = std::make_unique<FakeChild>();
    child->stdout_script =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}\r\n{\"a\":1}\n{\"last\":true}";
    child->chunk_size = 7;  // every boundary lands mid-object
    child->stderr_script = "server said hello\n";
    std::string sunk;
    StdioTransport transport{std::move(child), [&sunk](std::string_view b) { sunk += b; }};

    std::vector<std::string> lines;
    for (int i = 0; i < 100; ++i) {
        const std::optional<std::string> line = transport.recv(std::chrono::milliseconds{1});
        if (!line.has_value()) {
            break;
        }
        if (!line->empty()) {
            lines.push_back(*line);
        }
    }
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}");  // CRLF normalised
    CHECK(lines[1] == "{\"a\":1}");
    CHECK(lines[2] == "{\"last\":true}");  // no trailing newline, still delivered
    CHECK(transport.stderr_tail() == "server said hello");
    CHECK(sunk == "server said hello\n");
    // After EOF, closed for good.
    CHECK_FALSE(transport.recv(std::chrono::milliseconds{1}).has_value());
}

TEST_CASE("send writes one frame per line and fails once the child is gone", "[mcp][transport]") {
    auto child = std::make_unique<FakeChild>();
    FakeChild* raw = child.get();
    StdioTransport transport{std::move(child), nullptr};
    CHECK(transport.send(nlohmann::json{{"x", 1}}));
    REQUIRE(raw->writes.size() == 1);
    CHECK(raw->writes[0] == "{\"x\":1}\n");
    transport.close();
    CHECK_FALSE(transport.send(nlohmann::json{{"x", 2}}));
    CHECK_FALSE(transport.recv(std::chrono::milliseconds{1}).has_value());
}
