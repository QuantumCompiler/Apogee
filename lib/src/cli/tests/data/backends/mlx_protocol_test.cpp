#include "backends/mlx_protocol.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "transport/jsonl_framer.h"

/// The MLX protocol (27a), this side's half: golden lines both directions.
///
/// What this side writes for a known request is pinned byte for byte, and
/// what it makes of a recorded driver stream is pinned event by event -- at
/// every chunk size, through the one framer, since a pipe hands over bytes
/// in whatever sizes it likes and a line split between two reads must cost
/// nothing. The driver's half of the same lines is held by its stub-module
/// suite (`mlx_driver_test.cpp`).
namespace {

using apogee::backends::mlx::Event;
using apogee::backends::mlx::GenerateRequest;

const std::filesystem::path kFixtures = std::filesystem::path{APOGEE_TEST_FIXTURES} / "mlx";

std::string read(const std::filesystem::path& path) {
    const std::ifstream in{path, std::ios::binary};
    REQUIRE(in.good());
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in{text};
    std::string line;
    while (std::getline(in, line)) {
        out.push_back(line);
    }
    return out;
}

GenerateRequest golden_request() {
    GenerateRequest request;
    request.id = 7;
    request.messages.push_back(apogee::harness::ChatMessage::system("Be terse."));
    request.messages.push_back(apogee::harness::ChatMessage::user("Read notes.txt"));
    apogee::harness::ChatMessage call = apogee::harness::ChatMessage::assistant("");
    call.tool_calls.push_back(
        {.id = "call1", .name = "read_file", .arguments = R"({"path":"notes.txt"})"});
    request.messages.push_back(call);
    request.messages.push_back(apogee::harness::ChatMessage::from_tool_result(
        {.tool_call_id = "call1", .name = "read_file", .content = "milk", .is_error = false}));
    request.messages.push_back(apogee::harness::ChatMessage::user("thanks"));
    request.tools.push_back(
        {.name = "read_file",
         .description = "Read a file",
         .parameters_schema = R"({"type":"object","properties":{"path":{"type":"string"}}})"});
    request.sampling.temperature.value = 0.6;
    request.sampling.top_p.value = 0.9;
    request.sampling.seed = 42;
    request.max_tokens = 512;
    request.stop = {"</s>"};
    request.thinking = false;
    return request;
}

/// The events a stream yields when it arrives `chunk` bytes at a time.
std::vector<std::string> events_at(const std::string& stream, std::size_t chunk) {
    apogee::backends::JsonlFramer framer;
    std::vector<std::string> out;
    const auto on_line = [&out](std::string_view line) {
        if (const auto event = apogee::backends::mlx::parse_event(line)) {
            out.push_back(apogee::backends::mlx::describe(*event));
        }
    };
    for (std::size_t at = 0; at < stream.size(); at += chunk) {
        framer.feed(std::string_view{stream}.substr(at, chunk), on_line);
    }
    framer.flush(on_line);
    return out;
}

}  // namespace

TEST_CASE("a generate request is the golden line, and a cancel its own",
          "[backends][mlx][protocol]") {
    const std::vector<std::string> golden = lines_of(read(kFixtures / "generate_request.jsonl"));
    REQUIRE(golden.size() == 2);
    // The template shapes: a call's arguments as an object, a tool's schema
    // as one, the tool result naming the call it answers.
    CHECK(apogee::backends::mlx::generate_line(golden_request()) == golden[0]);
    CHECK(apogee::backends::mlx::cancel_line(8) == golden[1]);
}

TEST_CASE("text that is not JSON crosses as the string it is", "[backends][mlx][protocol]") {
    GenerateRequest request;
    apogee::harness::ChatMessage call = apogee::harness::ChatMessage::assistant("");
    call.tool_calls.push_back({.id = "c", .name = "f", .arguments = "not json"});
    request.messages.push_back(call);
    request.tools.push_back({.name = "f", .description = "", .parameters_schema = "{oops"});
    const std::string line = apogee::backends::mlx::generate_line(request);
    CHECK(line.find(R"("arguments":"not json")") != std::string::npos);
    // A schema that is not an object becomes an empty one: a template walks it.
    CHECK(line.find(R"("parameters":{})") != std::string::npos);
    CHECK(line.find(R"("seed":null)") != std::string::npos);
    CHECK(line.find(R"("session":true)") != std::string::npos);
}

TEST_CASE("the driver's stream parses to the same events at every chunk size",
          "[backends][mlx][protocol][framing]") {
    const std::string stream = read(kFixtures / "driver_stream.jsonl");
    const std::vector<std::string> expected{
        "ready gpt_oss",
        "#7 reasoning 'The user wants the file.'",
        "#7 text 'Reading it \xE2\x80\x94 now.'",
        // The stray line and the newer driver's unknown type are dropped, and
        // so is a call with no name: none carries anything to act on.
        R"(#7 tool_call read_file {"path":"notes.txt"})",
        "#7 done tool_calls 120/96/31",
        "error load: could not load /m: ValueError: Model type gemma9 not supported.",
    };
    for (const std::size_t chunk :
         {std::size_t{1}, std::size_t{3}, std::size_t{7}, std::size_t{4096}, stream.size()}) {
        INFO("chunk " << chunk);
        CHECK(events_at(stream, chunk) == expected);
    }
}

TEST_CASE("a ready line says what the driver found", "[backends][mlx][protocol]") {
    const auto ready = apogee::backends::mlx::parse_event(
        R"({"type":"ready","protocol":1,"model_type":"qwen3_vl","chat_template":true,)"
        R"("tool_parser":"json_tools","thinking":true,"mlx_lm":"0.32.0"})");
    REQUIRE(ready.has_value());
    CHECK(ready->kind == Event::Kind::Ready);
    CHECK(ready->protocol == 1);
    CHECK(ready->model_type == "qwen3_vl");
    CHECK(ready->chat_template);
    CHECK(ready->tool_parser == "json_tools");
    CHECK(ready->thinking);
    CHECK(ready->mlx_lm_version == "0.32.0");
    CHECK_FALSE(ready->id.has_value());

    CHECK_FALSE(apogee::backends::mlx::parse_event("[1,2]").has_value());
    CHECK_FALSE(apogee::backends::mlx::parse_event("").has_value());
}

TEST_CASE("a done line's finish maps onto the IR's reasons", "[backends][mlx][protocol]") {
    using apogee::backends::mlx::finish_reason;
    using apogee::harness::FinishReason;
    CHECK(finish_reason("stop") == FinishReason::Stop);
    CHECK(finish_reason("length") == FinishReason::Length);
    CHECK(finish_reason("tool_calls") == FinishReason::ToolCalls);
    CHECK(finish_reason("cancelled") == FinishReason::Cancelled);
    CHECK(finish_reason("something newer") == FinishReason::Stop);
}
