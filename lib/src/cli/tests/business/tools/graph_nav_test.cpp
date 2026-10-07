#include "tools/graph_nav.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "agent/tool.h"
#include "agentloop/graph_context.h"
#include "contracts/config.h"
#include "contracts/layout.h"
#include "embedstore/store.h"
#include "graph/navigate.h"
#include "mcp/serve_stdio.h"
#include "support/env_guard.h"
#include "support/graph_fixture.h"
#include "tools/toolsets.h"

/// The `graph` toolset (27l): four read-only tools over the one traversal
/// core -- registered with the native toolsets, never gated, served by
/// `__mcp-tools`; each returning the core's document byte for byte; every
/// cap bounded and every bad argument an error the model can act on; and a
/// scoped instance over an injected store path, the seam 27o hands a chat's
/// attachment graph to.
namespace {

using apogee::agent::ToolOutcome;
using apogee::agent::ToolRegistry;

/// A home with the named graph `work` over the collection `notes`.
struct World {
    apogee::testing::TempDir home{"tools-graph-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    apogee::harness::Config config =
        apogee::harness::parse_config("graphs:\n  work:\n    collections: [notes]\n", "<tools>");
    ToolRegistry registry;

    World() {
        apogee::embedstore::Store notes{apogee::harness::embeddings_dir() / "notes.db"};
        apogee::embedstore::Store work{apogee::agentloop::graph_db_path("work")};
        apogee::testing::build_navigation_graph(work, notes, "notes");
        apogee::tools::register_graph_tools(registry, {.config = &config, .scope = {}});
    }

    [[nodiscard]] ToolOutcome run(std::string_view tool, const std::string& arguments) const {
        const apogee::agent::Tool* found = registry.find(tool);
        REQUIRE(found != nullptr);
        return found->run(arguments);
    }

    [[nodiscard]] apogee::graph::OpenGraph open() const {
        return apogee::graph::OpenGraph{apogee::graph::resolve_graph_target(config, {})};
    }
};

[[nodiscard]] bool contains(std::string_view text, std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

}  // namespace

TEST_CASE("the graph toolset is read-only: registered, never gated, served by __mcp-tools",
          "[tools][graph]") {
    const apogee::testing::TempDir home{"tools-graph-native-" +
                                        std::to_string(std::random_device{}())};
    const apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    ToolRegistry registry;
    apogee::tools::register_native_toolsets(registry, apogee::tools::ToolsetOptions{});
    const std::vector<apogee::mcp::ServedTool> served = apogee::mcp::read_only_tools(registry);
    for (const std::string_view name : apogee::tools::graph_tool_names()) {
        INFO(name);
        const apogee::agent::Tool* tool = registry.find(name);
        REQUIRE(tool != nullptr);
        CHECK_FALSE(tool->writes);
        CHECK_FALSE(tool->outbound);
        CHECK_FALSE(apogee::agent::gated(*tool));
        CHECK(apogee::tools::toolset_of(name) == "graph");
        const auto it = std::ranges::find_if(
            served, [&](const apogee::mcp::ServedTool& one) { return one.name == name; });
        REQUIRE(it != served.end());
        CHECK(it->read_only);
        CHECK(it->input_schema.is_object());
        CHECK(it->input_schema["properties"].contains("graph"));
    }
    // Switched off like any toolset.
    ToolRegistry without;
    apogee::tools::ToolsetOptions options;
    options.disabled = {"graph"};
    apogee::tools::register_native_toolsets(without, options);
    for (const std::string_view name : apogee::tools::graph_tool_names()) {
        CHECK(without.find(name) == nullptr);
    }
}

TEST_CASE("each tool returns the core's document, byte for byte", "[tools][graph][parity]") {
    const World world;
    const apogee::graph::OpenGraph open = world.open();

    const ToolOutcome path =
        world.run("graph_path",
                  R"({"from":"main","to":"pkg.lib.helper","relations":["calls"],"directed":true})");
    REQUIRE_FALSE(path.is_error);
    CHECK(path.content ==
          apogee::graph::to_json(
              apogee::graph::find_path(open, apogee::graph::PathRequest{.from = "main",
                                                                        .to = "pkg.lib.helper",
                                                                        .max_hops = 8,
                                                                        .directed = true,
                                                                        .relations = {"calls"}}))
              .dump());
    CHECK(nlohmann::json::parse(path.content)["hops"] == 2);

    const ToolOutcome explain = world.run("graph_explain", R"({"node":"Vault"})");
    REQUIRE_FALSE(explain.is_error);
    CHECK(explain.content ==
          apogee::graph::to_json(
              apogee::graph::explain_node(open, apogee::graph::CardRequest{.node = "Vault"}))
              .dump());
    CHECK(nlohmann::json::parse(explain.content)["provenance"]["chunks"][0]["collection"] ==
          "notes");

    const ToolOutcome neighbors = world.run(
        "graph_neighbors",
        R"({"node":"pkg.lib.helper","relation":"calls","direction":"in","max_neighbors":"5"})");
    REQUIRE_FALSE(neighbors.is_error);
    CHECK(neighbors.content ==
          apogee::graph::to_json(
              apogee::graph::find_neighbors(open,
                                            apogee::graph::NeighborsRequest{
                                                .node = "pkg.lib.helper",
                                                .relation = "calls",
                                                .direction = apogee::embedstore::EdgeDirection::In,
                                                .max_per_relation = 5}))
              .dump());

    const ToolOutcome query = world.run("graph_query", R"({"question":"vault","graph":"work"})");
    REQUIRE_FALSE(query.is_error);
    CHECK(query.content ==
          apogee::graph::to_json(
              apogee::graph::run_query(open, apogee::graph::QueryRequest{.question = "vault"}))
              .dump());

    // `collection` reads the graph covering it.
    const ToolOutcome covered =
        world.run("graph_query", R"({"question":"vault","collection":"notes"})");
    CHECK(covered.content == query.content);
    // A relation list may come as one comma-separated string, and a local
    // model's template may send every value as text.
    CHECK(world
              .run("graph_path",
                   R"({"from":"main","to":"pkg.lib.helper","relations":"calls","directed":true})")
              .content == path.content);
    CHECK(
        world
            .run(
                "graph_path",
                R"({"from":"main","to":"pkg.lib.helper","relations":"['calls']","directed":"True","max_hops":"8","graph":"","collection":""})")
            .content == path.content);
    CHECK(
        world
            .run(
                "graph_path",
                R"({"from":"main","to":"pkg.lib.helper","relations":["'calls'"],"directed":"yes"})")
            .content == path.content);
}

TEST_CASE("every cap is bounded and every bad argument is an error the model can act on",
          "[tools][graph][bounds]") {
    const World world;

    struct Row {
        std::string tool;
        std::string arguments;
        std::string part;
    };

    const std::vector<Row> rows{
        {"graph_path", R"({"from":"main","to":"pkg.app.run","max_hops":33})",
         "Error: the hop cap must be between 1 and 32 (got 33)"},
        {"graph_path", R"({"from":"main","to":"pkg.app.run","max_hops":0})",
         "the hop cap must be between 1 and 32 (got 0)"},
        {"graph_path", R"({"from":"main","to":"pkg.app.run","max_hops":99999999999})",
         "the hop cap must be between 1 and 32"},
        {"graph_explain", R"({"node":"Vault","max_neighbors":101})",
         "the neighbour cap must be between 1 and 100"},
        {"graph_neighbors", R"({"node":"Vault","max_neighbors":0})",
         "the neighbour cap must be between 1 and 100"},
        {"graph_query", R"({"question":"vault","hops":3})", "a query walks 1 or 2 hops (got 3)"},
        {"graph_query", R"({"question":"vault","max_entities":51})",
         "the entity cap must be between 1 and 50 (got 51)"},
        {"graph_path", R"({"to":"pkg.app.run"})", "Error: from is required"},
        {"graph_explain", R"({})", "Error: node is required"},
        {"graph_query", R"({"question":"  "})", "question is required"},
        {"graph_path", R"(not json)", "arguments must be a JSON object"},
        {"graph_path", R"({"from":"main","to":"pkg.app.run","relations":7})",
         "relations must be a list of strings"},
        {"graph_path", R"({"from":"main","to":"pkg.app.run","directed":"maybe"})",
         "directed must be true or false"},
        {"graph_path", R"({"from":"main","to":"pkg.app.run","max_hops":"many"})",
         "max_hops must be an integer"},
        {"graph_neighbors", R"({"node":"Vault","direction":"sideways"})",
         "direction must be both, out or in"},
        {"graph_explain", R"({"node":"helper"})",
         "'helper' names 2 nodes in graph 'work' -- name one: function:pkg.lib.helper"},
        {"graph_explain", R"({"node":"Vault","graph":"work","collection":"notes"})",
         "name a graph or a collection, not both"},
        {"graph_explain", R"({"node":"Vault","graph":"nope"})",
         "no graph or collection named 'nope' -- built: work"},
    };
    for (const Row& row : rows) {
        INFO(row.tool << " " << row.arguments);
        const ToolOutcome outcome = world.run(row.tool, row.arguments);
        CHECK(outcome.is_error);
        CHECK(contains(outcome.content, row.part));
        CHECK(outcome.content.starts_with("Error: "));
    }
}

TEST_CASE("with no graph built, the tools say how to build one", "[tools][graph]") {
    const apogee::testing::TempDir home{"tools-graph-none-" +
                                        std::to_string(std::random_device{}())};
    const apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    ToolRegistry registry;
    apogee::tools::register_graph_tools(registry, {});
    const ToolOutcome outcome = registry.find("graph_query")->run(R"({"question":"x"})");
    CHECK(outcome.is_error);
    CHECK(contains(outcome.content, "no graph is built yet"));
}

TEST_CASE("a scoped toolset reads the store it was handed, and takes no selection",
          "[tools][graph][scope]") {
    const apogee::testing::TempDir dir{"tools-graph-scope-" +
                                       std::to_string(std::random_device{}())};
    const std::filesystem::path path = dir.path() / "attachments.db";
    {
        apogee::embedstore::Store store{path};
        apogee::testing::build_navigation_graph(store, store, "");
    }
    apogee::graph::GraphTarget target;
    target.name = "attachments";
    target.store_path = path;
    target.databases[""] = path;
    ToolRegistry registry;
    apogee::tools::register_graph_tools(registry, {.config = nullptr, .scope = target});

    const apogee::agent::Tool* explain = registry.find("graph_explain");
    REQUIRE(explain != nullptr);
    const nlohmann::json schema = nlohmann::json::parse(explain->parameters_schema);
    CHECK_FALSE(schema["properties"].contains("graph"));
    CHECK_FALSE(schema["properties"].contains("collection"));
    CHECK(contains(explain->description, "Reads the graph 'attachments'."));

    const ToolOutcome card = explain->run(R"({"node":"Vault","graph":"elsewhere"})");
    REQUIRE_FALSE(card.is_error);
    const nlohmann::json json = nlohmann::json::parse(card.content);
    CHECK(json["graph"] == "attachments");
    CHECK(json["provenance"]["chunks"][0]["source"] == "docs/atlas.md");
    const apogee::graph::OpenGraph open{target};
    CHECK(card.content ==
          apogee::graph::to_json(
              apogee::graph::explain_node(open, apogee::graph::CardRequest{.node = "Vault"}))
              .dump());
}

TEST_CASE("a registry's graph toolset is swapped for a scoped one, and nothing else moves",
          "[tools][graph][scope]") {
    const apogee::testing::TempDir home{"tools-graph-swap-" +
                                        std::to_string(std::random_device{}())};
    const apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    const std::filesystem::path path = home.path() / "attachments.db";
    {
        apogee::embedstore::Store store{path};
        apogee::testing::build_navigation_graph(store, store, "");
    }
    apogee::graph::GraphTarget target;
    target.name = "attachments";
    target.store_path = path;
    target.databases[""] = path;
    const std::string note =
        " Reads the code graph of the folder attached to this chat, lib/src/cli/source.";

    ToolRegistry registry;
    apogee::tools::register_native_toolsets(registry, apogee::tools::ToolsetOptions{});
    const ToolRegistry scoped = apogee::tools::with_graph_scope(
        registry, {.config = nullptr, .scope = target, .scope_note = note});

    // The same names; every other tool untouched; the environment note told
    // when to reach for them (26p's mechanism), after what it said before.
    CHECK(scoped.names() == registry.names());
    CHECK(scoped.environment() ==
          registry.environment() + "\n\n" + std::string{apogee::tools::scoped_graph_policy()});
    CHECK(apogee::tools::scoped_graph_policy().starts_with("How to use the code graph: "));
    // Asked by a registry narrowed to a toolset without them, it says nothing
    // of them.
    ToolRegistry narrowed;
    narrowed.set_environment(scoped.environment_source());
    narrowed.add(*scoped.find("read_file"));
    CHECK(narrowed.environment().find("code graph") == std::string::npos);
    for (const std::string& name : registry.names()) {
        const bool graph = std::ranges::find(apogee::tools::graph_tool_names(), name) !=
                           apogee::tools::graph_tool_names().end();
        if (!graph) {
            CHECK(scoped.find(name)->description == registry.find(name)->description);
            continue;
        }
        INFO(name);
        // The scoped set's: no selection, the note in place of the graph's
        // name -- so a question about that code ranks it (26g) -- ungated.
        const apogee::agent::Tool* tool = scoped.find(name);
        CHECK(tool->description.ends_with(note));
        CHECK(registry.find(name)->description.find(note) == std::string::npos);
        const nlohmann::json schema = nlohmann::json::parse(tool->parameters_schema);
        CHECK_FALSE(schema["properties"].contains("graph"));
        CHECK_FALSE(apogee::agent::gated(*tool));
    }
    // It reads the store it was handed, where the unscoped set finds none.
    const ToolOutcome card = scoped.find("graph_explain")->run(R"({"node":"Vault"})");
    REQUIRE_FALSE(card.is_error);
    CHECK(nlohmann::json::parse(card.content)["graph"] == "attachments");
    CHECK(registry.find("graph_explain")->run(R"({"node":"Vault"})").is_error);

    // Switched off, it stays off: the scope is the same toolset.
    ToolRegistry off;
    apogee::tools::register_native_toolsets(off,
                                            apogee::tools::ToolsetOptions{.disabled = {"graph"}});
    const ToolRegistry still = apogee::tools::with_graph_scope(off, {.scope = target});
    CHECK(still.names() == off.names());
    CHECK(still.find("graph_explain") == nullptr);
    // No scope, nothing changes.
    CHECK(apogee::tools::with_graph_scope(registry, {}).find("graph_explain")->description ==
          registry.find("graph_explain")->description);
}
