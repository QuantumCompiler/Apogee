#include "backends/think_filter.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

/// The reasoning filter, against real observed output.
///
/// The fixture at the top is not invented: it is what Qwen 3.6-27b actually
/// returned on this machine for "What is 2+2? Answer briefly." — every one of
/// those characters was reaching the user before this filter existed.
namespace {

using apogee::backends::default_think_pairs;
using apogee::backends::strip_think_blocks;
using apogee::backends::TagPair;
using apogee::backends::ThinkFilter;

/// Verbatim from a live run, 2026-09-07.
constexpr std::string_view kQwenReply = "<think>\n\n</think>\n\n4";

/// Feeds `text` in `chunk`-byte pieces and returns what a surface would show.
[[nodiscard]] std::string filtered(std::string_view text, std::size_t chunk,
                                   const std::vector<TagPair>& pairs,
                                   std::string* thinking = nullptr) {
    ThinkFilter filter{pairs};
    if (thinking != nullptr) {
        filter.on_thinking([thinking](std::string_view piece) { thinking->append(piece); });
    }
    std::string out;
    for (std::size_t offset = 0; offset < text.size(); offset += chunk) {
        out += filter.write(text.substr(offset, chunk));
    }
    out += filter.flush();
    return out;
}

}  // namespace

TEST_CASE("the observed Qwen reply loses its reasoning block", "[backends][think]") {
    // The bug, in one assertion. Before this filter, all of `<think>\n\n</think>`
    // reached the user as though it were the answer.
    CHECK(strip_think_blocks(kQwenReply, default_think_pairs()) == "\n\n4");
}

TEST_CASE("the result is identical at every chunk size", "[backends][think]") {
    // Tokens arrive in whatever pieces the model produces, so a marker split
    // across two of them is the normal case, not an edge one. A filter that
    // only ever sees whole markers in tests has never been tested.
    const std::string whole = filtered(kQwenReply, kQwenReply.size(), default_think_pairs());

    for (const std::size_t chunk : {std::size_t{1}, std::size_t{2}, std::size_t{3}, std::size_t{5},
                                    std::size_t{7}, std::size_t{64}}) {
        INFO("chunk: " << chunk);
        CHECK(filtered(kQwenReply, chunk, default_think_pairs()) == whole);
    }
}

TEST_CASE("a marker split across chunks is still recognised", "[backends][think]") {
    // The specific failure one-byte feeding is meant to catch: `<thi` then
    // `nk>`. Emitting the first half and swallowing the second would print a
    // broken tag AND lose the reasoning.
    ThinkFilter filter{default_think_pairs()};
    std::string out;
    out += filter.write("<thi");
    out += filter.write("nk>secret</thi");
    out += filter.write("nk>visible");
    out += filter.flush();

    CHECK(out == "visible");
}

TEST_CASE("reasoning reaches the thinking sink, not the answer", "[backends][think]") {
    // Two destinations, one stream. A surface with a thinking view shows it
    // there; one without simply drops it. Neither prints it as the reply.
    std::string thinking;
    const std::string answer =
        filtered("<think>working it out</think>the answer", 3, default_think_pairs(), &thinking);

    CHECK(answer == "the answer");
    CHECK(thinking == "working it out");
}

TEST_CASE("an empty pair list is a pass-through and cannot underflow",
          "[backends][think][regression]") {
    // Ommi's recorded bug: the hold-back was computed from the longest marker
    // and panicked on a negative slice when there were no markers at all. Here
    // the empty case does no arithmetic -- and an empty list on a KNOWN profile
    // is a real configuration, meaning "this family emits no reasoning".
    const std::string text = "<think>this should survive</think>";
    CHECK(filtered(text, 1, {}) == text);
    CHECK(filtered("", 1, {}).empty());
    CHECK(strip_think_blocks(text, {}) == text);
}

TEST_CASE("a partial marker at end of stream was never a marker", "[backends][think]") {
    // Held-back bytes must be emitted at flush rather than eaten: a reply that
    // legitimately ends in `<` should not lose it.
    CHECK(filtered("done <thi", 1, default_think_pairs()) == "done <thi");
    CHECK(filtered("a < b", 1, default_think_pairs()) == "a < b");
}

TEST_CASE("an unterminated block does not leak into the answer", "[backends][think]") {
    // A model cut off mid-thought. Its residue is reasoning, and putting it in
    // the answer would show working that was never finished.
    std::string thinking;
    const std::string answer =
        filtered("visible<think>cut off here", 4, default_think_pairs(), &thinking);

    CHECK(answer == "visible");
    CHECK(thinking == "cut off here");
}

TEST_CASE("a held-back partial close marker is reasoning, not answer", "[backends][think]") {
    // The version of the case above that a test can actually see. Inside a
    // block the filter streams reasoning out as it goes, holding back only what
    // could still become the close marker -- so at end of stream the residue is
    // just those few bytes. Ending the stream mid-`</think>` is what leaves a
    // meaningful amount held.
    //
    // Written after a mutation that routed exactly this residue into the answer
    // survived the test above: nearly nothing was left to leak, so the
    // assertion could not fail. This one leaves six bytes.
    std::string thinking;
    const std::string answer =
        filtered("<think>reasoning</thin", 64, default_think_pairs(), &thinking);

    CHECK(answer.empty());
    // The partial marker is part of the reasoning, and must not surface as the
    // model's reply.
    CHECK(answer.find("</thin") == std::string::npos);
    CHECK(thinking.find("reasoning") != std::string::npos);
}

TEST_CASE("several blocks in one reply are all removed", "[backends][think]") {
    CHECK(filtered("<think>a</think>one<think>b</think>two", 3, default_think_pairs()) == "onetwo");
}

TEST_CASE("text that merely resembles a tag is untouched", "[backends][think]") {
    // The filter keys on the exact markers. Prose about thinking is prose.
    CHECK(filtered("I think, therefore I am", 2, default_think_pairs()) ==
          "I think, therefore I am");
    CHECK(filtered("use <thinker> carefully", 2, default_think_pairs()) ==
          "use <thinker> carefully");
}

TEST_CASE("a profile's own pairs are used rather than the defaults", "[backends][think]") {
    // A family that wraps reasoning differently gets its own markers, and
    // markers it does NOT use pass through as ordinary text.
    const std::vector<TagPair> only_reasoning{{"<reasoning>", "</reasoning>"}};

    CHECK(filtered("<reasoning>hidden</reasoning>shown", 3, only_reasoning) == "shown");
    CHECK(filtered("<think>kept</think>", 3, only_reasoning) == "<think>kept</think>");
}

TEST_CASE("the filter reports whether it is inside a block", "[backends][think]") {
    ThinkFilter filter{default_think_pairs()};
    CHECK_FALSE(filter.inside());
    (void)filter.write("<think>mid");
    CHECK(filter.inside());
    (void)filter.write("</think>");
    CHECK_FALSE(filter.inside());
}
