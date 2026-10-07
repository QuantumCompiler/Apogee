#include "graph/report.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "embedstore/store.h"
#include "graph/navigate.h"
#include "support/env_guard.h"
#include "support/graph_fixture.h"

/// The architecture report (27m) over the committed fixture graph -- a named
/// graph `work` whose code tree `app` and collection `notes` meet at one
/// inferred relation -- with the report's twelve orphans added. No model, no
/// network: every fact is the store's, every hub 27l's degree, every list
/// capped and counted.
namespace {

using apogee::embedstore::Store;

[[nodiscard]] bool contains(std::string_view text, std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

/// The named shape: the graph's own database, its member's chunks beside.
struct Named {
    apogee::testing::TempDir dir{"graph-report-" + std::to_string(std::random_device{}())};
    Store notes{dir.path() / "notes.db"};
    Store work{dir.path() / "work.db"};

    explicit Named(bool orphans = true) {
        apogee::testing::build_navigation_graph(work, notes, "notes");
        if (orphans) {
            apogee::testing::add_report_orphans(work, notes, "notes");
        }
    }

    [[nodiscard]] apogee::embedstore::GraphNode node(std::string_view address) const {
        return apogee::graph::resolve_node(work, "work", address).node;
    }

    /// The fixture's one community again, now with no summary -- as a
    /// clustering with no model stores it.
    void unsummarise() {
        const std::string key = std::to_string(node("system:Atlas").id) + "," +
                                std::to_string(node("Vault").id) + "," +
                                std::to_string(node("kr-0001").id);
        (void)work.replace_community(
            key, {node("system:Atlas").id, node("Vault").id, node("kr-0001").id}, "", "");
    }
};

[[nodiscard]] std::vector<std::string> names_of(const std::vector<apogee::graph::NodeRef>& refs) {
    std::vector<std::string> out;
    out.reserve(refs.size());
    for (const apogee::graph::NodeRef& ref : refs) {
        out.push_back(ref.name);
    }
    return out;
}

}  // namespace

TEST_CASE("the report states the store's own counts and the origin mix",
          "[graph][report][overview]") {
    const Named fixture;
    const apogee::graph::GraphReport report = apogee::graph::build_report(fixture.work, "work");
    const apogee::embedstore::GraphStats stats = fixture.work.graph_stats();

    CHECK(report.graph == "work");
    CHECK(report.entities == stats.nodes);
    CHECK(report.relations == stats.edges);
    CHECK(report.by_type == stats.nodes_by_type);
    CHECK(report.unresolved_names == 2);
    CHECK(report.extracted == stats.edges_extracted);
    CHECK(report.inferred == stats.edges_inferred);
    CHECK(report.extracted + report.inferred == report.relations);
    CHECK(report.extracted > 0);
    CHECK(report.inferred > 0);
    // The members the store states: the code tree and the collection.
    CHECK(report.members == std::vector<std::string>{"app", "notes"});
    CHECK(report.cross_member);

    const nlohmann::json document = apogee::graph::to_json(report);
    CHECK(document["object"] == "graph.report");
    CHECK(document["origin"]["extracted"] == report.extracted);
    CHECK(document["origin"]["inferred"] == report.inferred);
    CHECK(document["hubs"]["metric"] == "degree");
}

TEST_CASE("hubs are 27l's degree ranking: the top ten, the card's own degree, no names",
          "[graph][report][hubs]") {
    const Named fixture;
    const apogee::graph::GraphReport report = apogee::graph::build_report(fixture.work, "work");
    const std::vector<apogee::graph::RankedNode> ranked =
        apogee::graph::rank_by_degree(fixture.work);

    REQUIRE(report.hubs.size() == apogee::graph::kReportHubs);
    CHECK(report.ranked == ranked.size());
    // Every node but the two unresolved names is ranked.
    CHECK(static_cast<std::int64_t>(report.ranked) == report.entities - report.unresolved_names);
    for (std::size_t i = 0; i < report.hubs.size(); ++i) {
        const apogee::graph::ReportHub& hub = report.hubs[i];
        INFO(hub.node.name);
        CHECK(hub.node.name == ranked[i].node.name);
        CHECK_FALSE(hub.node.unresolved);
        if (i > 0) {
            CHECK(hub.degree.total <= report.hubs[i - 1].degree.total);
        }
        // One source of truth: the card states the same degree.
        const apogee::graph::NodeCard card = apogee::graph::node_card(
            fixture.work, {}, "work", ranked[i].node, apogee::graph::kDefaultNeighborsPerRelation);
        CHECK(hub.degree.total == card.degree.total);
        CHECK(hub.degree.out == card.degree.out);
        CHECK(hub.degree.in == card.degree.in);
        std::int64_t counted = 0;
        for (const apogee::graph::NeighborGroup& group : hub.relations) {
            counted += group.total;
            CHECK(group.shown.size() <= static_cast<std::size_t>(apogee::graph::kReportNeighbors));
        }
        CHECK(counted == hub.degree.total);
    }
    // The busiest entity: pkg.lib.helper, called fifteen times past the cap.
    CHECK(report.hubs.front().node.name == "pkg.lib.helper");
    CHECK(report.hubs.front().degree.total == 18);
    const auto calls_in = std::ranges::find_if(report.hubs.front().relations, [](const auto& g) {
        return g.relation == "calls" && !g.outgoing;
    });
    REQUIRE(calls_in != report.hubs.front().relations.end());
    CHECK(calls_in->total == 17);
    CHECK(calls_in->shown.size() == 3);

    // The unresolved `.push_back` has fifteen edges; a name is not a hub.
    for (const apogee::graph::RankedNode& entry : ranked) {
        CHECK(entry.node.type != "name");
    }
}

TEST_CASE("communities carry the summaries they have, and say per community when there is none",
          "[graph][report][communities]") {
    Named fixture;
    {
        const apogee::graph::GraphReport report = apogee::graph::build_report(fixture.work, "work");
        CHECK(report.communities_total == 1);
        CHECK(report.communities_unsummarised == 0);
        REQUIRE(report.communities.size() == 1);
        CHECK(report.communities.front().summary ==
              "Atlas and the Vault it writes its readings to.");
        CHECK(report.communities.front().size == 3);
        CHECK(report.communities.front().members.size() == 3);
        CHECK(contains(report.human_summary, "1 community is stored; 1 has a summary."));
    }

    // Clustered with no model: the report reads what is stored and never
    // generates -- the community says it has no summary.
    fixture.unsummarise();
    const apogee::graph::GraphReport report = apogee::graph::build_report(fixture.work, "work");
    CHECK(report.communities_total == 1);
    CHECK(report.communities_unsummarised == 1);
    REQUIRE(report.communities.size() == 1);
    CHECK(report.communities.front().summary.empty());
    const nlohmann::json document = apogee::graph::to_json(report);
    CHECK_FALSE(document["communities"]["shown"][0].contains("summary"));
    CHECK(document["communities"]["unsummarised"] == 1);
    CHECK(contains(report.human_summary, "1 community is stored; none has a summary."));
}

TEST_CASE("a community names its five most-mentioned members, the ten largest listed",
          "[graph][report][communities]") {
    Named fixture;
    // Eleven more communities, the callers' and the chain's among them.
    std::vector<std::int64_t> callers;
    for (int i = 1; i <= 15; ++i) {
        callers.push_back(
            fixture.node("pkg.callers.c" + std::string(i < 10 ? "0" : "") + std::to_string(i)).id);
    }
    (void)fixture.work.replace_community("callers", callers, "", "");
    for (int i = 0; i <= 9; ++i) {
        const std::int64_t a = fixture.node("pkg.chain.n" + std::to_string(i)).id;
        const std::int64_t b = fixture.node("pkg.chain.n" + std::to_string(i + 1)).id;
        (void)fixture.work.replace_community("chain" + std::to_string(i), {a, b}, "", "");
    }
    const apogee::graph::GraphReport report = apogee::graph::build_report(fixture.work, "work");
    CHECK(report.communities_total == 12);
    CHECK(report.communities_unsummarised == 11);
    REQUIRE(report.communities.size() == apogee::graph::kReportCommunities);
    // Largest first: the callers' fifteen, five of them named.
    CHECK(report.communities.front().size == 15);
    CHECK(report.communities.front().members.size() == apogee::graph::kReportCommunityMembers);
    CHECK(contains(report.human_summary, "12 communities are stored; 1 has a summary."));
}

TEST_CASE("a graph with no communities stored says so", "[graph][report][communities]") {
    const apogee::testing::TempDir dir{"graph-report-bare-" +
                                       std::to_string(std::random_device{}())};
    Store store{dir.path() / "bare.db"};
    store.replace_source("a.md", {"Atlas and Vault."});
    const std::int64_t chunk = store.chunks_by_source("a.md").front().id;
    const std::int64_t atlas = store.upsert_node("Atlas", "system", "probes").id;
    const std::int64_t vault = store.upsert_node("Vault", "system", "storage").id;
    (void)store.add_mention(atlas, chunk);
    (void)store.add_mention(vault, chunk);
    store.upsert_edge(atlas, vault, "writes to", "");

    const apogee::graph::GraphReport report = apogee::graph::build_report(store, "bare");
    CHECK(report.communities_total == 0);
    CHECK(report.communities.empty());
    CHECK(contains(report.human_summary, "No communities are stored."));
    // A collection's own graph is the one `""` member: nothing to link.
    CHECK(report.members == std::vector<std::string>{""});
    CHECK_FALSE(report.cross_member);
    CHECK_FALSE(apogee::graph::to_json(report).contains("links"));
    CHECK(contains(report.human_summary, "Every relation was asserted by a model."));
}

TEST_CASE("cross-collection links: relations whose endpoints share no member, and shared entities",
          "[graph][report][links]") {
    Named fixture;
    const apogee::graph::GraphReport report = apogee::graph::build_report(fixture.work, "work");
    // Vault (notes) -[implemented by]-> pkg.lib.Store (app): the one bridge.
    CHECK(report.crossings_total == 1);
    REQUIRE(report.crossings.size() == 1);
    CHECK(report.crossings.front().from.name == "Vault");
    CHECK(report.crossings.front().from_members == std::vector<std::string>{"notes"});
    CHECK(report.crossings.front().relation == "implemented by");
    CHECK(report.crossings.front().origin == "inferred");
    CHECK(report.crossings.front().to.name == "pkg.lib.Store");
    CHECK(report.crossings.front().to_members == std::vector<std::string>{"app"});
    REQUIRE(report.links.size() == 1);
    CHECK(report.links.front().from == "app");
    CHECK(report.links.front().to == "notes");
    CHECK(report.links.front().relations == 1);
    CHECK(report.links.front().shared == 0);
    CHECK(report.shared_total == 0);

    // An entity both members state is shared, and links them.
    const apogee::graph::NodeRef vault = apogee::graph::node_ref(fixture.node("Vault"));
    (void)fixture.work.add_mention(fixture.node("Vault").id, "app", 1);
    const apogee::graph::GraphReport shared = apogee::graph::build_report(fixture.work, "work");
    CHECK(shared.shared_total == 1);
    REQUIRE(shared.shared.size() == 1);
    CHECK(shared.shared.front().node.name == vault.name);
    CHECK(shared.shared.front().members == std::vector<std::string>{"app", "notes"});
    CHECK(shared.links.front().shared == 1);
    // Stated in both, Vault's relation to the Store no longer crosses.
    CHECK(shared.crossings_total == 0);

    // A named graph of one member has nothing to link.
    const apogee::testing::TempDir dir{"graph-report-one-" +
                                       std::to_string(std::random_device{}())};
    Store notes{dir.path() / "notes.db"};
    Store one{dir.path() / "one.db"};
    notes.replace_source("a.md", {"Atlas and Vault."});
    const std::int64_t chunk = notes.chunks_by_source("a.md").front().id;
    const std::int64_t atlas = one.upsert_node("Atlas", "system", "").id;
    (void)one.add_mention(atlas, "notes", chunk);
    const apogee::graph::GraphReport single = apogee::graph::build_report(one, "one");
    CHECK(single.members == std::vector<std::string>{"notes"});
    CHECK_FALSE(single.cross_member);
    CHECK(single.links.empty());
}

TEST_CASE("decisions list what they concern, the most connected first, ten at most",
          "[graph][report][decisions]") {
    const Named fixture;
    const apogee::graph::GraphReport report = apogee::graph::build_report(fixture.work, "work");
    CHECK(report.decisions_total == 14);
    REQUIRE(report.decisions.size() == apogee::graph::kReportDecisions);
    // Every record concerns one node; the shipped one is mentioned, so first.
    const apogee::graph::ReportDecision& first = report.decisions.front();
    CHECK(first.node.name == "kr-0001");
    CHECK(first.node.status == "shipped");
    CHECK(first.node.discipline == "engineering");
    CHECK(first.decision == "Keep every reading in Vault — one store, one backup");
    REQUIRE(first.relations.size() == 1);
    CHECK(first.relations.front().relation == "concerns");
    CHECK(first.relations.front().outgoing);
    REQUIRE(first.relations.front().shown.size() == 1);
    CHECK(first.relations.front().shown.front().node.name == "Vault");
    CHECK(report.decisions[1].node.name == "kr-0101");
    CHECK(report.decisions.back().node.name == "kr-0109");
    CHECK(contains(report.human_summary, "14 decision records are attached."));
}

TEST_CASE("orphans: the entities with no relation, the most mentioned ten, counted",
          "[graph][report][orphans]") {
    const Named fixture;
    const apogee::graph::GraphReport report = apogee::graph::build_report(fixture.work, "work");
    CHECK(report.orphans_total == 12);
    REQUIRE(report.orphans.size() == apogee::graph::kReportOrphans);
    std::vector<std::string> names;
    for (const apogee::graph::ReportOrphan& orphan : report.orphans) {
        names.push_back(orphan.node.name);
    }
    CHECK(names == std::vector<std::string>{"Orphan 12", "Orphan 11", "Orphan 10", "Orphan 09",
                                            "Orphan 08", "Orphan 07", "Orphan 06", "Orphan 05",
                                            "Orphan 04", "Orphan 03"});
    CHECK(report.orphans.front().mentions == 12);
    CHECK(report.orphans.front().description == "An orphan, mentioned 12 time(s).");
    CHECK(contains(report.human_summary, "12 entities stand alone, with no relation."));

    const Named connected{false};
    const apogee::graph::GraphReport none = apogee::graph::build_report(connected.work, "work");
    CHECK(none.orphans_total == 0);
    CHECK(none.orphans.empty());
}

TEST_CASE("the report's community members and summary sentence name the store's facts",
          "[graph][report][summary]") {
    const Named fixture;
    const apogee::graph::GraphReport report = apogee::graph::build_report(fixture.work, "work");
    CHECK(names_of(report.communities.front().members) ==
          std::vector<std::string>{"Vault", "Atlas", "kr-0001"});
    CHECK(report.human_summary.starts_with(
        "Graph \"work\" holds " + std::to_string(report.entities) + " entities and " +
        std::to_string(report.relations) + " relations across 2 members (app, notes)."));
    CHECK(contains(report.human_summary,
                   "Its busiest entity is pkg.lib.helper (function), with 18 relations."));
    CHECK(contains(report.human_summary, "1 relation crosses between members."));
}

TEST_CASE("building the report writes nothing", "[graph][report][readonly]") {
    const Named fixture;
    const std::string before = fixture.work.graph_dump();
    (void)apogee::graph::build_report(fixture.work, "work");
    CHECK(fixture.work.graph_dump() == before);
}
