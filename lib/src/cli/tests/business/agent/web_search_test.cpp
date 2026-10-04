#include "agent/web_search.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "agent/tool.h"

/// `web_search` over a SearXNG instance (25e): responses recorded from a real
/// instance, every way a search fails naming what to change, and the rule
/// the whole tool hangs on -- never an empty success.
namespace {

using apogee::agent::FetchResult;
using apogee::agent::HttpUrl;
using apogee::agent::make_searxng_provider;
using apogee::agent::make_web_search_tool;
using apogee::agent::parse_http_url;
using apogee::agent::parse_searxng_response;
using apogee::agent::SearchRequest;
using apogee::agent::SearchResponse;
using apogee::agent::ToolOutcome;

std::string fixture(std::string_view name) {
    const std::filesystem::path path =
        std::filesystem::path{APOGEE_TEST_FIXTURES} / "searxng" / name;
    std::ifstream in{path, std::ios::binary};
    REQUIRE(in.good());
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

HttpUrl instance(std::string_view url = "http://127.0.0.1:8888") {
    std::optional<HttpUrl> parsed = parse_http_url(url);
    REQUIRE(parsed.has_value());
    return *parsed;
}

/// An instance that answers every search with `reply`, and remembers what
/// was asked of it.
struct Instance {
    FetchResult reply;
    std::vector<std::string> asked;

    [[nodiscard]] apogee::agent::UrlFetcher fetcher() {
        return [this](std::string_view url) {
            asked.emplace_back(url);
            return reply;
        };
    }
};

FetchResult ok(std::string body) {
    FetchResult result;
    result.status = 200;
    result.body = std::move(body);
    return result;
}

/// The tool over an instance answering `reply`.
ToolOutcome search(Instance& instance_, std::string_view arguments, std::size_t count = 5) {
    const apogee::agent::Tool tool = make_web_search_tool(
        make_searxng_provider(instance(), instance_.fetcher()), "127.0.0.1", count);
    return tool.run(arguments);
}

}  // namespace

TEST_CASE("recorded SearXNG answers parse into results, answers, boxes and failing engines",
          "[agent][search]") {
    SECTION("ordinary results, and the engine that failed beside them") {
        const SearchResponse response = parse_searxng_response(fixture("normal.json"));
        REQUIRE(response.error.empty());
        REQUIRE(response.results.size() == 6);
        CHECK(response.results[0].title.starts_with("Misc. bug: llama-server crashes since b6000"));
        CHECK(response.results[0].url == "https://github.com/ggml-org/llama.cpp/issues/18563");
        CHECK_FALSE(response.results[0].snippet.empty());
        CHECK(response.results[0].date.empty());  // no publishedDate: no date made up
        CHECK(response.unresponsive == std::vector<std::string>{"duckduckgo (CAPTCHA)"});
        CHECK(response.total == 0);  // this version sends no number_of_results
    }
    SECTION("a published date, as the day") {
        const SearchResponse response = parse_searxng_response(fixture("dated.json"));
        REQUIRE(response.error.empty());
        bool dated = false;
        for (const apogee::agent::SearchResult& result : response.results) {
            if (!result.date.empty()) {
                dated = true;
                CHECK(result.date.size() == 10);
                CHECK(result.date[4] == '-');
                CHECK(result.date[7] == '-');
            }
        }
        CHECK(dated);
    }
    SECTION("a direct answer in the object form current SearXNG sends") {
        const SearchResponse response = parse_searxng_response(fixture("answer.json"));
        REQUIRE(response.error.empty());
        CHECK(response.results.empty());
        CHECK(response.answers == std::vector<std::string>{"[en] avg(1, 2, 3) = 2"});
    }
    SECTION("an infobox, with the page it came from") {
        const SearchResponse response = parse_searxng_response(fixture("infobox.json"));
        REQUIRE(response.infoboxes.size() == 1);
        CHECK(response.infoboxes[0].title == "Albert Einstein");
        CHECK(response.infoboxes[0].url == "https://en.wikipedia.org/wiki/Albert_Einstein");
        CHECK(response.infoboxes[0].content.starts_with("Albert Einstein was a German-born"));
    }
    SECTION("nothing found, and nothing failed") {
        const SearchResponse response = parse_searxng_response(fixture("zero.json"));
        CHECK(response.error.empty());
        CHECK(response.results.empty());
        CHECK(response.unresponsive.empty());
    }
    SECTION("nothing found because the one engine asked failed") {
        const SearchResponse response = parse_searxng_response(fixture("unresponsive.json"));
        CHECK(response.results.empty());
        CHECK(response.unresponsive == std::vector<std::string>{"duckduckgo (CAPTCHA)"});
    }
    SECTION("anything else is an error that shows what came back") {
        const SearchResponse html = parse_searxng_response(fixture("forbidden.html"));
        CHECK(html.error.find("not SearXNG's JSON") != std::string::npos);
        CHECK(html.error.find("403 Forbidden") != std::string::npos);
        CHECK(parse_searxng_response("").error.find("it was empty") != std::string::npos);
        CHECK_FALSE(parse_searxng_response(R"({"query":"x"})").error.empty());
        CHECK_FALSE(parse_searxng_response(R"({"results":{}})").error.empty());
        CHECK_FALSE(parse_searxng_response("[1,2]").error.empty());
    }
    SECTION("the other shapes the API has used") {
        const SearchResponse response = parse_searxng_response(
            R"({"results":[{"title":"t","url":"https://a.example/","content":" two\n\n lines ",)"
            R"("publishedDate":"not a date"},{"title":"no url"}],"number_of_results":1234.0,)"
            R"("answers":["plain",{"content":"c"},{"text":"t"},{}],)"
            R"("infoboxes":[{"infobox":"B","content":"x","urls":[{"url":"https://b.example/"}]}],)"
            R"("unresponsive_engines":["bare",["pair","why"],["alone"],[""]]})");
        REQUIRE(response.error.empty());
        REQUIRE(response.results.size() == 1);  // a result with no URL is not one
        CHECK(response.results[0].snippet == "two lines");
        CHECK(response.results[0].date.empty());
        CHECK(response.total == 1234);
        CHECK(response.answers == std::vector<std::string>{"plain", "c", "t"});
        CHECK(response.infoboxes[0].url == "https://b.example/");
        CHECK(response.unresponsive == std::vector<std::string>{"bare", "pair (why)", "alone"});
    }
}

TEST_CASE("a search asks the instance's own path, the query form-encoded, JSON always",
          "[agent][search]") {
    SearchRequest request;
    request.query = "what's new in b6000 & c++?";
    CHECK(apogee::agent::searxng_search_url(instance(), request) ==
          "http://127.0.0.1:8888/search?q=what%27s+new+in+b6000+%26+c%2B%2B%3F&format=json");
    request.time_range = "week";
    CHECK(apogee::agent::searxng_search_url(instance("https://search.example/searxng"), request)
              .ends_with("/searxng/search?q=what%27s+new+in+b6000+%26+c%2B%2B%3F&format=json&"
                         "time_range=week"));
    request.query = "é";
    CHECK(apogee::agent::searxng_search_url(instance("http://h.example/base/"), request) ==
          "http://h.example/base/search?q=%C3%A9&format=json&time_range=week");
}

TEST_CASE("every way a search fails says what to change, never an empty success",
          "[agent][search]") {
    Instance searx;

    SECTION("JSON off: the setting to enable") {
        searx.reply.status = 403;
        searx.reply.body = fixture("forbidden.html");
        const ToolOutcome outcome = search(searx, R"({"query":"llama.cpp"})");
        CHECK(outcome.is_error);
        CHECK(outcome.content.find("search.formats") != std::string::npos);
        CHECK(outcome.content.find("settings.yml") != std::string::npos);
        // Other words will not fix an instance: the model is told to stop,
        // and the loop stops offering the tool.
        CHECK(outcome.content.find("do not search again") != std::string::npos);
        CHECK(outcome.unavailable);
    }
    SECTION("nothing listening: the configured URL") {
        searx.reply.error = "Couldn't connect to server";
        const ToolOutcome outcome = search(searx, R"({"query":"llama.cpp"})");
        CHECK(outcome.is_error);
        CHECK(outcome.unavailable);
        CHECK(outcome.content.find("http://127.0.0.1:8888/") != std::string::npos);
        CHECK(outcome.content.find("tools.search.url") != std::string::npos);
        CHECK(outcome.content.find("Couldn't connect") != std::string::npos);
    }
    SECTION("the bot limiter: the setting to turn off") {
        searx.reply.status = 429;
        CHECK(search(searx, R"({"query":"x"})").content.find("server.limiter") !=
              std::string::npos);
    }
    SECTION("a redirect: where it went") {
        searx.reply.status = 301;
        searx.reply.location = "https://127.0.0.1:8443/search";
        const ToolOutcome outcome = search(searx, R"({"query":"x"})");
        CHECK(outcome.is_error);
        CHECK(outcome.content.find("https://127.0.0.1:8443/search") != std::string::npos);
    }
    SECTION("any other status, and a body that is not the JSON") {
        searx.reply.status = 502;
        CHECK(search(searx, R"({"query":"x"})").content.find("HTTP 502") != std::string::npos);
        searx.reply = ok("<html>a login page</html>");
        const ToolOutcome html = search(searx, R"({"query":"x"})");
        CHECK(html.is_error);
        CHECK(html.content.find("a login page") != std::string::npos);
    }
    SECTION("every engine failed: an error naming them, not 'no results'") {
        searx.reply = ok(fixture("unresponsive.json"));
        const ToolOutcome outcome = search(searx, R"({"query":"llama.cpp release"})");
        CHECK(outcome.is_error);
        CHECK(outcome.content.find("duckduckgo (CAPTCHA)") != std::string::npos);
        // Engines come back: this one may be tried again later.
        CHECK(outcome.content.find("do not search again") == std::string::npos);
        CHECK_FALSE(outcome.unavailable);
    }
    SECTION("nothing found: a result that says so and names the query") {
        searx.reply = ok(fixture("zero.json"));
        const ToolOutcome outcome =
            search(searx, R"({"query":"zqxjvkwpt flurbnaxel","time_range":"day"})");
        CHECK_FALSE(outcome.is_error);
        CHECK_FALSE(outcome.unavailable);
        CHECK(outcome.content ==
              "No results for \"zqxjvkwpt flurbnaxel\" in the past day. Try other words, or a "
              "wider time_range.");
    }
    SECTION("what the model asked for wrong") {
        searx.reply = ok(fixture("normal.json"));
        CHECK(search(searx, R"({})").is_error);
        CHECK(search(searx, R"({"query":"  "})").is_error);
        CHECK(search(searx, "not json").is_error);
        const ToolOutcome range = search(searx, R"({"query":"x","time_range":"decade"})");
        CHECK(range.is_error);
        CHECK(range.content.find("day, week, month or year") != std::string::npos);
        CHECK_FALSE(range.unavailable);  // the model's own mistake: it can fix it
        CHECK(searx.asked.empty());      // none of them reached the instance
    }
}

TEST_CASE("results come back numbered with their URL, date and snippet, capped at the count",
          "[agent][search]") {
    Instance searx;
    searx.reply = ok(fixture("dated.json"));
    const ToolOutcome outcome =
        search(searx, R"({"query":"llama.cpp release","time_range":"month"})", 3);
    REQUIRE_FALSE(outcome.is_error);
    REQUIRE(searx.asked.size() == 1);
    CHECK(searx.asked[0].ends_with("q=llama.cpp+release&format=json&time_range=month"));

    const std::string& text = outcome.content;
    CHECK(text.starts_with("Results for \"llama.cpp release\":\n\n1. "));
    CHECK(text.find("\n2. ") != std::string::npos);
    CHECK(text.find("\n3. ") != std::string::npos);
    CHECK(text.find("\n4. ") == std::string::npos);  // the configured count, not the 44
    const SearchResponse all = parse_searxng_response(fixture("dated.json"));
    for (std::size_t i = 0; i < 3; ++i) {
        INFO(all.results[i].url);
        CHECK(text.find("\n   " + all.results[i].url + "\n") != std::string::npos);
        if (!all.results[i].date.empty()) {
            CHECK(text.find("   " + all.results[i].date) != std::string::npos);
        }
    }
    CHECK(text.ends_with("Open a result with fetch_url to read it."));

    SECTION("answers, boxes and the failing engines ride along") {
        searx.reply = ok(fixture("infobox.json"));
        const ToolOutcome boxed = search(searx, R"({"query":"Albert Einstein"})");
        CHECK(boxed.content.find("About Albert Einstein: Albert Einstein was") !=
              std::string::npos);
        CHECK(boxed.content.find("(https://en.wikipedia.org/wiki/Albert_Einstein)") !=
              std::string::npos);
        CHECK(boxed.content.find("Engines that did not answer: duckduckgo (CAPTCHA).") !=
              std::string::npos);
        searx.reply = ok(fixture("answer.json"));
        const ToolOutcome answered = search(searx, R"({"query":"avg 1 2 3"})");
        CHECK_FALSE(answered.is_error);
        CHECK(answered.content == "Answer: [en] avg(1, 2, 3) = 2");
    }
    SECTION("a total the instance states is shown beside the count") {
        searx.reply =
            ok(R"({"results":[{"title":"A","url":"https://a.example/"}],"number_of_results":90})");
        CHECK(search(searx, R"({"query":"a"})")
                  .content.starts_with("Results for \"a\" (1 of about 90):"));
    }
    SECTION("a long snippet is cut between characters, at every alignment") {
        std::string long_text;
        while (long_text.size() < 1000) {
            long_text += "é€";
        }
        // Two- and three-byte characters behind up to four ASCII bytes, so
        // the cut at a fixed length meets every position within a character.
        for (std::size_t shift = 0; shift < 5; ++shift) {
            searx.reply = ok(R"({"results":[{"title":"A","url":"https://a.example/","content":")" +
                             std::string(shift, 'a') + long_text + R"("}]})");
            const ToolOutcome cut = search(searx, R"({"query":"a"})");
            const std::size_t at = cut.content.find("   ", cut.content.find("https://a.example/"));
            const std::string line =
                cut.content.substr(at + 3, cut.content.find('\n', at) - at - 3);
            INFO("shift " << shift);
            CHECK(line.size() <= 303);
            CHECK(line.ends_with("..."));
            const std::string kept = line.substr(0, line.size() - 3);
            CHECK((kept.ends_with("é") || kept.ends_with("€")));
        }
    }
}

TEST_CASE("web_search is outbound to the configured host, and says what it would send",
          "[agent][search][permission]") {
    Instance searx;
    const apogee::agent::Tool tool =
        make_web_search_tool(make_searxng_provider(instance(), searx.fetcher()), "127.0.0.1", 5);
    CHECK(tool.name == "web_search");
    CHECK(tool.outbound);
    CHECK_FALSE(tool.writes);
    CHECK(tool.describe_target(R"({"query":"anything"})") == "127.0.0.1");
    CHECK(tool.describe_detail(R"({"query":"what changed"})") == "search: what changed");
    CHECK(tool.description.find("top 5 results") != std::string::npos);
}

TEST_CASE("the search section is read once: off, usable, or named as broken",
          "[agent][search][config]") {
    std::string problem;
    apogee::harness::SearchConfig config;
    CHECK_FALSE(apogee::agent::search_instance(config, problem).has_value());
    CHECK(problem.empty());  // absent: simply off

    config.url = "http://127.0.0.1:8888/?q=ignored";
    const std::optional<apogee::agent::SearchInstance> found =
        apogee::agent::search_instance(config, problem);
    REQUIRE(found.has_value());  // no provider named: the one there is
    CHECK(found->provider == "searxng");
    CHECK(found->base.host == "127.0.0.1");
    CHECK(found->base.str() == "http://127.0.0.1:8888/");
    CHECK(found->results == 5);

    config.provider = "brave";
    CHECK_FALSE(apogee::agent::search_instance(config, problem).has_value());
    CHECK(problem.find("'brave'") != std::string::npos);
    config.provider = "searxng";
    config.url = "searx.local:8888";
    CHECK_FALSE(apogee::agent::search_instance(config, problem).has_value());
    CHECK(problem.find("tools.search.url") != std::string::npos);
    config.url = "  ";
    CHECK_FALSE(apogee::agent::search_instance(config, problem).has_value());
    CHECK(problem.find("is not set") != std::string::npos);
}
