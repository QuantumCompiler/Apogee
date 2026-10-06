#include "render/graph_report.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <string_view>

#include "embedstore/store.h"
#include "graph/navigate.h"
#include "graph/report.h"
#include "support/env_guard.h"
#include "support/graph_fixture.h"

/// `graph report`'s Markdown (27m), byte-stable over the committed fixture
/// graph: the golden page, and the same page when the communities were
/// clustered with no model -- the no-summaries fallback said per community.
/// `APOGEE_GRAPH_REPORT_GOLDENS=<dir>` writes the pages there for review,
/// never into the tree.
namespace {

using apogee::embedstore::Store;

[[nodiscard]] bool contains(std::string_view text, std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

[[nodiscard]] std::string read_golden(const std::string& name) {
    const std::filesystem::path path =
        std::filesystem::path{APOGEE_TEST_FIXTURES} / "graph_report" / name;
    std::ifstream in{path, std::ios::binary};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

/// Writes `page` for review when asked to; never into the tree.
void offer(const std::string& name, const std::string& page) {
    const char* dir = std::getenv("APOGEE_GRAPH_REPORT_GOLDENS");
    if (dir != nullptr && *dir != '\0') {
        std::ofstream{std::filesystem::path{dir} / name, std::ios::binary} << page;
    }
}

struct Named {
    apogee::testing::TempDir dir{"graph-report-md-" + std::to_string(std::random_device{}())};
    Store notes{dir.path() / "notes.db"};
    Store work{dir.path() / "work.db"};

    Named() {
        apogee::testing::build_navigation_graph(work, notes, "notes");
        apogee::testing::add_report_orphans(work, notes, "notes");
    }

    [[nodiscard]] std::int64_t id(std::string_view address) const {
        return apogee::graph::resolve_node(work, "work", address).node.id;
    }

    [[nodiscard]] std::string page() const {
        return apogee::render::render_graph_report(
            apogee::graph::to_json(apogee::graph::build_report(work, "work")));
    }
};

}  // namespace

TEST_CASE("the report's Markdown is byte-stable over the fixture graph",
          "[render][graph][report][golden]") {
    const Named fixture;
    const std::string page = fixture.page();
    offer("work.md", page);
    CHECK(page == read_golden("work.md"));
    CHECK(fixture.page() == page);
    // The origin-mix line, as `graph stats` words it.
    CHECK(contains(page, " extracted (parsed from source), "));
    CHECK(contains(page, " inferred (asserted by a model).\n"));
}

TEST_CASE("with no summaries stored, each community says so -- nothing is generated",
          "[render][graph][report][golden]") {
    Named fixture;
    const std::int64_t atlas = fixture.id("system:Atlas");
    const std::int64_t vault = fixture.id("Vault");
    const std::int64_t record = fixture.id("kr-0001");
    (void)fixture.work.replace_community(
        std::to_string(atlas) + "," + std::to_string(vault) + "," + std::to_string(record),
        {atlas, vault, record}, "", "");
    const std::string page = fixture.page();
    offer("work_no_summaries.md", page);
    CHECK(page == read_golden("work_no_summaries.md"));
    CHECK(contains(page, "_No summary -- clustered with no model._"));
    CHECK(contains(page, "`apogee graph communities work -m <backend>` writes them"));
    CHECK_FALSE(contains(page, "Atlas and the Vault it writes its readings to."));
}

TEST_CASE("the sections run in reading order, and the human summary is last",
          "[render][graph][report]") {
    const Named fixture;
    const std::string page = fixture.page();
    std::size_t at = 0;
    for (const std::string_view heading :
         {"# Graph report: work\n", "\n## Overview\n", "\n## Origin\n", "\n## Hubs\n",
          "\n## Communities\n", "\n## Cross-collection links\n", "\n## Decisions\n",
          "\n## Orphans\n", "\n## Summary\n"}) {
        INFO(heading);
        const std::size_t found = page.find(heading, at);
        REQUIRE(found != std::string::npos);
        at = found;
    }
    // Nothing after the summary but its paragraph.
    CHECK(page.find("\n## ", at + 1) == std::string::npos);
    CHECK(page.ends_with(".\n"));
}

TEST_CASE("names are code spans whatever they hold; a collection's own graph links nothing",
          "[render][graph][report]") {
    CHECK(apogee::render::code_span("plain") == "`plain`");
    CHECK(apogee::render::code_span("a`b") == "``a`b``");
    CHECK(apogee::render::code_span("`edge`") == "`` `edge` ``");
    CHECK(apogee::render::code_span("two\nlines") == "`two lines`");

    const apogee::testing::TempDir dir{"graph-report-own-" +
                                       std::to_string(std::random_device{}())};
    Store store{dir.path() / "own.db"};
    store.replace_source("a.md", {"Atlas and Vault."});
    const std::int64_t chunk = store.chunks_by_source("a.md").front().id;
    const std::int64_t atlas = store.upsert_node("Atlas | Co", "system", "probes").id;
    const std::int64_t vault = store.upsert_node("Vault", "system", "storage").id;
    (void)store.add_mention(atlas, chunk);
    (void)store.add_mention(vault, chunk);
    store.upsert_edge(atlas, vault, "writes to", "");
    const std::string page = apogee::render::render_graph_report(
        apogee::graph::to_json(apogee::graph::build_report(store, "own")));
    CHECK_FALSE(contains(page, "## Cross-collection links"));
    CHECK_FALSE(contains(page, "## Decisions"));
    CHECK(contains(page, "No communities are stored -- `apogee graph communities own` clusters"));
    CHECK(contains(page, "Every entity has at least one relation."));
    CHECK(contains(page, "1. `Atlas | Co` (system) -- 1 relation (1 out, 0 in), 1 mention\n"));
}

TEST_CASE("a table cell escapes its pipes; a named graph of one member says it has nothing to link",
          "[render][graph][report]") {
    const nlohmann::json report{
        {"object", "graph.report"},
        {"graph", "g"},
        {"overview", {{"members", {"a|b", "c"}}}},
        {"links",
         {{"pairs",
           {{"total", 1},
            {"shown", {{{"from", "a|b"}, {"to", "c"}, {"relations", 2}, {"shared", 0}}}}}},
          {"crossings", {{"total", 0}, {"shown", nlohmann::json::array()}}},
          {"shared", {{"total", 0}, {"shown", nlohmann::json::array()}}}}},
        {"human_summary", "s"}};
    const std::string page = apogee::render::render_graph_report(report);
    CHECK(contains(page, "| `a\\|b` -- `c` | 2 | 0 |\n"));
    CHECK(contains(page, "no entity is stated in more than one."));

    const nlohmann::json single{{"object", "graph.report"},
                                {"graph", "one"},
                                {"overview", {{"members", {"notes"}}}},
                                {"human_summary", "s"}};
    CHECK(contains(apogee::render::render_graph_report(single),
                   "## Cross-collection links\n\nOne member, `notes` -- nothing to link.\n"));
}
