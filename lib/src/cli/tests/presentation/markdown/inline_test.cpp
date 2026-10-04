#include "markdown/inline.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

/// Inline Markdown against CommonMark's rules where models lean on them.
namespace {

using apogee::markdown::InlineOptions;
using apogee::markdown::render_inline;
using apogee::markdown::Span;

/// A span list as text with its look tagged: `[b:bold]`, `[i:it]`,
/// `[c:code]` (the code colour), `[s:gone]`, `[u:link]`, `[d:dim]`, and
/// combinations (`[bi:both]`).
[[nodiscard]] std::string tagged(const std::vector<Span>& spans) {
    std::string out;
    for (const Span& span : spans) {
        std::string tags;
        tags += span.attributes.bold ? "b" : "";
        tags += span.attributes.italic ? "i" : "";
        tags += span.attributes.strike ? "s" : "";
        tags += span.attributes.underline ? "u" : "";
        tags += span.attributes.dim ? "d" : "";
        tags += span.attributes.color == apogee::markdown::kCodeColor ? "c" : "";
        out += tags.empty() ? span.text : "[" + tags + ":" + span.text + "]";
    }
    return out;
}

[[nodiscard]] std::string tagged(std::string_view text, bool hyperlinks = false) {
    return tagged(render_inline(text, InlineOptions{.hyperlinks = hyperlinks}));
}

}  // namespace

TEST_CASE("bold, italic, strikethrough and code render without their markers",
          "[markdown][inline]") {
    CHECK(tagged("**bold** and *it* and ~~gone~~ and `x`") ==
          "[b:bold] and [i:it] and [s:gone] and [c:x]");
    CHECK(tagged("__bold__ and _it_") == "[b:bold] and [i:it]");
    CHECK(tagged("***both***") == "[bi:both]");
    CHECK(tagged("**bold _and italic_**") == "[b:bold ][bi:and italic]");
}

TEST_CASE("text that only looks like emphasis stays as written", "[markdown][inline]") {
    // The two that appear in answers constantly, and that a naive pairing of
    // asterisks gets wrong.
    CHECK(tagged("snake_case_name and __init__ style") == "snake_case_name and [b:init] style");
    CHECK(tagged("2 * 3 * 4 = 24") == "2 * 3 * 4 = 24");
    CHECK(tagged("~5 minutes") == "~5 minutes");
    CHECK(tagged("a * b") == "a * b");
    // Inside a word, an asterisk may emphasise and an underscore may not.
    CHECK(tagged("un*frigging*believable") == "un[i:frigging]believable");
    CHECK(tagged("un_frigging_believable") == "un_frigging_believable");
}

TEST_CASE("an unclosed marker is shown as written, as a streaming line has it",
          "[markdown][inline]") {
    CHECK(tagged("**bo") == "**bo");
    CHECK(tagged("`code") == "`code");
    CHECK(tagged("[link](https://") == "[link](https://");
    CHECK(tagged("**bold** and **more") == "[b:bold] and **more");
}

TEST_CASE("escapes and code spans are literal", "[markdown][inline]") {
    CHECK(tagged("\\*not\\* emphasis") == "*not* emphasis");
    CHECK(tagged("`**not bold**`") == "[c:**not bold**]");
    CHECK(tagged("`` a`b ``") == "[c:a`b]");
    CHECK(tagged("```x```") == "[c:x]");
}

TEST_CASE("links show their text, and their address where the terminal has no links",
          "[markdown][inline]") {
    CHECK(tagged("see [the docs](https://x.test/docs) now") ==
          "see [u:the docs][d: (https://x.test/docs)] now");
    // Where the terminal makes links clickable, the address rides the span.
    const std::vector<Span> linked =
        render_inline("[the docs](https://x.test/docs)", InlineOptions{.hyperlinks = true});
    REQUIRE(linked.size() == 1);
    CHECK(linked.front().text == "the docs");
    CHECK(linked.front().link == "https://x.test/docs");
    // Text that already is the address is not repeated.
    CHECK(tagged("[https://x.test](https://x.test)") == "[u:https://x.test]");
    CHECK(tagged("![a diagram](d.png)") == "[d:[image: a diagram]]");
}

TEST_CASE("bare and angle-bracket addresses are links without their punctuation",
          "[markdown][inline]") {
    CHECK(tagged("Visit https://x.test/a.") == "Visit [u:https://x.test/a].");
    CHECK(tagged("(see https://x.test/a)") == "(see [u:https://x.test/a])");
    CHECK(tagged("at <https://x.test>") == "at [u:https://x.test]");
    CHECK(tagged("washington.www.gov") == "washington.www.gov");
}

TEST_CASE("the base look applies to every span", "[markdown][inline]") {
    InlineOptions options;
    options.base.bold = true;
    const std::vector<Span> spans = render_inline("Title with `code`", options);
    for (const Span& span : spans) {
        CHECK(span.attributes.bold);
    }
    CHECK(spans.back().attributes.color == apogee::markdown::kCodeColor);
}
