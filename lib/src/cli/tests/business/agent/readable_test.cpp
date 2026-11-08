#include "agent/readable.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

/// The page reader (25f): the fixture corpus above all -- each page's own
/// text present, its furniture absent -- then the parser's edge cases, the
/// characters, a hostile page's cost (57v), and paging.
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
        // Teasers long enough to be chosen, none holding half the articles'
        // text: a listing, not a story, so every one is kept.
        const std::string entry = "<article><p>entry " + std::string(250, 'e') + "</p></article>";
        const std::string listing =
            read("<body><h1>Index</h1>" + entry + entry + entry + "</body>").text;
        CHECK(listing.find("# Index") != std::string::npos);
        std::size_t entries = 0;
        for (std::size_t at = listing.find("entry "); at != std::string::npos;
             at = listing.find("entry ", at + 1)) {
            ++entries;
        }
        CHECK(entries == 3);
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
        CHECK(read("<ul><li><h3>A heading item</h3></li></ul>").text == "- ### A heading item");
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
        // A block closes an open paragraph, as a browser does: the div here
        // is not inside the hidden paragraph, so it is seen.
        CHECK(read("<p hidden>gone<div>shown</div>").text == "shown");
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

namespace {

/// `fetch_url`'s body cap, `kFetchMaxBodyBytes` (presentation/cli/helpers.h):
/// the most a hostile page can hand the reader in one fetch.
constexpr std::size_t kMiB = std::size_t{1024} * 1024;
constexpr std::size_t kFetchCap = 5 * kMiB;

/// `unit` repeated until it fills at least `bytes`.
std::string filled(std::string_view unit, std::size_t bytes) {
    std::string out;
    out.reserve(bytes + unit.size());
    while (out.size() < bytes) {
        out += unit;
    }
    return out;
}

/// `html` read, failing the test when the read takes `ceiling` or longer.
/// The ceilings are wide (57v): the old reader missed each by minutes, so a
/// slow CI row never flakes them and a super-linear path never passes them.
ReadablePage read_within(const std::string& html, std::chrono::milliseconds ceiling) {
    const auto started = std::chrono::steady_clock::now();
    ReadablePage page = read(html);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    INFO(html.size() << " bytes read in " << elapsed.count() << " ms");
    CHECK(elapsed < ceiling);
    return page;
}

constexpr std::chrono::milliseconds kCeiling{2000};
constexpr std::chrono::milliseconds kCapCeiling{5000};

}  // namespace

TEST_CASE("a hostile page reads in time linear in its size, its text as before (57v)",
          "[agent][fetch][readable][hostile]") {
    SECTION("nested tables: a cell that holds a table is read once, not twice per level") {
        std::string deep;
        for (int i = 0; i < 25; ++i) {
            deep += "<table><tr><td>x";
        }
        deep += "inner";
        std::string expected;
        for (int i = 0; i < 24; ++i) {
            expected += "x\n";
        }
        expected += "xinner";
        CHECK(read_within(deep, kCeiling).text == expected);

        // Old-style layout: ten two-cell tables around a page of posts.
        std::string posts;
        for (int i = 0; posts.size() < std::size_t{229} * 1024; ++i) {
            posts += "<p>post " + std::to_string(i) + " " + std::string(80, 'w') + "</p>";
        }
        const auto wrapped = [](std::string page) {
            for (int i = 0; i < 10; ++i) {
                page = "<table><tr><td>" + page + "</td><td>side " + std::to_string(i) +
                       "</td></tr></table>";
            }
            return page;
        };
        const std::string layout = read_within(wrapped(posts), kCeiling).text;
        CHECK(layout.starts_with("post 0 "));
        CHECK(layout.ends_with("side 9"));

        // At the fetch cap: as deep as the parser keeps, and as much text.
        std::string capped_posts;
        for (int i = 0; capped_posts.size() < kFetchCap; ++i) {
            capped_posts += "<p>post " + std::to_string(i) + " " + std::string(80, 'w') + "</p>";
        }
        CHECK(read_within(wrapped(capped_posts), kCapCeiling).text.ends_with("side 9"));
        const std::string capped =
            read_within(filled("<table><tr><td>x", kFetchCap) + "inner", kCapCeiling).text;
        CHECK(capped.starts_with("x\nx\n"));
        CHECK(capped.ends_with("xinner"));
    }
    SECTION("bare ampersands: each `&` looks no further than an entity can be long") {
        const std::string amps(1000000, '&');
        CHECK(read_within(amps, kCeiling).text == amps);
        CHECK(read_within("<p><a href=\"/x\"><img alt=\"" + amps + "\"></a></p>", kCeiling).text ==
              "[" + amps + "](https://example.test/x)");
        CHECK(read_within("<title>" + amps + "</title><p>body</p>", kCeiling).title == amps);

        const std::string capped(kFetchCap, '&');
        CHECK(read_within(capped, kCapCeiling).text == capped);
        CHECK(read_within("<p><a href=\"/x\"><img alt=\"" + capped + "\"></a></p>", kCapCeiling)
                  .text.size() == capped.size() + std::string{"[](https://example.test/x)"}.size());
        CHECK(read_within("<title>" + capped + "</title><p>body</p>", kCapCeiling).title == capped);
    }
    SECTION("nested empty links: an image link's alt text is found without a scan") {
        std::string links;
        for (int i = 0; i < 250; ++i) {
            links += "<a href=x>";
        }
        CHECK(read_within(links + filled("<i></i>", 2 * kMiB), kCeiling).text.empty());
        CHECK(
            read_within(links + filled("<i></i>", kFetchCap) + "<img alt=pic>", kCapCeiling).text ==
            "[pic](https://example.test/a/x)");

        // Where links may still nest -- a link inside a caption, or a table
        // cell, inside a link -- every level finds its image at once.
        std::string captioned;
        for (int i = 0; i < 125; ++i) {
            captioned += "<a href=x><caption>";
        }
        std::string celled;
        for (int i = 0; i < 60; ++i) {
            celled += "<a href=x><table><tr><td>";
        }
        // Through a block, which a link start looks back no further than;
        // and through phrasing, which each link start closes and opens again.
        std::string blocked;
        std::string spanned;
        for (int i = 0; i < 250; ++i) {
            blocked += "<a href=x><div>";
            spanned += "<a href=x><span>";
        }
        for (const std::string& nested : {captioned, celled, blocked, spanned}) {
            CHECK(read_within(nested + filled("<i></i>", 2 * kMiB), kCeiling).text.empty());
            CHECK(read_within(nested + filled("<i></i>", kFetchCap), kCapCeiling).text.empty());
        }
    }
    SECTION("a link start finds the open link in reach without looking back") {
        // Every start tag under 254 open elements (one short of the cap, so
        // the links open): a look back through them for an open link, at
        // each of a million starts, is depth x size.
        const std::string open_spans = filled("<span>", std::size_t{254} * 6);
        CHECK(read_within(open_spans + filled("<a/>", 2 * kMiB), kCeiling).text.empty());
        CHECK(read_within(open_spans + filled("<a/>", kFetchCap), kCapCeiling).text.empty());
        CHECK(read_within(open_spans + filled("<a href=x>w</a>", kFetchCap), kCapCeiling)
                  .text.starts_with("[w](https://example.test/a/x)"));
    }
}

TEST_CASE("what the cost fixes leave as it was (57v)", "[agent][fetch][readable]") {
    SECTION("tables: data as rows, a table in a cell and a single column as paragraphs") {
        CHECK(read("<table><tr><td>outer<table><tr><td>a</td><td>b</td></tr></table></td>"
                   "<td>side</td></tr></table>")
                  .text == "outer\n\n| a | b |\n\nside");
        CHECK(read("<table><tr><td>one</td></tr><tr><td>two</td></tr></table>").text == "one\ntwo");
        // A table anywhere below a cell, not only directly in it.
        CHECK(read("<table><tr><td><div><p>x</p><table><tr><td>a</td><td>b</td></tr></table></div>"
                   "</td><td>side</td></tr></table>")
                  .text == "x\n\n| a | b |\n\nside");
        CHECK(read("<table><caption>cap</caption><tr><td>a</td><td>b</td></tr></table>").text ==
              "cap\n\n| a | b |");
        CHECK(read("<table><caption>cap</caption><tr><td>a<table><tr><td>c</td></tr></table></td>"
                   "<td>b</td></tr></table>")
                  .text == "cap\na\nc\nb");
        // A layout table decided by a long cell in one row and a table in another.
        CHECK(read("<table><tr><td>a</td><td>b</td></tr><tr><td>" + std::string(400, 'w') +
                   "</td><td>c</td></tr><tr><td>d<table><tr><td>e</td><td>f</td></tr></table>"
                   "</td></tr></table>")
                  .text == "ab\n" + std::string(400, 'w') + "c\nd\n\n| e | f |");
    }
    SECTION("an image link reads as its first image's alt text, however deep") {
        CHECK(read("<p><a href=/x><span><b><img alt=deep></b></span></a></p>").text ==
              "[deep](https://example.test/x)");
        CHECK(read("<p><a href=/x><img alt=\"\"><img alt=second></a></p>").text.empty());
        CHECK(read("<p><a href=/x><span>  </span><b><i><img alt=\"first\"></i></b>"
                   "<img alt=second></a></p>")
                  .text == "[first](https://example.test/x)");
    }
    SECTION("an entity is looked for in the 12 bytes after its `&`, and no further") {
        CHECK(read("<p>&#x0000000E9; &#x00000000E9; AT&T &amp &abcdefghijk; &abcdefghijkl;</p>")
                  .text == "é &#x00000000E9; AT&T &amp &abcdefghijk; &abcdefghijkl;");
    }
    SECTION("a link start tag closes an open link; one in a table cell stays in its cell") {
        CHECK(read("<p><a href=a>one<a href=b>two</a></p>").text ==
              "[one](https://example.test/a/a) [two](https://example.test/a/b)");
        CHECK(read("<p><a href=a>one <b>bold<a href=b>two</a> after</b></p>").text ==
              "[one bold](https://example.test/a/a) [two](https://example.test/a/b) after");
        CHECK(read("<p><a href=a>outer<table><tr><td><a href=b>cell</a></td><td>2</td></tr>"
                   "</table></a></p>")
                  .text ==
              "[outer | (cell)(https://example.test/a/b) | 2 |](https://example.test/a/a)");
    }
    SECTION("a link across a block nests as before, and a split link keeps the story whole") {
        std::string story;
        for (int i = 0; i < 6; ++i) {
            story += "The opening paragraph of the story says a good deal. ";
        }
        const std::string opening =
            "<nav><a href=\"/\">Home</a></nav><div role=\"main\">"
            "<h1>The headline</h1><p>" +
            story + "</p>";
        const std::string rest =
            "<p>The rest of the story.</p><p>And its last paragraph.</p></div>";
        const std::string comments = "<div><p>Comments and other site text.</p></div>";
        const std::string read_opening = "# The headline\n\n" + story.substr(0, story.size() - 1);
        const std::string read_rest = "The rest of the story.\n\nAnd its last paragraph.";

        // A link over a block holding a link: the inner start tag looks back
        // no further than the block, so the block stays open and the
        // story's end stays in the story. (Closing back through the block,
        // the block's own end tag closed the story's element instead.)
        CHECK(read(opening +
                   "<a href=\"/related\"><div class=\"teaser\">Related: <a href=\"/topic\">Topic"
                   "</a></div></a>" +
                   rest + comments)
                  .text == read_opening +
                               "\n\n[Related: (Topic)(https://example.test/topic)]"
                               "(https://example.test/related)\n\n" +
                               read_rest);
        // The same in an article: its body stays in it, the site's text out.
        std::string body;
        for (int i = 0; i < 8; ++i) {
            body += "The long body of the story, with many words in it. ";
        }
        CHECK(read("<nav><a href=\"/\">Home</a> <a href=\"/news\">News</a></nav>"
                   "<div class=\"content\"><article><h1>The headline</h1><a href=\"/post/1\" "
                   "class=\"card\"><div class=\"card-body\"><h2>Related story</h2><p>By <a "
                   "href=\"/author/x\">Author X</a></p></div></a><p>" +
                   body +
                   "</p><p>Its second paragraph.</p></article><div class=\"footer-links\"><p>"
                   "Other site text.</p></div></div>")
                  .text ==
              "# The headline\n\n[## Related story By (Author X)"
              "(https://example.test/author/x)](https://example.test/post/1)\n\n" +
                  body.substr(0, body.size() - 1) + "\n\nIts second paragraph.");
        // Two links nested in a card link's paragraph split; the second end
        // tag is the first link's own and closes nothing, not the card link
        // and the blocks in it.
        CHECK(read(opening +
                   "<a href=\"/card\"><div class=\"body\"><h2>Card</h2><p>Blurb <a href=\"/x\">"
                   "one<a href=\"/y\">two</a></a> after.</p></div></a>" +
                   rest + comments)
                  .text == read_opening +
                               "\n\n[## Card Blurb (one)(https://example.test/x) "
                               "(two)(https://example.test/y) after.](https://example.test/card)"
                               "\n\n" +
                               read_rest);
        // A link split through a `span` opens the span again, so its end tag
        // closes that span and not the one around the story.
        CHECK(read("<span class=\"wrap\">" + opening +
                   "<p><a href=\"/x\"><span>one <a href=\"/y\">two</a> three</span> four</a></p>" +
                   rest + "</span>" + comments)
                  .text == read_opening +
                               "\n\n[one](https://example.test/x) [two](https://example.test/y) "
                               "three four\n\n" +
                               read_rest);
    }
    SECTION("an owed end tag never keeps a later link open over the story") {
        std::string story;
        for (int i = 0; i < 6; ++i) {
            story += "The opening paragraph of the story says a good deal. ";
        }
        const std::string main_open = "<div role=\"main\"><h1>The headline</h1>";
        const std::string rest =
            "<p>The rest of the story.</p><p>And its last paragraph.</p></div>"
            "<div><p>Comments and other site text.</p></div>";
        const std::string read_story = story.substr(0, story.size() - 1);
        const std::string read_rest = "The rest of the story.\n\nAnd its last paragraph.";

        // A link left open in the nav, split by the next one, owes an end tag
        // that never comes. A later promo link over an unclosed paragraph
        // is a link opened after it: its `</a>` closes it, the promo is
        // dropped as furniture, and the story's end is read. (Taken as the
        // owed one, it closed nothing, and the promo held the story's end.)
        CHECK(read("<nav><a href=\"/\">Home <a href=\"/news\">News</a></nav>" + main_open + "<p>" +
                   story + "</p><a href=\"/subscribe\" class=\"promo-box\"><p>Subscribe today</a>" +
                   rest)
                  .text == "# The headline\n\n" + read_story + "\n\n" + read_rest);
        // The same where the split link's end tag is still owed: left open
        // in the story's own element, which is open still.
        CHECK(read("<nav><a href=\"/\">Home</a></nav>" + main_open + "<p>" + story +
                   "</p>See <a href=\"/one\">one <a href=\"/two\">two</a> here. <a "
                   "href=\"/subscribe\" class=\"promo-box\"><p>Subscribe today</a>" +
                   rest)
                  .text == "# The headline\n\n" + read_story +
                               "\n\nSee [one](https://example.test/one) "
                               "[two](https://example.test/two) here.\n\n" +
                               read_rest);
        // The same in the story's own intro: the card's text is its own.
        CHECK(read("<nav><a href=\"/\">Home</a></nav>" + main_open +
                   "<p>Intro with a sloppy <a href=\"/one\">first link <a href=\"/two\">second "
                   "link</a> and words after.</p><p>" +
                   story + "</p><a href=\"/card\"><p>Card teaser text</a>" + rest)
                  .text ==
              "# The headline\n\nIntro with a sloppy [first link](https://example.test/one) "
              "[second link](https://example.test/two) and words after.\n\n" +
                  read_story + "\n\n[Card teaser text](https://example.test/card)\n\n" + read_rest);
        // A card link whose paragraph splits two links: the end tag is owed
        // only while that paragraph is open, so the card's own `</a>`, after
        // a later unclosed paragraph, closes the card.
        const std::string card_open = "<nav><a href=\"/\">Home</a></nav>" + main_open + "<p>" +
                                      story + "</p><a href=\"/card\"";
        const std::string card_body =
            R"(><p>Blurb <a href="/x">one<a href="/y">two</a></p><p>Teaser</a>)";
        CHECK(read(card_open + " class=\"promo-card\"" + card_body + rest).text ==
              "# The headline\n\n" + read_story + "\n\n" + read_rest);
        CHECK(read(card_open + card_body + rest).text ==
              "# The headline\n\n" + read_story +
                  "\n\n[Blurb (one)(https://example.test/x) (two)(https://example.test/y) "
                  "Teaser](https://example.test/card)\n\n" +
                  read_rest);
    }
    SECTION("<base href=\"#x\"> is the page itself: its links stay where they were") {
        CHECK(read("<head><base href=\"#x\"></head><body><a href=\"c.html\">c</a> "
                   "<a href=\"?page=2\">next</a></body>")
                  .text ==
              "[c](https://example.test/a/c.html) [next](https://example.test/a/b.html?page=2)");
    }
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
    SECTION("a smaller first page, then larger ones, still covering it all") {
        std::string text;
        for (int i = 0; i < 60; ++i) {
            text += "Line " + std::to_string(i) + " " + std::string(40, 'l') + ".\n";
        }
        const apogee::agent::TextPage first = page_of(text, 0, 800, 300);
        CHECK(first.text.size() <= 300);
        CHECK(first.text.size() > 150);
        const apogee::agent::TextPage second = page_of(text, first.next, 800, 300);
        CHECK(second.text.size() > 300);
        CHECK(second.text.size() <= 800);
        CHECK(second.number == 2);
        CHECK(second.count == first.count);
        std::string joined;
        for (std::size_t offset = 0;;) {
            const apogee::agent::TextPage page = page_of(text, offset, 800, 300);
            joined += page.text;
            if (page.next == 0) {
                break;
            }
            offset = page.next;
        }
        CHECK(without_space(joined) == without_space(text));
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
