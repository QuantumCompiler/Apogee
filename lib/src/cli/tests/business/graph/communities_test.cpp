#include "graph/communities.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "embedstore/graph.h"
#include "embedstore/store.h"
#include "support/env_guard.h"

/// The global layer against fakes: the prompt pinned to the shipped file;
/// the detector (two clusters, the min-size filter, weighted-majority
/// assignment, determinism, edgeless nodes never joining); the key; the
/// rendered text; and build_communities -- populate, the unchanged skip, a
/// forced run, a membership change pruning and regenerating, a soft
/// summariser failure leaving the old summary, the embed phase last and its
/// healing on the next run, cancellation.
namespace {

using apogee::embedstore::GraphEdge;
using apogee::embedstore::GraphNode;
using apogee::embedstore::Store;
using apogee::graph::CommunitiesOptions;
using apogee::graph::CommunitiesResult;

std::string read_asset(const std::string& relative) {
    const std::filesystem::path path = std::filesystem::path{APOGEE_ASSETS_DIR} / relative;
    std::ifstream in{path, std::ios::binary};
    REQUIRE(in);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

GraphEdge edge(std::int64_t id, std::int64_t source, std::int64_t target, std::int64_t weight = 1,
               std::string relation = "links") {
    GraphEdge out;
    out.id = id;
    out.source_id = source;
    out.target_id = target;
    out.weight = weight;
    out.relation = std::move(relation);
    return out;
}

/// A summariser that answers from the rendered text and counts its calls,
/// failing when asked to.
struct FakeSummarizer {
    int calls = 0;
    bool fail = false;
    std::string last_text;

    [[nodiscard]] apogee::graph::SummarizeFn fn() {
        return [this](std::string_view text, const apogee::harness::CancellationToken&) {
            ++calls;
            last_text = std::string{text};
            if (fail) {
                throw std::runtime_error("the summariser is down");
            }
            return std::string{"  Summary #"} + std::to_string(calls) +
                   " of: " + std::string{text.substr(0, 20)} + "\n";
        };
    }
};

struct FakeEmbedder {
    int calls = 0;
    bool fail = false;

    [[nodiscard]] apogee::graph::EmbedFn fn() {
        return [this](std::string_view, const apogee::harness::CancellationToken&) {
            ++calls;
            if (fail) {
                throw std::runtime_error("the embedder is down");
            }
            return std::vector<float>{1.0F, 0.0F};
        };
    }
};

/// A store with one three-node cluster (Atlas, Vault, Probes) and a
/// two-node pair (Ledger, Archive) that is below the default size.
struct Fixture {
    apogee::testing::TempDir dir{"graph-communities-" + std::to_string(std::random_device{}())};
    Store store{dir.path() / "c.db"};
    std::int64_t atlas = 0;
    std::int64_t vault = 0;
    std::int64_t probes = 0;
    std::int64_t ledger = 0;
    std::int64_t archive = 0;

    Fixture() {
        store.replace_source("a.md", {"one", "two"});
        const std::int64_t chunk = store.chunks_by_source("a.md").front().id;
        atlas = store.upsert_node("Atlas", "system", "collects readings").id;
        vault = store.upsert_node("Vault", "system", "the warehouse").id;
        probes = store.upsert_node("Probes", "component", "").id;
        ledger = store.upsert_node("Ledger", "artifact", "").id;
        archive = store.upsert_node("Archive", "artifact", "").id;
        for (const std::int64_t id : {atlas, vault, probes, ledger, archive}) {
            (void)store.add_mention(id, chunk);
        }
        (void)store.add_mention(atlas, 999);
        store.upsert_edge(atlas, vault, "stores readings in", "");
        store.upsert_edge(atlas, vault, "stores readings in", "");
        store.upsert_edge(probes, atlas, "feeds", "raw readings");
        store.upsert_edge(ledger, archive, "is archived to", "");
    }
};

}  // namespace

TEST_CASE("the compiled-in community prompt byte-matches the shipped file",
          "[graph][communities][prompt]") {
    CHECK(std::string{apogee::graph::community_prompt()} ==
          read_asset("clerks/community_prompt.txt"));
    const std::string system = apogee::graph::community_system_prompt();
    CHECK(system.starts_with("You are a corpus analyst"));
    CHECK(system.ends_with("no code fences."));
    CHECK(system.find("OUTPUT FORMAT") == std::string::npos);  // prose, no schema block
}

TEST_CASE("detect_communities partitions by weighted label propagation, deterministically",
          "[graph][communities][detect]") {
    // Two triangles joined by one light edge; a pair; an isolated edgeless
    // node (7) that appears in no edge and so never joins.
    const std::vector<GraphEdge> edges{
        edge(1, 1, 2, 3), edge(2, 2, 3, 3), edge(3, 1, 3, 3), edge(4, 4, 5, 3),
        edge(5, 5, 6, 3), edge(6, 4, 6, 3), edge(7, 3, 4, 1), edge(8, 8, 9, 2),
    };
    const auto communities = apogee::graph::detect_communities(edges, 3);
    REQUIRE(communities.size() == 2);
    CHECK(communities[0] == std::vector<std::int64_t>{1, 2, 3});
    CHECK(communities[1] == std::vector<std::int64_t>{4, 5, 6});
    // The pair joins the listing at min-size 2; the default keeps it out.
    CHECK(apogee::graph::detect_communities(edges, 2).size() == 3);
    CHECK(apogee::graph::detect_communities(edges, 0).size() == 2);
    // Deterministic across runs.
    CHECK(apogee::graph::detect_communities(edges, 3) == communities);
    CHECK(apogee::graph::detect_communities({}, 3).empty());
}

TEST_CASE("a node adopts the label with the greatest edge-weight support",
          "[graph][communities][detect][weight]") {
    // Node 7 touches cluster A (1,2,3) with weight 1 and cluster B (4,5,6)
    // with weight 5: it lands in B, so B is the larger and listed first.
    const std::vector<GraphEdge> edges{
        edge(1, 1, 2, 3), edge(2, 2, 3, 3), edge(3, 1, 3, 3), edge(4, 4, 5, 3),
        edge(5, 5, 6, 3), edge(6, 4, 6, 3), edge(7, 7, 1, 1), edge(8, 7, 4, 5),
    };
    const auto communities = apogee::graph::detect_communities(edges, 3);
    REQUIRE(communities.size() == 2);
    CHECK(communities[0] == std::vector<std::int64_t>{4, 5, 6, 7});
    CHECK(communities[1] == std::vector<std::int64_t>{1, 2, 3});
}

TEST_CASE("a tie in support goes to the smallest label, so the torn node joins the earlier cluster",
          "[graph][communities][detect][tie]") {
    // Node 5 touches cluster A (1,2,3) and cluster B (6,7,8) with equal
    // weight: the smallest label wins the tie, and labels descend from ids,
    // so it joins A -- and A, now four, is listed first.
    const std::vector<GraphEdge> edges{
        edge(1, 1, 2, 3), edge(2, 2, 3, 3), edge(3, 1, 3, 3), edge(4, 6, 7, 3),
        edge(5, 7, 8, 3), edge(6, 6, 8, 3), edge(7, 5, 1, 2), edge(8, 5, 6, 2),
    };
    const auto communities = apogee::graph::detect_communities(edges, 3);
    REQUIRE(communities.size() == 2);
    CHECK(communities[0] == std::vector<std::int64_t>{1, 2, 3, 5});
    CHECK(communities[1] == std::vector<std::int64_t>{6, 7, 8});
}

TEST_CASE("the community key is the sorted member set, and the text lists members then relations",
          "[graph][communities][render]") {
    CHECK(apogee::graph::community_key({3, 1, 2}) == "1,2,3");
    CHECK(apogee::graph::community_key({}) == "");
    Fixture f;
    const std::vector<GraphNode> nodes = f.store.nodes_by_ids({f.atlas, f.vault, f.probes});
    const std::string text = apogee::graph::community_text(nodes, f.store.all_edges());
    CHECK(text ==
          "Entities:\n"
          "- Atlas (system): collects readings\n"
          "- Probes (component)\n"
          "- Vault (system): the warehouse\n"
          "Relations:\n"
          "- Atlas —[stores readings in]→ Vault\n"
          "- Probes —[feeds]→ Atlas: raw readings\n");
    // The relation cap: a cluster with more than the cap keeps the heaviest.
    std::vector<GraphEdge> many;
    for (std::int64_t i = 0; i < 30; ++i) {
        many.push_back(edge(i + 1, f.atlas, f.vault, 30 - i, "r" + std::to_string(i)));
    }
    const std::string capped = apogee::graph::community_text(nodes, many);
    std::size_t lines = 0;
    for (const char c : capped) {
        lines += c == '\n' ? 1 : 0;
    }
    CHECK(lines == 1 + 3 + 1 + apogee::graph::kMaxCommunityRelationLines);
    CHECK(capped.find("—[r0]→") != std::string::npos);
    CHECK(capped.find("—[r25]→") == std::string::npos);
}

TEST_CASE(
    "build_communities summarises new clusters once, skips unchanged ones, and prunes "
    "what no longer exists",
    "[graph][communities][build]") {
    Fixture f;
    FakeSummarizer summarizer;
    CommunitiesOptions options;
    options.model = "sum";
    std::vector<std::pair<int, int>> progress;
    options.on_progress = [&](int done, int total) { progress.emplace_back(done, total); };

    CommunitiesResult first =
        apogee::graph::build_communities(f.store, summarizer.fn(), {}, options);
    CHECK(first.detected == 1);
    CHECK(first.summarized == 1);
    CHECK(first.unchanged == 0);
    CHECK(first.pruned == 0);
    CHECK(first.embedded == 0);
    CHECK(summarizer.calls == 1);
    CHECK(summarizer.last_text.starts_with("Entities:\n- Atlas (system)"));
    CHECK(progress == std::vector<std::pair<int, int>>{{0, 1}});
    const auto stored = f.store.graph_communities();
    REQUIRE(stored.size() == 1);
    CHECK(stored.front().summary == "Summary #1 of: Entities:\n- Atlas (s");  // trimmed
    CHECK(stored.front().model == "sum");
    CHECK(stored.front().size == 3);
    // Retrievable through plain search, at once.
    CHECK(f.store.search("Summary", 5).size() == 1);

    // Unchanged membership: no call, no change.
    const CommunitiesResult second =
        apogee::graph::build_communities(f.store, summarizer.fn(), {}, options);
    CHECK(second.unchanged == 1);
    CHECK(second.summarized == 0);
    CHECK(summarizer.calls == 1);
    CHECK(f.store.graph_communities().front().id == stored.front().id);

    // Forced: regenerated, the row replaced under the same key.
    options.force = true;
    const CommunitiesResult forced =
        apogee::graph::build_communities(f.store, summarizer.fn(), {}, options);
    CHECK(forced.summarized == 1);
    CHECK(summarizer.calls == 2);
    REQUIRE(f.store.graph_communities().size() == 1);
    CHECK(f.store.graph_communities().front().summary.starts_with("Summary #2"));
    options.force = false;

    // A membership change: the pair joins the cluster through a new edge,
    // so the old community is pruned and the new one summarised.
    f.store.upsert_edge(f.vault, f.ledger, "keeps", "");
    f.store.upsert_edge(f.vault, f.ledger, "keeps", "");
    f.store.upsert_edge(f.vault, f.ledger, "keeps", "");
    const CommunitiesResult changed =
        apogee::graph::build_communities(f.store, summarizer.fn(), {}, options);
    CHECK(changed.detected == 1);
    CHECK(changed.summarized == 1);
    CHECK(changed.pruned == 1);
    CHECK(summarizer.calls == 3);
    REQUIRE(f.store.graph_communities().size() == 1);
    CHECK(f.store.graph_communities().front().size == 5);
    CHECK(f.store.search("Summary", 5).size() == 1);  // one pseudo-chunk, not two
}

TEST_CASE("a summariser failure is soft and leaves the old summary in place",
          "[graph][communities][build][failure]") {
    Fixture f;
    FakeSummarizer summarizer;
    CommunitiesOptions options;
    (void)apogee::graph::build_communities(f.store, summarizer.fn(), {}, options);
    REQUIRE(f.store.graph_communities().size() == 1);
    const std::string kept = f.store.graph_communities().front().summary;

    summarizer.fail = true;
    options.force = true;
    const CommunitiesResult failed =
        apogee::graph::build_communities(f.store, summarizer.fn(), {}, options);
    CHECK(failed.failed == 1);
    CHECK(failed.summarized == 0);
    CHECK(failed.pruned == 0);  // the key is still current: kept, not swept
    REQUIRE(f.store.graph_communities().size() == 1);
    CHECK(f.store.graph_communities().front().summary == kept);

    // An empty answer is a failure too.
    const auto blank = [](std::string_view, const apogee::harness::CancellationToken&) {
        return std::string{"   \n"};
    };
    CHECK(apogee::graph::build_communities(f.store, blank, {}, options).failed == 1);
    CHECK_THROWS_AS((void)apogee::graph::build_communities(f.store, {}, {}, options),
                    std::invalid_argument);
}

TEST_CASE(
    "the embed phase runs last over every summary without a vector, and heals a failure "
    "on the next run",
    "[graph][communities][build][embed]") {
    Fixture f;
    FakeSummarizer summarizer;
    FakeEmbedder embedder;
    embedder.fail = true;
    CommunitiesOptions options;
    const CommunitiesResult first =
        apogee::graph::build_communities(f.store, summarizer.fn(), embedder.fn(), options);
    CHECK(first.summarized == 1);
    CHECK(first.embedded == 0);
    CHECK(first.embed_error == "the embedder is down");
    CHECK(f.store.graph_communities().size() == 1);  // the summary is kept
    CHECK(f.store.communities_without_vectors().size() == 1);

    embedder.fail = false;
    const CommunitiesResult second =
        apogee::graph::build_communities(f.store, summarizer.fn(), embedder.fn(), options);
    CHECK(second.unchanged == 1);
    CHECK(second.summarized == 0);
    CHECK(second.embedded == 1);  // healed without a forced regeneration
    CHECK(second.embed_error.empty());
    CHECK(f.store.communities_without_vectors().empty());
    CHECK(f.store.search_vector({1.0F, 0.0F}, 5).size() == 1);
    CHECK(summarizer.calls == 1);
    CHECK(embedder.calls == 2);
}

TEST_CASE("cancellation between communities stops the run and keeps what finished",
          "[graph][communities][build][cancel]") {
    Fixture f;
    // Two clusters: the pair becomes one at min-size 2.
    FakeSummarizer summarizer;
    const apogee::harness::CancellationToken token = apogee::harness::CancellationToken::create();
    CommunitiesOptions options;
    options.min_size = 2;
    options.cancellation = token;
    options.on_progress = [&](int done, int) {
        if (done == 1) {
            token.cancel();
        }
    };
    const CommunitiesResult result =
        apogee::graph::build_communities(f.store, summarizer.fn(), {}, options);
    CHECK(result.cancelled);
    CHECK(result.summarized == 1);
    CHECK(summarizer.calls == 1);
    CHECK(f.store.graph_communities().size() == 1);
}
