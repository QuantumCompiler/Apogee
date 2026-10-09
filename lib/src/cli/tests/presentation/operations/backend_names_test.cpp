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
    cache.rosters["openai"] = ProviderRoster{{RosterModel{"gpt-5.2", "GPT"},
                                              RosterModel{"shared-model", "S"}},
                                             "2026-10-08"};
    cache.rosters["google"] =
        ProviderRoster{{RosterModel{"shared-model", "S"}}, "2026-10-08"};
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
