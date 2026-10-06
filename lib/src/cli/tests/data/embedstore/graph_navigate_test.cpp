#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "embedstore/graph.h"
#include "embedstore/graph_search.h"
#include "embedstore/store.h"
#include "support/env_guard.h"
#include "support/graph_fixture.h"

/// The store reads navigation walks with (27l): edges counted per relation
/// and direction, neighbours filtered and capped in SQL with structure before
/// unresolved names, the entity index over names alone, a code node by its
/// unqualified name (case-sensitive, metacharacters literal) or by the line
/// its span holds, a node's communities -- over the committed fixture graph.
namespace {

using apogee::embedstore::CodeSpan;
using apogee::embedstore::EdgeDirection;
using apogee::embedstore::GraphNode;
using apogee::embedstore::Neighbor;
using apogee::embedstore::NeighborFilter;
using apogee::embedstore::RelationCount;
using apogee::embedstore::Store;

struct Graph {
    apogee::testing::TempDir dir{"embedstore-graph-navigate-" +
                                 std::to_string(std::random_device{}())};
    Store store{dir.path() / "own.db"};

    Graph() {
        apogee::testing::build_navigation_graph(store, store, "");
    }

    [[nodiscard]] GraphNode node(const std::string& name, std::string_view type) const {
        for (const GraphNode& candidate : store.find_nodes(name)) {
            if (candidate.type == type && candidate.name == name) {
                return candidate;
            }
        }
        FAIL("no " << type << " " << name);
        return {};
    }
};

[[nodiscard]] std::vector<std::string> peers(const std::vector<Neighbor>& rows) {
    std::vector<std::string> out;
    for (const Neighbor& row : rows) {
        out.push_back(row.peer_name);
    }
    return out;
}

[[nodiscard]] std::vector<std::string> names(const std::vector<GraphNode>& nodes) {
    std::vector<std::string> out;
    for (const GraphNode& node : nodes) {
        out.push_back(node.name);
    }
    return out;
}

}  // namespace

TEST_CASE("has_graph says whether any node is stored", "[embedstore][graph][navigate]") {
    const apogee::testing::TempDir dir{"embedstore-graph-empty-" +
                                       std::to_string(std::random_device{}())};
    const Store empty{dir.path() / "empty.db"};
    CHECK_FALSE(empty.has_graph());
    const Graph g;
    CHECK(g.store.has_graph());
}

TEST_CASE("relation_counts counts a node's edges per relation and direction",
          "[embedstore][graph][navigate]") {
    const Graph g;
    const std::vector<RelationCount> counts =
        g.store.relation_counts(g.node("pkg.lib.helper", "function").id);
    // Fifteen callers, run and other: seventeen calls in; one defined_in out.
    REQUIRE(counts.size() == 2);
    CHECK(counts[0].relation == "calls");
    CHECK_FALSE(counts[0].outgoing);
    CHECK(counts[0].count == 17);
    CHECK(counts[1].relation == "defined_in");
    CHECK(counts[1].outgoing);
    CHECK(counts[1].count == 1);

    // One relation both ways is two counts.
    const std::vector<RelationCount> run =
        g.store.relation_counts(g.node("pkg.app.run", "function").id);
    REQUIRE(run.size() == 3);
    CHECK(run[0].relation == "calls");
    CHECK(run[0].outgoing);
    CHECK(run[0].count == 4);
    CHECK(run[1].relation == "calls");
    CHECK_FALSE(run[1].outgoing);
    CHECK(run[1].count == 1);
    CHECK(run[2].relation == "defined_in");
}

TEST_CASE("node_neighbors filters by relation, direction and peer type, and caps",
          "[embedstore][graph][navigate]") {
    const Graph g;
    const std::int64_t run = g.node("pkg.app.run", "function").id;

    NeighborFilter calls_out;
    calls_out.relations = {"calls"};
    calls_out.direction = EdgeDirection::Out;
    // Structure first, heaviest first: helper (two sites) before other, and
    // both before the unresolved names.
    CHECK(peers(g.store.node_neighbors(run, calls_out, 0)) ==
          std::vector<std::string>{"pkg.lib.helper", "pkg.lib.other", ".push_back", "json.dumps"});
    CHECK(peers(g.store.node_neighbors(run, calls_out, 2)) ==
          std::vector<std::string>{"pkg.lib.helper", "pkg.lib.other"});

    NeighborFilter in;
    in.direction = EdgeDirection::In;
    CHECK(peers(g.store.node_neighbors(run, in, 0)) == std::vector<std::string>{"pkg.app.main"});

    NeighborFilter defined;
    defined.relations = {"defined_in", "imports"};
    const std::vector<Neighbor> rows = g.store.node_neighbors(run, defined, 0);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].peer_name == "pkg/app.py");
    CHECK(rows[0].outgoing);
    CHECK(rows[0].origin == apogee::embedstore::kOriginExtracted);

    NeighborFilter decisions;
    decisions.peer_type = "decision";
    const std::vector<Neighbor> attached =
        g.store.node_neighbors(g.node("Vault", "system").id, decisions, 0);
    REQUIRE(attached.size() == 1);
    CHECK(attached[0].peer_name == "kr-0001");
    CHECK_FALSE(attached[0].outgoing);
    CHECK(attached[0].relation == "concerns");
}

TEST_CASE("search_node_names matches names, never descriptions, and keeps names out unless asked",
          "[embedstore][graph][navigate]") {
    const Graph g;
    // "warehouse" is only in Vault's description.
    CHECK(g.store.search_node_names("warehouse", 5, true).empty());
    CHECK_FALSE(g.store.search_nodes("warehouse", 5).empty());
    std::vector<std::string> found;
    for (const auto& hit : g.store.search_node_names("the vault please", 5, false)) {
        found.push_back(hit.node.name);
    }
    CHECK(found == std::vector<std::string>{"Vault"});
    // An unresolved name only when asked for.
    CHECK(g.store.search_node_names("dumps", 5, false).empty());
    const auto unresolved = g.store.search_node_names("dumps", 5, true);
    REQUIRE(unresolved.size() == 1);
    CHECK(unresolved[0].node.name == "json.dumps");
    CHECK(unresolved[0].score > 0.0);
}

TEST_CASE("code_nodes_ending finds a code node by the last parts of its name, case-sensitively",
          "[embedstore][graph][navigate]") {
    const Graph g;
    CHECK(names(g.store.code_nodes_ending("helper")) ==
          std::vector<std::string>{"pkg.lib.helper", "pkg.util.helper"});
    CHECK(names(g.store.code_nodes_ending("Store.add")) ==
          std::vector<std::string>{"pkg.lib.Store.add"});
    CHECK(names(g.store.code_nodes_ending("Store.Add")) ==
          std::vector<std::string>{"pkg.lib.Store.Add"});
    // A file by its tail at a slash; a name node by its tail at a dot.
    CHECK(names(g.store.code_nodes_ending("app.py")) == std::vector<std::string>{"pkg/app.py"});
    CHECK(names(g.store.code_nodes_ending("dumps")) == std::vector<std::string>{"json.dumps"});
    // Never a prose node, never a part that is not at a separator, and a
    // GLOB metacharacter is literal.
    CHECK(g.store.code_nodes_ending("Vault").empty());
    CHECK(g.store.code_nodes_ending("elper").empty());
    CHECK(g.store.code_nodes_ending("*").empty());
    CHECK(g.store.code_nodes_ending("h?lper").empty());
    CHECK(g.store.code_nodes_ending("").empty());
}

TEST_CASE("code_spans_at names the definitions holding a line, innermost first",
          "[embedstore][graph][navigate]") {
    const Graph g;
    const std::vector<apogee::embedstore::CodeFile> files = g.store.code_files();
    CHECK(files.size() == 6);
    CHECK(std::ranges::all_of(files, [](const auto& file) { return file.collection == "app"; }));

    std::vector<CodeSpan> spans = g.store.code_spans_at("app", "pkg/lib.py", 11);
    REQUIRE(spans.size() == 3);
    CHECK(spans[0].node_id == g.node("pkg.lib.Store.add", "function").id);
    CHECK(spans[1].node_id == g.node("pkg.lib.Store", "class").id);
    CHECK(spans[2].node_id == g.node("pkg/lib.py", "file").id);
    CHECK(spans[0].end_line == 12);
    // A reference is not a span; past every definition, nothing.
    CHECK(g.store.code_spans_at("app", "pkg/app.py", 8).size() == 2);
    CHECK(g.store.code_spans_at("app", "pkg/lib.py", 99).empty());
    CHECK(g.store.code_spans_at("other", "pkg/lib.py", 11).empty());
}

TEST_CASE("node_communities lists the communities a node is in", "[embedstore][graph][navigate]") {
    const Graph g;
    const auto communities = g.store.node_communities(g.node("Vault", "system").id);
    REQUIRE(communities.size() == 1);
    CHECK(communities[0].size == 3);
    CHECK(communities[0].summary == "Atlas and the Vault it writes its readings to.");
    CHECK(g.store.node_communities(g.node("pkg.app.run", "function").id).empty());
}
