#include "embedstore/graph_communities.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "embedstore/store.h"
#include "support/env_guard.h"

/// Community storage: the row, its membership and its pseudo-chunk written
/// together and retrievable through plain search on any retriever; the
/// vector arriving later; replacement by key, the prune sweep, delete_graph
/// taking them along; and the exclusions -- a summary is never planned for
/// extraction, never counted as corpus, never stale.
namespace {

using apogee::embedstore::GraphCommunity;
using apogee::embedstore::GraphNode;
using apogee::embedstore::Store;

struct Fixture {
    apogee::testing::TempDir dir{"embedstore-communities-" +
                                 std::to_string(std::random_device{}())};
    Store store{dir.path() / "c.db"};
    std::int64_t atlas = 0;
    std::int64_t vault = 0;
    std::int64_t probes = 0;

    Fixture() {
        store.replace_source("a.md", {"Atlas collects readings from the field probes"});
        const std::int64_t chunk = store.chunks_by_source("a.md").front().id;
        atlas = store.upsert_node("Atlas", "system", "collects readings").id;
        vault = store.upsert_node("Vault", "system", "the warehouse").id;
        probes = store.upsert_node("Probes", "component", "").id;
        (void)store.add_mention(atlas, chunk);
        (void)store.add_mention(vault, chunk);
        (void)store.add_mention(probes, chunk);
        (void)store.add_mention(atlas, 999);  // Atlas is the most mentioned
        store.upsert_edge(atlas, vault, "stores readings in", "");
        store.upsert_edge(probes, atlas, "feeds", "");
    }
};

}  // namespace

TEST_CASE("a community source is tagged and recognised", "[embedstore][graph][communities]") {
    CHECK(apogee::embedstore::community_source(7) == "graph://community/7");
    CHECK(apogee::embedstore::is_community_source("graph://community/7"));
    CHECK_FALSE(apogee::embedstore::is_community_source("docs/graph.md"));
    CHECK_FALSE(apogee::embedstore::is_community_source(""));
}

TEST_CASE(
    "a community round-trips with its membership and its pseudo-chunk, which plain search "
    "finds",
    "[embedstore][graph][communities]") {
    Fixture f;
    const std::int64_t id =
        f.store.replace_community("1,2,3", {f.atlas, f.vault, f.probes},
                                  "The main themes are Atlas and the warehouse.", "summariser");
    REQUIRE(id > 0);
    const std::vector<GraphCommunity> listed = f.store.graph_communities();
    REQUIRE(listed.size() == 1);
    CHECK(listed.front().id == id);
    CHECK(listed.front().member_key == "1,2,3");
    CHECK(listed.front().size == 3);
    CHECK(listed.front().summary == "The main themes are Atlas and the warehouse.");
    CHECK(listed.front().model == "summariser");
    CHECK_FALSE(listed.front().summarized_at.empty());
    // Members most-mentioned first.
    const std::vector<GraphNode> members = f.store.community_members(id);
    REQUIRE(members.size() == 3);
    CHECK(members.front().name == "Atlas");
    // The summary is an ordinary chunk under its graph:// source: the
    // lexical index sees it at once, with zero new query paths.
    const auto hits = f.store.search("main themes", 5);
    REQUIRE(hits.size() == 1);
    CHECK(hits.front().chunk.source == apogee::embedstore::community_source(id));
    CHECK(hits.front().chunk.text == "The main themes are Atlas and the warehouse.");
    CHECK(f.store.communities_without_vectors() == std::vector<std::int64_t>{id});
    CHECK(f.store.graph_stats().communities == 1);
}

TEST_CASE("a pseudo-chunk is never corpus: excluded from planning, coverage and staleness",
          "[embedstore][graph][communities][exclusion]") {
    Fixture f;
    const std::int64_t id =
        f.store.replace_community("1,2,3", {f.atlas, f.vault, f.probes}, "a summary", "m");
    CHECK(f.store.chunk_count() == 2);               // the store holds it...
    CHECK(f.store.graph_stats().total_chunks == 1);  // ...but it is not corpus
    CHECK_FALSE(f.store.source_chunk_spans().contains(apogee::embedstore::community_source(id)));
    // Staleness counts only real sources: a.md is stale (no state row), the
    // summary is not a source the planner knows.
    CHECK(f.store.graph_stats().stale_files == 1);
    f.store.set_source_state("a.md", 1, f.store.chunks_by_source("a.md").front().id, "");
    CHECK(f.store.graph_stats().stale_files == 0);
}

TEST_CASE("the vector arrives later, and the summary is then reachable by cosine too",
          "[embedstore][graph][communities][vector]") {
    Fixture f;
    const std::int64_t id =
        f.store.replace_community("1,2,3", {f.atlas, f.vault, f.probes}, "a summary", "m");
    f.store.update_community_embedding(id, {1.0F, 0.0F, 0.0F});
    CHECK(f.store.communities_without_vectors().empty());
    const auto hits = f.store.search_vector({1.0F, 0.0F, 0.0F}, 5);
    REQUIRE(hits.size() == 1);
    CHECK(hits.front().chunk.source == apogee::embedstore::community_source(id));
    CHECK(hits.front().chunk.text == "a summary");
    // Still one pseudo-chunk, still listed once, still searchable by text.
    CHECK(f.store.chunk_count() == 2);
    CHECK(f.store.search("summary", 5).size() == 1);
    CHECK_THROWS(f.store.update_community_embedding(9999, {1.0F}));
}

TEST_CASE(
    "replacing by key swaps the row and its chunk; prune sweeps the rest; delete_graph "
    "takes them all",
    "[embedstore][graph][communities][lifecycle]") {
    Fixture f;
    const std::int64_t first = f.store.replace_community("1,2", {f.atlas, f.vault}, "old", "m");
    const std::int64_t second = f.store.replace_community("1,2", {f.atlas, f.vault}, "new", "m");
    CHECK(second != first);
    REQUIRE(f.store.graph_communities().size() == 1);
    CHECK(f.store.graph_communities().front().summary == "new");
    CHECK(f.store.search("old", 5).empty());
    CHECK(f.store.search("new", 5).size() == 1);
    CHECK(f.store.community_members(first).empty());

    const std::int64_t other = f.store.replace_community("3", {f.probes}, "the probe cluster", "m");
    CHECK(f.store.graph_communities().size() == 2);
    CHECK(f.store.prune_communities(std::set<std::string>{"1,2"}) == 1);
    REQUIRE(f.store.graph_communities().size() == 1);
    CHECK(f.store.graph_communities().front().id == second);
    CHECK(f.store.search("cluster", 5).empty());
    CHECK(f.store.community_members(other).empty());
    CHECK(f.store.prune_communities(std::set<std::string>{"1,2"}) == 0);

    f.store.delete_graph();
    CHECK(f.store.graph_communities().empty());
    CHECK(f.store.search("new", 5).empty());
    // The real chunk is untouched.
    CHECK(f.store.chunk_count() == 1);
    CHECK(f.store.chunks_by_source("a.md").size() == 1);
}
