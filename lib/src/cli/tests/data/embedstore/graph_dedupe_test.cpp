#include "embedstore/graph_dedupe.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "embedstore/store.h"
#include "support/env_guard.h"

/// Vector dedup: a dry run writes nothing; the earliest node survives; edges
/// fold to a summed weight with the description merged, self-loops drop,
/// mentions union and recount; cross-type namesakes never merge; the
/// threshold is respected; decision nodes are skipped while an entity
/// control pair merges in the same run; a second run is a no-op.
namespace {

using apogee::embedstore::GraphNode;
using apogee::embedstore::MergeGroup;
using apogee::embedstore::Store;

struct Fixture {
    apogee::testing::TempDir dir{"embedstore-dedupe-" + std::to_string(std::random_device{}())};
    Store store{dir.path() / "c.db"};
    std::int64_t chunk1 = 0;
    std::int64_t chunk2 = 0;

    Fixture() {
        store.replace_source("a.md", {"first", "second"});
        chunk1 = store.chunks_by_source("a.md").front().id;
        chunk2 = store.chunks_by_source("a.md").back().id;
    }

    std::int64_t node(const std::string& name, const std::string& type,
                      const std::string& description, std::vector<float> vector,
                      std::vector<std::int64_t> chunks) {
        const std::int64_t id = store.upsert_node(name, type, description).id;
        if (!vector.empty()) {
            store.update_node_embedding(id, vector);
        }
        for (const std::int64_t chunk : chunks) {
            (void)store.add_mention(id, chunk);
        }
        return id;
    }

    [[nodiscard]] std::vector<std::string> names() const {
        std::vector<std::string> out;
        for (const GraphNode& node : store.nodes_by_ids({1, 2, 3, 4, 5, 6, 7, 8, 9, 10})) {
            out.push_back(node.name);
        }
        return out;
    }
};

}  // namespace

TEST_CASE(
    "the earliest node survives: edges repoint and fold, self-loops drop, mentions union, "
    "the description merges and clears the vector",
    "[embedstore][graph][dedupe]") {
    Fixture f;
    const std::int64_t k8s = f.node("K8s", "system", "", {1.0F, 0.0F, 0.0F, 0.0F}, {f.chunk1});
    const std::int64_t kubernetes = f.node("Kubernetes", "system", "an orchestrator",
                                           {0.99F, 0.1F, 0.0F, 0.0F}, {f.chunk1, f.chunk2});
    const std::int64_t vault = f.node("Vault", "system", "", {0.0F, 1.0F, 0.0F, 0.0F}, {f.chunk2});
    const std::int64_t ledger = f.node("Ledger", "artifact", "", {}, {f.chunk2});
    f.store.upsert_edge(k8s, vault, "runs on", "");
    f.store.upsert_edge(kubernetes, vault, "runs on", "the cluster runs on the vault");
    f.store.upsert_edge(kubernetes, vault, "runs on", "");  // weight 2
    f.store.upsert_edge(kubernetes, k8s, "alias of", "");   // would become a self-loop
    f.store.upsert_edge(kubernetes, ledger, "stores", "");  // repointed

    // Dry run: the groups, and nothing changed.
    const std::vector<MergeGroup> preview = f.store.dedupe_nodes(0.92, /*dry_run=*/true);
    REQUIRE(preview.size() == 1);
    CHECK(preview.front().kept.name == "K8s");
    REQUIRE(preview.front().merged.size() == 1);
    CHECK(preview.front().merged.front().name == "Kubernetes");
    CHECK(f.store.graph_stats().nodes == 4);
    CHECK(f.store.graph_stats().edges == 4);  // the corroborated pair is one row
    CHECK(f.store.find_nodes("kubernetes").size() == 1);

    const std::vector<MergeGroup> applied = f.store.dedupe_nodes(0.92, /*dry_run=*/false);
    REQUIRE(applied.size() == 1);
    CHECK(f.store.find_nodes("kubernetes").empty());
    CHECK(f.store.graph_stats().nodes == 3);
    const GraphNode kept = f.store.nodes_by_ids({k8s}).front();
    CHECK(kept.description == "an orchestrator");  // first non-empty wins
    CHECK(kept.dim == 0);                          // the text changed: the vector is cleared
    CHECK(kept.mention_count == 2);                // chunk1 (both) and chunk2 (Kubernetes)
    // K8s -> Vault folded to weight 3 with the description merged; the
    // alias edge dropped; the ledger edge repointed.
    const auto neighbors = f.store.node_neighbors(k8s);
    REQUIRE(neighbors.size() == 2);
    bool saw_vault = false;
    bool saw_ledger = false;
    for (const auto& neighbor : neighbors) {
        if (neighbor.peer_name == "Vault") {
            saw_vault = true;
            CHECK(neighbor.weight == 3);
            CHECK(neighbor.description == "the cluster runs on the vault");
        }
        if (neighbor.peer_name == "Ledger") {
            saw_ledger = true;
            CHECK(neighbor.relation == "stores");
        }
    }
    CHECK(saw_vault);
    CHECK(saw_ledger);
    CHECK(f.store.graph_stats().edges == 2);
    // The entity index followed the delete.
    CHECK(f.store.search_nodes("kubernetes", 5).empty());
    CHECK(f.store.search_nodes("orchestrator", 5).size() == 1);
    // A second run merges nothing: idempotent.
    CHECK(f.store.dedupe_nodes(0.92, false).empty());
}

TEST_CASE("namesakes of different types never merge, and the threshold is respected",
          "[embedstore][graph][dedupe][threshold]") {
    Fixture f;
    (void)f.node("Atlas", "system", "", {0.0F, 0.0F, 1.0F, 0.0F}, {f.chunk1});
    (void)f.node("Atlas", "person", "", {0.0F, 0.0F, 1.0F, 0.0F}, {f.chunk1});
    CHECK(f.store.dedupe_nodes(0.5, true).empty());

    (void)f.node("Vault", "system", "", {1.0F, 0.0F, 0.0F, 0.0F}, {f.chunk2});
    (void)f.node("Warehouse", "system", "", {0.7F, 0.7F, 0.0F, 0.0F}, {f.chunk2});  // ~0.707
    CHECK(f.store.dedupe_nodes(0.92, true).empty());
    const std::vector<MergeGroup> looser = f.store.dedupe_nodes(0.7, true);
    REQUIRE(looser.size() == 1);
    CHECK(looser.front().kept.name == "Vault");
    CHECK(looser.front().merged.front().name == "Warehouse");
    // Nodes without a vector are never considered, whatever the threshold --
    // even one no surface would pass, where an empty vector's zero cosine
    // would otherwise clear it.
    (void)f.node("Vault Two", "system", "", {}, {f.chunk2});
    CHECK(f.store.dedupe_nodes(0.0001, true).size() == 1);
    const std::vector<MergeGroup> floor = f.store.dedupe_nodes(0.0, true);
    for (const MergeGroup& group : floor) {
        CHECK(group.kept.name != "Vault Two");
        for (const GraphNode& node : group.merged) {
            CHECK(node.name != "Vault Two");
        }
    }
}

TEST_CASE("decision nodes are never merged, even beside an entity pair that is",
          "[embedstore][graph][dedupe][decision]") {
    Fixture f;
    const std::int64_t a = f.store.upsert_decision_node("kr-1", "drop the button", "{}").id;
    const std::int64_t b = f.store.upsert_decision_node("kr-2", "drop the button", "{}").id;
    f.store.update_node_embedding(a, {1.0F, 0.0F});
    f.store.update_node_embedding(b, {1.0F, 0.0F});
    (void)f.store.add_mention(a, f.chunk1);
    (void)f.store.add_mention(b, f.chunk2);
    const std::int64_t x = f.node("Cancel Button", "component", "", {0.0F, 1.0F}, {f.chunk1});
    const std::int64_t y = f.node("The cancel button", "component", "", {0.0F, 1.0F}, {f.chunk2});
    // Membership rows for the merged node are derived and go with it.
    (void)f.store.replace_community("k", {a, b, x, y}, "a summary", "m");

    const std::vector<MergeGroup> groups = f.store.dedupe_nodes(0.92, false);
    REQUIRE(groups.size() == 1);
    CHECK(groups.front().kept.id == x);
    CHECK(groups.front().merged.front().id == y);
    CHECK(f.store.find_nodes("kr-1").size() == 1);
    CHECK(f.store.find_nodes("kr-2").size() == 1);
    CHECK(f.store.find_nodes("the cancel button").empty());
    const auto members = f.store.community_members(f.store.graph_communities().front().id);
    CHECK(members.size() == 3);
    for (const GraphNode& member : members) {
        CHECK(member.id != y);
    }
}
