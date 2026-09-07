#include "harness/roles.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

/// The role-resolution chain, table-tested rung by rung.
///
/// This exists because Ommi shipped the chain twice and the copies disagreed:
/// a request ran on one backend from the CLI and another over HTTP. The table
/// below is the contract that makes a second copy detectable — if someone
/// reorders the four rungs in `roles.cpp`, exactly one row here goes red and
/// names which precedence broke.
namespace {

using apogee::harness::Config;
using apogee::harness::ModelRole;
using apogee::harness::resolve_backend_key;
using apogee::harness::RoleRequest;

[[nodiscard]] Config config_with(std::string chat, std::string embedding, std::string extraction) {
    Config config;
    config.models.default_backend = std::move(chat);
    config.models.default_embedding = std::move(embedding);
    config.models.default_extraction = std::move(extraction);
    return config;
}

struct Case {
    std::string what;
    Config config;
    RoleRequest request;
    std::string expected;
};

}  // namespace

TEST_CASE("the resolution chain answers each rung in order", "[harness][roles]") {
    const Config full = config_with("base", "embed", "extract");
    const Config bare = config_with("base", "", "");

    const std::vector<Case> cases{
        // --- rung 1: an explicit override beats everything ------------------
        {"override beats the chat default",
         full,
         {.role = ModelRole::Chat, .override = "picked"},
         "picked"},
        {"override beats an entry backend",
         full,
         {.role = ModelRole::Chat, .override = "picked", .entry_backend = "entry"},
         "picked"},
        {"override beats a role pointer",
         full,
         {.role = ModelRole::Embedding, .override = "picked"},
         "picked"},

        // --- rung 2: the feature's own pin ----------------------------------
        {"entry backend beats a role pointer",
         full,
         {.role = ModelRole::Embedding, .entry_backend = "entry"},
         "entry"},
        {"entry backend beats the chat default",
         full,
         {.role = ModelRole::Chat, .entry_backend = "entry"},
         "entry"},

        // --- rung 3: the role pointer ---------------------------------------
        {"embedding uses its own pointer", full, {.role = ModelRole::Embedding}, "embed"},
        {"extraction uses its own pointer", full, {.role = ModelRole::Extraction}, "extract"},
        {"chat has no pointer of its own and lands on the default",
         full,
         {.role = ModelRole::Chat},
         "base"},

        // --- rung 4: models.default -----------------------------------------
        {"embedding falls through to the default when unset",
         bare,
         {.role = ModelRole::Embedding},
         "base"},
        {"extraction falls through to the default when unset",
         bare,
         {.role = ModelRole::Extraction},
         "base"},
    };

    for (const Case& test : cases) {
        INFO(test.what);
        CHECK(resolve_backend_key(test.config, test.request) == test.expected);
    }
}

TEST_CASE("with every role pointer unset the chain is exactly the old behaviour",
          "[harness][roles]") {
    // The compatibility promise in one assertion: before roles existed, every
    // caller did `override.empty() ? models.default : override`. With the
    // pointers unset the resolver must be indistinguishable from that, or this
    // item silently changed what every existing config does.
    const Config bare = config_with("base", "", "");

    for (const ModelRole role : {ModelRole::Chat, ModelRole::Embedding, ModelRole::Extraction}) {
        for (const std::string_view override : {std::string_view{}, std::string_view{"picked"}}) {
            const std::string legacy =
                override.empty() ? bare.models.default_backend : std::string{override};
            CHECK(resolve_backend_key(bare, RoleRequest{.role = role, .override = override}) ==
                  legacy);
        }
    }
}

TEST_CASE("nothing configured resolves to nothing, not to a guess", "[harness][roles]") {
    // A resolver that invented a fallback here would turn "you have no config"
    // into "no backend named 'default'", which sends the user looking for a
    // backend instead of for their missing config.
    const Config empty;
    CHECK(resolve_backend_key(empty, RoleRequest{.role = ModelRole::Chat}).empty());
    CHECK(resolve_backend_key(empty, RoleRequest{.role = ModelRole::Embedding}).empty());
}

TEST_CASE("whitespace-only config values fall through instead of naming a backend",
          "[harness][roles]") {
    // A pointer of "  " is a typo, not a backend name. Returning it produces
    // "no backend named '  '"; falling through produces what the user meant.
    Config config = config_with("base", "   ", "\t\n");
    CHECK(resolve_backend_key(config, RoleRequest{.role = ModelRole::Embedding}) == "base");
    CHECK(resolve_backend_key(config, RoleRequest{.role = ModelRole::Extraction}) == "base");

    // And surrounding whitespace on a real value is trimmed rather than passed
    // through to a lookup that would miss.
    config.models.default_embedding = "  embed  ";
    CHECK(resolve_backend_key(config, RoleRequest{.role = ModelRole::Embedding}) == "embed");
}

TEST_CASE("a role names the config key it reads", "[harness][roles]") {
    // So an error message can say which line of the config to edit.
    CHECK(apogee::harness::to_string(ModelRole::Chat) == "default");
    CHECK(apogee::harness::to_string(ModelRole::Embedding) == "default_embedding");
    CHECK(apogee::harness::to_string(ModelRole::Extraction) == "default_extraction");
}
