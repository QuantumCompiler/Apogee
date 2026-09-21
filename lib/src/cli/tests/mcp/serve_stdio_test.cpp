#include "mcp/serve_stdio.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <sstream>
#include <string>
#include <vector>

#include "agent/tool.h"

/// The server side over string streams: three replies for four frames, the
/// error for an unknown method, junk skipped, and only read-only tools served.
namespace {

std::vector<nlohmann::json> frames_of(const std::string& out) {
    std::vector<nlohmann::json> frames;
    std::istringstream in{out};
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) {
            frames.push_back(nlohmann::json::parse(line));
        }
    }
    return frames;
}

}  // namespace

TEST_CASE("serve_stdio answers the four methods, one reply per request", "[mcp][serve]") {
    std::vector<apogee::mcp::ServedTool> tools{
        {"echo", "Echo", nlohmann::json{{"type", "object"}}, true}};
    std::string seen_name;
    nlohmann::json seen_args;
    const apogee::mcp::Dispatch dispatch = [&](std::string_view name, const nlohmann::json& args) {
        seen_name = std::string{name};
        seen_args = args;
        return apogee::mcp::ToolCallResult{"echoed: " + args.value("text", std::string{}), false};
    };
    std::istringstream in{
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})"
        "\n"
        R"({"jsonrpc":"2.0","method":"notifications/initialized"})"
        "\n"
        R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})"
        "\n"
        R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"echo","arguments":{"text":"hi"}}})"
        "\n"};
    std::ostringstream out;
    CHECK(apogee::mcp::serve_stdio(in, out, "test-server", "9.9", tools, dispatch) == 0);
    const std::vector<nlohmann::json> frames = frames_of(out.str());
    REQUIRE(frames.size() == 3);  // the notification has no reply
    CHECK(frames[0]["id"] == 1);
    CHECK(frames[0]["result"]["protocolVersion"] == "2025-03-26");
    CHECK(frames[0]["result"]["serverInfo"]["name"] == "test-server");
    CHECK(frames[0]["result"]["serverInfo"]["version"] == "9.9");
    CHECK(frames[1]["result"]["tools"][0]["name"] == "echo");
    CHECK(frames[1]["result"]["tools"][0]["annotations"]["readOnlyHint"] == true);
    CHECK(frames[2]["result"]["content"][0]["text"] == "echoed: hi");
    CHECK(frames[2]["result"]["isError"] == false);
    CHECK(seen_name == "echo");
    CHECK(seen_args["text"] == "hi");
}

TEST_CASE("an unknown method with an id is -32601; junk and notifications are ignored",
          "[mcp][serve]") {
    std::istringstream in{
        "not json\n\n"
        R"({"jsonrpc":"2.0","method":"notifications/whatever"})"
        "\n"
        R"({"jsonrpc":"2.0","id":5,"method":"resources/list"})"
        "\n"
        R"({"jsonrpc":"2.0","id":6,"method":"tools/list"})"
        "\n"};
    std::ostringstream out;
    CHECK(apogee::mcp::serve_stdio(in, out, "s", "1", {},
                                   [](std::string_view, const nlohmann::json&) {
                                       return apogee::mcp::ToolCallResult{};
                                   }) == 0);
    const std::vector<nlohmann::json> frames = frames_of(out.str());
    REQUIRE(frames.size() == 2);
    CHECK(frames[0]["id"] == 5);
    CHECK(frames[0]["error"]["code"] == -32601);
    CHECK(frames[1]["result"]["tools"].empty());
}

TEST_CASE("only a registry's read-only tools are served, and dispatch goes through the registry",
          "[mcp][serve][permission]") {
    apogee::agent::ToolRegistry registry;
    apogee::agent::Tool reader;
    reader.name = "read_thing";
    reader.description = "reads";
    reader.run = [](std::string_view args) {
        return apogee::agent::ToolOutcome{"read " + std::string{args}, false};
    };
    apogee::agent::Tool writer;
    writer.name = "write_thing";
    writer.description = "writes";
    writer.writes = true;
    writer.run = [](std::string_view) { return apogee::agent::ToolOutcome{"WROTE", false}; };
    registry.add(reader);
    registry.add(writer);

    const std::vector<apogee::mcp::ServedTool> served = apogee::mcp::read_only_tools(registry);
    REQUIRE(served.size() == 1);
    CHECK(served[0].name == "read_thing");
    CHECK(served[0].read_only);

    const apogee::mcp::Dispatch dispatch = apogee::mcp::dispatch_through(registry);
    CHECK(dispatch("read_thing", nlohmann::json{{"k", 1}}).text == R"(read {"k":1})");
    // A writing tool is not reachable over stdio at all: the gate lives in
    // the loop, and a server has nobody to ask.
    const apogee::mcp::ToolCallResult refused = dispatch("write_thing", nlohmann::json::object());
    CHECK(refused.is_error);
    CHECK(refused.text.find("no such tool") != std::string::npos);
    CHECK(dispatch("nope", nlohmann::json::object()).is_error);
}
