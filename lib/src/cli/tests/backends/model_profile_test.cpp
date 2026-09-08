#include "backends/model_profile.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

/// The profile registry and its resolution ladder.
///
/// The ladder's ORDER is the contract, and it is asserted rung by rung: an
/// explicit setting beats the file's declaration, which beats a guess from a
/// name, which beats nothing. Reordering those is the whole bug this table
/// exists to catch.
namespace {

using apogee::backends::behavior_for;
using apogee::backends::model_profiles;
using apogee::backends::ModelProfile;
using apogee::backends::reasoning_pairs_for;
using apogee::backends::resolve_profile;

}  // namespace

TEST_CASE("an explicit setting wins over everything", "[backends][profile]") {
    // Rung 1. The user has told us; nothing observed should override that.
    const ModelProfile* profile = resolve_profile("qwen3", "gemma3", "gemma-3-1b-it.gguf");
    REQUIRE(profile != nullptr);
    CHECK(profile->name == "qwen3");
}

TEST_CASE("the file's own architecture beats a guess from its name", "[backends][profile]") {
    // Rung 2 over rung 3, and the reason is the point: an architecture is a
    // FACT recorded in the GGUF, a name is a string somebody chose. This is
    // "specific before family" in the form that bites -- a file declaring
    // gemma3 must not be matched as something else by its filename.
    const ModelProfile* profile = resolve_profile({}, "qwen35", "gemma-3-1b-it.gguf");
    REQUIRE(profile != nullptr);
    CHECK(profile->name == "qwen3");
}

TEST_CASE("a name hint is used when the architecture is unknown", "[backends][profile]") {
    // Rung 3. A generous match on purpose: a wrong guess still beats the
    // generic fallback for any modern instruction-tuned model.
    const ModelProfile* profile = resolve_profile({}, "some-future-arch", "my-qwen-model.gguf");
    REQUIRE(profile != nullptr);
    CHECK(profile->name == "qwen3");
}

TEST_CASE("nothing recognisable resolves to no profile", "[backends][profile]") {
    // Rung 4, and it must be nullptr rather than a default entry: the caller
    // has to be able to tell "uncharacterised" from "characterised as plain",
    // because they call for opposite handling.
    CHECK(resolve_profile({}, "totally-unknown", "mystery-model.gguf") == nullptr);
    CHECK(resolve_profile({}, {}, {}) == nullptr);
}

TEST_CASE("a forced name that matches nothing falls through rather than failing",
          "[backends][profile]") {
    // `chat_template:` predates profiles and also names a template in the older
    // registry. A hard error here would break configs that were valid before.
    const ModelProfile* profile = resolve_profile("not-a-profile", "gemma3", {});
    REQUIRE(profile != nullptr);
    CHECK(profile->name == "gemma3");
}

TEST_CASE("the real architectures observed on disk resolve", "[backends][profile]") {
    // Exactly the strings the three models on the development machine report,
    // read from their GGUF headers rather than assumed.
    struct Case {
        std::string architecture;
        std::string expected;
    };

    const std::vector<Case> cases{
        {"gemma3", "gemma3"},
        {"qwen35", "qwen3"},
        {"llama", "llama3"},
    };

    for (const Case& test : cases) {
        INFO("architecture: " << test.architecture);
        const ModelProfile* profile = resolve_profile({}, test.architecture, {});
        REQUIRE(profile != nullptr);
        CHECK(profile->name == test.expected);
    }
}

TEST_CASE("an unprofiled model is handled permissively", "[backends][profile]") {
    // THE rule. An unrecognised model is far likelier uncharacterised than
    // featureless, and failing to recognise a reasoning block is the expensive
    // direction: the model's working gets printed as the answer.
    const apogee::harness::ModelBehavior behavior = behavior_for(nullptr);

    CHECK_FALSE(behavior.known());
    CHECK(behavior.reasoning_tags.empty());
    // ...but the FILTER gets the full default set, which is where permissive
    // actually has to happen.
    CHECK(reasoning_pairs_for(nullptr).size() == apogee::backends::default_think_pairs().size());
}

TEST_CASE("a known profile with no reasoning tags means it emits none", "[backends][profile]") {
    // The distinction that makes `known()` worth having. Gemma 3 was observed
    // emitting no reasoning wrapper, so its empty list is a RESULT -- and the
    // filter must honour it rather than substituting the defaults.
    const ModelProfile* gemma = resolve_profile({}, "gemma3", {});
    REQUIRE(gemma != nullptr);

    CHECK(gemma->reasoning.empty());
    CHECK(reasoning_pairs_for(gemma).empty());

    const apogee::harness::ModelBehavior behavior = behavior_for(gemma);
    CHECK(behavior.known());
    CHECK(behavior.reasoning_tags.empty());
}

TEST_CASE("a family observed emitting reasoning carries its markers", "[backends][profile]") {
    const ModelProfile* qwen = resolve_profile({}, "qwen35", {});
    REQUIRE(qwen != nullptr);

    REQUIRE(qwen->reasoning.size() == 1);
    CHECK(qwen->reasoning.front().open == "<think>");
    CHECK(qwen->reasoning.front().close == "</think>");
}

TEST_CASE("every profile says whether it was verified, and why", "[backends][profile][honesty]") {
    // Under the open-model policy there is no allowlist making any family a
    // promise, so the characterization state is the only signal a user gets.
    // A profile with no evidence line is one nobody can audit.
    for (const ModelProfile& profile : model_profiles()) {
        INFO("profile: " << profile.name);
        CHECK_FALSE(profile.name.empty());
        CHECK_FALSE(profile.evidence.empty());
        // An unverified profile must SAY it is unverified in its evidence, so
        // the two can never drift apart.
        if (!profile.verified) {
            CHECK(profile.evidence.find("not verified") != std::string::npos);
        }
    }
}

TEST_CASE("the two families actually run here are the verified ones",
          "[backends][profile][honesty]") {
    // Pins the characterization to what was really observed. If someone marks
    // a third family verified without running it, this is what asks them which
    // model they used.
    std::vector<std::string> verified;
    for (const ModelProfile& profile : model_profiles()) {
        if (profile.verified) {
            verified.push_back(profile.name);
        }
    }
    CHECK(verified == std::vector<std::string>{"gemma3", "qwen3"});
}
