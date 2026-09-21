#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "agentloop/graph_context.h"
#include "agentloop/rag.h"
#include "embedstore/store.h"
#include "harness/config.h"
#include "support/env_guard.h"

/// The retrieval precedence rule: a BUILT named graph covers its members
/// (the member's own graph is a decoy that must not appear), an unbuilt
/// entry covers nothing so the member falls back to its own graph, the
/// first entry wins a double-listing, and the seeds carry the member's
/// label into the walk.
namespace {

using apogee::agentloop::TurnGraph;
using apogee::embedstore::Store;

struct Fixture {
    apogee::testing::TempDir home{"graph-named-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    std::filesystem::path docs_db = home.path() / "embeddings" / "docs.db";
    apogee::harness::Config config;
    std::int64_t docs_chunk = 0;

    Fixture() {
        // docs: one chunk, its OWN graph carries a decoy neighbour.
        Store docs{docs_db};
        docs.replace_source("a.md", {"Atlas collects readings from the field probes"});
        docs_chunk = docs.chunks_by_source("a.md").front().id;
        const std::int64_t atlas = docs.upsert_node("Atlas", "system", "collects readings").id;
        const std::int64_t decoy = docs.upsert_node("Decoy", "system", "only in the own graph").id;
        (void)docs.add_mention(atlas, docs_chunk);
        (void)docs.add_mention(decoy, 999);
        docs.upsert_edge(atlas, decoy, "misleads", "");

        config = apogee::harness::parse_config(R"YAML(
embeddings:
  docs:
    graph:
      enabled: true
graphs:
  work:
    collections: [docs, meetings]
    hops: 2
    max_entities: 3
  later:
    collections: [docs]
)YAML",
                                               "<test>");
    }

    /// Builds the named graph's database: Atlas from docs' chunk, linked to
    /// a Shared entity mentioned from meetings.
    void build_work() const {
        Store work{apogee::agentloop::graph_db_path("work")};
        const std::int64_t atlas = work.upsert_node("Atlas", "system", "collects readings").id;
        const std::int64_t shared =
            work.upsert_node("Shared", "system", "known to the named graph alone").id;
        (void)work.add_mention(atlas, "docs", docs_chunk);
        (void)work.add_mention(shared, "meetings", 7);
        work.upsert_edge(atlas, shared, "feeds", "");
    }
};

}  // namespace

TEST_CASE("graph_db_path lives under the embeddings directory's graphs/, a derived location",
          "[agentloop][graph][named]") {
    const Fixture f;
    CHECK(apogee::agentloop::graph_db_path("work") ==
          f.home.path() / "embeddings" / "graphs" / "work.db");
}

TEST_CASE("named_graph_covering honours built_only, config order, and case-insensitive members",
          "[agentloop][graph][named][covering]") {
    const Fixture f;
    // Nothing built: retrieval sees no covering graph; chaining sees the
    // first entry by membership alone.
    CHECK_FALSE(apogee::agentloop::named_graph_covering(f.config, "docs", true).has_value());
    const auto by_config = apogee::agentloop::named_graph_covering(f.config, "docs", false);
    REQUIRE(by_config.has_value());
    CHECK(by_config->name == "work");
    CHECK(by_config->config->hops == 2);
    CHECK_FALSE(apogee::agentloop::named_graph_covering(f.config, "other", false).has_value());
    // Build the later entry only: the first entry is unbuilt and covers
    // nothing, so `later` covers docs for retrieval.
    {
        Store later{apogee::agentloop::graph_db_path("later")};
    }
    auto covering = apogee::agentloop::named_graph_covering(f.config, "DOCS", true);
    REQUIRE(covering.has_value());
    CHECK(covering->name == "later");
    // Once work is built too, the first entry wins the double-listing.
    f.build_work();
    covering = apogee::agentloop::named_graph_covering(f.config, "docs", true);
    REQUIRE(covering.has_value());
    CHECK(covering->name == "work");
}

TEST_CASE(
    "resolve_turn_graph prefers a built named graph, then the collection's own block, "
    "then nothing",
    "[agentloop][graph][named][precedence]") {
    const Fixture f;
    // Unbuilt: the member keeps its own enabled graph, with its own knobs.
    TurnGraph own = apogee::agentloop::resolve_turn_graph(f.config, "docs");
    CHECK(own.enabled);
    CHECK(own.store_path.empty());
    CHECK(own.name == "docs");
    CHECK(own.seed_collection.empty());
    CHECK(own.hops == 1);
    CHECK(own.max_entities == 8);
    // Built: the named graph, its knobs, the member's label on the seeds.
    f.build_work();
    const TurnGraph named = apogee::agentloop::resolve_turn_graph(f.config, "docs");
    CHECK(named.enabled);
    CHECK(named.store_path == apogee::agentloop::graph_db_path("work"));
    CHECK(named.name == "work");
    CHECK(named.seed_collection == "docs");
    CHECK(named.hops == 2);
    CHECK(named.max_entities == 3);
    // A member with no own block is covered too; a stranger is not.
    const TurnGraph meetings = apogee::agentloop::resolve_turn_graph(f.config, "meetings");
    CHECK(meetings.enabled);
    CHECK(meetings.name == "work");
    CHECK_FALSE(apogee::agentloop::resolve_turn_graph(f.config, "tickets").enabled);
}

TEST_CASE(
    "a turn through a built named graph renders the graph's section and never the "
    "member's own decoy; removing the database hands the member its own graph back",
    "[agentloop][graph][named][turn]") {
    const Fixture f;
    f.build_work();
    apogee::agentloop::RagTurn turn;
    turn.store_path = f.docs_db;
    turn.question = "field probes";
    turn.limit = 4;
    turn.collection = "docs";
    const TurnGraph graph = apogee::agentloop::resolve_turn_graph(f.config, "docs");
    turn.graph_enabled = graph.enabled;
    turn.graph_hops = graph.hops;
    turn.graph_max_entities = graph.max_entities;
    turn.graph_store_path = graph.store_path;
    turn.graph_name = graph.name;
    turn.graph_seed_collection = graph.seed_collection;
    const apogee::agentloop::RagResult through = apogee::agentloop::retrieve_for_turn(turn);
    CHECK(through.chunks == 1);
    CHECK(through.graph_entities == 1);
    REQUIRE(through.prefix.size() == 1);
    const std::string sent = through.prefix.front().content.plain_text();
    CHECK(sent.find("[Knowledge graph: work]") != std::string::npos);
    CHECK(sent.find("Shared (system): known to the named graph alone") != std::string::npos);
    CHECK(sent.find("Atlas —[feeds]→ Shared") != std::string::npos);
    CHECK(sent.find("Decoy") == std::string::npos);  // the tripwire

    // The named graph deleted: the member's own graph takes over again.
    std::filesystem::remove(apogee::agentloop::graph_db_path("work"));
    const TurnGraph own = apogee::agentloop::resolve_turn_graph(f.config, "docs");
    apogee::agentloop::RagTurn fallback = turn;
    fallback.graph_store_path = own.store_path;
    fallback.graph_name = own.name;
    fallback.graph_seed_collection = own.seed_collection;
    fallback.graph_hops = own.hops;
    fallback.graph_max_entities = own.max_entities;
    const apogee::agentloop::RagResult back = apogee::agentloop::retrieve_for_turn(fallback);
    const std::string own_sent = back.prefix.front().content.plain_text();
    CHECK(own_sent.find("[Knowledge graph: docs]") != std::string::npos);
    CHECK(own_sent.find("Decoy") != std::string::npos);
    CHECK(own_sent.find("Shared") == std::string::npos);
}

TEST_CASE("build_graph_section_labelled seeds a named graph by (collection, chunk)",
          "[agentloop][graph][named][section]") {
    const Fixture f;
    f.build_work();
    const Store work{apogee::agentloop::graph_db_path("work")};
    const apogee::agentloop::GraphSection section = apogee::agentloop::build_graph_section_labelled(
        work, "work",
        {apogee::embedstore::ChunkRef{.collection = "docs", .chunk_id = f.docs_chunk}}, "", 1, 8);
    CHECK(section.entities == 1);
    CHECK(section.text ==
          "[Knowledge graph: work]\n"
          "Shared (system): known to the named graph alone\n"
          "Atlas —[feeds]→ Shared");
    // The same chunk id under another member's label seeds nothing.
    CHECK(apogee::agentloop::build_graph_section_labelled(
              work, "work",
              {apogee::embedstore::ChunkRef{.collection = "meetings", .chunk_id = f.docs_chunk}},
              "", 1, 8)
              .empty());
}
