#include "embedstore/graph_code.h"

#include <catch2/catch_test_macros.hpp>

#include <sqlite3.h>

#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "embedstore/graph.h"
#include "embedstore/graph_dedupe.h"
#include "embedstore/store.h"
#include "support/env_guard.h"

/// Schema v6 (27k): an edge's origin and confidence, the code layer's own
/// provenance, and `sync_code_graph` -- the one transaction a code build
/// writes through. Every prose writer stamps `inferred`; the code layer
/// stamps `extracted`; an older store reads its edges as `inferred`; an
/// extracted edge never merges down; the prose layer is untouched by a sync;
/// code identity is the exact qualified name.
namespace {

using apogee::embedstore::CodeEdgeRow;
using apogee::embedstore::CodeFileState;
using apogee::embedstore::CodeMentionRow;
using apogee::embedstore::CodeNodeRow;
using apogee::embedstore::CodeSiteRow;
using apogee::embedstore::CodeSyncResult;
using apogee::embedstore::GraphNode;
using apogee::embedstore::Neighbor;
using apogee::embedstore::Store;

struct Scratch {
    apogee::testing::TempDir dir{"embedstore-code-" + std::to_string(std::random_device{}())};

    [[nodiscard]] std::filesystem::path db() const {
        return dir.path() / "g.db";
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

[[nodiscard]] CodeNodeRow code_node(std::string type, std::string name, std::string file,
                                    std::int64_t line, std::string description = "") {
    CodeNodeRow row;
    row.type = std::move(type);
    row.name = std::move(name);
    row.description = std::move(description);
    row.metadata = R"({"kind":"code"})";
    row.mentions.push_back(CodeMentionRow{.collection = "src",
                                          .file = std::move(file),
                                          .line = line,
                                          .end_line = line,
                                          .role = "definition"});
    return row;
}

[[nodiscard]] CodeEdgeRow code_edge(std::string source_type, std::string source,
                                    std::string target_type, std::string target,
                                    std::string relation, std::vector<std::int64_t> lines) {
    CodeEdgeRow row;
    row.source_type = std::move(source_type);
    row.source_name = std::move(source);
    row.target_type = std::move(target_type);
    row.target_name = std::move(target);
    row.relation = std::move(relation);
    for (const std::int64_t line : lines) {
        row.sites.push_back(CodeSiteRow{.collection = "src", .file = "a.cc", .line = line});
    }
    return row;
}

/// The one node of `type` named exactly `name`.
[[nodiscard]] GraphNode node_named(const Store& store, const std::string& name,
                                   std::string_view type) {
    for (const GraphNode& node : store.find_nodes(name)) {
        if (node.type == type && node.name == name) {
            return node;
        }
    }
    FAIL("no " << type << " named " << name);
    return {};
}

[[nodiscard]] std::optional<Neighbor> neighbor(const Store& store, std::int64_t node,
                                               std::string_view relation, std::string_view peer) {
    for (const Neighbor& item : store.node_neighbors(node)) {
        if (item.relation == relation && item.peer_name == peer) {
            return item;
        }
    }
    return std::nullopt;
}

/// a.cc with `run` calling `helper` twice and `printf` once.
[[nodiscard]] CodeSyncResult sync_small(Store& store, bool with_helper = true) {
    std::vector<CodeNodeRow> nodes{code_node("file", "a.cc", "a.cc", 1, "C++ file"),
                                   code_node("function", "run", "a.cc", 3, "void run()")};
    std::vector<CodeEdgeRow> edges{code_edge("function", "run", "file", "a.cc", "defined_in", {3})};
    if (with_helper) {
        nodes.push_back(code_node("function", "helper", "a.cc", 9, "int helper()"));
        edges.push_back(code_edge("function", "run", "function", "helper", "calls", {4, 5}));
        edges.push_back(code_edge("function", "helper", "file", "a.cc", "defined_in", {9}));
    }
    return store.sync_code_graph(nodes, edges);
}

}  // namespace

TEST_CASE("an older store gains origin and confidence on open, every earlier edge read as inferred",
          "[embedstore][graph][code][schema]") {
    const Scratch scratch;
    {
        Store store{scratch.db()};
        const std::int64_t a = store.upsert_node("Atlas", "system", "").id;
        const std::int64_t b = store.upsert_node("Vault", "system", "").id;
        store.upsert_edge(a, b, "stores in", "");
    }
    // Back to the v5 shape, as a build before 27k wrote it.
    raw_exec(scratch.db(),
             "ALTER TABLE kg_edges DROP COLUMN confidence;"
             "ALTER TABLE kg_edges DROP COLUMN origin;"
             "ALTER TABLE kg_state DROP COLUMN facts;"
             "ALTER TABLE kg_state DROP COLUMN content_hash;"
             "DROP TABLE kg_code_mentions;"
             "DROP TABLE kg_edge_sites;"
             "UPDATE store_meta SET value = '5' WHERE key = 'schema_version';");
    const Store store{scratch.db()};
    CHECK(store.schema_version() == 6);
    const GraphNode atlas = store.find_nodes("Atlas").front();
    const std::optional<Neighbor> edge = neighbor(store, atlas.id, "stores in", "Vault");
    REQUIRE(edge.has_value());
    CHECK(edge->origin == apogee::embedstore::kOriginInferred);
    CHECK(edge->confidence < 0.0);  // a model's edge: no confidence recorded
    CHECK(store.graph_stats().edges_inferred == 1);
    CHECK(store.graph_stats().edges_extracted == 0);
    // The code layer's tables exist again: a sync works on the migrated store.
    Store writable{scratch.db()};
    CHECK(sync_small(writable).nodes_added == 3);
}

TEST_CASE("every prose writer stamps inferred; the code layer stamps extracted at confidence 1",
          "[embedstore][graph][code][origin]") {
    const Scratch scratch;
    Store store{scratch.db()};
    const std::int64_t a = store.upsert_node("Atlas", "system", "").id;
    const std::int64_t b = store.upsert_node("Vault", "system", "").id;
    const std::int64_t c = store.upsert_node("Ledger", "artifact", "").id;
    store.upsert_edge(a, b, "stores in", "");
    (void)store.ensure_edge(a, c, "concerns", "");
    for (const Neighbor& item : store.node_neighbors(a)) {
        CHECK(item.origin == apogee::embedstore::kOriginInferred);
        CHECK(item.confidence < 0.0);
    }
    (void)sync_small(store);
    const GraphNode run = node_named(store, "run", "function");
    const std::optional<Neighbor> calls = neighbor(store, run.id, "calls", "helper");
    REQUIRE(calls.has_value());
    CHECK(calls->origin == apogee::embedstore::kOriginExtracted);
    CHECK(calls->confidence == apogee::embedstore::kExtractedConfidence);
    CHECK(calls->weight == 2);  // its two call sites
    const apogee::embedstore::GraphStats stats = store.graph_stats();
    CHECK(stats.edges_extracted == 3);
    CHECK(stats.edges_inferred == 2);
    CHECK(stats.edges == 5);
}

TEST_CASE("a sync converges the code layer and leaves the prose layer exactly as it was",
          "[embedstore][graph][code][sync]") {
    const Scratch scratch;
    Store store{scratch.db()};
    store.replace_source("notes.md", {"Atlas collects readings"});
    const std::int64_t chunk = store.chunks_by_source("notes.md").front().id;
    const std::int64_t atlas = store.upsert_node("Atlas", "system", "collects").id;
    const std::int64_t vault = store.upsert_node("Vault", "system", "").id;
    (void)store.add_mention(atlas, chunk);
    (void)store.add_mention(vault, chunk);
    store.upsert_edge(atlas, vault, "stores in", "");

    const CodeSyncResult first = sync_small(store);
    CHECK(first.nodes_added == 3);
    CHECK(first.edges_added == 3);
    CHECK(first.sites == 4);
    CHECK(first.mentions == 3);
    const std::int64_t run_id = node_named(store, "run", "function").id;
    const std::int64_t file_id = node_named(store, "a.cc", "file").id;
    store.update_node_embedding(file_id, {1.0F, 0.0F});

    // The same graph again: nothing changes, and every surviving id stays.
    const CodeSyncResult same = sync_small(store);
    CHECK(same.nodes_added == 0);
    CHECK(same.nodes_changed == 0);
    CHECK(same.nodes_removed == 0);
    CHECK(same.edges_added == 0);
    CHECK(same.edges_removed == 0);
    CHECK(node_named(store, "run", "function").id == run_id);

    // `helper` gone from the source: its node and both its edges go; `run`
    // keeps its id; the prose layer is untouched.
    const CodeSyncResult shrunk = sync_small(store, false);
    CHECK(shrunk.nodes_removed == 1);
    CHECK(shrunk.edges_removed == 2);
    CHECK(node_named(store, "run", "function").id == run_id);
    CHECK(store.find_nodes("helper").empty());
    CHECK(store.find_nodes("Atlas").front().mention_count == 1);
    CHECK(neighbor(store, atlas, "stores in", "Vault").has_value());
    CHECK(store.graph_stats().edges_inferred == 1);
    // A metadata-only change keeps the vector; a changed description clears it.
    CHECK(store.nodes_by_ids({file_id}).front().dim == 2);
    CodeNodeRow renamed = code_node("file", "a.cc", "a.cc", 1, "C++ file");
    renamed.metadata = R"({"kind":"code","lang":"cpp"})";
    (void)store.sync_code_graph({renamed}, {});
    CHECK(store.nodes_by_ids({file_id}).front().dim == 2);
    (void)store.sync_code_graph({code_node("file", "a.cc", "a.cc", 1, "C++ source")}, {});
    CHECK(store.nodes_by_ids({file_id}).front().dim == 0);
}

TEST_CASE("an edge whose endpoint is not in the sync, or that loops on itself, is skipped",
          "[embedstore][graph][code][sync]") {
    const Scratch scratch;
    Store store{scratch.db()};
    const CodeSyncResult result =
        store.sync_code_graph({code_node("function", "run", "a.cc", 3)},
                              {code_edge("function", "run", "function", "missing", "calls", {4}),
                               code_edge("function", "run", "function", "run", "calls", {5})});
    CHECK(result.edges_added == 0);
    CHECK(store.graph_stats().edges == 0);
}

TEST_CASE("code identity is the exact qualified name: Parse and parse are two functions",
          "[embedstore][graph][code][identity]") {
    const Scratch scratch;
    Store store{scratch.db()};
    (void)store.sync_code_graph({code_node("function", "store.Parse", "a.go", 3),
                                 code_node("function", "store.parse", "a.go", 9)},
                                {});
    CHECK(store.graph_stats().nodes == 2);
    // The exact spelling wins; a fold two code nodes share lists both; a
    // fold of one finds it.
    REQUIRE(store.find_nodes("store.Parse").size() == 1);
    CHECK(store.find_nodes("store.Parse").front().name == "store.Parse");
    CHECK(store.find_nodes("STORE.PARSE").size() == 2);
    (void)store.sync_code_graph({code_node("class", "geo::Shape", "a.cc", 3)}, {});
    REQUIRE(store.find_nodes("geo::shape").size() == 1);
    CHECK(store.find_nodes("geo::shape").front().name == "geo::Shape");
}

TEST_CASE("an inferred edge the source now states is promoted; an extracted one never demotes",
          "[embedstore][graph][code][origin]") {
    const Scratch scratch;
    Store store{scratch.db()};
    (void)sync_small(store, false);
    (void)store.sync_code_graph({code_node("file", "a.cc", "a.cc", 1, "C++ file"),
                                 code_node("function", "run", "a.cc", 3, "void run()"),
                                 code_node("function", "helper", "a.cc", 9, "int helper()")},
                                {code_edge("function", "run", "file", "a.cc", "defined_in", {3})});
    const std::int64_t run = node_named(store, "run", "function").id;
    const std::int64_t helper = node_named(store, "helper", "function").id;
    // A model asserted run -> helper before any parse stated it.
    store.upsert_edge(run, helper, "calls", "");
    REQUIRE(neighbor(store, run, "calls", "helper")->origin == "inferred");
    (void)sync_small(store);
    std::optional<Neighbor> edge = neighbor(store, run, "calls", "helper");
    REQUIRE(edge.has_value());
    CHECK(edge->origin == "extracted");
    CHECK(edge->confidence == 1.0);
    CHECK(edge->weight == 2);
    // A model re-stating it corroborates nothing about its origin.
    store.upsert_edge(run, helper, "calls", "");
    CHECK(neighbor(store, run, "calls", "helper")->origin == "extracted");
}

TEST_CASE("dedupe folds origin-aware: an extracted edge never merges down, and its sites move",
          "[embedstore][graph][code][dedupe]") {
    const Scratch scratch;
    Store store{scratch.db()};
    store.replace_source("a.md", {"first"});
    const std::int64_t chunk = store.chunks_by_source("a.md").front().id;
    const auto node = [&](const std::string& name, std::vector<float> vector) {
        const std::int64_t id = store.upsert_node(name, "system", "").id;
        store.update_node_embedding(id, vector);
        (void)store.add_mention(id, chunk);
        return id;
    };
    const std::int64_t k8s = node("K8s", {1.0F, 0.0F});
    const std::int64_t kubernetes = node("Kubernetes", {0.99F, 0.05F});
    const std::int64_t vault = node("Vault", {0.0F, 1.0F});
    store.upsert_edge(k8s, vault, "runs on", "");
    store.upsert_edge(kubernetes, vault, "runs on", "");
    // The merged node's edge is a parsed one, with a site; the survivor's a
    // model's. The fold keeps the stronger origin.
    raw_exec(scratch.db(),
             ("UPDATE kg_edges SET origin = 'extracted', confidence = 1.0 WHERE source_id = " +
              std::to_string(kubernetes) +
              "; INSERT INTO kg_edge_sites (edge_id, collection, file, line) SELECT id, 'src',"
              " 'a.cc', 7 FROM kg_edges WHERE source_id = " +
              std::to_string(kubernetes))
                 .c_str());
    Store reopened{scratch.db()};
    const std::vector<apogee::embedstore::MergeGroup> groups = reopened.dedupe_nodes(0.9, false);
    REQUIRE(groups.size() == 1);
    CHECK(groups.front().kept.id == k8s);
    const std::optional<Neighbor> folded = neighbor(reopened, k8s, "runs on", "Vault");
    REQUIRE(folded.has_value());
    CHECK(folded->origin == "extracted");
    CHECK(folded->confidence == 1.0);
    CHECK(folded->weight == 2);
    const std::vector<apogee::embedstore::EdgeSite> sites = reopened.edge_sites(folded->edge_id, 0);
    REQUIRE(sites.size() == 1);
    CHECK(sites.front().line == 7);
}

TEST_CASE("dedupe never considers a code node, and its scope says what each layer has",
          "[embedstore][graph][code][dedupe]") {
    const Scratch scratch;
    Store store{scratch.db()};
    // A header declaration and its definition: one node, stated twice.
    CodeNodeRow declared = code_node("function", "run", "a.cc", 3);
    declared.mentions.push_back(CodeMentionRow{
        .collection = "src", .file = "a.h", .line = 2, .end_line = 2, .role = "declaration"});
    (void)store.sync_code_graph(
        {code_node("function", "Run", "b.cc", 1), declared, code_node("name", "printf", "a.cc", 4)},
        {});
    // Both functions vectorised, identically: only identity keeps them apart.
    REQUIRE(store.find_nodes("RUN").size() == 2);
    for (const GraphNode& node : store.find_nodes("RUN")) {
        store.update_node_embedding(node.id, {1.0F, 0.0F});
    }
    apogee::embedstore::DedupeScope scope = store.dedupe_scope();
    CHECK(scope.code_entities == 2);  // a name node is not an entity
    CHECK(scope.code_identity_merges == 1);
    CHECK(scope.prose_entities == 0);
    CHECK(scope.prose_skip_reason() == "no prose entities -- nothing to compare by vector");
    // Identical vectors, near-identical names: still two functions.
    CHECK(store.dedupe_nodes(0.5, false).empty());
    CHECK(store.graph_stats().nodes == 3);

    (void)store.upsert_node("Atlas", "system", "");
    scope = store.dedupe_scope();
    CHECK(scope.prose_entities == 1);
    CHECK(scope.prose_with_vectors == 0);
    CHECK(scope.prose_skip_reason() ==
          "none of 1 prose entities has a vector -- no embedder built them");
    store.update_node_embedding(store.find_nodes("Atlas").front().id, {0.0F, 1.0F});
    CHECK(store.dedupe_scope().prose_skip_reason().empty());
}

TEST_CASE("a code file's state is its own: prose plans and reconciles never see or prune it",
          "[embedstore][graph][code][state]") {
    const Scratch scratch;
    Store store{scratch.db()};
    store.set_code_file_state(CodeFileState{.collection = "src",
                                            .file = "a.cc",
                                            .content_hash = "abc",
                                            .extractor = "cpp:x:extractor-1",
                                            .facts = "{}",
                                            .parsed_at = "2026-10-04T00:00:00Z"});
    store.set_code_file_state(CodeFileState{.collection = "src",
                                            .file = "b.py",
                                            .content_hash = "def",
                                            .extractor = "python:y:extractor-1",
                                            .facts = "{}",
                                            .parsed_at = {}});
    CHECK_THROWS_AS(store.set_code_file_state(CodeFileState{.collection = "src", .file = "c.cc"}),
                    std::invalid_argument);
    CHECK(store.code_file_states("src").size() == 2);
    CHECK(store.code_file_states("src").at("a.cc").content_hash == "abc");
    CHECK_FALSE(store.code_file_states("src").at("b.py").parsed_at.empty());
    CHECK(store.code_members() == std::vector<std::string>{"src"});
    // A prose build's plan reads prose rows only, and its reconciles -- over
    // a collection's own graph and a named graph's members -- prune prose
    // rows only.
    CHECK(store.source_states("src").empty());
    (void)store.reconcile_graph();
    (void)store.reconcile_graph_multi({});
    CHECK(store.code_file_states("src").size() == 2);
    const apogee::embedstore::GraphStats stats = store.graph_stats();
    CHECK(stats.code_files == 2);
    CHECK(stats.code_files_by_language.at("cpp") == 1);
    CHECK(stats.code_files_by_language.at("python") == 1);

    CHECK(store.remove_code_files("src", {"a.cc", "nope.cc"}) == 1);
    CHECK(store.code_file_states("src").size() == 1);
    CHECK(store.remove_code_member("src") == 1);
    CHECK(store.code_members().empty());
}

TEST_CASE("delete_graph takes the code layer with it", "[embedstore][graph][code]") {
    const Scratch scratch;
    Store store{scratch.db()};
    (void)sync_small(store);
    store.set_code_file_state(CodeFileState{
        .collection = "src", .file = "a.cc", .content_hash = "abc", .extractor = "x"});
    store.delete_graph();
    const apogee::embedstore::GraphStats stats = store.graph_stats();
    CHECK(stats.nodes == 0);
    CHECK(stats.edges == 0);
    CHECK(stats.code_mentions == 0);
    CHECK(stats.code_files == 0);
    raw_exec(scratch.db(), "SELECT 1");  // still a valid database
}

TEST_CASE("the dump is the graph alone: two stores holding it agree byte for byte",
          "[embedstore][graph][code][dump]") {
    const Scratch one;
    const Scratch two;
    Store first{one.db()};
    Store second{two.db()};
    // Built differently -- a detour through another graph, ids churned -- the
    // same final graph dumps the same bytes.
    (void)sync_small(first);
    (void)second.sync_code_graph({code_node("function", "other", "x.cc", 1)}, {});
    (void)sync_small(second, false);
    (void)sync_small(second);
    CHECK(first.graph_dump() == second.graph_dump());
    CHECK(first.graph_dump().find("edge function run -[calls]-> function helper") !=
          std::string::npos);
}

TEST_CASE("an unresolved name is navigable but never structure: no community edge, no context",
          "[embedstore][graph][code][expand]") {
    const Scratch scratch;
    Store store{scratch.db()};
    (void)store.sync_code_graph(
        {code_node("function", "run", "a.cc", 3), code_node("function", "helper", "a.cc", 9),
         code_node("name", ".push_back", "a.cc", 4)},
        {code_edge("function", "run", "function", "helper", "calls", {4}),
         code_edge("function", "run", "name", ".push_back", "calls", {5})});
    CHECK(store.graph_stats().edges == 2);
    CHECK(store.all_edges().size() == 1);
    CHECK(store.graph_stats().unresolved_names == 1);
    const std::int64_t run = node_named(store, "run", "function").id;
    const std::int64_t name = node_named(store, ".push_back", "name").id;
    const apogee::embedstore::Expansion expansion = store.graph_expand_labelled({}, {run}, 1, 8);
    bool helper_seen = false;
    for (const apogee::embedstore::ExpandEntity& entity : expansion.entities) {
        CHECK(entity.node.type != "name");
        helper_seen = helper_seen || entity.node.name == "helper";
    }
    CHECK(helper_seen);
    for (const apogee::embedstore::ExpandEdge& edge : expansion.edges) {
        CHECK(edge.origin == "extracted");
    }
    // A name node as a seed seeds nothing.
    CHECK(store.graph_expand_labelled({}, {name}, 1, 8).empty());
}
