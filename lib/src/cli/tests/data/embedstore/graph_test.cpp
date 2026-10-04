#include "embedstore/graph.h"

#include <catch2/catch_test_macros.hpp>

#include <sqlite3.h>

#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "embedstore/store.h"
#include "support/env_guard.h"

/// Schema v4: the knowledge-graph tables beside the chunks, their migration
/// and self-healing, and the mutation rules -- identity by (normalised name,
/// type), first-non-empty descriptions, the vector cleared on a text change,
/// corroboration versus facts, mention dedup, and the reconcile pass.
namespace {

using apogee::embedstore::GraphNode;
using apogee::embedstore::ReconcileResult;
using apogee::embedstore::Store;
using apogee::embedstore::UpsertResult;

struct Scratch {
    apogee::testing::TempDir dir{"embedstore-graph-" + std::to_string(std::random_device{}())};

    [[nodiscard]] std::filesystem::path db() const {
        return dir.path() / "collection.db";
    }
};

void raw_exec(const std::filesystem::path& path, const char* sql) {
    sqlite3* raw = nullptr;
    REQUIRE(sqlite3_open(path.string().c_str(), &raw) == SQLITE_OK);
    char* message = nullptr;
    const int rc = sqlite3_exec(raw, sql, nullptr, nullptr, &message);
    INFO((message == nullptr ? "" : message));
    sqlite3_free(message);
    sqlite3_close(raw);
    REQUIRE(rc == SQLITE_OK);
}

[[nodiscard]] std::int64_t first_chunk_id(const Store& store, std::string_view source) {
    const std::vector<apogee::embedstore::Chunk> chunks = store.chunks_by_source(source);
    REQUIRE_FALSE(chunks.empty());
    return chunks.front().id;
}

}  // namespace

TEST_CASE("normalize_entity_name folds case and whitespace", "[embedstore][graph]") {
    CHECK(apogee::embedstore::normalize_entity_name("  Atlas   Service ") == "atlas service");
    CHECK(apogee::embedstore::normalize_entity_name("K8s") == "k8s");
    CHECK(apogee::embedstore::normalize_entity_name("\tx\ny") == "x y");
    CHECK(apogee::embedstore::normalize_entity_name("").empty());
}

TEST_CASE("the graph schema is v4, idempotent on reopen, and heals a dropped trigger",
          "[embedstore][graph][schema]") {
    const Scratch scratch;
    {
        Store store{scratch.db()};
        CHECK(store.schema_version() == 5);
        CHECK(store.graph_stats().nodes == 0);
        (void)store.upsert_node("Atlas", "system", "");
    }
    // A second open changes nothing and loses nothing.
    {
        const Store again{scratch.db()};
        CHECK(again.schema_version() == 5);
        CHECK(again.graph_stats().nodes == 1);
    }
    // A dropped trigger comes back on open: an update after the reopen is
    // indexed, so the description is searchable.
    raw_exec(scratch.db(), "DROP TRIGGER kg_nodes_fts_au");
    {
        Store healed{scratch.db()};
        const UpsertResult merged = healed.upsert_node("atlas", "system", "the ingestion pipeline");
        CHECK(merged.mutated);
        const auto hits = healed.search_nodes("ingestion pipeline", 5);
        REQUIRE(hits.size() == 1);
        CHECK(hits.front().node.name == "Atlas");
    }
}

TEST_CASE(
    "upsert_node dedups by normalised name and type, merges descriptions first-non-empty, "
    "and a text change clears the vector",
    "[embedstore][graph][upsert]") {
    const Scratch scratch;
    Store store{scratch.db()};
    const UpsertResult first = store.upsert_node("Atlas", "system", "");
    CHECK(first.mutated);
    store.update_node_embedding(first.id, {1.0F, 2.0F});
    CHECK(store.node_vector(first.id).size() == 2);

    // Same entity, other casing and spacing: merged, the description filled,
    // and the vector cleared because the text it embedded changed.
    const UpsertResult merged = store.upsert_node("  atlas ", "system", "a service");
    CHECK(merged.id == first.id);
    CHECK(merged.mutated);
    CHECK(store.node_vector(first.id).empty());
    store.update_node_embedding(first.id, {3.0F});

    // An existing description is never overwritten, and the vector stays.
    const UpsertResult kept = store.upsert_node("ATLAS", "system", "another sentence");
    CHECK(kept.id == first.id);
    CHECK_FALSE(kept.mutated);
    CHECK(store.node_vector(first.id).size() == 1);
    const std::vector<GraphNode> nodes = store.find_nodes("atlas");
    REQUIRE(nodes.size() == 1);
    CHECK(nodes.front().name == "Atlas");  // the first casing wins
    CHECK(nodes.front().description == "a service");

    // Another type is another node.
    const UpsertResult component = store.upsert_node("Atlas", "component", "");
    CHECK(component.id != first.id);
    CHECK(store.find_nodes("atlas").size() == 2);
    CHECK(store.graph_stats().nodes == 2);
}

TEST_CASE("upsert_edge corroborates while ensure_edge stays a fact at weight 1",
          "[embedstore][graph][edges]") {
    const Scratch scratch;
    Store store{scratch.db()};
    const std::int64_t a = store.upsert_node("A", "system", "").id;
    const std::int64_t b = store.upsert_node("B", "system", "").id;
    store.upsert_edge(a, b, "depends on", "");
    store.upsert_edge(a, b, "depends on", "A calls B");
    std::vector<apogee::embedstore::Neighbor> neighbors = store.node_neighbors(a);
    REQUIRE(neighbors.size() == 1);
    CHECK(neighbors.front().weight == 2);
    CHECK(neighbors.front().description == "A calls B");  // filled, never overwritten
    CHECK(neighbors.front().outgoing);
    store.upsert_edge(a, b, "depends on", "something else");
    CHECK(store.node_neighbors(a).front().description == "A calls B");

    CHECK(store.ensure_edge(b, a, "concerns", ""));
    CHECK_FALSE(store.ensure_edge(b, a, "concerns", ""));
    neighbors = store.node_neighbors(b);
    REQUIRE(neighbors.size() == 2);
    for (const apogee::embedstore::Neighbor& neighbor : neighbors) {
        if (neighbor.relation == "concerns") {
            CHECK(neighbor.weight == 1);
            CHECK(neighbor.outgoing);
        }
    }
    CHECK(store.graph_stats().edges == 2);
}

TEST_CASE("add_mention dedups and maintains the salience counter",
          "[embedstore][graph][mentions]") {
    const Scratch scratch;
    Store store{scratch.db()};
    store.replace_source("a.md", {"one", "two"});
    const std::int64_t chunk = first_chunk_id(store, "a.md");
    const std::int64_t node = store.upsert_node("Atlas", "system", "").id;
    CHECK(store.add_mention(node, chunk));
    CHECK_FALSE(store.add_mention(node, chunk));
    CHECK(store.find_nodes("Atlas").front().mention_count == 1);
    CHECK(store.add_mention(node, chunk + 1));
    CHECK(store.find_nodes("Atlas").front().mention_count == 2);
    CHECK(store.graph_stats().mentions == 2);
}

TEST_CASE("reconcile prunes dead mentions and vanished sources, recounts, and drops orphans",
          "[embedstore][graph][reconcile]") {
    const Scratch scratch;
    Store store{scratch.db()};
    store.replace_source("a.md", {"alpha"});
    store.replace_source("b.md", {"beta"});
    const std::int64_t a_chunk = first_chunk_id(store, "a.md");
    const std::int64_t b_chunk = first_chunk_id(store, "b.md");
    const std::int64_t shared = store.upsert_node("Shared", "concept", "").id;
    const std::int64_t only_a = store.upsert_node("OnlyA", "concept", "").id;
    (void)store.add_mention(shared, a_chunk);
    (void)store.add_mention(shared, b_chunk);
    (void)store.add_mention(only_a, a_chunk);
    store.upsert_edge(only_a, shared, "relates to", "");
    store.set_source_state("a.md", 1, a_chunk, "m");
    store.set_source_state("b.md", 1, b_chunk, "m");

    // Nothing dead: a no-op.
    CHECK(store.reconcile_graph().total() == 0);

    // Re-ingesting a.md replaces its chunk ids; deleting b.md removes its
    // source. The mentions of both are dead, OnlyA loses its only support.
    store.replace_source("a.md", {"alpha again"});
    (void)store.delete_source("b.md");
    const ReconcileResult result = store.reconcile_graph();
    CHECK(result.mentions_pruned == 3);
    CHECK(result.states_pruned == 1);  // b.md vanished; a.md still exists
    CHECK(result.nodes_pruned == 2);
    CHECK(result.edges_pruned == 1);
    CHECK(store.graph_stats().nodes == 0);
    CHECK(store.source_states().count("a.md") == 1);
    CHECK(store.source_states().count("b.md") == 0);
}

TEST_CASE("a chunk id is never reused, so a same-count re-ingest always moves the highest id",
          "[embedstore][graph][ids]") {
    const Scratch scratch;
    Store store{scratch.db()};
    store.replace_source("a.md", {"one", "two"});
    const std::int64_t before = store.source_chunk_spans().at("a.md").max_id;
    store.replace_source("a.md", {"one again", "two again"});
    const std::int64_t after = store.source_chunk_spans().at("a.md").max_id;
    CHECK(after > before);
    CHECK(store.source_chunk_spans().at("a.md").count == 2);
    // Even after every row is gone, the sequence keeps counting up.
    (void)store.delete_source("a.md");
    store.replace_source("a.md", {"back"});
    CHECK(store.source_chunk_spans().at("a.md").max_id > after);
}

TEST_CASE("source states and chunk spans carry the fingerprint", "[embedstore][graph][state]") {
    const Scratch scratch;
    Store store{scratch.db()};
    store.replace_source("a.md", {"one", "two", "three"});
    store.replace_source("b.md", {"solo"});
    const auto spans = store.source_chunk_spans();
    REQUIRE(spans.size() == 2);
    CHECK(spans.at("a.md").count == 3);
    CHECK(spans.at("b.md").count == 1);
    CHECK(spans.at("a.md").max_id == store.chunks_by_source("a.md").back().id);

    store.set_source_state("a.md", 3, spans.at("a.md").max_id, "extractor-1");
    const auto states = store.source_states();
    REQUIRE(states.size() == 1);
    CHECK(states.at("a.md").chunk_count == 3);
    CHECK(states.at("a.md").max_chunk_id == spans.at("a.md").max_id);
    CHECK(states.at("a.md").model == "extractor-1");
    CHECK_FALSE(states.at("a.md").extracted_at.empty());
    // Upserted, not duplicated.
    store.set_source_state("a.md", 4, 99, "extractor-2");
    CHECK(store.source_states().at("a.md").model == "extractor-2");
    CHECK(store.source_states().at("a.md").max_chunk_id == 99);
}

TEST_CASE("delete_graph clears every graph row and leaves the chunks alone",
          "[embedstore][graph][delete]") {
    const Scratch scratch;
    Store store{scratch.db()};
    store.replace_source("a.md", {"alpha"});
    const std::int64_t node = store.upsert_node("Atlas", "system", "desc").id;
    (void)store.add_mention(node, first_chunk_id(store, "a.md"));
    store.set_source_state("a.md", 1, 1, "m");
    store.set_graph_meta("extract_model", "m");
    store.delete_graph();
    const apogee::embedstore::GraphStats stats = store.graph_stats();
    CHECK(stats.nodes == 0);
    CHECK(stats.mentions == 0);
    CHECK(stats.extract_model.empty());
    CHECK(store.source_states().empty());
    CHECK(store.chunk_count() == 1);
    CHECK(store.search_nodes("Atlas", 5).empty());  // the FTS index followed
}

TEST_CASE("graph meta round-trips and reads empty when unset", "[embedstore][graph][meta]") {
    const Scratch scratch;
    Store store{scratch.db()};
    CHECK(store.graph_meta("extract_model").empty());
    store.set_graph_meta("extract_model", "one");
    store.set_graph_meta("extract_model", "two");
    CHECK(store.graph_meta("extract_model") == "two");
}

TEST_CASE("the entity index never raises on hostile input", "[embedstore][graph][fts]") {
    const Scratch scratch;
    Store store{scratch.db()};
    (void)store.upsert_node("Atlas", "system", "stores readings");
    for (const char* query : {"AND", "NEAR", "\"unbalanced", "text:", "*", "(",
                              "'; DROP TABLE kg_nodes; --", "   ", "atlas OR vault"}) {
        CAPTURE(query);
        CHECK_NOTHROW(store.search_nodes(query, 5));
    }
    CHECK(store.search_nodes("readings", 5).size() == 1);
    CHECK(store.search_nodes("readings", 5).front().score > 0.0);
    CHECK(store.search_nodes("readings", 5).front().score < 1.0);
}

TEST_CASE(
    "upsert_decision_node replaces the description and metadata, clearing the vector only "
    "on a text change",
    "[embedstore][graph][decision]") {
    const Scratch scratch;
    Store store{scratch.db()};
    const UpsertResult first =
        store.upsert_decision_node("kr-1", "drop it — why", "{\"status\":\"shipped\"}");
    CHECK(first.mutated);
    store.update_node_embedding(first.id, {1.0F});
    // Metadata alone: replaced, not mutated, vector kept.
    const UpsertResult meta =
        store.upsert_decision_node("kr-1", "drop it — why", "{\"status\":\"superseded\"}");
    CHECK(meta.id == first.id);
    CHECK_FALSE(meta.mutated);
    CHECK(store.node_vector(first.id).size() == 1);
    CHECK(store.find_nodes("kr-1").front().metadata == "{\"status\":\"superseded\"}");
    // The text: replaced (never merged), mutated, vector cleared.
    const UpsertResult text =
        store.upsert_decision_node("kr-1", "keep it — a new why", "{\"status\":\"superseded\"}");
    CHECK(text.id == first.id);
    CHECK(text.mutated);
    CHECK(store.node_vector(first.id).empty());
    CHECK(store.find_nodes("kr-1").front().description == "keep it — a new why");
    CHECK(store.find_nodes("kr-1").front().type == apogee::embedstore::kNodeTypeDecision);
}

TEST_CASE("a decision node's metadata is its own JSON, and anything else reads as not a record",
          "[embedstore][graph][decision]") {
    const std::string json = apogee::embedstore::decision_node_metadata_json("shipped", "ux");
    const apogee::embedstore::DecisionNodeMetadata meta =
        apogee::embedstore::parse_decision_node_metadata(json);
    CHECK(meta.status == "shipped");
    CHECK(meta.discipline == "ux");
    CHECK(apogee::embedstore::parse_decision_node_metadata("").status.empty());
    CHECK(apogee::embedstore::parse_decision_node_metadata("{\"status\": \"shipped\"}")
              .status.empty());
    CHECK(apogee::embedstore::parse_decision_node_metadata("not json").status.empty());
    CHECK(apogee::embedstore::decision_node_metadata_json("rejected", "").find("discipline") ==
          std::string::npos);
}
