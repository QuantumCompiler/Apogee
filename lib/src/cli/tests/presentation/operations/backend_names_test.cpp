#include "operations/backend_names.h"

#include <catch2/catch_test_macros.hpp>

#include <random>
#include <string>

#include "backends/model_roster.h"
#include "contracts/config.h"
#include "support/env_guard.h"

namespace {

using apogee::backends::ProviderRoster;
using apogee::backends::RosterCache;
using apogee::backends::RosterModel;
using apogee::commands::resolve_roster_model;
using apogee::commands::resolve_session_model;
using apogee::commands::roster_pin_note;
using apogee::harness::BackendType;

/// A config with the named entries, and a roster cache written under a home
/// of this test's own.
struct Fixture {
    apogee::testing::TempDir dir{"names-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", dir.path().string()};
    apogee::harness::Config config;

    void backend(const std::string& name, BackendType type) {
        apogee::harness::BackendConfig entry;
        entry.type = type;
        config.backends.emplace(name, std::move(entry));
    }
};

}  // namespace

TEST_CASE("a roster model resolves on a sole configured owner, and only then",
          "[operations][backend-names][roster]") {
    Fixture fixture;
    fixture.backend("claude", BackendType::Anthropic);
    fixture.backend("work", BackendType::OpenAI);

    RosterCache cache;
    cache.rosters["anthropic"] =
        ProviderRoster{{RosterModel{"claude-opus-5-5", "Opus"}}, "2026-10-08"};
    cache.rosters["openai"] = ProviderRoster{
        {RosterModel{"gpt-5.2", "GPT"}, RosterModel{"shared-model", "S"}}, "2026-10-08"};
    cache.rosters["google"] = ProviderRoster{{RosterModel{"shared-model", "S"}}, "2026-10-08"};
    REQUIRE(apogee::backends::save_roster_cache(cache).empty());

    // One owner: the type's first entry by key order, the id verbatim.
    const auto sole = resolve_roster_model(fixture.config, "claude-opus-5-5");
    CHECK(sole.backend == "claude");
    CHECK(sole.model == "claude-opus-5-5");
    REQUIRE(sole.owners.size() == 1);
    CHECK(sole.owners.front() == "anthropic");

    // An unconfigured type's roster owns nothing: google is not configured,
    // so "shared-model" has exactly one CONFIGURED owner and resolves.
    const auto shared = resolve_roster_model(fixture.config, "shared-model");
    CHECK(shared.backend == "work");
    REQUIRE(shared.owners.size() == 1);

    // Two configured owners: named, nothing picked.
    fixture.backend("gem", BackendType::Google);
    const auto both = resolve_roster_model(fixture.config, "shared-model");
    CHECK(both.backend.empty());
    CHECK(both.owners.size() == 2);

    // Unknown everywhere: everything empty, the caller's error stands.
    const auto none = resolve_roster_model(fixture.config, "no-such-model");
    CHECK(none.backend.empty());
    CHECK(none.owners.empty());
}

TEST_CASE("a session's model name means one thing to -m, /model and complete -m",
          "[operations][backend-names][roster]") {
    Fixture fixture;
    fixture.backend("claude", BackendType::Anthropic);
    fixture.config.backends.at("claude").model = "claude-opus-4-1";
    fixture.backend("work", BackendType::OpenAI);
    fixture.backend("gem", BackendType::Google);
    fixture.backend("gpt-5.2", BackendType::Mock);

    RosterCache cache;
    cache.rosters["anthropic"] = ProviderRoster{
        {RosterModel{"claude-opus-4-1", "Opus 4.1"}, RosterModel{"claude-sonnet-5-5", "Sonnet"}},
        "2026-10-09"};
    cache.rosters["openai"] = ProviderRoster{
        {RosterModel{"gpt-5.2", "GPT"}, RosterModel{"shared-model", "S"}}, "2026-10-09"};
    cache.rosters["google"] = ProviderRoster{{RosterModel{"shared-model", "S"}}, "2026-10-09"};
    REQUIRE(apogee::backends::save_roster_cache(cache).empty());

    // A key, as the file spells it -- whatever case it was typed in.
    const auto key = resolve_session_model(fixture.config, "Claude");
    CHECK(key.backend == "claude");
    CHECK(key.pinned.empty());
    // An entry's own model is the entry, never a pin -- though its roster
    // lists it too.
    const auto own = resolve_session_model(fixture.config, "claude-opus-4-1");
    CHECK(own.backend == "claude");
    CHECK(own.pinned.empty());
    // A key that is also a roster model is the key.
    const auto both = resolve_session_model(fixture.config, "gpt-5.2");
    CHECK(both.backend == "gpt-5.2");
    CHECK(both.pinned.empty());

    // A roster model on its sole configured owner: pinned there, said so.
    const auto pinned = resolve_session_model(fixture.config, "claude-sonnet-5-5");
    CHECK(pinned.backend == "claude");
    CHECK(pinned.pinned == "claude-sonnet-5-5");
    CHECK(pinned.roster == "anthropic");
    CHECK(pinned.refusal.empty());
    CHECK(roster_pin_note(pinned) == "anthropic's roster, on backend 'claude'");

    // Two configured owners: refused, both named, the way to pin one given.
    const auto shared = resolve_session_model(fixture.config, "shared-model");
    CHECK(shared.backend.empty());
    CHECK(shared.refusal ==
          "'shared-model' is on google and openai's rosters -- pin it to one entry with 'apogee "
          "config add-backend <name> --type <type> --model shared-model'");

    // Nothing anywhere, or nothing named: everything empty.
    for (const char* nothing : {"no-such-model", ""}) {
        const auto none = resolve_session_model(fixture.config, nothing);
        CHECK(none.backend.empty());
        CHECK(none.pinned.empty());
        CHECK(none.refusal.empty());
    }
}

TEST_CASE("<backend>:<model> pins any model on an entry that names its model, and only there",
          "[operations][backend-names][pins]") {
    Fixture fixture;
    fixture.backend("claude", BackendType::ClaudeCli);
    fixture.backend("gem", BackendType::GeminiCli);
    fixture.backend("api", BackendType::Anthropic);
    fixture.config.backends.at("api").model = "claude-sonnet-5-5";
    fixture.backend("ollama", BackendType::OllamaCli);
    fixture.config.backends.at("ollama").model = "llama3:8b";
    fixture.backend("local", BackendType::LlamaCpp);
    fixture.backend("we:ird", BackendType::Mock);

    // On a vendor CLI, an API entry, any case of the key: the model verbatim,
    // said as no roster's.
    const auto opus = resolve_session_model(fixture.config, "Claude:opus");
    CHECK(opus.backend == "claude");
    CHECK(opus.pinned == "opus");
    CHECK(opus.roster.empty());
    CHECK(roster_pin_note(opus) == "on backend 'claude'");
    CHECK(resolve_session_model(fixture.config, "gem:gemini-2.5-pro").pinned == "gemini-2.5-pro");
    CHECK(resolve_session_model(fixture.config, "api:claude-haiku-4-5").pinned ==
          "claude-haiku-4-5");

    // A model holds colons of its own; an entry's own colon-holding model is
    // the entry, matched before any split.
    const auto cloud = resolve_session_model(fixture.config, "ollama:gemma4:31b:cloud");
    CHECK(cloud.backend == "ollama");
    CHECK(cloud.pinned == "gemma4:31b:cloud");
    const auto own = resolve_session_model(fixture.config, "llama3:8b");
    CHECK(own.backend == "ollama");
    CHECK(own.pinned.empty());
    // A key holding a colon is still a key, and the split goes past it.
    const auto weird = resolve_session_model(fixture.config, "we:ird:custom");
    CHECK(weird.backend == "we:ird");
    CHECK(weird.pinned == "custom");

    // The entry's own model, or none, is the entry itself.
    const auto same = resolve_session_model(fixture.config, "api:claude-sonnet-5-5");
    CHECK(same.backend == "api");
    CHECK(same.pinned.empty());
    const auto bare = resolve_session_model(fixture.config, "claude:");
    CHECK(bare.backend == "claude");
    CHECK(bare.pinned.empty());

    // An entry that runs a file has nothing to pin by name.
    const auto file = resolve_session_model(fixture.config, "local:anything");
    CHECK(file.backend.empty());
    CHECK(file.refusal ==
          "'local' runs the weights at its model_path -- a model is pinned by name only on an "
          "API or vendor-CLI backend");

    // A colon after no key is nothing.
    const auto none = resolve_session_model(fixture.config, "nobody:opus");
    CHECK(none.backend.empty());
    CHECK(none.refusal.empty());
}
