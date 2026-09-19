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

/// The build over several members into a named graph's own database:
/// cross-collection identity (one node, labelled mentions, weight
/// accumulating across members, per-collection state, the identity stamped,
/// a no-op second build), member removal converging, the limit and resume
/// across members, a missing member contributing nothing, a record in a
/// member materialised, and a dry run creating nothing.
namespace {

using apogee::embedstore::Store;
using apogee::graph::BuildOptions;
using apogee::graph::BuildResult;
using apogee::graph::Entity;
using apogee::graph::ExtractOutcome;
using apogee::graph::ExtractResult;
using apogee::graph::Member;
using apogee::graph::Relation;

ExtractResult about(std::vector<std::string> names, std::vector<Relation> relations = {}) {
    ExtractResult result;
    for (std::string& name : names) {
        result.entities.push_back(
            Entity{.name = std::move(name), .type = "system", .description = ""});
    }
    result.relations = std::move(relations);
    return result;
}

struct FakeExtractor {
    std::map<std::string, ExtractResult> answers;
    std::map<std::string, int> calls;

    [[nodiscard]] apogee::graph::ExtractFn fn() {
        return [this](std::string_view text, const apogee::harness::CancellationToken&) {
            const std::string key{text};
            ++calls[key];
            ExtractOutcome outcome;
            outcome.attempts = 1;
            const auto it = answers.find(key);
            outcome.result = it == answers.end() ? ExtractResult{} : it->second;
            return outcome;
        };
    }
};

/// docs (a.md: two chunks) and meetings (m.md: one chunk); every chunk
/// mentions Atlas, the docs ones also Vault with the same relation.
struct Fixture {
    apogee::testing::TempDir dir{"graph-multi-" + std::to_string(std::random_device{}())};
    Store graph{dir.path() / "work.db"};
    Store docs{dir.path() / "docs.db"};
    Store meetings{dir.path() / "meetings.db"};
    FakeExtractor fake;

    Fixture() {
        docs.replace_source("a.md", {"docs one", "docs two"});
        meetings.replace_source("m.md", {"meeting one"});
        const Relation stores{.source = "Atlas", .target = "Vault", .relation = "stores in"};
        fake.answers["docs one"] = about({"Atlas", "Vault"}, {stores});
        fake.answers["docs two"] = about({"Atlas", "Vault"}, {stores});
        fake.answers["meeting one"] = about({"Atlas", "Vault"}, {stores});
    }

    [[nodiscard]] std::vector<Member> members() const {
        return {Member{.collection = "docs", .store = &docs},
                Member{.collection = "meetings", .store = &meetings}};
    }

    [[nodiscard]] static BuildOptions opts() {
        BuildOptions options;
        options.model = "m";
        options.graph_name = "work";
        return options;
    }
};

}  // namespace

TEST_CASE(
    "a named build makes one node per entity across members, with labelled mentions, "
    "accumulated weight, per-collection state and its identity stamped",
    "[graph][multi][build]") {
    Fixture f;
    const BuildResult result =
        apogee::graph::build_multi(f.graph, f.members(), f.fake.fn(), nullptr, Fixture::opts());
    CHECK(result.files_planned == 2);
    CHECK(result.files_extracted == 2);
    CHECK(result.chunks_done == 3);
    CHECK(result.nodes_upserted == 2);
    CHECK(result.mentions_added == 6);
    const auto stats = f.graph.graph_stats_multi(
        apogee::embedstore::MemberStores{{"docs", &f.docs}, {"meetings", &f.meetings}});
    CHECK(stats.totals.nodes == 2);
    CHECK(stats.totals.mentions == 6);
    CHECK(stats.totals.total_chunks == 3);
    CHECK(stats.totals.chunks_with_mentions == 3);
    CHECK(stats.totals.stale_files == 0);
    // ONE Atlas, mentioned from both members; the relation corroborated by
    // every chunk across both.
    const apogee::embedstore::GraphNode atlas = f.graph.find_nodes("Atlas").front();
    CHECK(atlas.mention_count == 3);
    const auto refs = f.graph.node_mention_refs(atlas.id, 0);
    REQUIRE(refs.size() == 3);
    CHECK(refs[0].collection == "docs");
    CHECK(refs[2].collection == "meetings");
    CHECK(f.graph.node_neighbors(atlas.id).front().weight == 3);
    CHECK(f.graph.source_states("docs").size() == 1);
    CHECK(f.graph.source_states("meetings").size() == 1);
    CHECK(f.graph.source_states().empty());
    CHECK(f.graph.graph_meta(apogee::embedstore::kGraphMetaGraphName) == "work");
    CHECK(f.graph.graph_members() == std::vector<std::string>{"docs", "meetings"});
    CHECK(f.graph.graph_meta(apogee::embedstore::kGraphMetaExtractModel) == "m");
    // Nothing was written to a member.
    CHECK(f.docs.graph_stats().nodes == 0);
    CHECK(f.meetings.graph_stats().nodes == 0);
    // A second build plans nothing.
    const BuildResult again =
        apogee::graph::build_multi(f.graph, f.members(), f.fake.fn(), nullptr, Fixture::opts());
    CHECK(again.files_planned == 0);
    CHECK(f.fake.calls["docs one"] == 1);
}

TEST_CASE("membership converges: a member dropped from the list has its rows reconciled away",
          "[graph][multi][converge]") {
    Fixture f;
    (void)apogee::graph::build_multi(f.graph, f.members(), f.fake.fn(), nullptr, Fixture::opts());
    const BuildResult narrowed =
        apogee::graph::build_multi(f.graph, {Member{.collection = "docs", .store = &f.docs}},
                                   f.fake.fn(), nullptr, Fixture::opts());
    CHECK(narrowed.files_planned == 0);
    CHECK(narrowed.reconcile.mentions_pruned == 2);
    CHECK(narrowed.reconcile.states_pruned == 1);
    CHECK(f.graph.find_nodes("Atlas").front().mention_count == 2);
    CHECK(f.graph.source_states("meetings").empty());
    CHECK(f.graph.graph_members() == std::vector<std::string>{"docs"});
    // Weight is not recomputed by reconcile -- the meetings corroboration
    // stays until the edge is re-derived -- which is Ommi's shape too.
    CHECK(f.graph.node_neighbors(f.graph.find_nodes("Atlas").front().id).front().weight == 3);
}

TEST_CASE("the limit stops across members and the next build resumes with the rest",
          "[graph][multi][resume]") {
    Fixture f;
    BuildOptions options = Fixture::opts();
    options.limit = 2;
    const BuildResult first =
        apogee::graph::build_multi(f.graph, f.members(), f.fake.fn(), nullptr, options);
    CHECK(first.limit_hit);
    CHECK(first.files_extracted == 1);  // docs/a.md complete, meetings/m.md interrupted
    CHECK(f.graph.source_states("docs").size() == 1);
    CHECK(f.graph.source_states("meetings").empty());
    const BuildResult second =
        apogee::graph::build_multi(f.graph, f.members(), f.fake.fn(), nullptr, Fixture::opts());
    CHECK(second.files_planned == 1);
    CHECK(second.files_extracted == 1);
    CHECK(f.fake.calls["docs one"] == 1);
    CHECK(f.fake.calls["meeting one"] == 1);
    CHECK(f.graph.find_nodes("Atlas").front().mention_count == 3);
}

TEST_CASE("a missing member contributes nothing, and its earlier rows die",
          "[graph][multi][missing]") {
    Fixture f;
    (void)apogee::graph::build_multi(f.graph, f.members(), f.fake.fn(), nullptr, Fixture::opts());
    const BuildResult gone =
        apogee::graph::build_multi(f.graph,
                                   {Member{.collection = "docs", .store = &f.docs},
                                    Member{.collection = "meetings", .store = nullptr}},
                                   f.fake.fn(), nullptr, Fixture::opts());
    CHECK(gone.files_planned == 0);
    CHECK(gone.reconcile.mentions_pruned == 2);
    CHECK(gone.reconcile.states_pruned == 1);
    CHECK(f.graph.find_nodes("Atlas").front().mention_count == 2);
    CHECK(f.graph.graph_members() == std::vector<std::string>{"docs", "meetings"});
    CHECK_THROWS_AS(
        (void)apogee::graph::build_multi(f.graph, {}, f.fake.fn(), nullptr, Fixture::opts()),
        std::invalid_argument);
}

TEST_CASE(
    "a knowledge record in a member is a decision node in the named graph, with its "
    "concerns edge to an entity another member mentions",
    "[graph][multi][records]") {
    Fixture f;
    apogee::knowledge::Store records{f.dir.path() / "knowledge.db", f.dir.path() / "raw"};
    apogee::knowledge::Record record;
    record.id = "kr-1";
    record.decision = "Atlas is the only writer";
    record.intent = "route every reading through Atlas";
    record.status = "shipped";
    record.provenance.source = "chat";
    record.timestamp = apogee::knowledge::timestamp_for(std::chrono::system_clock::now());
    records.put(record, {}, "");
    const std::string index_text = records.chunks().chunk_by_id(*records.chunk_id("kr-1"))->text;
    f.fake.answers[index_text] = about({"Atlas"});

    std::vector<Member> members = f.members();
    members.push_back(Member{.collection = "knowledge", .store = &records.chunks()});
    const BuildResult result =
        apogee::graph::build_multi(f.graph, members, f.fake.fn(), nullptr, Fixture::opts());
    CHECK(result.record_nodes == 1);
    const std::vector<apogee::embedstore::GraphNode> decision = f.graph.find_nodes("kr-1");
    REQUIRE(decision.size() == 1);
    CHECK(decision.front().type == "decision");
    CHECK(f.graph.node_mention_refs(decision.front().id, 0).front().collection == "knowledge");
    // Atlas is ONE node: the docs chunks reach the decision in one hop.
    const auto expansion = f.graph.graph_expand_labelled(
        {apogee::embedstore::ChunkRef{.collection = "docs",
                                      .chunk_id = f.docs.chunks_by_source("a.md").front().id}},
        {}, 1, 8);
    bool reached = false;
    for (const auto& entity : expansion.entities) {
        reached = reached || entity.node.name == "kr-1";
    }
    CHECK(reached);
}

TEST_CASE("a dry run over members stores nothing and stamps no identity",
          "[graph][multi][dry-run]") {
    Fixture f;
    BuildOptions options = Fixture::opts();
    options.dry_run = true;
    const BuildResult result =
        apogee::graph::build_multi(f.graph, f.members(), f.fake.fn(), nullptr, options);
    CHECK(result.dry_run);
    CHECK(result.chunks_done == 3);
    CHECK(f.graph.graph_stats().nodes == 0);
    CHECK(f.graph.graph_members().empty());
    CHECK(f.graph.graph_meta(apogee::embedstore::kGraphMetaGraphName).empty());
}
