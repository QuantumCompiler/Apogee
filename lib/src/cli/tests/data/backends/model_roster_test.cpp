#include "backends/model_roster.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <regex>
#include <string>
#include <vector>

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

TEST_CASE("the roster cache round-trips, and anything unreadable is empty", "[backends][roster]") {
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

TEST_CASE("the CLIs that print no list are read from the binary, never saved, and a fetch wins",
          "[backends][roster][aliases]") {
    // 35: Claude Code's list is Anthropic's published ids, the Gemini CLI's
    // its own aliases -- read beside the cache, never written into it.
    const RosterHome home;
    const RosterCache known = apogee::backends::known_rosters();
    const ProviderRoster* claude = known.roster_for("claude-cli");
    REQUIRE(claude != nullptr);
    CHECK(claude->built_in);
    // The day it was last checked against Anthropic's list (ADR 0009).
    CHECK(std::regex_match(claude->fetched_at, std::regex{"20[0-9]{2}-[01][0-9]-[0-3][0-9]"}));
    std::vector<std::string> ids;
    for (const RosterModel& model : claude->models) {
        ids.push_back(model.id);
    }
    for (const char* id : {"claude-fable-5-1", "claude-opus-5-5", "claude-sonnet-5-5",
                           "claude-haiku-5-5", "claude-fable-5", "claude-opus-5"}) {
        CHECK(std::find(ids.begin(), ids.end(), id) != ids.end());
    }
    CHECK(claude->models.front().name == "Claude Fable 5.1");
    const ProviderRoster* gemini = known.roster_for("gemini-cli");
    REQUIRE(gemini != nullptr);
    CHECK(gemini->models.size() == 3);
    CHECK(std::regex_match(gemini->fetched_at, std::regex{"20[0-9]{2}-[01][0-9]-[0-3][0-9]"}));
    CHECK(known.roster_for("codex-cli") == nullptr);  // Codex lists its own, live

    // A fetch saved beside them keeps them out of the file.
    RosterCache fetched = known;
    fetched.rosters["codex-cli"] =
        ProviderRoster{{RosterModel{"gpt-5.5", "GPT-5.5"}}, "2026-10-10"};
    REQUIRE(save_roster_cache(fetched).empty());
    const RosterCache on_disk = load_roster_cache();
    CHECK(on_disk.rosters.size() == 1);
    CHECK(on_disk.roster_for("codex-cli") != nullptr);
    CHECK(apogee::backends::known_rosters().rosters.size() == 3);

    // A roster fetched for a type wins over the binary's.
    RosterCache grown = on_disk;
    grown.rosters["claude-cli"] =
        ProviderRoster{{RosterModel{"claude-next", "Next"}}, "2026-12-01"};
    REQUIRE(save_roster_cache(grown).empty());
    const RosterCache after = apogee::backends::known_rosters();
    const ProviderRoster* live = after.roster_for("claude-cli");
    REQUIRE(live != nullptr);
    CHECK_FALSE(live->built_in);
    CHECK(live->models.size() == 1);
}
