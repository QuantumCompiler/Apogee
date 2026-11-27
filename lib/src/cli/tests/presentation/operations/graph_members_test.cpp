#include "operations/graph_members.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>

#include "contracts/config.h"
#include "embedstore/store.h"
#include "operations/collections.h"
#include "support/env_guard.h"

/// A named graph's members, as the build, stats, show and the admin twins
/// open them.
namespace {

[[nodiscard]] std::string bytes_of(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

}  // namespace

TEST_CASE("a member written as a path is never opened; a plain one is",
          "[operations][graph][collection_name][M45]") {
    // CLI-M45: a hand-written `graphs:` member holding a path would have
    // been opened -- and its database migrated -- by `graph build`, `stats`,
    // `show` and their HTTP twins. It maps to null, as a missing member
    // does, and `check` names it.
    const apogee::testing::TempDir root{"m45-members-" + std::to_string(std::random_device{}())};
    const apogee::testing::EnvGuard home{"APOGEE_HOME", (root.path() / "home").string()};
    // An empty file is a database SQLite would write a schema into.
    const std::filesystem::path foreign = root.path() / "foreign.db";
    std::ofstream{foreign, std::ios::binary}.flush();
    const std::string before = bytes_of(foreign);
    REQUIRE(before.empty());
    {
        apogee::embedstore::Store store{apogee::commands::collection_path("a..b")};
        store.replace_source("b.md", {"the hound"});
    }
    apogee::harness::NamedGraphConfig named;
    named.collections = {"../../foreign", (root.path() / "foreign").string(), "a..b"};
    const apogee::commands::GraphMembers members = apogee::commands::open_graph_members(named);
    CHECK(members.stores.at("../../foreign") == nullptr);
    CHECK(members.stores.at((root.path() / "foreign").string()) == nullptr);
    REQUIRE(members.stores.at("a..b") != nullptr);
    CHECK(members.stores.at("a..b")->chunk_count() == 1);
    CHECK(bytes_of(foreign) == before);
    CHECK_FALSE(std::filesystem::exists(root.path() / "foreign.db-wal"));
}
