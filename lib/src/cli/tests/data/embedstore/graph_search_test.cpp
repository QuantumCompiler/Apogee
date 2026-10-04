#include "embedstore/graph_search.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "embedstore/store.h"
#include "support/env_guard.h"

/// The read side: stats, lookup, neighbourhoods, and the expansion walk --
/// exact SQL over mentions and edges, ranked by connecting weight times
/// mention count, capped, with the hop-0 seeds listed and the chunk seeds
/// not re-listed.
namespace {

using apogee::embedstore::Expansion;
using apogee::embedstore::GraphNode;
using apogee::embedstore::GraphStats;
using apogee::embedstore::Store;

/// A small graph: chunk 1 mentions Atlas; chunk 2 mentions Vault and Ledger;
/// chunk 3 mentions Ledger again. Atlas -> Vault (weight 2), Vault -> Ledger,
/// Ledger -> Archive, and Archive is mentioned by chunk 3 too.
struct Graph {
    apogee::testing::TempDir dir{"embedstore-graph-search-" +
                                 std::to_string(std::random_device{}())};
    Store store{dir.path() / "c.db"};
    std::int64_t chunk1 = 0;
    std::int64_t chunk2 = 0;
    std::int64_t chunk3 = 0;
    std::int64_t atlas = 0;
    std::int64_t vault = 0;
    std::int64_t ledger = 0;
    std::int64_t archive = 0;

    Graph() {
        store.replace_source("a.md", {"Atlas collects readings from the field probes"});
        store.replace_source("b.md", {"The Vault keeps every ledger entry", "Ledger totals"});
        chunk1 = store.chunks_by_source("a.md").front().id;
        chunk2 = store.chunks_by_source("b.md").front().id;
        chunk3 = store.chunks_by_source("b.md").back().id;
        atlas = store.upsert_node("Atlas", "system", "collects readings").id;
        vault = store.upsert_node("Vault", "system", "the warehouse Atlas stores readings in").id;
        ledger = store.upsert_node("Ledger", "artifact", "").id;
        archive = store.upsert_node("Archive", "artifact", "cold storage").id;
        (void)store.add_mention(atlas, chunk1);
        (void)store.add_mention(vault, chunk2);
        (void)store.add_mention(ledger, chunk2);
        (void)store.add_mention(ledger, chunk3);
        (void)store.add_mention(archive, chunk3);
        store.upsert_edge(atlas, vault, "stores readings in", "Atlas writes to Vault");
        store.upsert_edge(atlas, vault, "stores readings in", "");
        store.upsert_edge(vault, ledger, "keeps", "");
        store.upsert_edge(ledger, archive, "is archived to", "");
    }
};

[[nodiscard]] std::vector<std::string> names(const Expansion& expansion) {
    std::vector<std::string> out;
    for (const apogee::embedstore::ExpandEntity& entity : expansion.entities) {
        out.push_back(entity.node.name);
    }
    return out;
}

}  // namespace

TEST_CASE("graph_stats counts nodes, edges, mentions, types, coverage, and staleness",
          "[embedstore][graph][search][stats]") {
    Graph g;
    GraphStats stats = g.store.graph_stats();
    CHECK(stats.nodes == 4);
    CHECK(stats.edges == 3);
    CHECK(stats.mentions == 5);
    CHECK(stats.nodes_by_type.at("system") == 2);
    CHECK(stats.nodes_by_type.at("artifact") == 2);
    CHECK(stats.nodes_with_vectors == 0);
    CHECK(stats.total_chunks == 3);
    CHECK(stats.chunks_with_mentions == 3);
    CHECK(stats.stale_files == 2);  // no state rows yet
    CHECK(stats.extract_model.empty());
    CHECK(stats.built());

    g.store.set_graph_meta("extract_model", "m");
    g.store.set_graph_meta("failed_chunks", "2");
    const auto spans = g.store.source_chunk_spans();
    g.store.set_source_state("a.md", 1, spans.at("a.md").max_id, "m");
    g.store.set_source_state("b.md", 2, spans.at("b.md").max_id, "other-model");
    g.store.update_node_embedding(g.atlas, {1.0F, 0.0F});
    stats = g.store.graph_stats();
    CHECK(stats.stale_files == 1);  // b.md: another model
    CHECK(stats.failed_chunks == 2);
    CHECK(stats.extract_model == "m");
    CHECK(stats.nodes_with_vectors == 1);
    // A moved fingerprint is stale too, unless the row is legacy (max id 0).
    g.store.set_source_state("b.md", 2, spans.at("b.md").max_id + 5, "m");
    CHECK(g.store.graph_stats().stale_files == 1);
    g.store.set_source_state("b.md", 2, 0, "m");
    CHECK(g.store.graph_stats().stale_files == 0);
    CHECK_FALSE(Store{g.dir.path() / "empty.db"}.graph_stats().built());
}

TEST_CASE("find_nodes is exact by normalised name; search_nodes ranks partials; ids load in order",
          "[embedstore][graph][search][lookup]") {
    const Graph g;
    const std::vector<GraphNode> exact = g.store.find_nodes("  VAULT ");
    REQUIRE(exact.size() == 1);
    CHECK(exact.front().id == g.vault);
    CHECK(g.store.find_nodes("vaults").empty());

    const auto partial = g.store.search_nodes("warehouse readings", 5);
    REQUIRE_FALSE(partial.empty());
    CHECK(partial.front().node.name == "Vault");
    CHECK(g.store.search_nodes("nothing here matches", 5).empty());
    CHECK(g.store.search_nodes("readings", 1).size() == 1);
    CHECK(g.store.search_nodes("readings", 0).size() == 2);

    const std::vector<GraphNode> loaded = g.store.nodes_by_ids({g.ledger, g.atlas, 9999});
    REQUIRE(loaded.size() == 2);
    CHECK(loaded.front().id == g.atlas);
    CHECK(loaded.back().id == g.ledger);
    CHECK(g.store.nodes_by_ids({}).empty());
}

TEST_CASE("node_neighbors reports both directions by relation then weight; node_chunks is capped",
          "[embedstore][graph][search][neighbors]") {
    const Graph g;
    const auto vault = g.store.node_neighbors(g.vault);
    REQUIRE(vault.size() == 2);
    // Ordered by relation: "keeps" before "stores readings in".
    CHECK(vault[0].relation == "keeps");
    CHECK(vault[0].outgoing);
    CHECK(vault[0].peer_name == "Ledger");
    CHECK(vault[1].relation == "stores readings in");
    CHECK_FALSE(vault[1].outgoing);
    CHECK(vault[1].peer_name == "Atlas");
    CHECK(vault[1].weight == 2);
    CHECK(vault[1].description == "Atlas writes to Vault");

    CHECK(g.store.node_chunks(g.ledger, 0).size() == 2);
    CHECK(g.store.node_chunks(g.ledger, 1).size() == 1);
    CHECK(g.store.node_chunks(g.ledger, 1).front().id == g.chunk2);
    CHECK(g.store.node_chunks(9999, 0).empty());
}

TEST_CASE("graph_expand walks one or two hops from chunk seeds, ranks, caps, and lists edges",
          "[embedstore][graph][search][expand]") {
    const Graph g;
    // From chunk 1 (Atlas): one hop reaches Vault only. Atlas itself is not
    // re-listed -- its text is already in the injected chunk.
    Expansion one = g.store.graph_expand({g.chunk1}, {}, 1, 8);
    CHECK(names(one) == std::vector<std::string>{"Vault"});
    CHECK(one.entities.front().hop == 1);
    CHECK(one.entities.front().score == 2 * 1);  // weight 2 x one mention
    CHECK(one.entities.front().support_chunk == g.chunk2);
    REQUIRE(one.edges.size() == 1);
    CHECK(one.edges.front().source_name == "Atlas");
    CHECK(one.edges.front().relation == "stores readings in");
    CHECK(one.edges.front().weight == 2);

    // Two hops add Ledger (hop 2), never Archive (hop 3).
    Expansion two = g.store.graph_expand({g.chunk1}, {}, 2, 8);
    CHECK(names(two) == std::vector<std::string>{"Vault", "Ledger"});
    CHECK(two.entities.back().hop == 2);
    CHECK(two.entities.back().score == 1 * 2);  // weight 1 x two mentions
    CHECK(two.edges.size() == 2);

    // Hops are clamped, the cap holds, and ranking is hop then score.
    CHECK(names(g.store.graph_expand({g.chunk1}, {}, 7, 8)) == names(two));
    CHECK(g.store.graph_expand({g.chunk1}, {}, 2, 1).entities.size() == 1);
    CHECK(g.store.graph_expand({g.chunk1}, {}, 0, 0).entities.size() == 1);  // the defaults

    // From chunk 3 (Ledger, Archive): Vault is reached from Ledger; Archive
    // is a seed and not re-listed.
    Expansion from3 = g.store.graph_expand({g.chunk3}, {}, 1, 8);
    CHECK(names(from3) == std::vector<std::string>{"Vault"});
    // The edges among the neighbourhood include the seeds' own relation.
    bool ledger_archive = false;
    for (const apogee::embedstore::ExpandEdge& edge : from3.edges) {
        ledger_archive =
            ledger_archive || (edge.source_name == "Ledger" && edge.target_name == "Archive");
    }
    CHECK(ledger_archive);

    // Empty seeds: an empty expansion, never an error.
    CHECK(g.store.graph_expand({}, {}, 1, 8).empty());
    CHECK(g.store.graph_expand({9999}, {}, 1, 8).empty());
}

TEST_CASE("hop-0 seed nodes are listed themselves and ranked by mentions",
          "[embedstore][graph][search][expand][lexical]") {
    const Graph g;
    // A query-term hit on Ledger, with no chunk seeds: Ledger is listed at
    // hop 0 (nothing else carries its description), then its neighbours.
    const Expansion seeded = g.store.graph_expand({}, {g.ledger}, 1, 8);
    REQUIRE(seeded.entities.size() == 3);
    CHECK(seeded.entities[0].node.name == "Ledger");
    CHECK(seeded.entities[0].hop == 0);
    CHECK(seeded.entities[0].score == 2);
    // Hop-1 neighbours sorted by score: Archive (1x1) and Vault (1x1) tie,
    // then by name.
    CHECK(seeded.entities[1].hop == 1);
    CHECK(seeded.entities[1].node.name == "Archive");
    CHECK(seeded.entities[2].node.name == "Vault");
}
