#include "backends/markup_filter.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

#include "backends/model_profile.h"

/// The control-token header filter, against real observed output.
///
/// The fixtures are not invented. They are what gpt-oss-20b (MXFP4) actually
/// returned on this machine on 2026-09-07 -- every one of those characters was
/// reaching the user as the answer before this filter existed.
namespace {

using apogee::backends::HeaderMarker;
using apogee::backends::MarkupFilter;
using apogee::backends::strip_markup_headers;

/// The two header shapes gpt-oss emits: one that closes, one that does not.
[[nodiscard]] std::vector<HeaderMarker> gpt_oss_headers() {
    return {{.open = "<|channel|>", .close = "<|message|>"}, {.open = "<|start|>", .close = ""}};
}

/// Feeds `text` in `chunk`-byte pieces and returns what a surface would show.
[[nodiscard]] std::string filtered(std::string_view text, std::size_t chunk,
                                   const std::vector<HeaderMarker>& headers) {
    MarkupFilter filter{headers};
    std::string out;
    for (std::size_t offset = 0; offset < text.size(); offset += chunk) {
        out += filter.write(text.substr(offset, chunk));
    }
    out += filter.flush();
    return out;
}

}  // namespace

TEST_CASE("the observed gpt-oss framing is removed", "[backends][markup]") {
    // Verbatim from a live run. The reasoning block itself belongs to
    // ThinkFilter -- this asserts only that the FRAMING goes.
    constexpr std::string_view kReply = "<|start|>assistant<|channel|>final<|message|>4";
    CHECK(strip_markup_headers(kReply, gpt_oss_headers()) == "4");
}

TEST_CASE("a header that does not close is bounded by its identifier", "[backends][markup]") {
    // The rule that keeps an unterminated header costing one word rather than a
    // paragraph: models emit the close inconsistently, so the identifier run is
    // what actually bounds a header.
    CHECK(strip_markup_headers("<|start|>assistant here is the answer", gpt_oss_headers()) ==
          " here is the answer");
}

TEST_CASE("the result is identical at every chunk size", "[backends][markup]") {
    // gpt-oss tokenizes `commentary` as `comment` + `ary`, so a marker split
    // across reads is the normal case rather than an edge one.
    constexpr std::string_view kReply =
        "<|start|>assistant<|channel|>final<|message|>Paris is the capital.";
    const std::string whole = filtered(kReply, kReply.size(), gpt_oss_headers());
    REQUIRE(whole == "Paris is the capital.");

    for (const std::size_t chunk : {std::size_t{1}, std::size_t{2}, std::size_t{3}, std::size_t{5},
                                    std::size_t{7}, std::size_t{11}, std::size_t{64}}) {
        INFO("chunk: " << chunk);
        CHECK(filtered(kReply, chunk, gpt_oss_headers()) == whole);
    }
}

TEST_CASE("an empty header list is a pass-through", "[backends][markup]") {
    // An unprofiled model pays nothing, and -- the point -- has nothing deleted
    // from its answer on a guess.
    CHECK(filtered("<|channel|>final<|message|>text", 3, {}) == "<|channel|>final<|message|>text");
}

TEST_CASE("a partial marker at end of stream was never a marker", "[backends][markup]") {
    // A model that legitimately ends a sentence with `<` must keep it.
    MarkupFilter filter{gpt_oss_headers()};
    std::string out = filter.write("2 < 3, and 4 <");
    out += filter.flush();
    CHECK(out == "2 < 3, and 4 <");
}

TEST_CASE("text that merely resembles a marker is untouched", "[backends][markup]") {
    CHECK(strip_markup_headers("use <|start of file|> as a delimiter", gpt_oss_headers()) ==
          "use <|start of file|> as a delimiter");
}

TEST_CASE("a stray close with no open before it is dropped", "[backends][markup]") {
    // Models emit these when the open token is dropped. Framing either way.
    CHECK(strip_markup_headers("<|message|>Paris", gpt_oss_headers()) == "Paris");
}

TEST_CASE("several headers in one reply are all removed", "[backends][markup]") {
    CHECK(strip_markup_headers(
              "<|start|>assistant<|channel|>final<|message|>a<|start|>assistant<|channel|>"
              "final<|message|>b",
              gpt_oss_headers()) == "ab");
}

TEST_CASE("a reply that starts with a blank line keeps it", "[backends][markup]") {
    // The whitespace skipped past an unclosed header's identifier is given
    // back: a Markdown answer opening on a blank line is not framing.
    CHECK(strip_markup_headers("<|start|>assistant\n\n- a bullet", gpt_oss_headers()) ==
          "\n\n- a bullet");
}

namespace {

using apogee::backends::TurnMarkerFilter;

/// `stream` through a base model's filter in `pieces`, as a stream arrives:
/// what came out, and whether a marker ended it.
std::pair<std::string, bool> through(const std::vector<std::string>& pieces) {
    TurnMarkerFilter filter{apogee::backends::base_turn_markers()};
    std::string out;
    bool stopped = false;
    for (const std::string& piece : pieces) {
        out += filter.write(piece, stopped);
        if (stopped) {
            break;
        }
    }
    if (!stopped) {
        out += filter.flush();
    }
    return {out, stopped};
}

}  // namespace

TEST_CASE("a base model's spilled turn markers end its reply, whole, chained or cut short",
          "[backends][markup][base]") {
    // The motivating session's byte patterns (Gemma 4 E4B, 2026-10-03).
    CHECK(through({"It is 67°F in Lehi.<|end|"}) ==
          std::pair<std::string, bool>{"It is 67°F in Lehi.", false});
    CHECK(through({"Bitcoin is at $67,200.<|end|><|im|"}).first == "Bitcoin is at $67,200.");
    CHECK(through({"Bitcoin is at $67,200.<|end|><|im|"}).second);
    // Each family, whole: the reply is over, whatever it invents next.
    CHECK(through({"Paris.<|im_end|>\n<|im_start|>user\nand Spain?"}).first == "Paris.");
    CHECK(through({"Paris.<|eot_id|>assistant"}).first == "Paris.");
    CHECK(through({"Paris.<|endoftext|>The"}).first == "Paris.");
    // Cut short and followed by more: ended all the same.
    CHECK(through({"Paris.<|im|\nuser: more"}).first == "Paris.");
    CHECK(through({"Paris.<|end\nUser:"}).first == "Paris.");

    SECTION("split anywhere across reads, no part of a marker gets out") {
        const std::string stream = "The Jazz won 112-104.<|end|><|im|";
        for (std::size_t first = 1; first < stream.size(); ++first) {
            for (std::size_t second = first + 1; second <= stream.size(); ++second) {
                const auto [out, stopped] =
                    through({stream.substr(0, first), stream.substr(first, second - first),
                             stream.substr(second)});
                INFO(first << " " << second);
                CHECK(out == "The Jazz won 112-104.");
            }
        }
    }
    SECTION("a fragment still open when the stream ends is never emitted") {
        CHECK(through({"Done.", "<|"}).first == "Done.");
        CHECK(through({"Done.<|e"}).first == "Done.");
        CHECK(through({"Done.<|zz"}).first == "Done.");
        // A lone `<` was never one.
        CHECK(through({"a < b and b <"}).first == "a < b and b <");
    }
    SECTION("a marker of no family is dropped where it stands, and the reply goes on") {
        // Seen on Gemma 4 E4B: `Bitcoin is 3000 <|fiap| (via Mark ...`.
        CHECK(through({"Bitcoin is 3000 <|fiap| (via Mark)"}) ==
              std::pair<std::string, bool>{"Bitcoin is 3000  (via Mark)", false});
        CHECK(through({"x <|foo|> y"}).first == "x  y");
        CHECK(through({"the <|e is a ", "letter"}).first == "the  is a letter");
        CHECK(through({"<|im|> goes, ", "<|e"}).first == " goes, ");
        // A `<|` that starts no name is text.
        CHECK(through({"a <| b, and <||> too"}).first == "a <| b, and <||> too");
    }
    SECTION("with no markers it is a pass-through") {
        TurnMarkerFilter none;
        bool stopped = false;
        CHECK(none.write("a <|end|> b", stopped) == "a <|end|> b");
        CHECK_FALSE(stopped);
        CHECK_FALSE(none.active());
    }
}
