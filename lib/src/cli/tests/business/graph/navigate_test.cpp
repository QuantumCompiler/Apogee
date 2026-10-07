#include "graph/navigate.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "agentloop/graph_context.h"
#include "contracts/config.h"
#include "contracts/layout.h"
#include "embedstore/store.h"
#include "support/env_guard.h"
#include "support/graph_fixture.h"

/// The one traversal core (27l), table-tested over the committed fixture
/// graph with no model and no network: addressing (`name`, `kind:name`,
/// `path:line`, a code node's unqualified name) and its honest failures;
/// shortest paths -- disconnected, self, the hop cap, direction, relation
/// filters, an unresolved name that ends a path and never carries one; the
/// card and the neighbour lists with their caps; the query under the
/// expansion's budget; which graph a selection names; and that none of it
/// writes.
namespace {

using apogee::embedstore::EdgeDirection;
using apogee::embedstore::GraphNode;
using apogee::embedstore::Store;
using apogee::graph::NavigationError;
using Kind = apogee::graph::NavigationError::Kind;

struct Graph {
    apogee::testing::TempDir dir{"graph-navigate-" + std::to_string(std::random_device{}())};
    Store store{dir.path() / "own.db"};

    Graph() {
        apogee::testing::build_navigation_graph(store, store, "");
    }

    [[nodiscard]] GraphNode node(std::string_view address) const {
        return apogee::graph::resolve_node(store, "own", address).node;
    }

    [[nodiscard]] apogee::graph::PathResult path(std::string_view from, std::string_view to,
                                                 int max_hops = apogee::graph::kDefaultPathHops,
                                                 bool directed = false,
                                                 std::vector<std::string> relations = {}) const {
        return apogee::graph::shortest_path(
            store, "own", node(from), node(to),
            apogee::graph::PathRequest{.from = std::string{from},
                                       .to = std::string{to},
                                       .max_hops = max_hops,
                                       .directed = directed,
                                       .relations = std::move(relations)});
    }
};

/// The kind a failure carries, and its message, from `body`.
struct Failure {
    Kind kind = Kind::InvalidArgument;
    std::string message;
    std::vector<apogee::graph::NodeRef> candidates;
};

[[nodiscard]] Failure failure_of(const std::function<void()>& body) {
    try {
        body();
    } catch (const NavigationError& e) {
        return Failure{.kind = e.kind(), .message = e.what(), .candidates = e.candidates()};
    }
    FAIL("no NavigationError was thrown");
    return {};
}

[[nodiscard]] bool contains(std::string_view text, std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

[[nodiscard]] std::size_t codepoints(std::string_view text) {
    return static_cast<std::size_t>(std::ranges::count_if(
        text, [](char c) { return (static_cast<unsigned char>(c) & 0xC0U) != 0x80U; }));
}

[[nodiscard]] std::vector<std::string> path_names(const apogee::graph::PathResult& result) {
    std::vector<std::string> out;
    for (const apogee::graph::NodeRef& node : result.nodes) {
        out.push_back(node.name);
    }
    return out;
}

}  // namespace

// ---- addressing -------------------------------------------------------------------

TEST_CASE("an address resolves by exact name, kind:name, path:line or an unqualified name",
          "[graph][navigate][resolve]") {
    const Graph g;

    struct Row {
        std::string address;
        std::string name;
        std::string type;
        std::string matched;
    };

    const std::vector<Row> rows{
        {"pkg.app.main", "pkg.app.main", "function", "exact"},
        {"  pkg.app.main  ", "pkg.app.main", "function", "exact"},
        {"function:pkg.app.main", "pkg.app.main", "function", "kind"},
        {"FUNCTION:pkg.app.main", "pkg.app.main", "function", "kind"},
        {"main", "pkg.app.main", "function", "unqualified"},
        {"function:main", "pkg.app.main", "function", "unqualified"},
        {"Store.Add", "pkg.lib.Store.Add", "function", "unqualified"},
        {"pkg.lib.Store.Add", "pkg.lib.Store.Add", "function", "exact"},
        {"system:Atlas", "Atlas", "system", "kind"},
        {"organization:atlas", "Atlas", "organization", "kind"},
        {"kr-0001", "kr-0001", "decision", "exact"},
        {"decision:kr-0001", "kr-0001", "decision", "kind"},
        {"json.dumps", "json.dumps", "name", "exact"},
        {"name:json.dumps", "json.dumps", "name", "kind"},
        {"pkg/app.py", "pkg/app.py", "file", "exact"},
        // path:line: the innermost definition holding the line; the path as
        // recorded, under its member, or a unique tail at a slash.
        {"pkg/lib.py:11", "pkg.lib.Store.add", "function", "path"},
        {"pkg/lib.py:19", "pkg/lib.py", "file", "path"},
        {"lib.py:7", "pkg.lib.other", "function", "path"},
        {"app/pkg/lib.py:2", "pkg.lib.helper", "function", "path"},
        {"pkg/app.py:8", "pkg.app.run", "function", "path"},
    };
    for (const Row& row : rows) {
        INFO(row.address);
        const apogee::graph::ResolvedNode resolved =
            apogee::graph::resolve_node(g.store, "own", row.address);
        CHECK(resolved.node.name == row.name);
        CHECK(resolved.node.type == row.type);
        CHECK(resolved.matched == row.matched);
    }
}

TEST_CASE("a name several nodes answer to is never picked: the candidates are listed",
          "[graph][navigate][resolve]") {
    const Graph g;

    struct Row {
        std::string address;
        std::vector<std::string> addresses;
    };

    const std::vector<Row> rows{
        // Two prose types share a name.
        {"Atlas", {"organization:Atlas", "system:Atlas"}},
        // Two modules define the unqualified name.
        {"helper", {"function:pkg.lib.helper", "function:pkg.util.helper"}},
        // A code name folded to two spellings -- code identity is case-sensitive.
        {"pkg.lib.store.add", {"function:pkg.lib.Store.Add", "function:pkg.lib.Store.add"}},
    };
    for (const Row& row : rows) {
        INFO(row.address);
        const Failure failure =
            failure_of([&] { (void)apogee::graph::resolve_node(g.store, "own", row.address); });
        CHECK(failure.kind == Kind::Ambiguous);
        std::vector<std::string> listed;
        for (const apogee::graph::NodeRef& candidate : failure.candidates) {
            listed.push_back(apogee::graph::node_address(candidate));
            CHECK(contains(failure.message, apogee::graph::node_address(candidate)));
        }
        std::ranges::sort(listed);
        CHECK(listed == row.addresses);
        CHECK(contains(failure.message, "names 2 nodes in graph 'own' -- name one: "));
    }
    // A code candidate says where it is.
    const Failure helper =
        failure_of([&] { (void)apogee::graph::resolve_node(g.store, "own", "helper"); });
    CHECK(contains(helper.message, "function:pkg.lib.helper (pkg/lib.py:1)"));
}

TEST_CASE("an address that names nothing says so, with the near matches",
          "[graph][navigate][resolve]") {
    const Graph g;
    const Failure near =
        failure_of([&] { (void)apogee::graph::resolve_node(g.store, "own", "the vault"); });
    CHECK(near.kind == Kind::NotFound);
    CHECK(near.message == "no node named 'the vault' in graph 'own' -- near matches: system:Vault");
    REQUIRE(near.candidates.size() == 1);

    const Failure none =
        failure_of([&] { (void)apogee::graph::resolve_node(g.store, "own", "zzqx"); });
    CHECK(none.kind == Kind::NotFound);
    CHECK(none.message == "no node named 'zzqx' in graph 'own' -- nothing has a name near it");

    // A kind that names nothing of that kind: the near matches are for the name.
    const Failure kinded = failure_of(
        [&] { (void)apogee::graph::resolve_node(g.store, "own", "class:pkg.lib.helper"); });
    CHECK(kinded.kind == Kind::NotFound);
    CHECK(contains(kinded.message, "no node named 'pkg.lib.helper'"));

    // A known file with nothing at that line; an unknown file is read as a name.
    const Failure line =
        failure_of([&] { (void)apogee::graph::resolve_node(g.store, "own", "pkg/lib.py:99"); });
    CHECK(line.kind == Kind::NotFound);
    CHECK(line.message == "nothing in graph 'own' is defined or declared at pkg/lib.py:99");
    const Failure file =
        failure_of([&] { (void)apogee::graph::resolve_node(g.store, "own", "nowhere.py:3"); });
    CHECK(file.kind == Kind::NotFound);
    CHECK(contains(file.message, "no node named 'nowhere.py:3'"));
    // A path's tail matches at a slash, never inside a file's name.
    const Failure tail =
        failure_of([&] { (void)apogee::graph::resolve_node(g.store, "own", "ib.py:7"); });
    CHECK(tail.kind == Kind::NotFound);
    CHECK(contains(tail.message, "no node named 'ib.py:7'"));

    const Failure empty =
        failure_of([&] { (void)apogee::graph::resolve_node(g.store, "own", " "); });
    CHECK(empty.kind == Kind::InvalidArgument);
}

// ---- path -------------------------------------------------------------------------

TEST_CASE("path finds the shortest walk, each hop with its relation, origin and direction",
          "[graph][navigate][path]") {
    const Graph g;
    const apogee::graph::PathResult forward = g.path("pkg.app.main", "pkg.lib.helper");
    REQUIRE(forward.found);
    CHECK(path_names(forward) ==
          std::vector<std::string>{"pkg.app.main", "pkg.app.run", "pkg.lib.helper"});
    REQUIRE(forward.steps.size() == 2);
    CHECK(forward.steps[0].relation == "calls");
    CHECK(forward.steps[0].origin == "extracted");
    CHECK(forward.steps[0].forward);
    CHECK(forward.steps[0].at == "pkg/app.py:13");
    CHECK(forward.steps[1].at == "pkg/app.py:6");
    CHECK(forward.steps[1].weight == 2);

    // The other way round, undirected: the hops read backwards.
    const apogee::graph::PathResult backward = g.path("pkg.lib.helper", "pkg.app.main");
    REQUIRE(backward.found);
    CHECK(path_names(backward) ==
          std::vector<std::string>{"pkg.lib.helper", "pkg.app.run", "pkg.app.main"});
    CHECK_FALSE(backward.steps[0].forward);
    CHECK_FALSE(backward.steps[1].forward);
    // Directed, the callee reaches nothing.
    CHECK_FALSE(g.path("pkg.lib.helper", "pkg.app.main", 8, /*directed=*/true).found);

    // Across the layers: a model's relations, then the parser's.
    const apogee::graph::PathResult mixed = g.path("system:Atlas", "pkg.lib.helper");
    REQUIRE(mixed.found);
    CHECK(path_names(mixed) == std::vector<std::string>{"Atlas", "Vault", "pkg.lib.Store",
                                                        "pkg/lib.py", "pkg.lib.helper"});
    CHECK(mixed.steps[0].origin == "inferred");
    CHECK(mixed.steps[0].relation == "stores readings in");
    CHECK(mixed.steps[0].at.empty());
    CHECK(mixed.steps[0].description == "Atlas writes its readings to Vault");
    CHECK(mixed.steps[1].relation == "implemented by");
    CHECK(mixed.steps[2].origin == "extracted");
    CHECK_FALSE(mixed.steps[3].forward);
}

TEST_CASE("path's table: self, disconnected, names, caps, direction and relations",
          "[graph][navigate][path]") {
    const Graph g;
    // A node to itself: found, no hop.
    const apogee::graph::PathResult self = g.path("pkg.app.main", "pkg.app.main");
    CHECK(self.found);
    CHECK(self.steps.empty());
    CHECK(path_names(self) == std::vector<std::string>{"pkg.app.main"});

    // Disconnected but for an unresolved name: a call to json.dumps connects
    // nothing, at any cap.
    for (const int cap : {8, 32}) {
        const apogee::graph::PathResult apart = g.path("pkg.app.main", "pkg.island.alone", cap);
        CHECK_FALSE(apart.found);
        CHECK(apart.nodes.empty());
        CHECK(apogee::graph::to_json(apart)["note"] ==
              "no path within " + std::to_string(cap) + " hops");
    }
    // ...but a name may end a path, or start one.
    CHECK(g.path("pkg.app.run", "json.dumps").steps.size() == 1);
    CHECK(g.path("json.dumps", "pkg.island.alone").steps.size() == 1);

    // The chain is ten calls long: past the default cap directed, within an
    // explicit one -- and two hops undirected, through the file.
    CHECK_FALSE(g.path("pkg.chain.n0", "pkg.chain.n10", 8, true).found);
    const apogee::graph::PathResult chain = g.path("pkg.chain.n0", "pkg.chain.n10", 10, true);
    REQUIRE(chain.found);
    CHECK(chain.steps.size() == 10);
    CHECK_FALSE(g.path("pkg.chain.n0", "pkg.chain.n10", 9, true).found);
    const apogee::graph::PathResult around = g.path("pkg.chain.n0", "pkg.chain.n10");
    CHECK(path_names(around) ==
          std::vector<std::string>{"pkg.chain.n0", "pkg/chain.py", "pkg.chain.n10"});

    // Relations narrow the walk.
    const apogee::graph::PathResult structural =
        g.path("pkg.app.main", "pkg.lib.helper", 8, false, {"defined_in", "imports"});
    CHECK(path_names(structural) ==
          std::vector<std::string>{"pkg.app.main", "pkg/app.py", "pkg/lib.py", "pkg.lib.helper"});
    CHECK(structural.relations == std::vector<std::string>{"defined_in", "imports"});
    CHECK_FALSE(g.path("pkg.app.main", "pkg.lib.helper", 8, false, {"inherits"}).found);

    // Never unbounded.
    for (const int cap : {0, -1, apogee::graph::kMaxPathHops + 1}) {
        const Failure failure =
            failure_of([&] { (void)g.path("pkg.app.main", "pkg.app.run", cap); });
        CHECK(failure.kind == Kind::InvalidArgument);
        CHECK(contains(failure.message, "the hop cap must be between 1 and 32"));
    }
}

TEST_CASE("path is the same path every time", "[graph][navigate][path]") {
    const Graph g;
    const std::string first = apogee::graph::to_json(g.path("system:Atlas", "pkg.app.main")).dump();
    for (int i = 0; i < 3; ++i) {
        CHECK(apogee::graph::to_json(g.path("system:Atlas", "pkg.app.main")).dump() == first);
    }
}

// ---- explain and neighbors --------------------------------------------------------

TEST_CASE("a card states degree, groups by relation and direction, and caps each group",
          "[graph][navigate][card]") {
    const Graph g;
    const GraphNode helper = g.node("pkg.lib.helper");
    const apogee::graph::NodeCard card =
        apogee::graph::node_card(g.store, {{"", &g.store}}, "own", helper, 12);
    CHECK(card.degree.total == 18);
    CHECK(card.degree.in == 17);
    CHECK(card.degree.out == 1);
    REQUIRE(card.relations.size() == 2);
    const apogee::graph::NeighborGroup& callers = card.relations[0];
    CHECK(callers.relation == "calls");
    CHECK_FALSE(callers.outgoing);
    CHECK(callers.total == 17);
    REQUIRE(callers.shown.size() == 12);
    CHECK(callers.shown[0].node.name == "pkg.app.run");  // the heaviest first
    CHECK(callers.shown[0].weight == 2);
    CHECK(callers.shown[0].at == "pkg/app.py:6");
    CHECK(callers.shown[1].node.name == "pkg.callers.c01");
    CHECK(callers.shown[1].node.file == "pkg/callers.py");
    CHECK(callers.shown[1].node.line == 1);
    CHECK(card.relations[1].relation == "defined_in");
    CHECK(card.relations[1].outgoing);
    // Provenance: the definition, at its lines.
    REQUIRE(card.code_mentions.size() == 1);
    CHECK(card.code_mentions[0].file == "pkg/lib.py");
    CHECK(card.code_mentions[0].role == "definition");
    CHECK(card.chunk_mentions.empty());
    CHECK(card.description == "def helper(x: int) -> int");

    const apogee::graph::NodeCard narrow =
        apogee::graph::node_card(g.store, {{"", &g.store}}, "own", helper, 3);
    CHECK(narrow.relations[0].shown.size() == 3);
    CHECK(narrow.relations[0].total == 17);
    const nlohmann::json json = apogee::graph::to_json(narrow);
    CHECK(json["relations"][0]["total"] == 17);
    CHECK(json["relations"][0]["neighbors"].size() == 3);

    for (const int cap : {0, apogee::graph::kMaxNeighborsPerRelation + 1}) {
        const Failure failure =
            failure_of([&] { (void)apogee::graph::node_card(g.store, {}, "own", helper, cap); });
        CHECK(failure.kind == Kind::InvalidArgument);
    }
}

TEST_CASE("a prose card carries its chunks, its community and its decisions",
          "[graph][navigate][card]") {
    const Graph g;
    const apogee::graph::NodeCard vault =
        apogee::graph::node_card(g.store, {{"", &g.store}}, "own", g.node("Vault"), 12);
    CHECK(vault.degree.total == 3);
    REQUIRE(vault.chunk_mentions.size() >= 2);
    CHECK(vault.chunk_mentions[0].source == "docs/atlas.md");
    CHECK(vault.chunk_mentions[1].source == "docs/vault.md");
    CHECK(vault.chunk_mentions[0].chunk == 0);
    CHECK(vault.code_mentions.empty());
    REQUIRE(vault.communities.size() == 1);
    CHECK(vault.communities[0].size == 3);
    CHECK(vault.communities[0].summary == "Atlas and the Vault it writes its readings to.");
    REQUIRE(vault.decisions.size() == 1);
    CHECK(vault.decisions[0].node.name == "kr-0001");
    CHECK(vault.decisions[0].node.status == "shipped");
    CHECK(vault.decisions[0].decision == "Keep every reading in Vault — one store, one backup");

    const apogee::graph::NodeCard record =
        apogee::graph::node_card(g.store, {{"", &g.store}}, "own", g.node("kr-0001"), 12);
    CHECK(record.node.status == "shipped");
    CHECK(record.node.discipline == "engineering");
    CHECK(apogee::graph::to_json(record)["node"]["discipline"] == "engineering");

    // A member whose database is gone: its chunks are named missing, never dropped.
    const apogee::graph::NodeCard orphaned =
        apogee::graph::node_card(g.store, {{"", nullptr}}, "own", g.node("Vault"), 12);
    REQUIRE_FALSE(orphaned.chunk_mentions.empty());
    CHECK(orphaned.chunk_mentions[0].missing);
    CHECK(apogee::graph::to_json(orphaned)["provenance"]["chunks"][0]["missing"] == true);
}

TEST_CASE("a card's provenance, decisions and description are bounded too",
          "[graph][navigate][card]") {
    const Graph g;
    // Fourteen chunks mention Vault: twelve listed, the count said.
    const apogee::graph::NodeCard vault =
        apogee::graph::node_card(g.store, {{"", &g.store}}, "own", g.node("Vault"), 12);
    CHECK(vault.mentions == 14);
    CHECK(vault.chunk_mentions.size() == apogee::graph::kCardMentions);
    // Fourteen references to .push_back: twelve lines listed.
    const apogee::graph::NodeCard push_back =
        apogee::graph::node_card(g.store, {{"", &g.store}}, "own", g.node(".push_back"), 12);
    CHECK(push_back.mentions == 14);
    CHECK(push_back.code_mentions.size() == apogee::graph::kCardMentions);
    // Thirteen records concern Ledger: twelve listed; its description clipped.
    const apogee::graph::NodeCard ledger =
        apogee::graph::node_card(g.store, {{"", &g.store}}, "own", g.node("Ledger"), 12);
    CHECK(ledger.decisions.size() == apogee::graph::kCardDecisions);
    CHECK(ledger.relations[0].total == 13);
    CHECK(ledger.description.ends_with("…"));
    CHECK(codepoints(ledger.description) == apogee::graph::kDescriptionClip + 1);
}

TEST_CASE("an unresolved name's card: navigable, referenced, structure first",
          "[graph][navigate][card]") {
    const Graph g;
    const apogee::graph::NodeCard dumps =
        apogee::graph::node_card(g.store, {{"", &g.store}}, "own", g.node("json.dumps"), 12);
    CHECK(dumps.node.unresolved);
    CHECK(dumps.node.file.empty());
    REQUIRE(dumps.code_mentions.size() == 3);
    CHECK(dumps.code_mentions[0].role == "reference");
    REQUIRE(dumps.relations.size() == 1);
    CHECK(dumps.relations[0].total == 3);
    CHECK(apogee::graph::to_json(dumps)["node"]["unresolved"] == true);
}

TEST_CASE("neighbors narrows to a relation and a direction", "[graph][navigate][neighbors]") {
    const Graph g;
    const GraphNode helper = g.node("pkg.lib.helper");
    const auto read = [&](std::string relation, EdgeDirection direction, int cap = 12) {
        return apogee::graph::neighborhood(
            g.store, "own", helper,
            apogee::graph::NeighborsRequest{.node = "pkg.lib.helper",
                                            .relation = std::move(relation),
                                            .direction = direction,
                                            .max_per_relation = cap});
    };
    const apogee::graph::Neighborhood in = read("calls", EdgeDirection::In);
    REQUIRE(in.groups.size() == 1);
    CHECK(in.groups[0].total == 17);
    CHECK(in.groups[0].shown.size() == 12);
    CHECK(in.degree.total == 18);  // the node's, whatever the filter
    CHECK(read("calls", EdgeDirection::Out).groups.empty());
    CHECK(read("nope", EdgeDirection::Both).groups.empty());
    CHECK(read("", EdgeDirection::Out).groups.size() == 1);
    CHECK(read("", EdgeDirection::Both, 100).groups[0].shown.size() == 17);
    const nlohmann::json json = apogee::graph::to_json(in);
    CHECK(json["direction"] == "in");
    CHECK(json["relation"] == "calls");
    CHECK(json["object"] == "graph.neighbors");
    CHECK(failure_of([&] { (void)read("", EdgeDirection::Both, 101); }).kind ==
          Kind::InvalidArgument);
}

// ---- query ------------------------------------------------------------------------

TEST_CASE("query matches exactly, then by name, then says nothing matched",
          "[graph][navigate][query]") {
    const Graph g;
    const apogee::graph::QueryResult exact =
        apogee::graph::query_graph(g.store, "own", "pkg.lib.helper", 1, 8);
    CHECK(exact.match == "exact");
    REQUIRE(exact.seeds.size() == 1);
    REQUIRE(exact.entities.size() == 8);
    CHECK(exact.entities[0].node.name == "pkg.lib.helper");
    CHECK(exact.entities[0].hop == 0);
    CHECK(exact.entities[1].node.name == "pkg.app.run");  // weight 2 x one mention
    CHECK(exact.entities[1].hop == 1);
    CHECK_FALSE(exact.truncated);
    REQUIRE_FALSE(exact.relations.empty());
    CHECK(exact.relations[0].from == "pkg.app.run");
    CHECK(exact.relations[0].origin == "extracted");

    const apogee::graph::QueryResult words = apogee::graph::query_graph(
        g.store, "own", "where are the readings kept in the vault", 1, 8);
    CHECK(words.match == "names");
    REQUIRE(words.seeds.size() == 1);
    CHECK(words.seeds[0].name == "Vault");
    std::vector<std::string> entities;
    for (const auto& entity : words.entities) {
        entities.push_back(entity.node.name);
    }
    CHECK(entities == std::vector<std::string>{"Vault", "Atlas", "kr-0001", "pkg.lib.Store"});

    CHECK(apogee::graph::query_graph(g.store, "own", "zzqx wwqy", 1, 8).match == "none");
    // An unresolved name is never a seed.
    const apogee::graph::QueryResult name =
        apogee::graph::query_graph(g.store, "own", "json.dumps", 1, 8);
    CHECK(name.match == "none");
    CHECK(name.entities.empty());
}

TEST_CASE("query keeps the expansion's budget and caps", "[graph][navigate][query]") {
    const Graph g;
    // Half the entity cap at most are seeds, so the neighbourhood has room.
    const apogee::graph::QueryResult seeded =
        apogee::graph::query_graph(g.store, "own", "helper other run main", 1, 4);
    CHECK(seeded.seeds.size() <= 2);
    CHECK(seeded.entities.size() <= 4);

    const apogee::graph::QueryResult wide =
        apogee::graph::query_graph(g.store, "own", "pkg.lib.helper", 2, 50);
    CHECK(wide.truncated);
    // What the payload carries fits the section a turn would inject, line
    // for line as the section renders it.
    std::size_t cost = codepoints("[Knowledge graph: own]") + 1;
    for (const auto& entity : wide.entities) {
        cost += codepoints(apogee::graph::node_label(entity.node) +
                           (entity.description.empty() ? "" : ": " + entity.description)) +
                1;
    }
    for (const auto& relation : wide.relations) {
        const std::string label = relation.origin == "extracted"
                                      ? relation.relation + "·" + relation.origin
                                      : relation.relation;
        cost += codepoints(relation.from + " —[" + label + "]→ " + relation.to +
                           (relation.description.empty() ? "" : ": " + relation.description)) +
                1;
    }
    CHECK(cost <= apogee::agentloop::kGraphSectionBudget);
    CHECK(cost > apogee::agentloop::kGraphSectionBudget / 2);
    // A cut never leaves a relation without its entities.
    std::vector<std::string> listed;
    for (const auto& entity : wide.entities) {
        listed.push_back(entity.node.name);
    }
    for (const auto& relation : wide.relations) {
        INFO(relation.from << " -> " << relation.to);
        CHECK(std::ranges::find(listed, relation.from) != listed.end());
        CHECK(std::ranges::find(listed, relation.to) != listed.end());
    }

    for (const auto& [hops, entities] :
         std::vector<std::pair<int, int>>{{0, 8}, {3, 8}, {1, 0}, {1, 51}}) {
        const Failure failure = failure_of(
            [&] { (void)apogee::graph::query_graph(g.store, "own", "helper", hops, entities); });
        CHECK(failure.kind == Kind::InvalidArgument);
    }
    CHECK(failure_of([&] { (void)apogee::graph::query_graph(g.store, "own", "  ", 1, 8); }).kind ==
          Kind::InvalidArgument);
}

TEST_CASE("query: an entity the budget cuts ends the section, as a turn's does",
          "[graph][navigate][query]") {
    // Sigma's neighbours: Alpha, whose line alone overruns what is left of
    // the budget, then Beta, which would fit. A turn's section stops at
    // Alpha -- it never skips ahead -- and so does the query.
    const apogee::testing::TempDir dir{"graph-navigate-cut-" +
                                       std::to_string(std::random_device{}())};
    Store store{dir.path() / "cut.db"};
    const std::int64_t sigma = store.upsert_node("Sigma", "system", "the seed").id;
    const std::int64_t alpha = store.upsert_node("Alpha", "system", std::string(1480, 'a')).id;
    const std::int64_t beta = store.upsert_node("Beta", "system", "short").id;
    store.upsert_edge(sigma, alpha, "feeds", "");
    store.upsert_edge(sigma, beta, "feeds", "");
    const apogee::graph::QueryResult cut = apogee::graph::query_graph(store, "cut", "Sigma", 1, 8);
    CHECK(cut.truncated);
    REQUIRE(cut.entities.size() == 1);
    CHECK(cut.entities[0].node.name == "Sigma");
    CHECK(cut.relations.empty());
}

// ---- read-only --------------------------------------------------------------------

TEST_CASE("navigation never writes", "[graph][navigate][readonly]") {
    const Graph g;
    const std::string before = g.store.graph_dump();
    (void)g.path("system:Atlas", "pkg.app.main");
    (void)g.path("pkg.app.main", "pkg.island.alone", 32);
    (void)apogee::graph::node_card(g.store, {{"", &g.store}}, "own", g.node("Vault"), 12);
    (void)apogee::graph::neighborhood(g.store, "own", g.node("pkg.lib.helper"), {});
    (void)apogee::graph::query_graph(g.store, "own", "pkg.lib.helper", 2, 50);
    (void)failure_of([&] { (void)apogee::graph::resolve_node(g.store, "own", "helper"); });
    CHECK(g.store.graph_dump() == before);
    CHECK(g.store.graph_communities().size() == 1);
}

// ---- which graph ------------------------------------------------------------------

namespace {

/// A home with a named graph `work` over the collection `notes`, a
/// collection `docs` with its own graph, an empty collection `plain`, and an
/// unbuilt named graph `later`.
struct Home {
    apogee::testing::TempDir home{"graph-navigate-home-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    apogee::harness::Config config = apogee::harness::parse_config(
        "graphs:\n  work:\n    collections: [notes]\n    hops: 2\n    max_entities: 6\n"
        "  later:\n    collections: [plain]\n",
        "<navigate>");

    explicit Home(bool with_docs = true) {
        Store notes{apogee::harness::embeddings_dir() / "notes.db"};
        Store work{apogee::agentloop::graph_db_path("work")};
        apogee::testing::build_navigation_graph(work, notes, "notes");
        const Store plain{apogee::harness::embeddings_dir() / "plain.db"};
        if (with_docs) {
            Store docs{apogee::harness::embeddings_dir() / "docs.db"};
            apogee::testing::build_navigation_graph(docs, docs, "");
        }
    }

    [[nodiscard]] apogee::graph::GraphTarget target(std::string graph,
                                                    std::string collection = {}) const {
        return apogee::graph::resolve_graph_target(
            config, apogee::graph::GraphSelection{.graph = std::move(graph),
                                                  .collection = std::move(collection)});
    }
};

}  // namespace

TEST_CASE("a selection names one graph: graphs-first, a collection through its covering graph",
          "[graph][navigate][target]") {
    const Home home;
    CHECK(apogee::graph::built_graph_names(home.config) ==
          std::vector<std::string>{"work", "docs"});

    const apogee::graph::GraphTarget work = home.target("work");
    CHECK(work.name == "work");
    CHECK(work.store_path == apogee::agentloop::graph_db_path("work"));
    REQUIRE(work.databases.size() == 1);
    CHECK(work.databases.at("notes") == apogee::harness::embeddings_dir() / "notes.db");
    CHECK(work.hops == 2);
    CHECK(work.max_entities == 6);

    const apogee::graph::GraphTarget docs = home.target("docs");
    CHECK(docs.store_path == apogee::harness::embeddings_dir() / "docs.db");
    CHECK(docs.databases.at("") == docs.store_path);
    CHECK(docs.hops == 1);

    // A member collection reads its built named graph; another its own.
    CHECK(home.target("", "notes").name == "work");
    CHECK(home.target("", "docs").name == "docs");

    // A named graph's chunk mentions resolve through its member.
    const apogee::graph::OpenGraph open{work};
    const apogee::graph::NodeCard card = apogee::graph::explain_node(
        open, apogee::graph::CardRequest{.node = "Vault", .max_per_relation = 12});
    REQUIRE(card.chunk_mentions.size() >= 2);
    CHECK(card.chunk_mentions[0].collection == "notes");
    CHECK(card.chunk_mentions[1].source == "docs/vault.md");
    CHECK(card.matched == "exact");
}

TEST_CASE("a selection that names nothing, or two things, says so", "[graph][navigate][target]") {
    const Home home;

    struct Row {
        std::string graph;
        std::string collection;
        Kind kind;
        std::string part;
    };

    const std::vector<Row> rows{
        {"", "", Kind::InvalidArgument, "2 graphs are built -- name one: work, docs"},
        {"work", "notes", Kind::InvalidArgument, "name a graph or a collection, not both"},
        {"nope", "", Kind::NotFound, "no graph or collection named 'nope' -- built: work, docs"},
        {"", "nope", Kind::NotFound, "no collection named 'nope'"},
        {"../x", "", Kind::InvalidArgument, "not a plain graph or collection name"},
        {"", "a/b", Kind::InvalidArgument, "not a plain collection name"},
        {"later", "", Kind::NotBuilt,
         "graph 'later' has not been built yet -- run: apogee graph build later"},
    };
    for (const Row& row : rows) {
        INFO(row.graph << " | " << row.collection);
        const Failure failure = failure_of([&] { (void)home.target(row.graph, row.collection); });
        CHECK(failure.kind == row.kind);
        CHECK(contains(failure.message, row.part));
    }
    // A collection with no graph of its own opens to a refusal naming the build.
    const Failure plain =
        failure_of([&] { const apogee::graph::OpenGraph open{home.target("plain")}; });
    CHECK(plain.kind == Kind::NotBuilt);
    CHECK(plain.message == "no graph built for 'plain' -- run: apogee graph build plain");
}

TEST_CASE("with no selection, the one graph built is the one read", "[graph][navigate][target]") {
    const Home home{/*with_docs=*/false};
    CHECK(home.target("").name == "work");

    const apogee::testing::TempDir empty{"graph-navigate-empty-" +
                                         std::to_string(std::random_device{}())};
    const apogee::testing::EnvGuard guard{"APOGEE_HOME", empty.path().string()};
    const Failure none = failure_of([&] {
        (void)apogee::graph::resolve_graph_target(apogee::harness::Config{},
                                                  apogee::graph::GraphSelection{});
    });
    CHECK(none.kind == Kind::NotBuilt);
    CHECK(contains(none.message, "no graph is built yet"));
}

TEST_CASE("payload pieces render a node one way everywhere", "[graph][navigate][json]") {
    const Graph g;
    const apogee::graph::NodeRef helper = apogee::graph::node_ref(g.node("pkg.lib.helper"));
    CHECK(apogee::graph::node_label(helper) == "pkg.lib.helper (function, pkg/lib.py:1)");
    CHECK(apogee::graph::node_address(helper) == "function:pkg.lib.helper");
    CHECK(
        apogee::graph::to_json(helper).dump() ==
        R"({"end_line":3,"file":"pkg/lib.py","line":1,"member":"app","name":"pkg.lib.helper","type":"function"})");
    CHECK(apogee::graph::node_label(apogee::graph::node_ref(g.node("kr-0001"))) ==
          "kr-0001 (decision, shipped)");
    CHECK(apogee::graph::node_label(apogee::graph::node_ref(g.node("json.dumps"))) ==
          "json.dumps (name, unresolved)");
    CHECK(apogee::graph::node_label(apogee::graph::node_ref(g.node("Vault"))) == "Vault (system)");
    EdgeDirection direction = EdgeDirection::Both;
    CHECK(apogee::graph::direction_from_string("in", direction));
    CHECK(direction == EdgeDirection::In);
    CHECK_FALSE(apogee::graph::direction_from_string("sideways", direction));
    CHECK(apogee::graph::to_string(EdgeDirection::Out) == "out");
}
