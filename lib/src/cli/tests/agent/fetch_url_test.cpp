#include "agent/fetch_url.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "agent/tool.h"

/// `fetch_url` as an outbound tool: the host the gate is asked about, the
/// redirects it follows one hop at a time, and the URL it rebuilds so the
/// host asked about is the host reached.
namespace {

using apogee::agent::dispatch;
using apogee::agent::DispatchContext;
using apogee::agent::FetchResult;
using apogee::agent::GateRequest;
using apogee::agent::HttpUrl;
using apogee::agent::make_fetch_url_tool;
using apogee::agent::parse_http_url;
using apogee::agent::Permission;
using apogee::agent::resolve_redirect;
using apogee::agent::ToolOutcome;
using apogee::agent::ToolRegistry;
using apogee::harness::ToolCall;

/// A scripted web: each URL answers with its page or its redirect, and every
/// URL actually requested is recorded -- the assertion that matters for a
/// refusal is that nothing was fetched.
struct Web {
    std::map<std::string, FetchResult, std::less<>> pages;
    std::vector<std::string> requested;

    [[nodiscard]] apogee::agent::UrlFetcher fetcher() {
        return [this](std::string_view url) {
            requested.emplace_back(url);
            const auto it = pages.find(url);
            return it == pages.end() ? FetchResult{404, "", "", ""} : it->second;
        };
    }
};

FetchResult page(std::string body) {
    return FetchResult{200, std::move(body), "", ""};
}

FetchResult redirect(std::string location, long status = 302) {
    return FetchResult{status, "", "", std::move(location)};
}

/// The gate as a surface builds it: allowed hosts pass, anything else is
/// asked through `answer` (null: nobody to ask), and every question is kept.
struct Gate {
    std::vector<std::string> allowed;
    std::optional<bool> answer;
    std::vector<std::pair<std::string, std::string>> asked;  // (target, detail)

    [[nodiscard]] DispatchContext context() {
        DispatchContext context;
        context.permission = [this](const GateRequest& request) {
            CHECK(request.outbound);
            CHECK(request.tool == "fetch_url");
            for (const std::string& host : allowed) {
                if (host == request.target) {
                    return Permission::Allow;
                }
            }
            return Permission::Ask;
        };
        if (answer.has_value()) {
            context.confirm = [this](const GateRequest& request) {
                asked.emplace_back(request.target, request.detail);
                return *answer;
            };
        }
        return context;
    }
};

ToolOutcome fetch(Web& web, Gate& gate, std::string_view url) {
    ToolRegistry registry;
    registry.add(make_fetch_url_tool(web.fetcher()));
    return dispatch(registry,
                    ToolCall{"c", "fetch_url", R"({"url":")" + std::string{url} + R"("})"},
                    gate.context());
}

bool contains(const std::string& text, std::string_view part) {
    return text.find(part) != std::string::npos;
}

}  // namespace

TEST_CASE("a new host is asked about by name, with the whole URL shown", "[agent][fetch][gate]") {
    Web web;
    web.pages["https://docs.python.org/3/?q=1"] = page("<p>Docs</p>");
    Gate gate;
    gate.answer = true;

    const ToolOutcome outcome = fetch(web, gate, "https://DOCS.python.org./3/?q=1#frag");
    CHECK_FALSE(outcome.is_error);
    CHECK(contains(outcome.content, "Docs"));
    REQUIRE(gate.asked.size() == 1);
    CHECK(gate.asked[0].first == "docs.python.org");  // canonical: the key `always` writes
    CHECK(gate.asked[0].second == "https://docs.python.org/3/?q=1");
    CHECK(web.requested == std::vector<std::string>{"https://docs.python.org/3/?q=1"});
}

TEST_CASE("a refused host is a tool result, and nothing is sent to it", "[agent][fetch][gate]") {
    Web web;
    web.pages["https://evil.example/?data=secret"] = page("x");
    Gate asked_no;
    asked_no.answer = false;
    const ToolOutcome refused = fetch(web, asked_no, "https://evil.example/?data=secret");
    CHECK(refused.is_error);
    CHECK(contains(refused.content, "permission to reach evil.example was not given"));
    CHECK(web.requested.empty());

    // Nobody to ask -- a pipe, `serve` -- is the same refusal, without a
    // question.
    Gate nobody;
    CHECK(fetch(web, nobody, "https://evil.example/").is_error);
    CHECK(nobody.asked.empty());
    CHECK(web.requested.empty());

    // A listed host passes where nobody can be asked.
    web.pages["https://evil.example/"] = page("listed");
    nobody.allowed = {"evil.example"};
    const ToolOutcome listed = fetch(web, nobody, "https://evil.example/");
    CHECK_FALSE(listed.is_error);
    CHECK(contains(listed.content, "listed"));
}

TEST_CASE("a redirect to a new host is asked about hop by hop", "[agent][fetch][gate]") {
    Web web;
    web.pages["https://allowed.example/a"] = redirect("/b");                          // same host
    web.pages["https://allowed.example/b"] = redirect("https://new.example/c", 301);  // new host
    web.pages["https://new.example/c"] = page("<p>arrived</p>");

    SECTION("allowed: followed, with the final URL named") {
        Gate gate{{"allowed.example"}, true, {}};
        const ToolOutcome outcome = fetch(web, gate, "https://allowed.example/a");
        CHECK_FALSE(outcome.is_error);
        CHECK(contains(outcome.content, "arrived"));
        CHECK(contains(outcome.content, "redirected to https://new.example/c"));
        // Only the new host was asked about: the same host was just allowed.
        REQUIRE(gate.asked.size() == 1);
        CHECK(gate.asked[0].first == "new.example");
        CHECK(gate.asked[0].second == "https://new.example/c");
    }
    SECTION("refused: nothing is fetched from the new host") {
        Gate gate{{"allowed.example"}, false, {}};
        const ToolOutcome outcome = fetch(web, gate, "https://allowed.example/a");
        CHECK(outcome.is_error);
        CHECK(contains(outcome.content, "permission to reach new.example was not given"));
        CHECK(web.requested ==
              std::vector<std::string>{"https://allowed.example/a", "https://allowed.example/b"});
    }
    SECTION("nobody to ask: the same refusal") {
        Gate gate{{"allowed.example"}, std::nullopt, {}};
        CHECK(fetch(web, gate, "https://allowed.example/a").is_error);
        CHECK(web.requested.size() == 2);
    }
}

TEST_CASE("redirects that go nowhere fetch_url can follow are refused", "[agent][fetch]") {
    Web web;
    Gate gate{{"a.example"}, true, {}};
    web.pages["https://a.example/file"] = redirect("file:///etc/passwd");
    CHECK(contains(fetch(web, gate, "https://a.example/file").content, "not an http or https"));
    web.pages["https://a.example/none"] = redirect("");
    CHECK(contains(fetch(web, gate, "https://a.example/none").content, "no Location"));
    web.pages["https://a.example/loop"] = redirect("/loop");
    const ToolOutcome loop = fetch(web, gate, "https://a.example/loop");
    CHECK(loop.is_error);
    CHECK(contains(loop.content, "redirected more than 10 times"));
    CHECK(web.requested.size() == 2 + 11);
}

TEST_CASE("a URL the gate cannot read is refused before anything is sent", "[agent][fetch][gate]") {
    Web web;
    Gate gate;
    gate.answer = true;
    for (const char* url : {
             "https://allowed.example@evil.example/",      // userinfo
             "https://allowed.example\\\\@evil.example/",  // a backslash (JSON-escaped)
             "https://evil%2eexample/",                    // a percent-escaped host
             "https://*.example/",                         // a pattern
             "https://a.example:0/",                       // no such port
             "https://a.example:99999/",
             "https://a.example:/",
             "ftp://a.example/",
             "how do I sort a list",
         }) {
        INFO(url);
        const ToolOutcome outcome = fetch(web, gate, url);
        CHECK(outcome.is_error);
    }
    CHECK(gate.asked.empty());
    CHECK(web.requested.empty());
}

TEST_CASE("the URL fetched is rebuilt from what the gate saw", "[agent][fetch]") {
    const std::optional<HttpUrl> url =
        parse_http_url("  HTTPS://Example.COM.:8443/a b/\xc3\xa9?q=<x>#fragment ");
    REQUIRE(url.has_value());
    CHECK(url->scheme == "https");
    CHECK(url->host == "example.com");
    CHECK(url->port == "8443");
    // Nothing unprintable reaches the transport or a prompt.
    CHECK(url->str() == "https://example.com:8443/a%20b/%C3%A9?q=%3Cx%3E");

    CHECK(parse_http_url("http://[::1]:8080/x")->str() == "http://[::1]:8080/x");
    CHECK(parse_http_url("http://[::1]/")->host == "::1");
    CHECK(parse_http_url("https://example.com")->str() == "https://example.com/");
    CHECK(parse_http_url("https://example.com?q")->str() == "https://example.com/?q");
    // A control character in the path cannot become a terminal escape.
    CHECK(parse_http_url("https://example.com/\x1b[2J")->str() == "https://example.com/%1B[2J");
}

TEST_CASE("redirect locations resolve against the page they came from", "[agent][fetch]") {
    const HttpUrl base = *parse_http_url("https://example.com:8443/docs/page?x=1");
    const auto to = [&base](std::string_view location) {
        const std::optional<HttpUrl> url = resolve_redirect(base, location);
        return url.has_value() ? url->str() : std::string{"(refused)"};
    };
    CHECK(to("https://other.example/y") == "https://other.example/y");
    CHECK(to("//other.example/y") == "https://other.example/y");
    CHECK(to("/root") == "https://example.com:8443/root");
    CHECK(to("sibling") == "https://example.com:8443/docs/sibling");
    CHECK(to("?y=2") == "https://example.com:8443/docs/page?y=2");
    CHECK(to("javascript:alert(1)") == "(refused)");
    CHECK(to("mailto:a@b.example") == "(refused)");
    CHECK(to("") == "(refused)");
}

TEST_CASE("an outbound tool with no target reader cannot be registered", "[agent][tool][gate]") {
    apogee::agent::Tool tool;
    tool.name = "leaky";
    tool.outbound = true;
    tool.run = [](std::string_view) { return ToolOutcome{"sent", false}; };
    ToolRegistry registry;
    CHECK_THROWS_AS(registry.add(tool), std::invalid_argument);

    // And a call it names no target for is refused unrun, whatever the gate
    // would have said.
    tool.describe_target = [](std::string_view) { return std::string{}; };
    registry.add(tool);
    DispatchContext allow_all;
    allow_all.permission = [](const GateRequest&) { return Permission::Allow; };
    const ToolOutcome outcome = dispatch(registry, ToolCall{"c", "leaky", "{}"}, allow_all);
    CHECK(outcome.is_error);
    CHECK(outcome.content != "sent");  // the tool never ran
    CHECK(contains(outcome.content, "could not tell which website"));
    // The gate's own answer, asked directly: no target, no permission.
    CHECK_FALSE(apogee::agent::permitted(tool, "{}", allow_all));
    CHECK_FALSE(apogee::agent::permitted_target(tool, "", "", allow_all));
    CHECK(apogee::agent::permitted_target(tool, "a.example", "", allow_all));
}
