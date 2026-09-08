#include "backends/native_tool_calls.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "backends/markup_filter.h"
#include "backends/model_profile.h"
#include "backends/think_filter.h"

/// Native (control-token) tool calls, against real observed output.
///
/// `kToolCall` is verbatim from gpt-oss-20b (MXFP4) on 2026-09-07, asked to
/// read a file with one tool declared. Before this parser existed, all of it
/// was printed to the user as the answer and nothing dispatched.
namespace {

using apogee::backends::header_markers_for;
using apogee::backends::MarkupFilter;
using apogee::backends::parse_native_tool_calls;
using apogee::backends::reasoning_pairs_for;
using apogee::backends::resolve_profile;
using apogee::backends::ThinkFilter;
using apogee::backends::tool_call_openers;
using apogee::backends::ToolCallGate;

/// Verbatim from a live run, 2026-09-07.
constexpr std::string_view kToolCall =
    "<|channel|>analysis<|message|>We need to read file /tmp/notes.txt. Use function "
    "read_file.<|end|><|start|>assistant<|channel|>commentary to=functions.read_file "
    "<|constrain|>json<|message|>{\"path\":\"/tmp/notes.txt\"}";

/// Verbatim from a live run, 2026-09-07.
constexpr std::string_view kPlainAnswer =
    "<|channel|>analysis<|message|>The user asks: \"What is 2+2? Answer briefly.\" The answer is "
    "4.<|end|><|start|>assistant<|channel|>final<|message|>4";

/// What the provider does, in the order the provider does it. Using the
/// registry rather than hand-written markers is deliberate: a test that
/// declares its own markers would still pass if the gpt-oss profile were
/// deleted.
struct Pipeline {
    ThinkFilter think{reasoning_pairs_for(resolve_profile("", "gpt-oss", ""))};
    ToolCallGate gate{true};
    MarkupFilter markup{header_markers_for(resolve_profile("", "gpt-oss", ""))};
    std::string thinking;

    Pipeline() {
        think.on_thinking([this](std::string_view piece) { thinking.append(piece); });
    }

    [[nodiscard]] std::string run(std::string_view text, std::size_t chunk) {
        std::string out;
        for (std::size_t offset = 0; offset < text.size(); offset += chunk) {
            out += markup.write(gate.write(think.write(text.substr(offset, chunk))));
        }
        out += markup.write(gate.write(think.flush()));
        out += markup.write(gate.flush());
        out += markup.flush();
        return out;
    }
};

}  // namespace

TEST_CASE("the observed tool call is parsed", "[backends][nativetools]") {
    const std::vector<apogee::harness::ToolCall> calls = parse_native_tool_calls(kToolCall);
    REQUIRE(calls.size() == 1);
    // The `functions.` namespace is the template's, not the registry's: a tool
    // registered as `read_file` must be found by that name.
    CHECK(calls.front().name == "read_file");
    CHECK(calls.front().arguments == R"({"path":"/tmp/notes.txt"})");
}

TEST_CASE("the observed tool call shows the user nothing and dispatches",
          "[backends][nativetools]") {
    // Both halves of the bug in one assertion: no markup on screen, and a call
    // that actually exists.
    Pipeline pipeline;
    const std::string shown = pipeline.run(kToolCall, kToolCall.size());
    CHECK(shown.empty());
    REQUIRE(pipeline.gate.calls().size() == 1);
    CHECK(pipeline.gate.calls().front().name == "read_file");
    // The reasoning went to the thinking view rather than the answer.
    CHECK(pipeline.thinking.find("read file /tmp/notes.txt") != std::string::npos);
}

TEST_CASE("the observed plain answer survives the whole pipeline as its answer",
          "[backends][nativetools]") {
    Pipeline pipeline;
    CHECK(pipeline.run(kPlainAnswer, kPlainAnswer.size()) == "4");
}

TEST_CASE("the pipeline gives the same result at every chunk size", "[backends][nativetools]") {
    // Tokens arrive in whatever pieces the model produces, and gpt-oss splits
    // `commentary` into `comment` + `ary` -- straight through the middle of an
    // opener.
    for (const std::string_view fixture : {kPlainAnswer, kToolCall}) {
        for (const std::size_t chunk :
             {std::size_t{1}, std::size_t{2}, std::size_t{3}, std::size_t{5}, std::size_t{7},
              std::size_t{13}, std::size_t{64}}) {
            INFO("chunk: " << chunk);
            Pipeline whole;
            Pipeline split;
            CHECK(split.run(fixture, chunk) == whole.run(fixture, fixture.size()));
        }
    }
}

TEST_CASE("a span that opens like a call but does not parse shows its text",
          "[backends][nativetools]") {
    // The safety net. No answer, no tool, and no error is the least debuggable
    // outcome there is, and it is exactly how an unrecognised grammar variant
    // presents.
    constexpr std::string_view kBroken = "<|channel|>commentary to=functions.read_file oops";
    REQUIRE(parse_native_tool_calls(kBroken).empty());

    ToolCallGate gate{true};
    std::string out = gate.write(kBroken);
    out += gate.flush();
    CHECK(out == kBroken);
    CHECK(gate.calls().empty());
}

TEST_CASE("a truncated argument object is not dispatched", "[backends][nativetools]") {
    // Half an argument is worse than none: it dispatches a real tool against a
    // path, URL, or command the model never finished writing.
    CHECK(parse_native_tool_calls(
              "<|channel|>commentary to=functions.write_file <|constrain|>json<|message|>"
              "{\"path\":\"/tmp/a\",\"content\":\"unterm")
              .empty());
}

TEST_CASE("a brace inside a value does not end the object early", "[backends][nativetools]") {
    const auto calls = parse_native_tool_calls(
        "<|channel|>commentary to=functions.run <|constrain|>json<|message|>"
        "{\"cmd\":\"awk '{print $1}' f\"}");
    REQUIRE(calls.size() == 1);
    CHECK(calls.front().arguments == R"({"cmd":"awk '{print $1}' f"})");
}

TEST_CASE("the constrain hint is optional", "[backends][nativetools]") {
    // It is the model's choice to emit one, and a call without it is a call.
    const auto calls = parse_native_tool_calls(
        "<|channel|>commentary to=functions.read_file <|message|>{\"path\":\"/a\"}");
    REQUIRE(calls.size() == 1);
    CHECK(calls.front().name == "read_file");
}

TEST_CASE("a malformed call cannot hide a valid one after it", "[backends][nativetools]") {
    // The scan does not abandon itself on the first thing that fails to parse.
    const auto calls = parse_native_tool_calls(
        "<|channel|>commentary to=functions.broken nonsense"
        "<|channel|>commentary to=functions.read_file <|message|>{\"path\":\"/b\"}");
    REQUIRE(calls.size() == 1);
    CHECK(calls.front().name == "read_file");
    CHECK(calls.front().arguments == R"({"path":"/b"})");
}

TEST_CASE("a truncated call cannot absorb the one after it", "[backends][nativetools]") {
    // What the next-opener bound is actually for, and it took a surviving
    // mutation to find an input that shows it. The first call's argument object
    // is unterminated; the second supplies a closing brace before opening its
    // own. Scanning for balance across the boundary finds a "balanced" object
    // spanning both -- so a real tool dispatches with another call's text as
    // its arguments. Bounding the body at the next opener rejects both, and
    // rejecting a call is always recoverable: the safety net shows the text.
    CHECK(parse_native_tool_calls("<|channel|>commentary to=functions.a <|message|>{\"x\":1"
                                  "<|channel|>commentary to=functions.b <|message|>}")
              .empty());
}

TEST_CASE("prose containing a brace cannot dispatch", "[backends][nativetools]") {
    CHECK(parse_native_tool_calls("the syntax is {\"a\": 1} in JSON").empty());
}

TEST_CASE("a garbled name is not dispatched", "[backends][nativetools]") {
    // The opener is present and the arguments parse, so everything except the
    // name says "this is a call". Without a plausibility check the registry is
    // handed whatever span sat between `to=` and the message -- an empty name,
    // or a path -- and a lookup failure is a much later and much stranger
    // symptom than a call that was never made.
    CHECK(
        parse_native_tool_calls("<|channel|>commentary to=functions.<|message|>{\"a\":1}").empty());
    CHECK(parse_native_tool_calls("<|channel|>commentary to=../../etc/passwd <|message|>{\"a\":1}")
              .empty());

    // A namespaced name from a tool server is still ordinary, and must pass.
    const auto calls = parse_native_tool_calls(
        "<|channel|>commentary to=functions.mcp__fs__write_file <|message|>{\"a\":1}");
    REQUIRE(calls.size() == 1);
    CHECK(calls.front().name == "mcp__fs__write_file");
}

TEST_CASE("display and parser draw their openers from one function", "[backends][nativetools]") {
    // THE parity guardrail. A marker the display does not know leaks raw markup
    // to the user; one the parser does not know silently drops the call. Two
    // lists that must agree are two lists that will not, so there is one.
    //
    // Asserted by construction: for every opener the shared list names, the
    // gate must suppress it AND the parser must accept a well-formed call
    // built from it.
    REQUIRE_FALSE(tool_call_openers().empty());
    // Named, so that emptying the list is not the only mutation this catches.
    CHECK(std::ranges::find(tool_call_openers(), "<|channel|>commentary to=") !=
          tool_call_openers().end());

    for (const std::string& opener : tool_call_openers()) {
        INFO("opener: " << opener);
        const std::string call = opener + "functions.thing <|message|>{\"a\":1}";

        // The parser accepts it...
        const auto parsed = parse_native_tool_calls(call);
        REQUIRE(parsed.size() == 1);
        CHECK(parsed.front().name == "thing");

        // ...and the display withheld exactly it.
        ToolCallGate gate{true};
        std::string shown = gate.write(call);
        shown += gate.flush();
        CHECK(shown.empty());
        CHECK(gate.calls().size() == 1);
    }
}

TEST_CASE("the gpt-oss profile publishes the shared opener list",
          "[backends][nativetools][profile]") {
    // The third consumer: `ModelBehavior` carries the openers out of the
    // backends layer as plain data, and it must be the same list.
    const auto* profile = resolve_profile("", "gpt-oss", "");
    REQUIRE(profile != nullptr);
    const apogee::harness::ModelBehavior behavior = apogee::backends::behavior_for(profile);
    CHECK(behavior.native_tool_calls);
    CHECK(behavior.tool_call_openers == tool_call_openers());
}

TEST_CASE("a disabled gate is a pure pass-through", "[backends][nativetools]") {
    // A family characterized as emitting no native calls must not have text
    // withheld from it on the strength of another family's grammar.
    ToolCallGate gate{false};
    std::string out = gate.write("<|channel|>commentary to=functions.x <|message|>{}");
    out += gate.flush();
    CHECK(out == "<|channel|>commentary to=functions.x <|message|>{}");
    CHECK(gate.calls().empty());
}
