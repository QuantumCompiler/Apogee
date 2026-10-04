#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "embedstore/graph.h"
#include "embedstore/store.h"
#include "support/env_guard.h"

/// The provenance columns in use: labelled mentions and state rows, the
/// cross-database reconcile (a dead chunk in a member, a vanished source, an
/// ex-member, a missing member), the named graph's identity in graph_meta,
/// the per-member stats, mention refs, and the labelled expansion seeds.
namespace {

using apogee::embedstore::ChunkRef;
using apogee::embedstore::MemberStores;
using apogee::embedstore::ReconcileResult;
using apogee::embedstore::Store;

/// A graph store G over two member stores: docs (a.md: two chunks) and
/// meetings (m.md: one chunk). Atlas is mentioned by a docs chunk and the
/// meetings chunk; Vault by the second docs chunk.
struct Fixture {
    apogee::testing::TempDir dir{"embedstore-graph-multi-" +
                                 std::to_string(std::random_device{}())};
    Store graph{dir.path() / "work.db"};
    Store docs{dir.path() / "docs.db"};
    Store meetings{dir.path() / "meetings.db"};
    std::int64_t docs_a1 = 0;
    std::int64_t docs_a2 = 0;
    std::int64_t meet_m1 = 0;
    std::int64_t atlas = 0;
    std::int64_t vault = 0;

    Fixture() {
        docs.replace_source("a.md", {"Atlas collects readings", "The Vault keeps them"});
        meetings.replace_source("m.md", {"Atlas was discussed on Monday"});
        docs_a1 = docs.chunks_by_source("a.md").front().id;
        docs_a2 = docs.chunks_by_source("a.md").back().id;
        meet_m1 = meetings.chunks_by_source("m.md").front().id;
        atlas = graph.upsert_node("Atlas", "system", "collects readings").id;
        vault = graph.upsert_node("Vault", "system", "").id;
        REQUIRE(graph.add_mention(atlas, "docs", docs_a1));
        REQUIRE(graph.add_mention(atlas, "meetings", meet_m1));
        REQUIRE(graph.add_mention(vault, "docs", docs_a2));
        graph.upsert_edge(atlas, vault, "stores readings in", "");
        graph.set_source_state("docs", "a.md", 2, docs_a2, "m");
        graph.set_source_state("meetings", "m.md", 1, meet_m1, "m");
    }

    [[nodiscard]] MemberStores members() const {
        return MemberStores{{"docs", &docs}, {"meetings", &meetings}};
    }
};

}  // namespace

TEST_CASE("mentions and state rows are keyed by their member collection",
          "[embedstore][graph][multi]") {
    Fixture f;
    // The same chunk id under two labels is two rows: chunk ids mean nothing
    // across databases.
    CHECK(f.graph.add_mention(f.atlas, "docs", 4242));
    CHECK_FALSE(f.graph.add_mention(f.atlas, "docs", 4242));
    CHECK(f.graph.add_mention(f.atlas, "meetings", 4242));
    CHECK(f.graph.nodes_by_ids({f.atlas}).front().mention_count == 4);
    CHECK(f.graph.source_states("docs").size() == 1);
    CHECK(f.graph.source_states("meetings").size() == 1);
    CHECK(f.graph.source_states().empty());  // the '' rows: none in a named graph
    CHECK(f.graph.source_states("docs").at("a.md").chunk_count == 2);
    const std::vector<ChunkRef> refs = f.graph.node_mention_refs(f.atlas, 0);
    REQUIRE(refs.size() == 4);
    CHECK(refs.front().collection == "docs");
    CHECK(refs.back().collection == "meetings");
    CHECK(refs.back().chunk_id == 4242);
    CHECK(f.graph.node_mention_refs(f.atlas, 1).size() == 1);
    // A named graph's own chunk list is empty: the evidence lives elsewhere.
    CHECK(f.graph.node_chunks(f.atlas, 0).empty());
    CHECK(f.docs.chunk_ids_existing({f.docs_a1, f.docs_a2, 9999}) ==
          std::set<std::int64_t>{f.docs_a1, f.docs_a2});
}

TEST_CASE(
    "reconcile_graph_multi prunes per member: a dead chunk, a vanished source, an "
    "ex-member, a missing member",
    "[embedstore][graph][multi][reconcile]") {
    Fixture f;
    // Nothing dead: a no-op.
    CHECK(f.graph.reconcile_graph_multi(f.members()).total() == 0);

    // A re-ingest of a.md in docs: the old chunk ids die, the source stays.
    f.docs.replace_source("a.md", {"Atlas collects readings", "The Vault keeps them"});
    ReconcileResult after = f.graph.reconcile_graph_multi(f.members());
    CHECK(after.mentions_pruned == 2);  // atlas@docs_a1, vault@docs_a2
    CHECK(after.states_pruned == 0);
    CHECK(after.nodes_pruned == 1);  // Vault had no other mention
    CHECK(after.edges_pruned == 1);
    CHECK(f.graph.nodes_by_ids({f.atlas}).front().mention_count == 1);  // meetings remains

    // A member dropped from the map: everything it contributed goes.
    (void)f.graph.add_mention(f.atlas, "docs", f.docs.chunks_by_source("a.md").front().id);
    after = f.graph.reconcile_graph_multi(MemberStores{{"meetings", &f.meetings}});
    CHECK(after.mentions_pruned == 1);
    CHECK(after.states_pruned == 1);  // docs/a.md
    CHECK(f.graph.source_states("docs").empty());
    CHECK(f.graph.nodes_by_ids({f.atlas}).front().mention_count == 1);

    // A missing member (null store) reads as empty: its rows die too, and
    // the node with nothing left is retired.
    after = f.graph.reconcile_graph_multi(MemberStores{{"meetings", nullptr}});
    CHECK(after.mentions_pruned == 1);
    CHECK(after.states_pruned == 1);
    CHECK(after.nodes_pruned == 1);
    CHECK(f.graph.graph_stats().nodes == 0);
}

TEST_CASE("a vanished source in a member prunes its state row", "[embedstore][graph][multi]") {
    Fixture f;
    (void)f.meetings.delete_source("m.md");
    const ReconcileResult after = f.graph.reconcile_graph_multi(f.members());
    CHECK(after.states_pruned == 1);
    CHECK(after.mentions_pruned == 1);
    CHECK(f.graph.source_states("meetings").empty());
    CHECK(f.graph.source_states("docs").size() == 1);
}

TEST_CASE("a named graph records its identity, and delete_graph clears it",
          "[embedstore][graph][multi][meta]") {
    Fixture f;
    CHECK(f.graph.graph_members().empty());
    f.graph.set_graph_meta(apogee::embedstore::kGraphMetaGraphName, "work");
    f.graph.set_graph_members({"meetings", "docs"});
    CHECK(f.graph.graph_meta(apogee::embedstore::kGraphMetaGraphName) == "work");
    CHECK(f.graph.graph_members() == std::vector<std::string>{"docs", "meetings"});  // sorted
    f.graph.delete_graph();
    CHECK(f.graph.graph_meta(apogee::embedstore::kGraphMetaGraphName).empty());
    CHECK(f.graph.graph_members().empty());
}

TEST_CASE("graph_stats_multi sums coverage over the members and breaks it down",
          "[embedstore][graph][multi][stats]") {
    Fixture f;
    f.graph.set_graph_meta(apogee::embedstore::kGraphMetaExtractModel, "m");
    const auto stats = f.graph.graph_stats_multi(f.members());
    CHECK(stats.totals.nodes == 2);
    CHECK(stats.totals.mentions == 3);
    CHECK(stats.totals.total_chunks == 3);
    CHECK(stats.totals.chunks_with_mentions == 3);
    CHECK(stats.totals.stale_files == 0);
    REQUIRE(stats.members.size() == 2);
    CHECK(stats.members.front().collection == "docs");
    CHECK(stats.members.front().mentions == 2);
    CHECK(stats.members.front().total_chunks == 2);
    CHECK(stats.members.front().chunks_with_mentions == 2);
    CHECK_FALSE(stats.members.front().missing);
    CHECK(stats.members.back().collection == "meetings");
    CHECK(stats.members.back().mentions == 1);
    // A re-ingest makes docs stale; a missing member reports zero chunks.
    f.docs.replace_source("a.md", {"Atlas collects readings", "The Vault keeps them"});
    const auto drifted =
        f.graph.graph_stats_multi(MemberStores{{"docs", &f.docs}, {"meetings", nullptr}});
    CHECK(drifted.members.front().stale_files == 1);
    CHECK(drifted.totals.stale_files == 1);
    CHECK(drifted.members.back().missing);
    CHECK(drifted.members.back().total_chunks == 0);
    CHECK(drifted.members.back().mentions == 1);  // rows it still holds
}

TEST_CASE("expansion seeds are labelled: a docs chunk id under the wrong label seeds nothing",
          "[embedstore][graph][multi][expand]") {
    Fixture f;
    const auto right = f.graph.graph_expand_labelled(
        {ChunkRef{.collection = "docs", .chunk_id = f.docs_a1}}, {}, 1, 8);
    REQUIRE(right.entities.size() == 1);
    CHECK(right.entities.front().node.name == "Vault");
    CHECK(right.entities.front().support_collection == "docs");
    CHECK(right.entities.front().support_chunk == f.docs_a2);
    const auto wrong = f.graph.graph_expand_labelled(
        {ChunkRef{.collection = "meetings", .chunk_id = f.docs_a2}}, {}, 1, 8);
    CHECK(wrong.empty());
    // The unlabelled form is the '' label -- nothing in a named graph.
    CHECK(f.graph.graph_expand({f.docs_a1, f.docs_a2}, {}, 1, 8).empty());
}
