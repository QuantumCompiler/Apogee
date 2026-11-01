#include "acp/jsonrpc.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <sstream>
#include <string>
#include <vector>

/// JSON-RPC 2.0 over newline-delimited stdio (56a): what a line is, and the
/// one writer that answers and asks.
namespace {

using apogee::acp::Frame;
using Kind = apogee::acp::Frame::Kind;

[[nodiscard]] std::vector<nlohmann::json> lines_of(const std::string& text) {
    std::vector<nlohmann::json> out;
    std::istringstream in{text};
    for (std::string line; std::getline(in, line);) {
        out.push_back(nlohmann::json::parse(line));
    }
    return out;
}

}  // namespace

TEST_CASE("a line is a request, a notification, a response -- or why it is none",
          "[acp][jsonrpc]") {
    Frame frame = apogee::acp::parse_frame(
        R"({"jsonrpc":"2.0","id":7,"method":"session/prompt","params":{"sessionId":"s"},"x":1})");
    CHECK(frame.kind == Kind::Request);
    CHECK(frame.id == 7);
    CHECK(frame.method == "session/prompt");
    CHECK(frame.params.at("sessionId") == "s");

    frame = apogee::acp::parse_frame(R"({"jsonrpc":"2.0","id":"a","method":"initialize"})");
    CHECK(frame.kind == Kind::Request);
    CHECK(frame.id == "a");
    CHECK(frame.params.is_null());

    frame = apogee::acp::parse_frame(R"({"jsonrpc":"2.0","method":"session/cancel"})");
    CHECK(frame.kind == Kind::Notification);

    frame = apogee::acp::parse_frame(R"({"jsonrpc":"2.0","id":3,"result":{"ok":true}})");
    CHECK(frame.kind == Kind::Response);
    CHECK(frame.result.at("ok") == true);
    frame = apogee::acp::parse_frame(R"({"jsonrpc":"2.0","id":3,"error":{"code":-1}})");
    CHECK(frame.kind == Kind::Response);
    CHECK(frame.error.at("code") == -1);

    CHECK(apogee::acp::parse_frame("not json").kind == Kind::Unparsable);
    CHECK(apogee::acp::parse_frame("{\"jsonrpc\":").kind == Kind::Unparsable);
    for (const char* invalid : {"[1,2]", "42", R"({"id":1})", R"({"id":[1],"method":"m"})",
                                R"({"id":1,"method":7})", R"({"method":""})"}) {
        INFO(invalid);
        const Frame bad = apogee::acp::parse_frame(invalid);
        CHECK(bad.kind == Kind::Invalid);
        CHECK_FALSE(bad.problem.empty());
    }
}

TEST_CASE("frames are one line each, and text that is not UTF-8 never throws", "[acp][jsonrpc]") {
    const nlohmann::json reply =
        apogee::acp::response(5, nlohmann::json{{"text", std::string{"a\nb\xff"}}});
    const std::string text = apogee::acp::frame_text(reply);
    CHECK(text.find('\n') == std::string::npos);
    CHECK(nlohmann::json::parse(text).at("result").at("text") == "a\nb\xEF\xBF\xBD");
    CHECK(apogee::acp::error_response(nullptr, apogee::acp::kParseError, "parse error") ==
          nlohmann::json{{"jsonrpc", "2.0"},
                         {"id", nullptr},
                         {"error", {{"code", -32700}, {"message", "parse error"}}}});
    CHECK(apogee::acp::notification("session/update", {{"a", 1}}) ==
          nlohmann::json{{"jsonrpc", "2.0"}, {"method", "session/update"}, {"params", {{"a", 1}}}});
}

TEST_CASE("the peer asks with ids of its own and routes each answer back", "[acp][jsonrpc]") {
    std::ostringstream out;
    apogee::acp::Peer peer{out};
    std::vector<std::string> answers;
    const auto record = [&answers](const std::string& who) {
        return [&answers, who](const nlohmann::json* result, const nlohmann::json* error) {
            answers.push_back(who + ":" +
                              (result != nullptr  ? result->dump()
                               : error != nullptr ? "error"
                                                  : "gone"));
        };
    };
    peer.ask("session/request_permission", {{"sessionId", "s"}}, record("first"));
    peer.ask("elicitation/create", {{"sessionId", "s"}}, record("second"));
    const std::vector<nlohmann::json> sent = lines_of(out.str());
    REQUIRE(sent.size() == 2);
    CHECK(sent[0].at("method") == "session/request_permission");
    CHECK(sent[0].at("jsonrpc") == "2.0");
    CHECK(sent[0].at("id") != sent[1].at("id"));

    // Answered out of order, each reaches its own asker; an id nobody asked
    // with reaches nobody.
    CHECK(peer.deliver(apogee::acp::parse_frame(
        R"({"jsonrpc":"2.0","id":)" + sent[1].at("id").dump() + R"(,"error":{"code":1}})")));
    CHECK(peer.deliver(apogee::acp::parse_frame(
        R"({"jsonrpc":"2.0","id":)" + sent[0].at("id").dump() + R"(,"result":{"ok":1}})")));
    CHECK_FALSE(
        peer.deliver(apogee::acp::parse_frame(R"({"jsonrpc":"2.0","id":999,"result":{}})")));
    CHECK(answers == std::vector<std::string>{"second:error", R"(first:{"ok":1})"});

    // The host gone: what waits is answered with nothing, and so is a later ask.
    peer.ask("session/request_permission", {}, record("third"));
    peer.close();
    peer.ask("session/request_permission", {}, record("fourth"));
    CHECK(answers.size() == 4);
    CHECK(answers[2] == "third:gone");
    CHECK(answers[3] == "fourth:gone");
    // ...and the ask after the host went is never sent: it would reach nobody.
    CHECK(lines_of(out.str()).size() == 3);
}
