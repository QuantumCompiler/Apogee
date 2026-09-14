#include "mcp/types.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

/// The wire: framing, namespacing, and the payload parsers.
namespace {

using apogee::mcp::IncomingMessage;
using apogee::mcp::namespaced_name;
using apogee::mcp::parse_incoming;
using apogee::mcp::parse_tool_call;
using apogee::mcp::parse_tools_list;
using apogee::mcp::split_namespaced;

}  // namespace

TEST_CASE("a response has an id, a notification does not, junk is nothing", "[mcp][types]") {
    const std::optional<IncomingMessage> response =
        parse_incoming(R"({"jsonrpc":"2.0","id":7,"result":{"ok":true}})");
    REQUIRE(response.has_value());
    CHECK(response->id == 7);
    CHECK_FALSE(response->is_notification());
    CHECK(response->result["ok"] == true);

    const std::optional<IncomingMessage> error =
        parse_incoming(R"({"jsonrpc":"2.0","id":8,"error":{"code":-32601,"message":"nope"}})");
    REQUIRE(error.has_value());
    REQUIRE(error->error.has_value());
    CHECK(error->error->code == -32601);
    CHECK(error->error->message == "nope");

    const std::optional<IncomingMessage> note =
        parse_incoming(R"({"jsonrpc":"2.0","method":"notifications/tools/list_changed"})");
    REQUIRE(note.has_value());
    CHECK(note->is_notification());
    CHECK(note->method == "notifications/tools/list_changed");

    CHECK_FALSE(parse_incoming("not json").has_value());
    CHECK_FALSE(parse_incoming("[1,2]").has_value());
    CHECK_FALSE(parse_incoming(R"({"id":{"nested":1}})").has_value());
    CHECK(parse_incoming(R"({"id":"12","result":{}})")->id == 12);
}

TEST_CASE("tool names are namespaced with a double underscore and split on the first",
          "[mcp][types]") {
    CHECK(namespaced_name("files", "read_file") == "mcp__files__read_file");
    std::string server;
    std::string tool;
    CHECK(split_namespaced("mcp__files__read_file", server, tool));
    CHECK(server == "files");
    CHECK(tool == "read_file");
    // A tool whose own name carries a double underscore keeps everything
    // after the FIRST delimiter.
    CHECK(split_namespaced("mcp__srv__a__b", server, tool));
    CHECK(server == "srv");
    CHECK(tool == "a__b");
    CHECK_FALSE(split_namespaced("read_file", server, tool));
    CHECK_FALSE(split_namespaced("mcp__srv", server, tool));
    CHECK_FALSE(split_namespaced("mcp____tool", server, tool));
    CHECK_FALSE(split_namespaced("mcp__srv__", server, tool));
}

TEST_CASE("tools/list keeps the schema raw and reads the read-only hint", "[mcp][types]") {
    const nlohmann::json result = nlohmann::json::parse(R"({"tools":[
        {"name":"a","description":"A","inputSchema":{"type":"object","properties":{"x":{"type":"string"}}},
         "annotations":{"readOnlyHint":true}},
        {"name":"b"},
        {"description":"nameless"},
        {"name":"c","annotations":{"readOnlyHint":"yes"}}
    ]})");
    const std::vector<apogee::mcp::ToolInfo> tools = parse_tools_list(result);
    REQUIRE(tools.size() == 3);
    CHECK(tools[0].name == "a");
    CHECK(tools[0].input_schema["properties"]["x"]["type"] == "string");
    CHECK(tools[0].read_only_hint == true);
    CHECK(tools[1].name == "b");
    CHECK(tools[1].input_schema["type"] == "object");  // a default, so the model gets a schema
    CHECK_FALSE(tools[1].read_only_hint.has_value());  // unknown -- destructive to the gate
    CHECK_FALSE(tools[2].read_only_hint.has_value());  // a non-boolean hint is no hint
    CHECK(parse_tools_list(nlohmann::json::object()).empty());
}

TEST_CASE("tools/call joins text blocks, skips others, and carries isError", "[mcp][types]") {
    const apogee::mcp::ToolCallResult ok = parse_tool_call(nlohmann::json::parse(
        R"({"content":[{"type":"text","text":"one "},{"type":"image","data":"x"},{"type":"text","text":"two"}]})"));
    CHECK(ok.text == "one two");
    CHECK_FALSE(ok.is_error);
    const apogee::mcp::ToolCallResult failed = parse_tool_call(
        nlohmann::json::parse(R"({"content":[{"type":"text","text":"bad"}],"isError":true})"));
    CHECK(failed.is_error);
    CHECK(failed.text == "bad");
}
