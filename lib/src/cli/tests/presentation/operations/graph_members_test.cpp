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

TEST_CASE("a new member the config cannot hold is refused; one it refers to is said honestly",
          "[operations][graph][collection_name][M46]") {
    // CLI-M46: `config add-graph team --collections "my notes,notes"` said
    // "ingest registers it on first use" of a name no ingest could register.
    const apogee::testing::TempDir root{"m46-members-" + std::to_string(std::random_device{}())};
    const apogee::testing::EnvGuard home{"APOGEE_HOME", (root.path() / "home").string()};
    apogee::harness::Config config;
    apogee::harness::NamedGraphConfig graph;

    graph.collections = {"new one"};
    apogee::commands::NamedGraphValidation result =
        apogee::commands::validate_named_graph(config, "team", graph);
    CHECK(result.error.find("member collection name 'new one'") != std::string::npos);
    CHECK(result.error.find("a name is letters, digits") != std::string::npos);

    graph.collections = {"fresh"};
    result = apogee::commands::validate_named_graph(config, "team", graph);
    CHECK(result.error.empty());
    REQUIRE(result.warnings.size() == 1);
    CHECK(result.warnings.front().find("ingest registers it on first use") != std::string::npos);

    // A name the config already refers to is not new: kept, and said as it is.
    apogee::harness::AgentConfig agent;
    agent.collection = "new one";
    config.agents["h"] = agent;
    graph.collections = {"new one"};
    result = apogee::commands::validate_named_graph(config, "team", graph);
    CHECK(result.error.empty());
    REQUIRE(result.warnings.size() == 1);
    CHECK(result.warnings.front().find("without an entry") != std::string::npos);
    CHECK(result.warnings.front().find("registers it") == std::string::npos);

    // A store already on disk is a member, whatever its name.
    {
        apogee::embedstore::Store store{apogee::commands::collection_path("on disk")};
        store.replace_source("a.md", {"the hound"});
    }
    graph.collections = {"on disk"};
    result = apogee::commands::validate_named_graph(apogee::harness::Config{}, "team", graph);
    CHECK(result.error.empty());
    CHECK(result.warnings.empty());
}

TEST_CASE("a new graph's name is at most 200 bytes; an existing one keeps its own",
          "[operations][graph][M46]") {
    const apogee::testing::TempDir root{"m46-graph-name-" + std::to_string(std::random_device{}())};
    const apogee::testing::EnvGuard home{"APOGEE_HOME", (root.path() / "home").string()};
    apogee::harness::Config config;
    apogee::harness::NamedGraphConfig graph;
    graph.collections = {"notes"};
    CHECK(
        apogee::commands::validate_named_graph(config, std::string(200, 'g'), graph).error.empty());
    const std::string long_name(201, 'g');
    CHECK(apogee::commands::validate_named_graph(config, long_name, graph)
              .error.find("at most 200") != std::string::npos);
    config.graphs.emplace_back(long_name, graph);
    CHECK(apogee::commands::validate_named_graph(config, long_name, graph).error.empty());
}
