#include "backends/model_roster.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "contracts/layout.h"
#include "support/env_guard.h"

namespace {

using apogee::backends::load_roster_cache;
using apogee::backends::ProviderRoster;
using apogee::backends::RosterCache;
using apogee::backends::RosterModel;
using apogee::backends::save_roster_cache;

/// A home of this test's own, so the cache file lands nowhere real.
struct RosterHome {
    apogee::testing::TempDir dir{"roster-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", dir.path().string()};
};

}  // namespace

TEST_CASE("the roster cache round-trips, and anything unreadable is empty",
          "[backends][roster]") {
    RosterHome home;

    // Absent: empty, never a throw.
    CHECK(load_roster_cache().rosters.empty());

    RosterCache cache;
    cache.rosters["anthropic"] =
        ProviderRoster{{RosterModel{"claude-opus-5-5", "Claude Opus 5.5"},
                        RosterModel{"claude-sonnet-5-5", "Claude Sonnet 5.5"}},
                       "2026-10-08"};
    REQUIRE(save_roster_cache(cache).empty());

    const RosterCache read = load_roster_cache();
    REQUIRE(read.rosters.size() == 1);
    const ProviderRoster* roster = read.roster_for("anthropic");
    REQUIRE(roster != nullptr);
    CHECK(roster->fetched_at == "2026-10-08");
    REQUIRE(roster->models.size() == 2);
    CHECK(roster->models[0].id == "claude-opus-5-5");
    CHECK(read.roster_for("openai") == nullptr);

    // Disposable: truncated, not JSON, or another schema each read as empty.
    const std::filesystem::path path = apogee::harness::roster_cache_path();
    for (const std::string& bytes :
         {std::string{"{\"schema\":1,\"rosters\""}, std::string{"not json"},
          std::string{"{\"schema\":99,\"rosters\":{}}"}}) {
        std::ofstream{path, std::ios::binary} << bytes;
        CHECK(load_roster_cache().rosters.empty());
    }
}
