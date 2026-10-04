#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "embedstore/graph.h"
#include "embedstore/store.h"
#include "graph/build.h"
#include "knowledge/record.h"
#include "knowledge/store.h"
#include "support/env_guard.h"

/// Knowledge records as first-class nodes, seeded through `knowledge::Store`
/// so the chunk shape is production's: the reserved `decision` type, the
/// `concerns` and `supersedes` edges, the idempotent rebuild, the status
/// refresh, retirement with the record, re-materialisation on the new chunk,
/// the extractor never forging one, and description-only embedding.
namespace {

using apogee::embedstore::Store;
using apogee::graph::BuildOptions;
using apogee::graph::BuildResult;
using apogee::graph::Entity;
using apogee::graph::ExtractOutcome;
using apogee::graph::ExtractResult;
using apogee::knowledge::Record;

struct Scratch {
    apogee::testing::TempDir dir{"graph-records-" + std::to_string(std::random_device{}())};
    apogee::knowledge::Store records{dir.path() / "knowledge.db", dir.path() / "raw"};
};

Record record(std::string id, std::string decision, std::string intent, std::string status,
              std::string supersedes = {}) {
    Record out;
    out.id = std::move(id);
    out.decision = std::move(decision);
    out.intent = std::move(intent);
    out.status = std::move(status);
    out.discipline = "ux";
    out.provenance.source = "chat";
    out.timestamp = apogee::knowledge::timestamp_for(std::chrono::system_clock::now());
    out.supersedes = std::move(supersedes);
    return out;
}

/// The extractor pulls "cancel button" from any text that mentions it, and
/// emits a `decision` entity from a text that asks for one -- to prove it is
/// dropped.
apogee::graph::ExtractFn extractor(int* calls = nullptr) {
    return [calls](std::string_view text, const apogee::harness::CancellationToken&) {
        if (calls != nullptr) {
            ++*calls;
        }
        ExtractOutcome outcome;
        ExtractResult result;
        if (text.find("cancel button") != std::string_view::npos) {
            result.entities.push_back(Entity{
                .name = "cancel button", .type = "component", .description = "a checkout control"});
        }
        if (text.find("FORGE") != std::string_view::npos) {
            result.entities.push_back(
                Entity{.name = "kr-forged", .type = "decision", .description = "forged"});
        }
        outcome.result = std::move(result);
        return outcome;
    };
}

BuildOptions opts() {
    BuildOptions options;
    options.model = "m";
    return options;
}

[[nodiscard]] std::map<std::string, std::string> relations_of(const Store& store, std::int64_t id) {
    std::map<std::string, std::string> out;
    for (const apogee::embedstore::Neighbor& neighbor : store.node_neighbors(id)) {
        out[neighbor.relation] = neighbor.peer_name + (neighbor.outgoing ? " out" : " in") + " x" +
                                 std::to_string(neighbor.weight);
    }
    return out;
}

}  // namespace

TEST_CASE("decision_node_description is decision then intent, clipped to 400 codepoints",
          "[graph][records][description]") {
    CHECK(apogee::graph::decision_node_description(record(
              "kr-1", "drop it", "testers were lost", "shipped")) == "drop it — testers were lost");
    CHECK(apogee::graph::decision_node_description(record("kr-1", "", "only why", "shipped")) ==
          "only why");
    CHECK(apogee::graph::decision_node_description(record("kr-1", "only what", "", "shipped")) ==
          "only what");
    const std::string long_intent(500, 'x');
    const std::string clipped =
        apogee::graph::decision_node_description(record("kr-1", "", long_intent, "shipped"));
    CHECK(clipped.ends_with("…"));
    CHECK(clipped.size() < long_intent.size());
    // 399 x's and the ellipsis: 400 codepoints.
    CHECK(clipped == std::string(399, 'x') + "…");
    // Multi-byte text is clipped by codepoint, never mid-character.
    std::string accented;
    for (int i = 0; i < 500; ++i) {
        accented += "é";
    }
    const std::string clipped_accented =
        apogee::graph::decision_node_description(record("kr-1", "", accented, "shipped"));
    CHECK(clipped_accented.ends_with("…"));
    CHECK(clipped_accented.size() == 399 * 2 + 3);
}

TEST_CASE(
    "a build materialises every record as a decision node with its edges, and a missing "
    "supersedes target is skipped without a placeholder",
    "[graph][records][materialize]") {
    Scratch scratch;
    Record old = record("kr-old", "keep the cancel button", "it was always there", "superseded");
    Record fresh = record("kr-new", "drop the cancel button", "testers mistook it for back",
                          "shipped", "kr-old");
    Record dangling = record("kr-dangling", "unrelated", "supersedes a record that is gone",
                             "shipped", "kr-gone");
    scratch.records.put(old, {}, "");
    scratch.records.put(fresh, {}, "");
    scratch.records.put(dangling, {}, "");
    Store& store = scratch.records.chunks();
    store.replace_source("docs.md", {"The cancel button sits on the checkout page."});

    const BuildResult result = apogee::graph::build(store, extractor(), nullptr, opts());
    CHECK(result.record_nodes == 3);
    CHECK(result.supersedes_edges == 1);
    CHECK(result.supersedes_skipped == 1);
    const apogee::embedstore::GraphStats stats = store.graph_stats();
    CHECK(stats.nodes_by_type.at("decision") == 3);
    CHECK(stats.nodes_by_type.at("component") == 1);
    CHECK(stats.nodes == 4);  // no placeholder for kr-gone
    CHECK(store.find_nodes("kr-gone").empty());

    const apogee::embedstore::GraphNode node = store.find_nodes("kr-new").front();
    CHECK(node.type == apogee::embedstore::kNodeTypeDecision);
    CHECK(node.description == "drop the cancel button — testers mistook it for back");
    CHECK(node.mention_count == 1);
    const apogee::embedstore::DecisionNodeMetadata meta =
        apogee::embedstore::parse_decision_node_metadata(node.metadata);
    CHECK(meta.status == "shipped");
    CHECK(meta.discipline == "ux");
    // Its one mention is the record's own chunk.
    CHECK(store.node_chunks(node.id, 0).size() == 1);
    CHECK(store.node_chunks(node.id, 0).front().id == *scratch.records.chunk_id("kr-new"));
    // concerns -> the entity extracted from its own text; supersedes -> kr-old.
    const auto relations = relations_of(store, node.id);
    CHECK(relations.at("concerns") == "cancel button out x1");
    CHECK(relations.at("supersedes") == "kr-old out x1");
    // The docs chunk reaches the decision through the shared entity.
    const std::int64_t button = store.find_nodes("cancel button").front().id;
    CHECK(store.find_nodes("cancel button").front().mention_count == 3);
    const apogee::embedstore::Expansion from_docs =
        store.graph_expand({store.chunks_by_source("docs.md").front().id}, {}, 1, 8);
    bool reached = false;
    for (const apogee::embedstore::ExpandEntity& entity : from_docs.entities) {
        reached = reached || entity.node.name == "kr-new";
    }
    CHECK(reached);
    (void)button;
}

TEST_CASE(
    "a rebuild is idempotent, deterministic edges stay at weight 1, and a status edit is "
    "refreshed without a re-embed",
    "[graph][records][rebuild]") {
    Scratch scratch;
    Record old = record("kr-old", "keep the cancel button", "it was always there", "shipped");
    Record fresh = record("kr-new", "drop the cancel button", "testers mistook it for back",
                          "shipped", "kr-old");
    scratch.records.put(old, {}, "");
    scratch.records.put(fresh, {}, "");
    Store& store = scratch.records.chunks();
    std::vector<std::string> embedded;
    const apogee::graph::EmbedFn embed = [&embedded](std::string_view text,
                                                     const apogee::harness::CancellationToken&) {
        embedded.emplace_back(text);
        return std::vector<float>{1.0F};
    };
    const BuildResult first = apogee::graph::build(store, extractor(), embed, opts());
    CHECK(first.nodes_upserted == 3);
    // Decision nodes embed by description alone -- the id carries no meaning.
    CHECK(std::find(embedded.begin(), embedded.end(),
                    "drop the cancel button — testers mistook it for back") != embedded.end());
    CHECK(std::find(embedded.begin(), embedded.end(), "cancel button: a checkout control") !=
          embedded.end());
    for (const std::string& text : embedded) {
        CHECK(text.find("kr-") == std::string::npos);
    }
    embedded.clear();

    const BuildResult second = apogee::graph::build(store, extractor(), embed, opts());
    CHECK(second.files_planned == 0);
    CHECK(second.record_nodes == 2);
    CHECK(second.nodes_upserted == 0);
    CHECK(second.edges_upserted == 0);
    CHECK(embedded.empty());
    CHECK(store.graph_stats().nodes == 3);
    const std::int64_t node = store.find_nodes("kr-new").front().id;
    CHECK(relations_of(store, node).at("supersedes") == "kr-old out x1");
    CHECK(relations_of(store, node).at("concerns") == "cancel button out x1");

    // A forced rebuild re-derives the deterministic edges and re-extracts
    // the chunks: the facts stay at weight 1 while the extracted relation
    // corroborates.
    BuildOptions forced = opts();
    forced.force = true;
    const BuildResult again = apogee::graph::build(store, extractor(), embed, forced);
    CHECK(again.files_planned == 2);
    CHECK(relations_of(store, node).at("supersedes") == "kr-old out x1");
    CHECK(relations_of(store, node).at("concerns") == "cancel button out x1");
    CHECK(store.find_nodes("cancel button").front().mention_count == 2);  // mentions dedup

    // The record's status changes -- an edit that churns no chunk. The next
    // build refreshes the node's metadata; the description and its vector
    // are untouched, so nothing is re-embedded.
    REQUIRE(scratch.records.supersede("kr-old").has_value());
    const BuildResult third = apogee::graph::build(store, extractor(), embed, opts());
    CHECK(third.nodes_upserted == 0);
    CHECK(embedded.empty());
    const apogee::embedstore::GraphNode refreshed = store.find_nodes("kr-old").front();
    CHECK(apogee::embedstore::parse_decision_node_metadata(refreshed.metadata).status ==
          "superseded");
    CHECK(refreshed.dim == 1);
}

TEST_CASE(
    "a deleted record's node is retired by reconcile, and a re-put record is re-materialised "
    "on its new chunk",
    "[graph][records][lifecycle]") {
    Scratch scratch;
    Record fresh =
        record("kr-new", "drop the cancel button", "testers mistook it for back", "shipped");
    scratch.records.put(fresh, {}, "");
    Store& store = scratch.records.chunks();
    (void)apogee::graph::build(store, extractor(), nullptr, opts());
    const std::int64_t old_chunk = *scratch.records.chunk_id("kr-new");
    REQUIRE(store.graph_stats().nodes == 2);

    // Re-put: the chunk id moves (a same-count re-ingest of the record's
    // source), the fingerprint catches it, the node lands on the new chunk.
    scratch.records.put(fresh, {}, "");
    const std::int64_t new_chunk = *scratch.records.chunk_id("kr-new");
    REQUIRE(new_chunk != old_chunk);
    const BuildResult rebuilt = apogee::graph::build(store, extractor(), nullptr, opts());
    CHECK(rebuilt.reconcile.nodes_pruned == 2);
    CHECK(rebuilt.files_planned == 1);
    CHECK(rebuilt.record_nodes == 1);
    const apogee::embedstore::GraphNode node = store.find_nodes("kr-new").front();
    CHECK(store.node_chunks(node.id, 0).front().id == new_chunk);
    CHECK(store.graph_stats().nodes == 2);

    // Delete: the chunk is gone, so the node goes with the next reconcile.
    REQUIRE(scratch.records.remove("kr-new"));
    const BuildResult after = apogee::graph::build(store, extractor(), nullptr, opts());
    CHECK(after.reconcile.nodes_pruned == 2);
    CHECK(after.record_nodes == 0);
    CHECK(store.graph_stats().nodes == 0);
}

TEST_CASE("an extractor-emitted decision is dropped; a dry run only counts the records",
          "[graph][records][reserved]") {
    Scratch scratch;
    Record fresh =
        record("kr-new", "drop the cancel button", "FORGE testers mistook it for back", "shipped");
    scratch.records.put(fresh, {}, "");
    Store& store = scratch.records.chunks();
    store.replace_source("docs.md", {"FORGE: we decided to keep the cancel button"});

    BuildOptions dry = opts();
    dry.dry_run = true;
    const BuildResult counted = apogee::graph::build(store, extractor(), nullptr, dry);
    CHECK(counted.record_nodes == 1);
    CHECK(store.graph_stats().nodes == 0);

    const BuildResult built = apogee::graph::build(store, extractor(), nullptr, opts());
    CHECK(built.record_nodes == 1);
    CHECK(store.graph_stats().nodes_by_type.at("decision") == 1);  // only the materialised one
    CHECK(store.find_nodes("kr-forged").empty());
}
