#include "agent/tool.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "agent/fetch_url.h"
#include "agent/readable.h"
#include "contracts/errors.h"

using apogee::agent::dispatch;
using apogee::agent::DispatchContext;
using apogee::agent::FetchResult;
using apogee::agent::GateRequest;
using apogee::agent::make_fetch_url_tool;
using apogee::agent::Permission;
using apogee::agent::permitted;
using apogee::agent::Tool;
using apogee::agent::ToolOutcome;
using apogee::agent::ToolRegistry;
using apogee::harness::ToolCall;

namespace {

Tool simple_tool(std::string name, bool writes = false) {
    Tool tool;
    tool.name = std::move(name);
    tool.description = "d";
    tool.writes = writes;
    tool.run = [](std::string_view) { return ToolOutcome{"ran", false}; };
    return tool;
}

}  // namespace

TEST_CASE("the registry rejects duplicates and nameless tools", "[agent][tool]") {
    // A silently shadowed tool is a bug found months later, if ever.
    ToolRegistry registry;
    registry.add(simple_tool("a"));

    CHECK_THROWS_AS(registry.add(simple_tool("a")), std::invalid_argument);
    CHECK_THROWS_AS(registry.add(simple_tool("")), std::invalid_argument);

    Tool no_impl;
    no_impl.name = "b";
    CHECK_THROWS_AS(registry.add(std::move(no_impl)), std::invalid_argument);

    CHECK(registry.size() == 1);
    CHECK(registry.find("a") != nullptr);
    CHECK(registry.find("missing") == nullptr);
}

TEST_CASE("the registry renders IR tool definitions", "[agent][tool]") {
    ToolRegistry registry;
    registry.add(simple_tool("alpha"));
    registry.add(simple_tool("beta"));

    const auto definitions = registry.definitions();
    REQUIRE(definitions.size() == 2);
    CHECK(definitions[0].name == "alpha");
    CHECK(definitions[1].name == "beta");
    CHECK_FALSE(definitions[0].parameters_schema.empty());
}

TEST_CASE("permission: read tools bypass, write tools are gated", "[agent][tool][permission]") {
    const Tool reader = simple_tool("r", false);
    const Tool writer = simple_tool("w", true);

    DispatchContext none;
    CHECK(permitted(reader, "{}", none));
    // Ask with nobody to ask is DENY -- allowing because no one objected is the
    // wrong direction to fail.
    CHECK_FALSE(permitted(writer, "{}", none));

    DispatchContext allow;
    allow.permission = [](const GateRequest&) { return Permission::Allow; };
    CHECK(permitted(writer, "{}", allow));

    DispatchContext deny;
    deny.permission = [](const GateRequest&) { return Permission::Deny; };
    CHECK_FALSE(permitted(writer, "{}", deny));

    DispatchContext ask_yes;
    ask_yes.permission = [](const GateRequest&) { return Permission::Ask; };
    ask_yes.confirm = [](const GateRequest&) { return true; };
    CHECK(permitted(writer, "{}", ask_yes));

    DispatchContext ask_no = ask_yes;
    ask_no.confirm = [](const GateRequest&) { return false; };
    CHECK_FALSE(permitted(writer, "{}", ask_no));
}

TEST_CASE("the permission prompt is given the operation's target", "[agent][tool][permission]") {
    // "Allow write_file?" is not a question anyone can answer well. The path is
    // the entire decision.
    Tool writer = simple_tool("write_file", true);
    writer.describe_target = [](std::string_view arguments) {
        return "parsed:" + std::string{arguments};
    };

    std::string seen_tool;
    std::string seen_target;
    DispatchContext context;
    context.permission = [](const GateRequest&) { return Permission::Ask; };
    context.confirm = [&](const GateRequest& request) {
        seen_tool = request.tool;
        seen_target = request.target;
        CHECK_FALSE(request.outbound);
        return true;
    };

    CHECK(permitted(writer, R"({"path":"/tmp/x"})", context));
    CHECK(seen_tool == "write_file");
    CHECK(seen_target == R"(parsed:{"path":"/tmp/x"})");
}

TEST_CASE("the gate hears whether a tool edits, and a refusal says the surface's reason",
          "[agent][tool][permission][modes]") {
    // 39b: accept-edits keys on what the tool says of itself, and a mode's
    // refusal is said to the model so it does not retry against it.
    ToolRegistry registry;
    Tool editor = simple_tool("write_file", true);
    editor.edits = true;
    registry.add(editor);
    registry.add(simple_tool("run_command", true));

    std::vector<bool> edits;
    DispatchContext context;
    context.permission = [&edits](const GateRequest& request) {
        edits.push_back(request.edits);
        return Permission::Deny;
    };
    const ToolOutcome plain = dispatch(registry, ToolCall{"c", "run_command", "{}"}, context);
    CHECK(plain.is_error);
    CHECK(plain.content.find("the user denied permission to run 'run_command'") !=
          std::string::npos);

    context.refusal = [](const GateRequest& request) {
        return request.edits ? std::string{"plan mode is on."} : std::string{};
    };
    const ToolOutcome planned = dispatch(registry, ToolCall{"c", "write_file", "{}"}, context);
    CHECK(planned.content ==
          "Error: 'write_file' was not run: plan mode is on. Do not retry it; "
          "continue without it or ask what to do instead.");
    // A surface with no reason for this call keeps the plain words.
    const ToolOutcome again = dispatch(registry, ToolCall{"c", "run_command", "{}"}, context);
    CHECK(again.content.find("the user denied permission") != std::string::npos);
    CHECK(edits == std::vector<bool>{false, true, false});
}

TEST_CASE("dispatch reports progress and clears it", "[agent][tool]") {
    ToolRegistry registry;
    registry.add(simple_tool("thing"));

    std::vector<std::string> statuses;
    DispatchContext context;
    context.on_status = [&statuses](std::string_view detail) { statuses.emplace_back(detail); };

    const auto outcome = dispatch(registry, ToolCall{"c", "thing", "{}"}, context);
    CHECK(outcome.content == "ran");
    CHECK_FALSE(outcome.is_error);
    REQUIRE(statuses.size() == 2);
    CHECK(statuses[0] == "[tool] thing");
    CHECK(statuses[1].empty());  // back to rest
}

TEST_CASE("dispatch reports each call's state, keyed on its id, beside the status line",
          "[agent][tool][calls]") {
    // 56a: pending before the gate, running where `[tool] <name>` is said,
    // then done or failed -- what an ACP host draws as the call's card.
    using apogee::agent::ToolCallReport;
    using apogee::agent::ToolCallState;

    struct Seen {
        std::string id;
        std::string name;
        ToolCallState state;
        std::string target;
    };

    std::vector<Seen> seen;
    std::vector<std::string> asked_for;
    DispatchContext context;
    context.on_call = [&seen](const ToolCallReport& report) {
        seen.push_back({std::string{report.id}, std::string{report.name}, report.state,
                        std::string{report.target}});
    };
    context.permission = [](const GateRequest&) { return Permission::Ask; };
    bool answer = true;
    context.confirm = [&answer, &asked_for](const GateRequest& request) {
        asked_for.emplace_back(request.call_id);
        return answer;
    };

    ToolRegistry registry;
    Tool reader = simple_tool("read_file");
    reader.describe_target = [](std::string_view) { return std::string{"notes.md"}; };
    registry.add(reader);
    Tool writer = simple_tool("write_file", true);
    writer.describe_target = [](std::string_view) { return std::string{"out.txt"}; };
    registry.add(writer);
    Tool broken = simple_tool("broken");
    broken.run = [](std::string_view) { return ToolOutcome{"Error: no", true}; };
    registry.add(broken);

    (void)dispatch(registry, ToolCall{"call-1", "read_file", "{}"}, context);
    REQUIRE(seen.size() == 3);
    CHECK(seen[0].id == "call-1");
    CHECK(seen[0].name == "read_file");
    CHECK(seen[0].target == "notes.md");
    CHECK(seen[0].state == ToolCallState::Pending);
    CHECK(seen[1].state == ToolCallState::Running);
    CHECK(seen[2].state == ToolCallState::Done);
    // A read asks nobody.
    CHECK(asked_for.empty());

    seen.clear();
    (void)dispatch(registry, ToolCall{"call-2", "write_file", "{}"}, context);
    REQUIRE(seen.size() == 3);
    CHECK(seen[2].state == ToolCallState::Done);
    // The prompt is told which call it is asking about.
    CHECK(asked_for == std::vector<std::string>{"call-2"});

    seen.clear();
    answer = false;
    const ToolOutcome refused = dispatch(registry, ToolCall{"call-3", "write_file", "{}"}, context);
    CHECK(refused.is_error);
    REQUIRE(seen.size() == 2);
    CHECK(seen[0].state == ToolCallState::Pending);
    CHECK(seen[1].state == ToolCallState::Failed);
    CHECK(seen[1].id == "call-3");

    seen.clear();
    (void)dispatch(registry, ToolCall{"call-4", "broken", "{}"}, context);
    REQUIRE(seen.size() == 3);
    CHECK(seen[2].state == ToolCallState::Failed);

    seen.clear();
    (void)dispatch(registry, ToolCall{"call-5", "no_such_tool", "{}"}, context);
    REQUIRE(seen.size() == 2);
    CHECK(seen[0].state == ToolCallState::Pending);
    CHECK(seen[1].state == ToolCallState::Failed);
    CHECK(seen[1].name == "no_such_tool");

    // A gate that throws -- the turn cancelled while its question was open,
    // or a driver gone mid-question -- still ends the call, `failed`, before
    // the throw goes on: no surface is left drawing it `pending`, and the
    // tool never ran.
    seen.clear();
    bool ran = false;
    Tool guarded = simple_tool("edit_file", true);
    guarded.describe_target = [](std::string_view) { return std::string{"a.txt"}; };
    guarded.run = [&ran](std::string_view) {
        ran = true;
        return ToolOutcome{"ran", false};
    };
    registry.add(guarded);
    context.confirm = [](const GateRequest&) -> bool { throw apogee::harness::CancelledError(); };
    CHECK_THROWS_AS(dispatch(registry, ToolCall{"call-6", "edit_file", "{}"}, context),
                    apogee::harness::CancelledError);
    CHECK_FALSE(ran);
    REQUIRE(seen.size() == 2);
    CHECK(seen[0].state == ToolCallState::Pending);
    CHECK(seen[1].state == ToolCallState::Failed);
    CHECK(seen[1].id == "call-6");
    CHECK(seen[1].target == "a.txt");

    seen.clear();
    Tool fetcher = simple_tool("fetch_url");
    fetcher.outbound = true;
    fetcher.describe_target = [](std::string_view) { return std::string{"example.com"}; };
    registry.add(fetcher);
    context.confirm = [](const GateRequest&) -> bool {
        throw std::runtime_error("the driver closed stdin with a permission prompt unanswered");
    };
    CHECK_THROWS_AS(dispatch(registry, ToolCall{"call-7", "fetch_url", "{}"}, context),
                    std::runtime_error);
    REQUIRE(seen.size() == 2);
    CHECK(seen[0].state == ToolCallState::Pending);
    CHECK(seen[1].state == ToolCallState::Failed);
    CHECK(seen[1].target == "example.com");

    CHECK(apogee::agent::to_string(ToolCallState::Pending) == "pending");
    CHECK(apogee::agent::to_string(ToolCallState::Running) == "running");
    CHECK(apogee::agent::to_string(ToolCallState::Done) == "done");
    CHECK(apogee::agent::to_string(ToolCallState::Failed) == "failed");
}

TEST_CASE("a tool's output that is not UTF-8 leaves dispatch as UTF-8", "[agent][tool][utf8]") {
    // A Latin-1 file, a command's raw bytes, a stream cut mid-character: the
    // result reaches history, the session file, the next request and an MCP
    // client's frame, each a strict JSON dump -- so dispatch, which every
    // surface runs a tool through, hands back text. Each maximal ill-formed
    // part is one U+FFFD; valid output is untouched, byte for byte.
    const std::string valid = "na\xC3\xAFve \xE2\x82\xAC \xF0\x9F\x98\x80\n";
    for (const auto& [said, expected] : std::vector<std::pair<std::string, std::string>>{
             {"caf\xE9 cr\xE8me", "caf\xEF\xBF\xBD cr\xEF\xBF\xBDme"},
             {"cut \xE2\x82", "cut \xEF\xBF\xBD"},
             {valid, valid}}) {
        ToolRegistry registry;
        Tool tool;
        tool.name = "raw";
        tool.description = "d";
        tool.run = [raw = said](std::string_view) { return ToolOutcome{raw, false}; };
        registry.add(std::move(tool));
        const ToolOutcome outcome = dispatch(registry, ToolCall{"c", "raw", "{}"}, {});
        CHECK(outcome.content == expected);
        CHECK_FALSE(outcome.is_error);
    }
}

// ---------------------------------------------------------------------------
// fetch_url (its gate, redirects and URL parsing are in fetch_url_test.cpp)
// ---------------------------------------------------------------------------

namespace {

/// The tool body with a gate that allows every hop -- what these cases test
/// is the page handling, not the guard.
ToolOutcome run_fetch(const Tool& tool, std::string_view arguments) {
    const apogee::agent::TargetGate allow_all = [](std::string_view, std::string_view) {
        return true;
    };
    return tool.run_gated(arguments, allow_all);
}

}  // namespace

TEST_CASE("the reader keeps prose and drops markup", "[agent][fetch]") {
    const auto read = [](std::string_view html) {
        return apogee::agent::extract_readable(
                   html, *apogee::agent::parse_http_url("https://example.test/"))
            .text;
    };
    CHECK(read("<p>Hello <b>world</b></p>") == "Hello world");
    // script and style contain code, not prose; they still part the words.
    CHECK(read("a<script>var x = 1;</script>b") == "a b");
    CHECK(read("a<style>p{color:red}</style>b") == "a b");
    // Entities are decoded, numeric ones too.
    CHECK(read("&lt;tag&gt; &amp; more &#233;&#x2014;") == "<tag> & more \u00e9\u2014");
    // A block is a break; an inline tag, as a browser shows it, is not.
    CHECK(read("<p>a</p><p>b</p>") == "a\n\nb");
    CHECK(read("a<br>b") == "a\nb");
    CHECK(read("<b>Hel</b>lo") == "Hello");
    // Whitespace collapses.
    CHECK(read("a   \n\n  b") == "a b");
    CHECK(read("").empty());
}

TEST_CASE("fetch_url returns page text through the injected fetcher", "[agent][fetch]") {
    // Injected, so this is hermetic: no network, which is what keeps the loop's
    // conformance suite runnable on every push.
    std::string requested;
    const Tool tool = make_fetch_url_tool([&requested](std::string_view url) {
        requested = url;
        return FetchResult{200, "<html><body><h1>Title</h1><p>Body text</p></body></html>", ""};
    });

    const ToolOutcome outcome = run_fetch(tool, R"({"url":"https://example.test/page"})");

    CHECK(requested == "https://example.test/page");
    CHECK_FALSE(outcome.is_error);
    CHECK(outcome.content.find("Title") != std::string::npos);
    CHECK(outcome.content.find("Body text") != std::string::npos);
    CHECK(outcome.content.find("<h1>") == std::string::npos);
    // Reading a page changes nothing on this machine, so it does not write;
    // but the URL leaves it, so the tool is gated as outbound.
    CHECK_FALSE(tool.writes);
    CHECK(tool.outbound);
}

TEST_CASE("fetch_url reports bad arguments as fixable errors", "[agent][fetch]") {
    const Tool tool =
        make_fetch_url_tool([](std::string_view) { return FetchResult{200, "ok", ""}; });

    CHECK(run_fetch(tool, "not json").is_error);
    CHECK(run_fetch(tool, "{}").is_error);
    // It cannot search -- a query where a URL belongs is a clear error, not a
    // confusing empty result.
    const auto outcome = run_fetch(tool, R"({"url":"how do I sort a list"})");
    CHECK(outcome.is_error);
    CHECK(outcome.content.find("not an http or https URL") != std::string::npos);
}

TEST_CASE("fetch_url surfaces transport and HTTP failures", "[agent][fetch]") {
    const Tool failing = make_fetch_url_tool(
        [](std::string_view) { return FetchResult{0, "", "connection refused"}; });
    const auto transport = run_fetch(failing, R"({"url":"https://x.test"})");
    CHECK(transport.is_error);
    CHECK(transport.content.find("connection refused") != std::string::npos);

    const Tool not_found =
        make_fetch_url_tool([](std::string_view) { return FetchResult{404, "gone", ""}; });
    const auto http = run_fetch(not_found, R"({"url":"https://x.test"})");
    CHECK(http.is_error);
    CHECK(http.content.find("404") != std::string::npos);
}

TEST_CASE("fetch_url pages a long text and says where to read on", "[agent][fetch]") {
    // A model handed silently truncated text will confidently answer about the
    // part it never saw.
    const Tool tool = make_fetch_url_tool(
        [](std::string_view) { return FetchResult{200, std::string(5000, 'x'), ""}; }, 100);

    const auto outcome = run_fetch(tool, R"({"url":"https://x.test"})");
    CHECK_FALSE(outcome.is_error);
    CHECK(outcome.content.find("Page 1 of 50") != std::string::npos);
    CHECK(outcome.content.find("offset 100.]") != std::string::npos);
    CHECK(outcome.content.size() < 400);
}

TEST_CASE("fetch_url reports an empty page rather than an empty result", "[agent][fetch]") {
    const Tool tool = make_fetch_url_tool(
        [](std::string_view) { return FetchResult{200, "<html><body></body></html>", ""}; });
    const auto outcome = run_fetch(tool, R"({"url":"https://x.test"})");
    CHECK_FALSE(outcome.is_error);
    CHECK(outcome.content.find("no readable text") != std::string::npos);
}

TEST_CASE("fetch_url with no fetcher wired says so", "[agent][fetch]") {
    const Tool tool = make_fetch_url_tool(nullptr);
    const auto outcome = run_fetch(tool, R"({"url":"https://x.test"})");
    CHECK(outcome.is_error);
    CHECK(outcome.content.find("not available") != std::string::npos);
}
