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
        CHECK(contains(outcome.content,
                       "URL: https://new.example/c (redirected from https://allowed.example/a)"));
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
    SECTION("a host allowed by an answer is not asked again on its own redirect") {
        // Not listed, so the first question is a real one; the same-host hop
        // must not become a second. (With the host on the allow-list the
        // checker never reaches the prompt, and a repeat would go unseen.)
        Gate gate{{}, true, {}};
        CHECK_FALSE(fetch(web, gate, "https://allowed.example/a").is_error);
        REQUIRE(gate.asked.size() == 2);
        CHECK(gate.asked[0].first == "allowed.example");
        CHECK(gate.asked[1].first == "new.example");
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

namespace {

FetchResult typed(std::string body, std::string content_type) {
    FetchResult result{200, std::move(body), "", ""};
    result.content_type = std::move(content_type);
    return result;
}

ToolOutcome read_page(const FetchResult& reply, std::string_view arguments,
                      std::size_t max_bytes = apogee::agent::kFetchPageBytes) {
    const apogee::agent::Tool tool =
        make_fetch_url_tool([reply](std::string_view) { return reply; }, max_bytes);
    return tool.run_gated(arguments, [](std::string_view, std::string_view) { return true; });
}

}  // namespace

TEST_CASE("a page reads as its content under a header naming where it came from",
          "[agent][fetch][readable]") {
    const ToolOutcome outcome = read_page(
        typed("<html><head><title>The Title</title></head><body><nav>menu</nav><main><h1>Heading"
              "</h1><p>Body with <a href=\"/next\">a link</a>.</p></main></body></html>",
              "text/html; charset=utf-8"),
        R"({"url":"https://site.test/dir/page"})");
    REQUIRE_FALSE(outcome.is_error);
    CHECK(outcome.content ==
          "Title: The Title\nURL: https://site.test/dir/page\n\n# Heading\n\n"
          "Body with [a link](https://site.test/next).");

    // After a redirect: the final address, and where it came from.
    Web web;
    web.pages["https://old.test/"] = redirect("https://old.test/moved");
    web.pages["https://old.test/moved"] = typed("<p>moved text</p>", "text/html");
    Gate gate;
    gate.allowed = {"old.test"};
    const ToolOutcome moved = fetch(web, gate, "https://old.test/");
    CHECK(moved.content.starts_with(
        "URL: https://old.test/moved (redirected from https://old.test/)\n\nmoved text"));
}

TEST_CASE("text passes through, and anything that is not a page is refused by name",
          "[agent][fetch][readable]") {
    const std::string url = R"({"url":"https://files.test/x"})";
    SECTION("text types are read as they are") {
        CHECK(read_page(typed("{\"a\": 1}", "application/json"), url)
                  .content.ends_with("\n\n{\"a\": 1}"));
        CHECK(read_page(typed("# Notes\n\n- one", "text/markdown"), url)
                  .content.ends_with("\n\n# Notes\n\n- one"));
        CHECK(read_page(typed("<a><b/></a>", "application/atom+xml"), url)
                  .content.ends_with("<a><b/></a>"));  // XML is text, not a page to extract
        CHECK(read_page(typed("plain <b>not markup</b>", "text/plain"), url)
                  .content.ends_with("plain <b>not markup</b>"));
    }
    SECTION("an unlabelled body is read for what it is") {
        CHECK(
            read_page(typed("<!doctype html><p>page</p>", ""), url).content.ends_with("\n\npage"));
        CHECK(read_page(typed("just text", "application/octet-stream"), url)
                  .content.ends_with("just text"));
    }
    SECTION("a PDF, by type or by its first bytes, an image, an archive, binary") {
        for (const FetchResult& reply :
             {typed("%PDF-1.7 ...", "application/pdf"), typed("%PDF-1.4 ...", "text/html")}) {
            const ToolOutcome pdf = read_page(reply, url);
            CHECK(pdf.is_error);
            CHECK(pdf.content.find("is a PDF, which fetch_url does not read") != std::string::npos);
            CHECK(pdf.content.find("PDF-1") == std::string::npos);  // never its bytes
        }
        CHECK(read_page(typed(std::string("\x89PNG\r\n\x1a\n\0", 9), "image/png"), url)
                  .content.find("is an image (image/png)") != std::string::npos);
        CHECK(read_page(typed("PK\x03\x04", "application/zip"), url)
                  .content.find("is an archive (application/zip)") != std::string::npos);
        CHECK(read_page(typed(std::string("\x7f"
                                          "ELF\0\0",
                                          6),
                              ""),
                        url)
                  .content.find("is binary data") != std::string::npos);
        CHECK(read_page(typed("x", "application/vnd.ms-excel"), url)
                  .content.find("is application/vnd.ms-excel") != std::string::npos);
    }
    SECTION("a declared charset is honoured, and stray bytes never reach the model") {
        CHECK(read_page(typed("caf\xE9", "text/plain; charset=ISO-8859-1"), url)
                  .content.ends_with("café"));
        CHECK(read_page(typed("<meta charset=\"windows-1252\"><p>\x93hi\x94</p>", "text/html"), url)
                  .content.ends_with("“hi”"));
        CHECK(read_page(typed("ok\xFF", "text/plain"), url).content.ends_with("ok�"));
    }
    SECTION("a page with nothing to read says so, and why that may be") {
        const ToolOutcome empty = read_page(typed("<html><head><title>App</title></head><body><div "
                                                  "id=\"root\"></div><script>x()</script>"
                                                  "</body></html>",
                                                  "text/html"),
                                            url);
        CHECK_FALSE(empty.is_error);
        CHECK(empty.content.find("Title: App") != std::string::npos);
        CHECK(empty.content.find("no readable text") != std::string::npos);
        CHECK(empty.content.find("JavaScript") != std::string::npos);
    }
}

TEST_CASE("a long page is read in pages with offset, to its last section",
          "[agent][fetch][readable][paging]") {
    std::string html = "<html><head><title>Long</title></head><body><main>";
    for (int i = 1; i <= 30; ++i) {
        html += "<h2>Section " + std::to_string(i) + "</h2><p>" + std::string(180, 's') +
                " end of "
                "section " +
                std::to_string(i) + ".</p>";
    }
    html += "</main></body></html>";
    const FetchResult reply = typed(html, "text/html");

    std::size_t offset = 0;
    std::string everything;
    int calls = 0;
    for (;;) {
        ++calls;
        REQUIRE(calls < 20);
        const ToolOutcome outcome = read_page(
            reply, R"({"url":"https://long.test/","offset":)" + std::to_string(offset) + "}", 1024);
        REQUIRE_FALSE(outcome.is_error);
        CHECK(outcome.content.starts_with("Title: Long\nURL: https://long.test/\nPage " +
                                          std::to_string(calls) + " of "));
        everything += outcome.content;
        const std::size_t at = outcome.content.find("with the same url and offset ");
        if (at == std::string::npos) {
            CHECK(outcome.content.ends_with(": the end of the page.]"));
            break;
        }
        const std::size_t next = std::stoul(outcome.content.substr(at + 29));
        CHECK(next > offset);
        offset = next;
    }
    CHECK(calls > 3);
    for (int i = 1; i <= 30; ++i) {
        INFO(i);
        // Every section once: no gap, no overlap.
        const std::string heading = "## Section " + std::to_string(i) + "\n";
        CHECK(everything.find(heading) != std::string::npos);
        CHECK(everything.find(heading) == everything.rfind(heading));
    }
    CHECK(everything.find("end of section 30.") != std::string::npos);

    // An offset past the end, and one that is not a number, are errors.
    const ToolOutcome past =
        read_page(reply, R"({"url":"https://long.test/","offset":999999})", 1024);
    CHECK(past.is_error);
    CHECK(past.content.find("past the end") != std::string::npos);
    const ToolOutcome negative = read_page(reply, R"({"url":"https://long.test/","offset":-1})");
    CHECK(negative.is_error);
    CHECK(negative.content.find("0 or more") != std::string::npos);
    CHECK(read_page(reply, R"({"url":"https://long.test/","offset":"soon"})").is_error);
    CHECK_FALSE(read_page(reply, R"({"url":"https://long.test/","offset":"0"})").is_error);
}
