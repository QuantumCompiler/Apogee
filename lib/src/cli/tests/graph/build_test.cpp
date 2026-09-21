#include "graph/build.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "backends/mock.h"
#include "embedstore/store.h"
#include "harness/config.h"
#include "harness/harness.h"
#include "support/env_guard.h"

/// The build loop against fakes: populate, dry-run, the resume rule, the
/// (count, max id, model) fingerprint, soft failures and the retry, the
/// limit, cancellation, and the embed phase that never loses extraction.
namespace {

using apogee::embedstore::Store;
using apogee::graph::BuildOptions;
using apogee::graph::BuildResult;
using apogee::graph::Entity;
using apogee::graph::ExtractOutcome;
using apogee::graph::ExtractResult;
using apogee::graph::Relation;

/// An extractor that answers from a table keyed by chunk text and counts
/// every call per text -- the resume assertions read those counts.
struct FakeExtractor {
    std::map<std::string, ExtractResult> answers;
    std::map<std::string, int> calls;
    /// Texts whose first N calls fail.
    std::map<std::string, int> fail_first;

    [[nodiscard]] apogee::graph::ExtractFn fn() {
        return [this](std::string_view text, const apogee::harness::CancellationToken&) {
            const std::string key{text};
            ++calls[key];
            ExtractOutcome outcome;
            outcome.attempts = 1;
            if (const auto it = fail_first.find(key);
                it != fail_first.end() && calls[key] <= it->second) {
                outcome.error = "the extractor did not return entities and relations";
                return outcome;
            }
            const auto it = answers.find(key);
            outcome.result = it == answers.end() ? ExtractResult{} : it->second;
            return outcome;
        };
    }
};

ExtractResult about(std::vector<std::string> names, std::vector<Relation> relations = {}) {
    ExtractResult result;
    for (std::string& name : names) {
        result.entities.push_back(
            Entity{.name = std::move(name), .type = "concept", .description = ""});
    }
    result.relations = std::move(relations);
    return result;
}

struct Scratch {
    apogee::testing::TempDir dir{"graph-build-" + std::to_string(std::random_device{}())};
    Store store{dir.path() / "c.db"};
};

/// Two sources: a.md with two chunks, b.md with one.
void seed(Store& store) {
    store.replace_source("a.md", {"alpha one", "alpha two"});
    store.replace_source("b.md", {"beta one"});
}

BuildOptions opts(std::string model = "m") {
    BuildOptions options;
    options.model = std::move(model);
    return options;
}

}  // namespace

TEST_CASE(
    "a build populates nodes, edges and mentions, stamps every finished file, and records "
    "the model",
    "[graph][build][populate]") {
    Scratch scratch;
    seed(scratch.store);
    FakeExtractor fake;
    fake.answers["alpha one"] =
        about({"Atlas", "Vault"},
              {Relation{.source = "Atlas", .target = "Vault", .relation = "stores in"}});
    fake.answers["alpha two"] = about({"Atlas"});
    fake.answers["beta one"] =
        about({"Vault", "Ledger"},
              {Relation{.source = "Vault", .target = "Ledger", .relation = "keeps"}});

    const BuildResult result = apogee::graph::build(scratch.store, fake.fn(), nullptr, opts());
    CHECK(result.files_planned == 2);
    CHECK(result.files_extracted == 2);
    CHECK(result.chunks_done == 3);
    CHECK(result.chunks_failed == 0);
    CHECK(result.nodes_upserted == 3);
    CHECK(result.edges_upserted == 2);
    CHECK(result.mentions_added == 5);
    CHECK(result.nodes_embedded == 0);
    CHECK_FALSE(result.limit_hit);
    CHECK(result.reconcile.total() == 0);
    const apogee::embedstore::GraphStats stats = scratch.store.graph_stats();
    CHECK(stats.nodes == 3);
    CHECK(stats.edges == 2);
    CHECK(stats.mentions == 5);
    CHECK(stats.extract_model == "m");
    CHECK(stats.stale_files == 0);
    CHECK(scratch.store.find_nodes("Atlas").front().mention_count == 2);
    REQUIRE(scratch.store.source_states().size() == 2);
    CHECK(scratch.store.source_states().at("a.md").chunk_count == 2);
    CHECK(scratch.store.source_states().at("a.md").max_chunk_id ==
          scratch.store.chunks_by_source("a.md").back().id);

    // A second build with nothing changed plans nothing and calls nothing.
    const BuildResult again = apogee::graph::build(scratch.store, fake.fn(), nullptr, opts());
    CHECK(again.files_planned == 0);
    CHECK(fake.calls["alpha one"] == 1);
}

TEST_CASE("a dry run extracts, reports, and commits nothing", "[graph][build][dry-run]") {
    Scratch scratch;
    seed(scratch.store);
    FakeExtractor fake;
    fake.answers["alpha one"] = about({"Atlas"});
    std::vector<std::string> seen;
    BuildOptions options = opts();
    options.dry_run = true;
    options.on_extract = [&seen](const apogee::embedstore::Chunk& chunk,
                                 const ExtractResult& result) {
        seen.push_back(chunk.text + ":" + std::to_string(result.entities.size()));
    };
    const BuildResult result = apogee::graph::build(scratch.store, fake.fn(), nullptr, options);
    CHECK(result.dry_run);
    CHECK(result.chunks_done == 3);
    CHECK(seen == std::vector<std::string>{"alpha one:1", "alpha two:0", "beta one:0"});
    CHECK(scratch.store.graph_stats().nodes == 0);
    CHECK(scratch.store.source_states().empty());
    CHECK(scratch.store.graph_meta("extract_model").empty());
}

TEST_CASE("an interrupted build resumes: the limit stops mid-file and only that file re-extracts",
          "[graph][build][resume]") {
    Scratch scratch;
    seed(scratch.store);
    FakeExtractor fake;
    fake.answers["alpha one"] = about({"Atlas"});
    fake.answers["alpha two"] = about({"Atlas"});
    fake.answers["beta one"] = about({"Vault"});
    BuildOptions limited = opts();
    limited.limit = 1;
    const BuildResult first = apogee::graph::build(scratch.store, fake.fn(), nullptr, limited);
    CHECK(first.limit_hit);
    CHECK(first.chunks_done == 1);
    CHECK(first.files_extracted == 0);
    CHECK(scratch.store.source_states().empty());   // a.md unstamped
    CHECK(scratch.store.graph_stats().nodes == 1);  // what finished is stored

    // Resume: a.md re-extracts (both chunks), b.md extracts once.
    const BuildResult second = apogee::graph::build(scratch.store, fake.fn(), nullptr, opts());
    CHECK(second.files_planned == 2);
    CHECK(second.files_extracted == 2);
    CHECK(fake.calls["alpha one"] == 2);
    CHECK(fake.calls["alpha two"] == 1);
    CHECK(fake.calls["beta one"] == 1);
    CHECK(scratch.store.find_nodes("Atlas").front().mention_count == 2);  // mentions dedup

    // A third build is a no-op; --force re-extracts everything.
    CHECK(apogee::graph::build(scratch.store, fake.fn(), nullptr, opts()).files_planned == 0);
    BuildOptions forced = opts();
    forced.force = true;
    CHECK(apogee::graph::build(scratch.store, fake.fn(), nullptr, forced).files_planned == 2);
    CHECK(fake.calls["beta one"] == 2);
}

TEST_CASE("a model change makes every source stale", "[graph][build][model]") {
    Scratch scratch;
    seed(scratch.store);
    FakeExtractor fake;
    (void)apogee::graph::build(scratch.store, fake.fn(), nullptr, opts("first"));
    CHECK(apogee::graph::build(scratch.store, fake.fn(), nullptr, opts("first")).files_planned ==
          0);
    const BuildResult changed =
        apogee::graph::build(scratch.store, fake.fn(), nullptr, opts("second"));
    CHECK(changed.files_planned == 2);
    CHECK(scratch.store.source_states().at("a.md").model == "second");
    CHECK(scratch.store.graph_stats().extract_model == "second");
}

TEST_CASE("the fingerprint catches a same-count re-ingest, and a third build is a no-op",
          "[graph][build][fingerprint]") {
    Scratch scratch;
    seed(scratch.store);
    FakeExtractor fake;
    fake.answers["alpha one"] = about({"Atlas"});
    fake.answers["alpha one v2"] = about({"Atlas"});
    fake.answers["beta one"] = about({"Vault"});
    (void)apogee::graph::build(scratch.store, fake.fn(), nullptr, opts());
    CHECK(scratch.store.graph_stats().nodes == 2);

    // Re-ingest a.md with the SAME number of chunks: the ids move, the
    // count does not. Reconcile prunes the dead mentions -- and without the
    // max-id half of the fingerprint the planner would read a.md as up to
    // date, and Atlas would be gone until a forced rebuild.
    scratch.store.replace_source("a.md", {"alpha one v2", "alpha two"});
    const BuildResult rebuilt = apogee::graph::build(scratch.store, fake.fn(), nullptr, opts());
    CHECK(rebuilt.reconcile.mentions_pruned == 1);
    CHECK(rebuilt.reconcile.nodes_pruned == 1);
    CHECK(rebuilt.files_planned == 1);  // a.md only
    CHECK(fake.calls["beta one"] == 1);
    CHECK(fake.calls["alpha one v2"] == 1);
    CHECK(scratch.store.graph_stats().nodes == 2);  // Atlas is back
    CHECK(scratch.store.find_nodes("Atlas").front().mention_count == 1);

    CHECK(apogee::graph::build(scratch.store, fake.fn(), nullptr, opts()).files_planned == 0);

    // A legacy state row -- max id 0 -- compares the count only.
    scratch.store.set_source_state("a.md", 2, 0, "m");
    scratch.store.replace_source("a.md", {"alpha one v2", "alpha two"});
    CHECK(apogee::graph::build(scratch.store, fake.fn(), nullptr, opts()).files_planned == 0);
    CHECK(apogee::graph::source_stale(
              apogee::embedstore::SourceState{.chunk_count = 2, .max_chunk_id = 0, .model = "m"},
              apogee::embedstore::ChunkSpan{.count = 2, .max_id = 77}, "m") == false);
    CHECK(apogee::graph::source_stale(
        apogee::embedstore::SourceState{.chunk_count = 2, .max_chunk_id = 5, .model = "m"},
        apogee::embedstore::ChunkSpan{.count = 2, .max_id = 77}, "m"));
    CHECK(apogee::graph::source_stale(
        apogee::embedstore::SourceState{.chunk_count = 3, .max_chunk_id = 77, .model = "m"},
        apogee::embedstore::ChunkSpan{.count = 2, .max_id = 77}, "m"));
    CHECK(apogee::graph::source_stale(
        apogee::embedstore::SourceState{.chunk_count = 2, .max_chunk_id = 77, .model = "other"},
        apogee::embedstore::ChunkSpan{.count = 2, .max_id = 77}, "m"));
}

TEST_CASE("a failed chunk is retried once, then counted, and its file stays unstamped",
          "[graph][build][failure]") {
    Scratch scratch;
    seed(scratch.store);
    FakeExtractor fake;
    fake.answers["alpha one"] = about({"Atlas"});
    fake.answers["alpha two"] = about({"Vault"});
    fake.fail_first["alpha two"] = 2;  // fails the call and the retry
    fake.fail_first["beta one"] = 1;   // the retry absorbs it
    std::vector<std::string> failed;
    BuildOptions options = opts();
    options.on_chunk_failed = [&failed](const apogee::embedstore::Chunk& chunk,
                                        std::string_view error) {
        failed.push_back(chunk.text + ": " + std::string{error});
    };
    const BuildResult result = apogee::graph::build(scratch.store, fake.fn(), nullptr, options);
    CHECK(result.chunks_done == 3);
    CHECK(result.chunks_failed == 1);
    CHECK(result.files_extracted == 1);  // b.md
    REQUIRE(failed.size() == 1);
    CHECK(failed.front().starts_with("alpha two: the extractor did not return"));
    CHECK(fake.calls["alpha two"] == 2);
    CHECK(fake.calls["beta one"] == 2);
    CHECK(scratch.store.graph_stats().nodes == 1);  // Atlas from alpha one is kept
    CHECK(scratch.store.graph_stats().failed_chunks == 1);
    CHECK(scratch.store.source_states().count("a.md") == 0);
    CHECK(scratch.store.source_states().count("b.md") == 1);

    // The next build retries only a.md.
    fake.fail_first.clear();
    const BuildResult retried = apogee::graph::build(scratch.store, fake.fn(), nullptr, opts());
    CHECK(retried.files_planned == 1);
    CHECK(retried.chunks_failed == 0);
    CHECK(scratch.store.graph_stats().nodes == 2);
    CHECK(scratch.store.graph_stats().failed_chunks == 0);
}

TEST_CASE("cancellation stops between chunks, keeps what finished, and the next build resumes",
          "[graph][build][cancel]") {
    Scratch scratch;
    seed(scratch.store);
    apogee::harness::CancellationToken token = apogee::harness::CancellationToken::create();
    int calls = 0;
    const apogee::graph::ExtractFn extract = [&](std::string_view,
                                                 const apogee::harness::CancellationToken&) {
        ++calls;
        ExtractOutcome outcome;
        outcome.result = about({"Atlas"});
        token.cancel();  // requested after the first chunk lands
        return outcome;
    };
    BuildOptions options = opts();
    options.cancellation = token;
    const BuildResult result = apogee::graph::build(scratch.store, extract, nullptr, options);
    CHECK(result.cancelled);
    CHECK(calls == 1);
    CHECK(scratch.store.graph_stats().nodes == 1);
    CHECK(scratch.store.source_states().empty());
    CHECK(scratch.store.graph_meta("extract_model").empty());  // not stamped mid-run

    FakeExtractor fake;
    const BuildResult resumed = apogee::graph::build(scratch.store, fake.fn(), nullptr, opts());
    CHECK_FALSE(resumed.cancelled);
    CHECK(resumed.files_planned == 2);
}

TEST_CASE(
    "the embed phase runs last over new or changed nodes, and a soft failure keeps the "
    "extraction",
    "[graph][build][embed]") {
    Scratch scratch;
    seed(scratch.store);
    FakeExtractor fake;
    fake.answers["alpha one"] = about({"Atlas"});
    fake.answers["beta one"] = about({"Vault"});
    // A node that already exists, described and mentioned: the build finds
    // it unchanged and does not re-embed it.
    const std::int64_t existing =
        scratch.store.upsert_node("Atlas", "concept", "already described").id;
    (void)scratch.store.add_mention(existing, scratch.store.chunks_by_source("a.md").front().id);
    std::vector<std::string> embedded;
    const apogee::graph::EmbedFn embed = [&embedded](std::string_view text,
                                                     const apogee::harness::CancellationToken&) {
        embedded.emplace_back(text);
        return std::vector<float>{1.0F, 2.0F};
    };
    BuildOptions options = opts();
    options.embed_model = "mock-space";
    const BuildResult result = apogee::graph::build(scratch.store, fake.fn(), embed, options);
    CHECK(result.nodes_embedded == 1);  // Vault is new; Atlas existed unchanged
    CHECK(embedded == std::vector<std::string>{"Vault"});
    CHECK(scratch.store.node_vector(scratch.store.find_nodes("Vault").front().id).size() == 2);
    CHECK(scratch.store.graph_meta("embed_model") == "mock-space");
    CHECK(scratch.store.graph_stats().nodes_with_vectors == 1);

    // A failing embedder stops the phase, keeps the build, records no model.
    scratch.store.delete_graph();
    const apogee::graph::EmbedFn broken =
        [](std::string_view, const apogee::harness::CancellationToken&) -> std::vector<float> {
        throw std::runtime_error("endpoint down");
    };
    const BuildResult soft = apogee::graph::build(scratch.store, fake.fn(), broken, options);
    CHECK(soft.embed_error == "endpoint down");
    CHECK(soft.nodes_embedded == 0);
    CHECK(soft.files_extracted == 2);
    CHECK(scratch.store.graph_stats().nodes == 2);
    CHECK(scratch.store.graph_meta("embed_model").empty());
    CHECK(scratch.store.source_states().size() == 2);
}

TEST_CASE(
    "resolve_entity_embedder follows the embedding spend rule: unmetered or pinned to vector "
    "embeds, a metered chain without the ask does not",
    "[graph][build][embedder]") {
    apogee::harness::Config config;
    apogee::harness::BackendConfig mock;
    mock.type = apogee::harness::BackendType::Mock;
    mock.embedding_model = "mock-space";
    config.backends.emplace("embed", mock);
    config.models.default_embedding = "embed";
    apogee::harness::Harness harness{config};
    auto embedder = std::make_shared<apogee::backends::MockEmbeddingProvider>("embed");
    embedder->set_model_name("mock-space");
    harness.register_provider("embed", embedder);
    harness.use_default_router();

    const apogee::graph::EntityEmbedder free =
        apogee::graph::resolve_entity_embedder(harness, config, "", "");
    REQUIRE(free.embed);
    CHECK(free.model == "mock-space");
    CHECK(free.note.empty());
    CHECK(free.embed("Atlas: collects readings", {}).size() == 8);

    embedder->set_metered(true);
    const apogee::graph::EntityEmbedder metered =
        apogee::graph::resolve_entity_embedder(harness, config, "", "");
    CHECK_FALSE(metered.embed);
    CHECK(metered.model.empty());
    CHECK(metered.note.find("paid embedder") != std::string::npos);
    // The collection's own pin is the explicit ask.
    const apogee::graph::EntityEmbedder pinned =
        apogee::graph::resolve_entity_embedder(harness, config, "", "vector");
    REQUIRE(pinned.embed);
    CHECK(pinned.model == "mock-space");
    // No embedder at all: no function, a reason.
    apogee::harness::Harness bare{apogee::harness::Config{}};
    bare.use_default_router();
    const apogee::graph::EntityEmbedder none =
        apogee::graph::resolve_entity_embedder(bare, apogee::harness::Config{}, "", "vector");
    CHECK_FALSE(none.embed);
    CHECK_FALSE(none.note.empty());
}
