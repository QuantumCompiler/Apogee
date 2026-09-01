#include "agent/tool.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "agent/fetch_url.h"

using apogee::agent::dispatch;
using apogee::agent::DispatchContext;
using apogee::agent::FetchResult;
using apogee::agent::make_fetch_url_tool;
using apogee::agent::Permission;
using apogee::agent::permitted;
using apogee::agent::strip_html;
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
    allow.permission = [](std::string_view, std::string_view) { return Permission::Allow; };
    CHECK(permitted(writer, "{}", allow));

    DispatchContext deny;
    deny.permission = [](std::string_view, std::string_view) { return Permission::Deny; };
    CHECK_FALSE(permitted(writer, "{}", deny));

    DispatchContext ask_yes;
    ask_yes.permission = [](std::string_view, std::string_view) { return Permission::Ask; };
    ask_yes.confirm = [](std::string_view, std::string_view) { return true; };
    CHECK(permitted(writer, "{}", ask_yes));

    DispatchContext ask_no = ask_yes;
    ask_no.confirm = [](std::string_view, std::string_view) { return false; };
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
    context.permission = [](std::string_view, std::string_view) { return Permission::Ask; };
    context.confirm = [&](std::string_view tool, std::string_view target) {
        seen_tool = tool;
        seen_target = target;
        return true;
    };

    CHECK(permitted(writer, R"({"path":"/tmp/x"})", context));
    CHECK(seen_tool == "write_file");
    CHECK(seen_target == R"(parsed:{"path":"/tmp/x"})");
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

// ---------------------------------------------------------------------------
// fetch_url
// ---------------------------------------------------------------------------

TEST_CASE("strip_html keeps prose and drops markup", "[agent][fetch]") {
    CHECK(strip_html("<p>Hello <b>world</b></p>") == "Hello world");
    // script and style contain code, not prose.
    CHECK(strip_html("a<script>var x = 1;</script>b") == "a b");
    CHECK(strip_html("a<style>p{color:red}</style>b") == "a b");
    // Entities are decoded.
    CHECK(strip_html("&lt;tag&gt; &amp; more") == "<tag> & more");
    // A tag boundary is a word boundary -- without that, "a</b>b" becomes "ab".
    CHECK(strip_html("a</b>b") == "a b");
    // Whitespace collapses.
    CHECK(strip_html("a   \n\n  b") == "a b");
    CHECK(strip_html("").empty());
}

TEST_CASE("fetch_url returns page text through the injected fetcher", "[agent][fetch]") {
    // Injected, so this is hermetic: no network, which is what keeps the loop's
    // conformance suite runnable on every push.
    std::string requested;
    const Tool tool = make_fetch_url_tool([&requested](std::string_view url) {
        requested = url;
        return FetchResult{200, "<html><body><h1>Title</h1><p>Body text</p></body></html>", ""};
    });

    const ToolOutcome outcome = tool.run(R"({"url":"https://example.test/page"})");

    CHECK(requested == "https://example.test/page");
    CHECK_FALSE(outcome.is_error);
    CHECK(outcome.content.find("Title") != std::string::npos);
    CHECK(outcome.content.find("Body text") != std::string::npos);
    CHECK(outcome.content.find("<h1>") == std::string::npos);
    // Reading a page changes nothing, so it never prompts.
    CHECK_FALSE(tool.writes);
}

TEST_CASE("fetch_url reports bad arguments as fixable errors", "[agent][fetch]") {
    const Tool tool =
        make_fetch_url_tool([](std::string_view) { return FetchResult{200, "ok", ""}; });

    CHECK(tool.run("not json").is_error);
    CHECK(tool.run("{}").is_error);
    // It cannot search -- a query where a URL belongs is a clear error, not a
    // confusing empty result.
    const auto outcome = tool.run(R"({"url":"how do I sort a list"})");
    CHECK(outcome.is_error);
    CHECK(outcome.content.find("not an http or https URL") != std::string::npos);
}

TEST_CASE("fetch_url surfaces transport and HTTP failures", "[agent][fetch]") {
    const Tool failing = make_fetch_url_tool(
        [](std::string_view) { return FetchResult{0, "", "connection refused"}; });
    const auto transport = failing.run(R"({"url":"https://x.test"})");
    CHECK(transport.is_error);
    CHECK(transport.content.find("connection refused") != std::string::npos);

    const Tool not_found =
        make_fetch_url_tool([](std::string_view) { return FetchResult{404, "gone", ""}; });
    const auto http = not_found.run(R"({"url":"https://x.test"})");
    CHECK(http.is_error);
    CHECK(http.content.find("404") != std::string::npos);
}

TEST_CASE("fetch_url truncates and says so", "[agent][fetch]") {
    // A model handed silently truncated text will confidently answer about the
    // part it never saw.
    const Tool tool = make_fetch_url_tool(
        [](std::string_view) { return FetchResult{200, std::string(5000, 'x'), ""}; }, 100);

    const auto outcome = tool.run(R"({"url":"https://x.test"})");
    CHECK_FALSE(outcome.is_error);
    CHECK(outcome.content.find("[truncated]") != std::string::npos);
    CHECK(outcome.content.size() < 200);
}

TEST_CASE("fetch_url reports an empty page rather than an empty result", "[agent][fetch]") {
    const Tool tool = make_fetch_url_tool(
        [](std::string_view) { return FetchResult{200, "<html><body></body></html>", ""}; });
    const auto outcome = tool.run(R"({"url":"https://x.test"})");
    CHECK_FALSE(outcome.is_error);
    CHECK(outcome.content.find("no readable text") != std::string::npos);
}

TEST_CASE("fetch_url with no fetcher wired says so", "[agent][fetch]") {
    const Tool tool = make_fetch_url_tool(nullptr);
    const auto outcome = tool.run(R"({"url":"https://x.test"})");
    CHECK(outcome.is_error);
    CHECK(outcome.content.find("not available") != std::string::npos);
}
