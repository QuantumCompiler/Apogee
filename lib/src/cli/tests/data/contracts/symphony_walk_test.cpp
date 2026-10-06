#include "contracts/symphony_walk.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/config.h"

/// The static walk over a symphony's plays (27r): loops -- the symphony's
/// own name, mutual, transitive, a loop below the root -- refused with their
/// path; the depth cap at its edge; a played name nothing defines refused
/// for a complete lookup and late-bound for one that is not; the member
/// calls one play makes, each played symphony counted as often as it is
/// played.
namespace {

using apogee::harness::SymphonySpec;
using apogee::harness::SymphonyStage;
using apogee::harness::SymphonyWalk;
using apogee::harness::walk_symphony;

/// A symphony whose stages are `kinds` in order: a role stage for "-", a
/// stage playing that name otherwise.
SymphonySpec symphony(std::string name, const std::vector<std::string>& kinds) {
    SymphonySpec spec;
    spec.name = std::move(name);
    std::size_t index = 0;
    for (const std::string& kind : kinds) {
        SymphonyStage stage;
        stage.name = "s" + std::to_string(++index);
        if (kind == "-") {
            stage.role = "utility";
            stage.prompt = "{{input}}";
        } else {
            stage.play = kind;
        }
        spec.stages.push_back(std::move(stage));
    }
    return spec;
}

/// A library of definitions the walk looks names up in.
struct Library {
    std::map<std::string, SymphonySpec> specs;

    void add(SymphonySpec spec) {
        std::string name = spec.name;
        specs.emplace(std::move(name), std::move(spec));
    }

    [[nodiscard]] apogee::harness::SymphonyLookup lookup() const {
        return [this](std::string_view name) -> const SymphonySpec* {
            const auto found = specs.find(std::string{name});
            return found == specs.end() ? nullptr : &found->second;
        };
    }

    [[nodiscard]] SymphonyWalk walk(std::string_view root, std::int64_t depth = 4,
                                    bool complete = true) const {
        return walk_symphony(specs.at(std::string{root}), lookup(), depth, complete);
    }
};

}  // namespace

TEST_CASE("a symphony that plays none is one deep, one call per stage", "[contracts][symphony]") {
    Library library;
    library.add(symphony("solo", {"-", "-", "-"}));
    const SymphonyWalk walked = library.walk("solo");
    CHECK(walked.ok());
    CHECK(walked.depth == 1);
    CHECK(walked.stage_calls == 3);
    CHECK(walked.reached.empty());
}

TEST_CASE("every loop is refused, its path named", "[contracts][symphony]") {
    Library library;
    // Its own name.
    library.add(symphony("self", {"-", "self"}));
    // Mutual.
    library.add(symphony("a", {"b"}));
    library.add(symphony("b", {"-", "a"}));
    // Transitive, through two others.
    library.add(symphony("x", {"y"}));
    library.add(symphony("y", {"z"}));
    library.add(symphony("z", {"-", "-", "x"}));
    // Below the root: the root is not on the loop, and the loop is still one.
    library.add(symphony("top", {"-", "b"}));

    CHECK(library.walk("self").problem ==
          "stage 2 (s2): plays 'self', which is already playing -- a loop, self → self; a "
          "symphony may not reach itself, directly or through another");
    CHECK(library.walk("a").problem ==
          "a → b, stage 2 (s2): plays 'a', which is already playing -- a loop, a → b → a; a "
          "symphony may not reach itself, directly or through another");
    CHECK_THAT(library.walk("x").problem,
               Catch::Matchers::StartsWith("x → y → z, stage 3 (s3): plays 'x', which is "
                                           "already playing -- a loop, x → y → z → x;"));
    CHECK_THAT(library.walk("top").problem,
               Catch::Matchers::ContainsSubstring("a loop, top → b → a → b;"));
    // A loop is a loop whatever the depth cap, and whether or not names are
    // late-bound.
    CHECK_FALSE(library.walk("a", 100).ok());
    CHECK_FALSE(library.walk("a", 4, false).ok());
    // Names compare as symphony names do: case aside.
    Library cased;
    cased.add(symphony("Loop", {"loop"}));
    CHECK_THAT(cased.walk("Loop").problem, Catch::Matchers::ContainsSubstring("a loop"));
}

TEST_CASE("nesting is capped: four deep plays, five is refused with its path",
          "[contracts][symphony]") {
    Library library;
    library.add(symphony("l5", {"-"}));
    library.add(symphony("l4", {"l5"}));
    library.add(symphony("l3", {"l4"}));
    library.add(symphony("l2", {"l3"}));
    library.add(symphony("l1", {"l2"}));

    const SymphonyWalk four = library.walk("l2");
    CHECK(four.ok());
    CHECK(four.depth == 4);
    CHECK(four.reached == std::vector<std::string>{"l3", "l4", "l5"});

    const SymphonyWalk five = library.walk("l1");
    CHECK(five.problem ==
          "l1 → l2 → l3 → l4, stage 1 (s1): plays 'l5', which nests 5 deep -- l1 → l2 → l3 → "
          "l4 → l5, and the cap is 4 (symphony_caps.depth)");
    // The cap is the caller's: a config that allows five plays it.
    CHECK(library.walk("l1", 5).ok());
    CHECK(library.walk("l1", 5).depth == 5);
    CHECK_FALSE(library.walk("l2", 3).ok());
}

TEST_CASE("a subtree walked once is measured again where it would go too deep",
          "[contracts][symphony]") {
    // `deep` is reached twice: first two deep (sound), then three deep, where
    // its own two levels pass the cap of four. The second visit must not take
    // the first's word for it.
    Library library;
    library.add(symphony("leaf", {"-"}));
    library.add(symphony("deep", {"leaf"}));
    library.add(symphony("mid", {"deep"}));
    library.add(symphony("root", {"deep", "mid"}));
    CHECK(library.walk("root", 4).ok());
    CHECK_THAT(library.walk("root", 3).problem,
               Catch::Matchers::StartsWith("root → mid → deep, stage 1 (s1): plays 'leaf', which "
                                           "nests 4 deep"));
}

TEST_CASE("a played name nothing defines: refused when the lookup is complete, a leaf when not",
          "[contracts][symphony]") {
    Library library;
    library.add(symphony("chain", {"-", "ghost"}));
    CHECK(library.walk("chain").problem ==
          "stage 2 (s2): plays 'ghost', and no symphony is named 'ghost'");
    const SymphonyWalk late = library.walk("chain", 4, false);
    CHECK(late.ok());
    CHECK(late.depth == 2);
    CHECK(late.stage_calls == 2);
    CHECK(late.reached.empty());
}

TEST_CASE("one play's member calls: each played symphony as often as it is played",
          "[contracts][symphony]") {
    Library library;
    library.add(symphony("pair", {"-", "-"}));
    library.add(symphony("twice", {"pair", "-", "pair"}));
    library.add(symphony("outer", {"twice", "twice", "-"}));
    const SymphonyWalk walked = library.walk("outer");
    REQUIRE(walked.ok());
    CHECK(walked.stage_calls == (2 * (2 + 1 + 2)) + 1);
    CHECK(walked.depth == 3);
    CHECK(walked.reached == std::vector<std::string>{"twice", "pair"});
}

TEST_CASE("a path reads as a play narrates it", "[contracts][symphony]") {
    CHECK(apogee::harness::symphony_path({"outer", "inner"}) == "outer → inner");
    CHECK(apogee::harness::symphony_path({"solo"}) == "solo");
    CHECK(apogee::harness::symphony_path({}).empty());
}
