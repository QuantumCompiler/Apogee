#include "agent/readable.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

/// The page reader (25f): the fixture corpus above all -- each page's own
/// text present, its furniture absent -- then the parser's edge cases, the
/// characters, and paging.
namespace {

using apogee::agent::extract_readable;
using apogee::agent::HttpUrl;
using apogee::agent::page_of;
using apogee::agent::parse_http_url;
using apogee::agent::ReadablePage;

std::string fixture(std::string_view name) {
    const std::filesystem::path path = std::filesystem::path{APOGEE_TEST_FIXTURES} / "web" / name;
    std::ifstream in{path, std::ios::binary};
    REQUIRE(in.good());
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

HttpUrl url(std::string_view text) {
    const std::optional<HttpUrl> parsed = parse_http_url(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

ReadablePage read(std::string_view html, std::string_view page = "https://example.test/a/b.html") {
    return extract_readable(html, url(page));
}

/// Every link target in `text` is an absolute http(s) URL.
void links_are_absolute(const std::string& text) {
    for (std::size_t at = text.find("]("); at != std::string::npos; at = text.find("](", at + 2)) {
        const std::string target = text.substr(at + 2, text.find(')', at) - at - 2);
        INFO(target);
        CHECK((target.starts_with("https://") || target.starts_with("http://")));
    }
}

/// `menu` is on the saved page, and nowhere in what the reader kept -- so its
/// absence means the reader dropped it, not that the page never had it.
void dropped(const std::string& html, const std::string& text, std::string_view menu) {
    INFO(menu);
    REQUIRE(html.find(menu) != std::string::npos);
    CHECK(text.find(menu) == std::string::npos);
}

bool valid_utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        const std::size_t length = lead < 0x80             ? 1
                                   : (lead & 0xE0) == 0xC0 ? 2
                                   : (lead & 0xF0) == 0xE0 ? 3
                                   : (lead & 0xF8) == 0xF0 ? 4
                                                           : 0;
        if (length == 0 || i + length > text.size()) {
            return false;
        }
        for (std::size_t k = 1; k < length; ++k) {
            if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) {
                return false;
            }
        }
        i += length;
    }
    return true;
}

std::string without_space(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c != ' ' && c != '\n' && c != '\t' && c != '\r') {
            out += c;
        }
    }
    return out;
}

}  // namespace

TEST_CASE("documentation: the module's own text, code and links, not the sidebar",
          "[agent][fetch][readable]") {
    const std::string html = fixture("python-json-docs.html");
    const ReadablePage page = read(html, "https://docs.python.org/3/library/json.html");
    CHECK(page.title == "json — JSON encoder and decoder — Python 3.14.7 documentation");
    const std::string& text = page.text;
    CHECK(text.starts_with("# `json` — JSON encoder and decoder"));
    CHECK(text.find("[RFC 7159](https://datatracker.ietf.org/doc/html/rfc7159.html)") !=
          std::string::npos);
    // A relative link, resolved against the page.
    CHECK(text.find("[`pickle`](https://docs.python.org/3/library/pickle.html)") !=
          std::string::npos);
    CHECK(text.find("```\n>>> import json\n>>> json.dumps(['foo'") != std::string::npos);
    CHECK(text.find("### Command-line options") != std::string::npos);
    // The footnotes, which Sphinx puts in <aside>: text, not a sidebar.
    CHECK(text.find("JSON permits literal U+2028 (LINE SEPARATOR)") != std::string::npos);
    // The heading's pilcrow link is dropped with it.
    CHECK(text.find("¶") == std::string::npos);
    for (const std::string_view menu :
         {"Table of Contents", "Previous topic", "Report a bug", "Show source", "Theme"}) {
        dropped(html, text, menu);
    }
    links_are_absolute(text);
}

TEST_CASE("an encyclopedia article: the article, its sections and references",
          "[agent][fetch][readable]") {
    const std::string html = fixture("wikipedia-metasearch.html");
    const ReadablePage page = read(html, "https://en.wikipedia.org/wiki/Metasearch_engine");
    CHECK(page.title == "Metasearch engine - Wikipedia");
    const std::string& text = page.text;
    CHECK(text.starts_with("# Metasearch engine"));
    CHECK(text.find("A metasearch engine (or [search aggregator]"
                    "(https://en.wikipedia.org/wiki/Search_aggregator)) is an online") !=
          std::string::npos);
    CHECK(text.find("## History") != std::string::npos);
    CHECK(text.find("## See also\n\n- [Federated search]") != std::string::npos);
    CHECK(text.find("## References") != std::string::npos);
    // The language menu inside <main>, the page tools, the edit links, the
    // navigation boxes and the categories: all furniture.
    for (const std::string_view menu :
         {"Jump to content", "Main menu", "Deutsch", "Edit links", "Toggle the table of contents",
          "Retrieved from", "Privacy policy", "Create account"}) {
        dropped(html, text, menu);
    }
    CHECK(text.find("[edit]") == std::string::npos);
    links_are_absolute(text);
}

TEST_CASE("a news story: the article, never the banners, ads, forms or teasers around it",
          "[agent][fetch][readable]") {
    const std::string html = fixture("news-article.html");
    const ReadablePage page = read(html, "https://valleyledger.example/news/local/solar");
    const std::string& text = page.text;
    CHECK(text.find("# City council approves riverside solar park after two-year review") !=
          std::string::npos);
    CHECK(text.find("By [Maria Okafor](https://valleyledger.example/staff/maria-okafor)") !=
          std::string::npos);
    CHECK(text.find("The city council voted 7–2 on Monday night") != std::string::npos);
    CHECK(text.find("[a heron colony](https://valley-wildlife.example/reports/heron-colony)") !=
          std::string::npos);
    CHECK(text.find("## What happens next") != std::string::npos);
    CHECK(text.find("- Capacity: 40 megawatts across 96,000 panels") != std::string::npos);
    CHECK(text.find("> “This is the largest single step") != std::string::npos);
    // The story's own header and footer are the story's: kept.
    CHECK(text.find("This article was updated on September 22") != std::string::npos);
    for (const std::string_view menu :
         {"Accept All Cookies", "Skip to main content", "Sign in", "Subscribe for $1",
          "Advertisement", "Share on X", "Get the Morning Ledger", "Most read", "Related stories",
          "Comments (14)", "Privacy Policy", "All rights reserved", "dataLayer"}) {
        dropped(html, text, menu);
    }
    links_are_absolute(text);
}

TEST_CASE("a GitHub release: the notes, the code and the assets, not the site around them",
          "[agent][fetch][readable]") {
    const std::string html = fixture("github-release.html");
    const ReadablePage page =
        read(html, "https://github.example/example-org/fastlib/releases/tag/v2.4.0");
    const std::string& text = page.text;
    CHECK(text.find("# fastlib v2.4.0") != std::string::npos);
    CHECK(text.find("## What's changed") != std::string::npos);
    CHECK(text.find("New `strict_keys` option") != std::string::npos);
    CHECK(text.find("([#1840](https://github.example/example-org/fastlib/pull/1840))") !=
          std::string::npos);
    CHECK(text.find("```\ndecoder = Decoder(max_depth=1024)\n```") != std::string::npos);
    CHECK(text.find("[Source code (zip)](https://github.example/example-org/fastlib/archive/refs/"
                    "tags/v2.4.0.zip)") != std::string::npos);
    for (const std::string_view menu :
         {"Skip to content", "Pricing", "Sign up", "Pull requests", "Releases: example-org/fastlib",
          "Terms", "GitHub,", "perform that action", "another tab or window"}) {
        dropped(html, text, menu);
    }
    links_are_absolute(text);
}

TEST_CASE("a page built by scripts has no readable text, and nothing of its scripts",
          "[agent][fetch][readable]") {
    const std::string html = fixture("script-app.html");
    const ReadablePage page = read(html);
    CHECK(page.title == "Dashboard · Acme Cloud");
    CHECK(page.text.empty());
}

TEST_CASE("the content landmark: main, a dominant article, else the body",
          "[agent][fetch][readable]") {
    const std::string filler(300, 'x');
    SECTION("role=main over the rest of the body") {
        const ReadablePage page = read("<body><div>menu words</div><div role=\"main\"><p>" +
                                       filler + "</p></div><div>footer words</div></body>");
        CHECK(page.text == filler);
    }
    SECTION("one dominant article over its teasers; a list of equals keeps them all") {
        const std::string teaser = "<article><p>teaser " + std::string(60, 't') + "</p></article>";
        CHECK(read("<body><article><p>" + filler + "</p></article>" + teaser + teaser + "</body>")
                  .text == filler);
        const std::string listing =
            read("<body><h1>Index</h1>" + teaser + teaser + teaser + "</body>").text;
        CHECK(listing.find("# Index") != std::string::npos);
        CHECK(listing.find("teaser") != std::string::npos);
    }
    SECTION("a page's header and footer go; a section's stay") {
        const std::string text =
            read("<body><header>site banner</header><div><h1>Story</h1><p>" + filler +
                 "</p><section><header>section header</header><p>body</p><footer>section "
                 "footnote</footer></section></div><footer>site footer</footer></body>")
                .text;
        CHECK(text.find("# Story") != std::string::npos);
        CHECK(text.find("section header") != std::string::npos);
        CHECK(text.find("section footnote") != std::string::npos);
        CHECK(text.find("site banner") == std::string::npos);
        CHECK(text.find("site footer") == std::string::npos);
    }
    SECTION("hidden things stay hidden") {
        const std::string text =
            read(
                "<p>seen</p><p hidden>a</p><p aria-hidden=\"true\">b</p><p style=\"display: "
                "none\">c</p><div role=\"navigation\">d</div><nav>e</nav><aside>f</aside>")
                .text;
        CHECK(text == "seen");
    }
    SECTION("a name that looks like furniture never drops the content itself") {
        const std::string text = read("<body><div class=\"page-with-sidebar\"><h1>Title</h1><p>" +
                                      filler + "</p><div class=\"sidebar\">side</div></div></body>")
                                     .text;
        CHECK(text.find("# Title") != std::string::npos);
        CHECK(text.find(filler) != std::string::npos);
        CHECK(text.find("side") == std::string::npos);
    }
    SECTION("a form that wraps the whole page is the page") {
        const std::string text = read("<body><form id=\"aspnetForm\"><h1>Title</h1><p>" + filler +
                                      "</p></form><form><input>search</form></body>")
                                     .text;
        CHECK(text.find(filler) != std::string::npos);
        CHECK(text.find("search") == std::string::npos);  // a search box is not the page
    }
}

TEST_CASE("markup becomes light Markdown", "[agent][fetch][readable]") {
    SECTION("headings, lists and their nesting") {
        CHECK(read("<h2>Two</h2><h3>Three</h3>").text == "## Two\n\n### Three");
        CHECK(read("<ol><li>one<li>two<ul><li>inner</li></ul></ol>").text ==
              "1. one\n2. two\n  - inner");
        CHECK(read("<ul><li><p>para</p><p>more</p></li><li>next</li></ul>").text ==
              "- para\nmore\n- next");
        CHECK(read("<ol><li><div><p>deep</p></div></li></ol>").text == "1. deep");
    }
    SECTION("code: fenced blocks keep their whitespace; inline code is backticked") {
        CHECK(read("<pre>  a  <b>b</b>\n    c&lt;d</pre>").text == "```\n  a  b\n    c<d\n```");
        CHECK(read("<p>call <code>f(x)</code> now</p>").text == "call `f(x)` now");
    }
    SECTION("tables: rows with a header rule; a layout table as paragraphs") {
        CHECK(read("<table><tr><th>k</th><th>v</th></tr><tr><td>a|b</td><td>1</td></tr></table>")
                  .text == "| k | v |\n| --- | --- |\n| a\\|b | 1 |");
        const std::string long_cell(400, 'w');
        CHECK(read("<table><tr><td><p>" + long_cell + "</p></td><td>side</td></tr></table>").text ==
              long_cell + "\n\nside");
    }
    SECTION("links: absolute, dot segments resolved, in-page and script links as words") {
        const std::string text =
            read(
                "<p><a href=\"../c/d.html?x=1#frag\">up</a> <a href=\"#top\">top</a> "
                "<a href=\"javascript:void(0)\">js</a> <a href=\"mailto:a@b.example\">mail</a> "
                "<a href=\"#s\">¶</a> <a href=\"/img\"><img alt=\"logo\"></a> "
                "<a href=\"//cdn.example/x\">cdn</a> <a href=\"x\"></a></p>",
                "https://example.test/a/b/page.html")
                .text;
        CHECK(text ==
              "[up](https://example.test/a/c/d.html?x=1) top js mail "
              "[logo](https://example.test/img) [cdn](https://cdn.example/x)");
    }
    SECTION("<base href> moves where relative links point") {
        CHECK(read("<head><base href=\"https://docs.example/v2/\"></head><body><a "
                   "href=\"guide.html\">g</a></body>")
                  .text == "[g](https://docs.example/v2/guide.html)");
    }
    SECTION("blockquotes, definition lists, breaks") {
        CHECK(read("<blockquote><p>a</p><p>b</p></blockquote>").text == "> a\n>\n> b");
        CHECK(read("<dl><dt>term</dt><dd>meaning</dd></dl>").text == "term\nmeaning");
        CHECK(read("line one<br>line two").text == "line one\nline two");
    }
    SECTION("broken markup still reads") {
        CHECK(read("<p>one<p>two<div>three").text == "one\n\ntwo\n\nthree");
        CHECK(read("<ul><li>a<li>b</ul>").text == "- a\n- b");
        CHECK(read("x < y</p></span></div>z").text == "x < yz");  // stray ends close nothing
        CHECK(read("<!-- hidden --><p>shown</p><![CDATA[x]]><?php echo 1 ?>").text == "shown");
        CHECK(read("<p title='x > y'>quoted</p>").text == "quoted");
    }
    SECTION("a page nested past the depth limit is read, not overflowed") {
        std::string deep;
        for (int i = 0; i < 20000; ++i) {
            deep += "<div>";
        }
        deep += "bottom";
        CHECK(read(deep).text == "bottom");
    }
}

TEST_CASE("characters: entities, charsets and stray bytes all end as UTF-8",
          "[agent][fetch][readable]") {
    CHECK(read("&amp; &lt;&gt; &quot;&apos; caf&eacute; &#233; &#xE9; &#x1F600; &#8212; "
               "&mdash; &unknown; &#0; &#xD800; &#150; &shy;x")
              .text == "& <> \"' café é é 😀 — — &unknown; � � – x");
    CHECK(apogee::agent::as_utf8("caf\xE9", "iso-8859-1") == "café");
    CHECK(apogee::agent::as_utf8("\x93quoted\x94", "windows-1252") == "“quoted”");
    CHECK(apogee::agent::as_utf8("ok\xFFok\xC3", "") == "ok�ok�");
    CHECK(apogee::agent::as_utf8("\xC0\xAF", "utf-8") == "��");  // an overlong '/'
    CHECK(apogee::agent::as_utf8("é€😀", "utf-8") == "é€😀");
    CHECK(apogee::agent::charset_of("text/html; charset=\"ISO-8859-1\"") == "iso-8859-1");
    CHECK(apogee::agent::charset_of("text/html").empty());
    CHECK(apogee::agent::meta_charset("<head><meta charset=\"windows-1252\">") == "windows-1252");
    CHECK(apogee::agent::meta_charset(
              "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=iso-8859-1\">") ==
          "iso-8859-1");
    CHECK(apogee::agent::meta_charset("<p>no meta</p>").empty());
}

TEST_CASE("paging walks the whole text with no gap, no overlap, and never inside a character",
          "[agent][fetch][readable][paging]") {
    const auto walk = [](const std::string& text, std::size_t limit) {
        std::string joined;
        std::size_t offset = 0;
        std::size_t pages = 0;
        std::size_t count = 0;
        do {
            const apogee::agent::TextPage page = page_of(text, offset, limit);
            REQUIRE(page.count > 0);
            ++pages;
            INFO("limit " << limit << ", page " << pages);
            CHECK(page.number == pages);
            CHECK(page.text.size() <= limit);
            CHECK(valid_utf8(page.text));
            count = page.count;
            joined += page.text;
            REQUIRE((page.next == 0 || page.next > offset));
            offset = page.next;
        } while (offset != 0);
        CHECK(pages == count);
        CHECK(without_space(joined) == without_space(text));
    };
    SECTION("paragraphs, cut between them") {
        std::string text;
        for (int i = 0; i < 40; ++i) {
            text += "Paragraph " + std::to_string(i) + " " + std::string(90, 'p') + ".\n\n";
        }
        walk(text, 500);
        const apogee::agent::TextPage first = page_of(text, 0, 500);
        CHECK(first.text.ends_with("."));  // a whole paragraph, not half of one
    }
    SECTION("multi-byte text with no break anywhere, at every limit") {
        std::string text;
        while (text.size() < 300) {
            text += "é€😀";
        }
        for (std::size_t limit = 4; limit < 60; ++limit) {
            walk(text, limit);
        }
    }
    SECTION("the edges") {
        CHECK(page_of("short", 0, 100).text == "short");
        CHECK(page_of("short", 0, 100).count == 1);
        CHECK(page_of("short", 0, 100).next == 0);
        CHECK(page_of("short", 99, 100).count == 0);  // past the end
        CHECK(page_of("", 0, 100).count == 1);        // nothing to read, from the start
        const std::string text = "aaaa\n\nbbbb\n\ncccc";
        // An offset inside a page starts there, counted from where it lands.
        const apogee::agent::TextPage mid = page_of(text, 7, 5);
        CHECK(mid.text == "bbb");
    }
}
