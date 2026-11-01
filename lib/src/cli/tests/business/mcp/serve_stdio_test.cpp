#include "mcp/serve_stdio.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <sstream>
#include <stdexcept>
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

TEST_CASE("a served tool's output that is not UTF-8 reaches the client as a frame",
          "[mcp][serve][utf8]") {
    // `__mcp-tools` serves read_file: a Latin-1 file's bytes used to throw
    // in the reply's dump and end the server.
    apogee::agent::ToolRegistry registry;
    apogee::agent::Tool reader;
    reader.name = "read_thing";
    reader.description = "reads";
    reader.run = [](std::string_view) { return apogee::agent::ToolOutcome{"caf\xE9", false}; };
    registry.add(reader);

    std::istringstream in{
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"read_thing"}})"
        "\n"};
    std::ostringstream out;
    REQUIRE_NOTHROW(apogee::mcp::serve_stdio(in, out, "s", "1",
                                             apogee::mcp::read_only_tools(registry),
                                             apogee::mcp::dispatch_through(registry)));
    const std::vector<nlohmann::json> frames = frames_of(out.str());
    REQUIRE(frames.size() == 1);
    CHECK(frames[0]["result"]["content"][0]["text"] == "caf\xEF\xBF\xBD");
}

TEST_CASE("reads join the served set after the tools, read-only, each answered by its own name",
          "[mcp][serve][reads]") {
    // 54a: the status reads ride beside a registry's tools -- listed after
    // them, marked read-only, called by name -- and every other name still
    // reaches the registry's dispatch.
    std::vector<apogee::mcp::ServedTool> tools{
        {"echo", "Echo", nlohmann::json{{"type", "object"}}, true}};
    const apogee::mcp::Dispatch dispatch = [](std::string_view name, const nlohmann::json&) {
        return apogee::mcp::ToolCallResult{"tool " + std::string{name}, false};
    };
    nlohmann::json seen_args;
    std::vector<apogee::mcp::ServedRead> reads;
    reads.push_back(apogee::mcp::ServedRead{
        .name = "status",
        .description = "A status document",
        .input_schema =
            nlohmann::json{{"type", "object"}, {"properties", {{"which", {{"type", "string"}}}}}},
        .answer =
            [&seen_args](const nlohmann::json& args) {
                seen_args = args;
                return apogee::mcp::ToolCallResult{R"({"ok":true})", false};
            },
    });
    reads.push_back(apogee::mcp::ServedRead{
        .name = "bare",
        .description = "No schema given",
        .input_schema = {},
        .answer = [](const nlohmann::json&) { return apogee::mcp::ToolCallResult{"{}", false}; },
    });
    const apogee::mcp::ServedSet served =
        apogee::mcp::with_reads(tools, dispatch, std::move(reads));

    REQUIRE(served.tools.size() == 3);
    CHECK(served.tools[0].name == "echo");
    CHECK(served.tools[1].name == "status");
    CHECK(served.tools[1].read_only);
    CHECK(served.tools[1].input_schema["properties"]["which"]["type"] == "string");
    CHECK(served.tools[2].name == "bare");
    CHECK(served.tools[2].input_schema == nlohmann::json{{"type", "object"}});

    std::istringstream in{
        R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})"
        "\n"
        R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"status","arguments":{"which":"x"}}})"
        "\n"
        R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"echo"}})"
        "\n"};
    std::ostringstream out;
    CHECK(apogee::mcp::serve_stdio(in, out, "s", "1", served.tools, served.dispatch) == 0);
    const std::vector<nlohmann::json> frames = frames_of(out.str());
    REQUIRE(frames.size() == 3);
    REQUIRE(frames[0]["result"]["tools"].size() == 3);
    CHECK(frames[0]["result"]["tools"][1]["name"] == "status");
    CHECK(frames[0]["result"]["tools"][1]["annotations"]["readOnlyHint"] == true);
    CHECK(frames[1]["result"]["content"][0]["text"] == R"({"ok":true})");
    CHECK(frames[1]["result"]["isError"] == false);
    CHECK(seen_args == nlohmann::json{{"which", "x"}});
    CHECK(frames[2]["result"]["content"][0]["text"] == "tool echo");
}

TEST_CASE("a read that throws answers an error and the server goes on; its text is mended",
          "[mcp][serve][reads][utf8]") {
    std::vector<apogee::mcp::ServedRead> reads;
    reads.push_back(apogee::mcp::ServedRead{
        .name = "broken",
        .description = "Throws",
        .input_schema = nlohmann::json{{"type", "object"}},
        .answer = [](const nlohmann::json&) -> apogee::mcp::ToolCallResult {
            throw std::runtime_error("the config at /tmp/caf\xE9 will not load");
        },
    });
    reads.push_back(apogee::mcp::ServedRead{
        .name = "latin",
        .description = "Not UTF-8",
        .input_schema = nlohmann::json{{"type", "object"}},
        .answer =
            [](const nlohmann::json&) { return apogee::mcp::ToolCallResult{"caf\xE9", false}; },
    });
    const apogee::mcp::ServedSet served = apogee::mcp::with_reads(
        {},
        [](std::string_view, const nlohmann::json&) {
            return apogee::mcp::ToolCallResult{"fallback", true};
        },
        std::move(reads));

    std::istringstream in{
        R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"broken"}})"
        "\n"
        R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"latin"}})"
        "\n"
        R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"nope"}})"
        "\n"};
    std::ostringstream out;
    REQUIRE_NOTHROW(apogee::mcp::serve_stdio(in, out, "s", "1", served.tools, served.dispatch));
    const std::vector<nlohmann::json> frames = frames_of(out.str());
    REQUIRE(frames.size() == 3);
    CHECK(frames[0]["result"]["isError"] == true);
    CHECK(frames[0]["result"]["content"][0]["text"] ==
          "Error: the config at /tmp/caf\xEF\xBF\xBD will not load");
    CHECK(frames[1]["result"]["isError"] == false);
    CHECK(frames[1]["result"]["content"][0]["text"] == "caf\xEF\xBF\xBD");
    CHECK(frames[2]["result"]["content"][0]["text"] == "fallback");
}

TEST_CASE("a read never shadows a served tool or another read", "[mcp][serve][reads]") {
    // Tools are only ever added (54a): a host's registered names keep their
    // meaning, so a collision is refused where the set is built.
    const apogee::mcp::Dispatch dispatch = [](std::string_view, const nlohmann::json&) {
        return apogee::mcp::ToolCallResult{};
    };
    const auto read_named = [](std::string name) {
        return apogee::mcp::ServedRead{
            .name = std::move(name),
            .description = "d",
            .input_schema = nlohmann::json{{"type", "object"}},
            .answer = [](const nlohmann::json&) { return apogee::mcp::ToolCallResult{}; },
        };
    };
    const std::vector<apogee::mcp::ServedTool> tools{
        {"read_file", "Reads", nlohmann::json{{"type", "object"}}, true}};
    CHECK_THROWS_AS(apogee::mcp::with_reads(tools, dispatch, {read_named("read_file")}),
                    std::invalid_argument);
    CHECK_THROWS_AS(
        apogee::mcp::with_reads(tools, dispatch, {read_named("check"), read_named("check")}),
        std::invalid_argument);
    CHECK_NOTHROW(apogee::mcp::with_reads(tools, dispatch, {read_named("check")}));
}

TEST_CASE("calls join after the reads, not read-only, each answered by its own name",
          "[mcp][serve][calls]") {
    // 54b: a model-calling tool is listed as one -- readOnlyHint false -- and
    // answered, mended and guarded as a read is; nothing it is named shadows.
    const auto answering = [](std::string name, std::string text) {
        return apogee::mcp::ServedRead{
            .name = std::move(name),
            .description = "d",
            .input_schema = nlohmann::json{{"type", "object"}},
            .answer =
                [text = std::move(text)](const nlohmann::json&) {
                    return apogee::mcp::ToolCallResult{text, false};
                },
        };
    };
    const std::vector<apogee::mcp::ServedTool> tools{
        {"read_file", "Reads", nlohmann::json{{"type", "object"}}, true}};
    const apogee::mcp::Dispatch dispatch = [](std::string_view name, const nlohmann::json&) {
        return apogee::mcp::ToolCallResult{"tool " + std::string{name}, false};
    };
    apogee::mcp::ServedSet served =
        apogee::mcp::with_reads(tools, dispatch, {answering("check", "{}")});
    served = apogee::mcp::with_calls(
        std::move(served), {answering("complete", "an answer caf\xE9"), answering("second", "")});
    REQUIRE(served.tools.size() == 4);
    CHECK(served.tools[2].name == "complete");
    CHECK_FALSE(served.tools[2].read_only);
    CHECK(served.tools[1].read_only);

    std::istringstream in{
        R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})"
        "\n"
        R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"complete"}})"
        "\n"
        R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"check"}})"
        "\n"
        R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"read_file"}})"
        "\n"};
    std::ostringstream out;
    CHECK(apogee::mcp::serve_stdio(in, out, "s", "1", served.tools, served.dispatch) == 0);
    const std::vector<nlohmann::json> frames = frames_of(out.str());
    REQUIRE(frames.size() == 4);
    CHECK(frames[0]["result"]["tools"][2]["annotations"]["readOnlyHint"] == false);
    CHECK(frames[0]["result"]["tools"][1]["annotations"]["readOnlyHint"] == true);
    CHECK(frames[1]["result"]["content"][0]["text"] == "an answer caf\xEF\xBF\xBD");
    CHECK(frames[2]["result"]["content"][0]["text"] == "{}");
    CHECK(frames[3]["result"]["content"][0]["text"] == "tool read_file");

    // A call named like a tool, a read or another call is refused where the
    // set is built.
    for (const char* taken : {"read_file", "check", "complete"}) {
        INFO(taken);
        CHECK_THROWS_AS(apogee::mcp::with_calls(served, {answering(taken, "")}),
                        std::invalid_argument);
    }
    CHECK_THROWS_WITH(apogee::mcp::with_calls(apogee::mcp::ServedSet{tools, dispatch},
                                              {answering("c", ""), answering("c", "")}),
                      "the call 'c' would shadow a tool of the same name");
}
