#include "commands/graph.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "agentloop/graph_context.h"
#include "commands/helpers.h"
#include "commands/registry.h"
#include "commands/root.h"
#include "embedstore/store.h"
#include "harness/config.h"
#include "harness/harness.h"
#include "support/env_guard.h"

/// `apogee graph` on the scripted mock as the extractor: a build over an
/// ingested collection, the cost policy (a metered default refused, a named
/// backend allowed, a vendor CLI refused), `--dry-run`, resume, the
/// auto-enable write, stats, show, delete.
namespace {

using apogee::embedstore::Store;

constexpr std::string_view kExtraction =
    R"({"entities": [{"name": "Atlas", "type": "system", "description": "collects readings"},
                     {"name": "Vault", "type": "system", "description": "the warehouse"}],
        "relations": [{"source": "Atlas", "target": "Vault", "relation": "stores readings in",
                       "description": ""}]})";

std::string script_of(const std::vector<std::string>& texts) {
    nlohmann::json turns = nlohmann::json::array();
    for (const std::string& text : texts) {
        turns.push_back({{"text", text}});
    }
    return nlohmann::json{{"turns", turns}}.dump();
}

void write(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << content;
}

std::string bytes(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

struct Fixture {
    apogee::testing::TempDir home{"graph-cli-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    std::filesystem::path config_path = home.path() / "config" / "config.yaml";

    /// `default_metered` makes the default backend a paid one (Anthropic
    /// with a key), which the cost policy refuses by fall-through.
    explicit Fixture(std::string_view extra_config = {}, bool default_metered = false) {
        write(home.path() / "extractor.json", script_of({std::string{kExtraction}}));
        write(home.path() / "prose.json", script_of({"I cannot produce JSON.", "still prose"}));
        // A mock that claims to be metered: the cost policy against a paid
        // backend, with no network.
        write(
            home.path() / "metered.json",
            nlohmann::json{{"turns", nlohmann::json::array({{{"text", std::string{kExtraction}}}})},
                           {"metered", true}}
                .dump());
        write(home.path() / "docs" / "a.md", "Atlas collects readings from the field probes.");
        write(home.path() / "docs" / "b.md", "The warehouse keeps every record for seven years.");
        std::string config =
            "models:\n  default: " + std::string{default_metered ? "paid" : "extractor"} + "\n";
        config += "backends:\n";
        config += "  extractor:\n    type: mock\n    model_path: " +
                  (home.path() / "extractor.json").string() + "\n";
        config +=
            "  prose:\n    type: mock\n    model_path: " + (home.path() / "prose.json").string() +
            "\n";
        config += "  paid:\n    type: anthropic\n    api_key: sk-ant-test\n";
        config += "  paidmock:\n    type: mock\n    model_path: " +
                  (home.path() / "metered.json").string() + "\n";
        config += "  vendor:\n    type: claude-cli\n";
        config += extra_config;
        write(config_path, config);
    }

    int run(const std::vector<std::string>& args, std::string* out = nullptr,
            std::string* err = nullptr) const {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        std::istringstream fed{std::string{}};
        std::streambuf* old_out = std::cout.rdbuf(captured_out.rdbuf());
        std::streambuf* old_err = std::cerr.rdbuf(captured_err.rdbuf());
        std::streambuf* old_in = std::cin.rdbuf(fed.rdbuf());
        int code = -1;
        try {
            apogee::commands::RootCommand root{apogee::commands::default_registry()};
            std::vector<std::string> full{"--config", config_path.string()};
            full.insert(full.end(), args.begin(), args.end());
            std::vector<const char*> argv{"apogee"};
            for (const std::string& arg : full) {
                argv.push_back(arg.c_str());
            }
            code = root.run(static_cast<int>(argv.size()), argv.data());
        } catch (...) {
            std::cout.rdbuf(old_out);
            std::cerr.rdbuf(old_err);
            std::cin.rdbuf(old_in);
            throw;
        }
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
        std::cin.rdbuf(old_in);
        if (out != nullptr) {
            *out = captured_out.str();
        }
        if (err != nullptr) {
            *err = captured_err.str();
        }
        return code;
    }

    void ingest() const {
        std::string out;
        std::string err;
        INFO(err);
        REQUIRE(run({"embed", "ingest", "notes", (home.path() / "docs").string()}, &out, &err) ==
                0);
    }

    [[nodiscard]] Store store() const {
        return Store{home.path() / "embeddings" / "notes.db"};
    }
};

}  // namespace

TEST_CASE(
    "graph build extracts through the scripted mock, stamps the sources, and sets "
    "graph.enabled through the config editor",
    "[commands][graph][build]") {
    const Fixture fixture;
    fixture.ingest();
    const std::string before = bytes(fixture.config_path);
    std::string out;
    std::string err;
    REQUIRE(fixture.run({"graph", "build", "notes"}, &out, &err) == 0);
    INFO(out);
    CHECK(out.find("Graph build complete for \"notes\":") != std::string::npos);
    CHECK(out.find("Files extracted:   2 of 2 planned") != std::string::npos);
    CHECK(out.find("Chunks extracted:  2") != std::string::npos);
    CHECK(out.find("Nodes upserted:    2") != std::string::npos);
    CHECK(out.find("entity vectors skipped") != std::string::npos);  // no embedder: FTS-only
    CHECK(out.find("graph.enabled set on 'notes'") != std::string::npos);
    CHECK(err.find("[graph] extracting") != std::string::npos);
    const Store store = fixture.store();
    CHECK(store.graph_stats().nodes == 2);
    CHECK(store.graph_stats().edges == 1);
    CHECK(store.graph_stats().mentions == 4);
    CHECK(store.graph_stats().extract_model == "extractor");
    CHECK(store.source_states().size() == 2);
    // The one config write, byte-exact: the graph block appended to the
    // entry ingest registered.
    const std::string after = bytes(fixture.config_path);
    CHECK(after.starts_with(before));
    CHECK(after.find("  notes:\n") != std::string::npos);
    CHECK(after.ends_with("    graph:\n      enabled: true\n"));
    const apogee::harness::Config config = apogee::harness::load_config(fixture.config_path);
    CHECK(config.find_embedding("notes")->graph.enabled);

    // A second build is a no-op and does not touch the config.
    REQUIRE(fixture.run({"graph", "build", "notes"}, &out) == 0);
    CHECK(out.find("Nothing to extract") != std::string::npos);
    CHECK(bytes(fixture.config_path) == after);

    // stats, show, delete.
    REQUIRE(fixture.run({"graph", "stats", "notes"}, &out) == 0);
    CHECK(out.find("Knowledge graph for \"notes\":") != std::string::npos);
    CHECK(out.find("Nodes:     2 (system 2)") != std::string::npos);
    CHECK(out.find("Coverage:  100% of 2 chunks") != std::string::npos);
    CHECK(out.find("Extractor: extractor") != std::string::npos);
    REQUIRE(fixture.run({"graph", "show", "notes", "atlas"}, &out) == 0);
    CHECK(out.find("Atlas (system) -- 2 mention(s), no vector") != std::string::npos);
    CHECK(out.find("[stores readings in]") != std::string::npos);
    CHECK(out.find("-> Vault (system)") != std::string::npos);
    CHECK(out.find("Supporting chunks:") != std::string::npos);
    REQUIRE(fixture.run({"graph", "show", "notes", "warehouse", "--chunks", "0"}, &out) == 0);
    CHECK(out.find("closest: Vault") != std::string::npos);
    CHECK(fixture.run({"graph", "show", "notes", "nothing-like-this"}, &out, &err) == 1);
    REQUIRE(fixture.run({"graph", "delete", "notes"}, &out) == 0);
    CHECK(out.find("Deleted the graph for \"notes\": 2 node(s), 1 edge(s), 4 mention(s)") !=
          std::string::npos);
    CHECK(fixture.store().graph_stats().nodes == 0);
    CHECK(fixture.store().chunk_count() == 2);
    REQUIRE(fixture.run({"graph", "stats", "notes"}, &out) == 0);
    CHECK(out.find("No graph built for \"notes\"") != std::string::npos);
}

TEST_CASE("graph build --dry-run prints every extraction and stores nothing",
          "[commands][graph][dry-run]") {
    const Fixture fixture;
    fixture.ingest();
    const std::string before = bytes(fixture.config_path);
    std::string out;
    REQUIRE(fixture.run({"graph", "build", "notes", "--dry-run", "--limit", "1"}, &out) == 0);
    CHECK(out.find("[chunk 0]") != std::string::npos);
    CHECK(out.find("  Atlas (system) -- collects readings") != std::string::npos);
    CHECK(out.find("  Atlas -[stores readings in]-> Vault") != std::string::npos);
    CHECK(out.find("Dry run over \"notes\": 1 chunk(s) extracted across 2 file(s), 0 failed -- "
                   "nothing was stored.") != std::string::npos);
    CHECK(fixture.store().graph_stats().nodes == 0);
    CHECK(bytes(fixture.config_path) == before);
}

TEST_CASE(
    "the cost policy: a metered default is refused by fall-through, a named one allowed, "
    "a vendor CLI refused by type, a missing collection refused",
    "[commands][graph][cost]") {
    const Fixture fixture{{}, /*default_metered=*/true};
    fixture.ingest();
    std::string out;
    std::string err;
    CHECK(fixture.run({"graph", "build", "notes"}, &out, &err) == 1);
    CHECK(err.find("never runs on a metered backend") != std::string::npos);
    CHECK(err.find("-m paid") != std::string::npos);
    CHECK(err.find("graph.extract_backend") != std::string::npos);
    CHECK(err.find("extraction role") != std::string::npos);
    CHECK(fixture.store().graph_stats().nodes == 0);
    // Named explicitly, a metered backend is allowed: the fall-through is
    // what the policy refuses, not the backend.
    REQUIRE(fixture.run({"graph", "build", "notes", "-m", "paidmock"}, &out, &err) == 0);
    CHECK(fixture.store().graph_stats().nodes == 2);
    // And as the metered default, refused again once it is the fall-through.
    const Fixture paid_default{{}, true};
    {
        std::string config = bytes(paid_default.config_path);
        write(paid_default.config_path,
              config.replace(config.find("default: paid"), 13, "default: paidmock"));
    }
    paid_default.ingest();
    CHECK(paid_default.run({"graph", "build", "notes"}, &out, &err) == 1);
    CHECK(err.find("never runs on a metered backend") != std::string::npos);
    // A vendor CLI is refused by type; an unconfigured name is refused too.
    CHECK(fixture.run({"graph", "build", "notes", "-m", "vendor"}, &out, &err) == 1);
    CHECK(err.find("vendor-CLI backend") != std::string::npos);
    CHECK(fixture.run({"graph", "build", "notes", "-m", "ghost"}, &out, &err) == 1);
    CHECK(err.find("not configured") != std::string::npos);
    // An unmetered backend named through the collection's own block.
    const Fixture pinned{"embeddings:\n  notes:\n    graph:\n      extract_backend: extractor\n",
                         true};
    pinned.ingest();
    REQUIRE(pinned.run({"graph", "build", "notes"}, &out, &err) == 0);
    CHECK(out.find("with extractor...") != std::string::npos);
    CHECK(pinned.store().graph_stats().nodes == 2);
    // The extraction role names it too: the metered default is never reached.
    const Fixture role{{}, true};
    {
        std::string config = bytes(role.config_path);
        const std::string models = "models:\n  default: paid\n";
        REQUIRE(config.starts_with(models));
        write(role.config_path,
              models + "  default_extraction: extractor\n" + config.substr(models.size()));
    }
    role.ingest();
    REQUIRE(role.run({"graph", "build", "notes"}, &out, &err) == 0);
    CHECK(role.store().graph_stats().nodes == 2);
    CHECK(role.run({"graph", "build", "nothing"}, &out, &err) == 1);
    CHECK(err.find("no graph or collection named 'nothing'") != std::string::npos);
    CHECK(fixture.run({"graph", "build", "../x"}, &out, &err) == 1);
}

TEST_CASE(
    "a clerk that never conforms counts failed chunks, leaves the file unstamped, and the "
    "next build retries it",
    "[commands][graph][failure]") {
    const Fixture fixture;
    fixture.ingest();
    std::string out;
    std::string err;
    REQUIRE(fixture.run({"graph", "build", "notes", "-m", "prose"}, &out, &err) == 0);
    INFO(out);
    CHECK(out.find("FAILED: the extractor did not return") != std::string::npos);
    CHECK(out.find("Chunks extracted:  0 (2 failed after retry -- re-run to retry them)") !=
          std::string::npos);
    CHECK(fixture.store().source_states().empty());
    CHECK(fixture.store().graph_stats().failed_chunks == 2);
    REQUIRE(fixture.run({"graph", "stats", "notes"}, &out) == 0);
    CHECK(out.find("No graph built") != std::string::npos);
    REQUIRE(fixture.run({"graph", "build", "notes"}, &out) == 0);
    CHECK(out.find("Files extracted:   2 of 2 planned") != std::string::npos);
    CHECK(fixture.store().graph_stats().failed_chunks == 0);
}

TEST_CASE(
    "config add-graph writes the entry under the CLI's rules, and delete-graph removes "
    "only the entry",
    "[commands][graph][config]") {
    const Fixture fixture;
    fixture.ingest();
    std::string out;
    std::string err;
    const std::string before = bytes(fixture.config_path);
    // Refusals, each naming the rule.
    CHECK(fixture.run({"config", "add-graph", "notes", "--collections", "notes"}, &out, &err) == 1);
    CHECK(err.find("already a collection name") != std::string::npos);
    CHECK(fixture.run({"config", "add-graph", "work", "--collections", " , "}, &out, &err) == 1);
    CHECK(err.find("at least one member") != std::string::npos);
    CHECK(fixture.run({"config", "add-graph", "work", "--collections", "notes", "--extract-backend",
                       "ghost"},
                      &out, &err) == 1);
    CHECK(err.find("'ghost' is not configured") != std::string::npos);
    CHECK(fixture.run({"config", "add-graph", "work", "--collections", "notes", "--hops", "3"},
                      &out, &err) == 1);
    CHECK(err.find("hops must be 1 or 2") != std::string::npos);
    CHECK(fixture.run({"config", "add-graph", "../w", "--collections", "notes"}, &out, &err) == 1);
    CHECK(bytes(fixture.config_path) == before);
    // An unknown member is a warning: ingest registers it on first use.
    REQUIRE(fixture.run({"config", "add-graph", "work", "--collections", "notes, later",
                         "--extract-backend", "extractor", "--hops", "2"},
                        &out, &err) == 0);
    CHECK(err.find("member collection 'later' is not configured yet") != std::string::npos);
    CHECK(out.find("added graph 'work'") != std::string::npos);
    const std::string added = bytes(fixture.config_path);
    CHECK(added.starts_with(before));
    CHECK(
        added.ends_with("graphs:\n  work:\n    collections: [notes, later]\n"
                        "    extract_backend: extractor\n    hops: 2\n"));
    const apogee::harness::Config config = apogee::harness::load_config(fixture.config_path);
    REQUIRE(config.find_graph("work") != nullptr);
    CHECK(config.find_graph("work")->collections == std::vector<std::string>{"notes", "later"});
    // Duplicate needs --force; force replaces in place.
    CHECK(fixture.run({"config", "add-graph", "work", "--collections", "notes"}, &out, &err) == 1);
    REQUIRE(fixture.run({"config", "add-graph", "work", "--collections", "notes", "--force"}, &out,
                        &err) == 0);
    CHECK(apogee::harness::load_config(fixture.config_path).find_graph("work")->collections ==
          std::vector<std::string>{"notes"});
    // Delete leaves the database alone and is the inverse of the add.
    {
        Store built{apogee::agentloop::graph_db_path("work")};
    }
    REQUIRE(fixture.run({"config", "delete-graph", "work"}, &out, &err) == 0);
    CHECK(out.find("removed graph 'work'") != std::string::npos);
    CHECK(bytes(fixture.config_path) == before + "\ngraphs:\n");  // the header stays
    CHECK(std::filesystem::exists(apogee::agentloop::graph_db_path("work")));
    CHECK(fixture.run({"config", "delete-graph", "work"}, &out, &err) == 1);
}

TEST_CASE(
    "a named graph builds into its own database over its members, takes retrieval "
    "precedence, reports per member, shows chunks by collection, and deletes its file",
    "[commands][graph][named]") {
    const Fixture fixture;
    fixture.ingest();
    write(fixture.home.path() / "docs2" / "c.md", "Atlas was discussed at the Monday meeting.");
    std::string out;
    std::string err;
    REQUIRE(fixture.run({"embed", "ingest", "meetings", (fixture.home.path() / "docs2").string()},
                        &out, &err) == 0);
    REQUIRE(fixture.run({"config", "add-graph", "work", "--collections", "notes,meetings,absent"},
                        &out, &err) == 0);
    // Unbuilt: stats and show say so; nothing to delete.
    REQUIRE(fixture.run({"graph", "stats", "work"}, &out, &err) == 0);
    CHECK(out.find("No graph built for \"work\"") != std::string::npos);
    CHECK(fixture.run({"graph", "show", "work", "atlas"}, &out, &err) == 1);
    CHECK(err.find("has not been built yet") != std::string::npos);
    REQUIRE(fixture.run({"graph", "delete", "work"}, &out, &err) == 0);
    CHECK(out.find("No graph to delete") != std::string::npos);
    // A dry run creates nothing -- not even the file.
    REQUIRE(fixture.run({"graph", "build", "work", "--dry-run"}, &out, &err) == 0);
    CHECK(out.find("Dry run over \"work\": 3 chunk(s)") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(apogee::agentloop::graph_db_path("work")));

    const std::string config_before = bytes(fixture.config_path);
    REQUIRE(fixture.run({"graph", "build", "work"}, &out, &err) == 0);
    INFO(out);
    CHECK(out.find("Building knowledge graph \"work\" over [notes, meetings, absent] with "
                   "extractor...") != std::string::npos);
    CHECK(out.find("member collection \"absent\" has no database") != std::string::npos);
    CHECK(out.find("Graph build complete for \"work\":") != std::string::npos);
    CHECK(out.find("Files extracted:   3 of 3 planned") != std::string::npos);
    CHECK(out.find("Nodes upserted:    2") != std::string::npos);
    CHECK(err.find("[graph] extracting notes: ") != std::string::npos);
    CHECK(err.find("[graph] extracting meetings: ") != std::string::npos);
    // Nothing to enable: the entry is the enablement, the config untouched.
    CHECK(bytes(fixture.config_path) == config_before);
    CHECK(std::filesystem::exists(apogee::agentloop::graph_db_path("work")));
    CHECK(fixture.store().graph_stats().nodes == 0);  // the member was never written
    {
        const Store work{apogee::agentloop::graph_db_path("work")};
        CHECK(work.graph_stats().nodes == 2);
        CHECK(work.graph_stats().mentions == 6);
        CHECK(work.find_nodes("Atlas").front().mention_count == 3);
        CHECK(work.graph_meta(apogee::embedstore::kGraphMetaGraphName) == "work");
        CHECK(work.graph_members() == std::vector<std::string>{"absent", "meetings", "notes"});
    }
    REQUIRE(fixture.run({"graph", "build", "work"}, &out, &err) == 0);
    CHECK(out.find("Nothing to extract") != std::string::npos);

    REQUIRE(fixture.run({"graph", "stats", "work"}, &out, &err) == 0);
    INFO(out);
    CHECK(out.find("Knowledge graph \"work\" over [notes, meetings, absent]:") !=
          std::string::npos);
    CHECK(out.find("Nodes:     2 (system 2)") != std::string::npos);
    CHECK(out.find("Coverage:  100% of 3 chunks") != std::string::npos);
    CHECK(out.find("  Members:") != std::string::npos);
    CHECK(out.find("    notes: 4 mention(s), 2/2 chunk(s) covered") != std::string::npos);
    CHECK(out.find("    meetings: 2 mention(s), 1/1 chunk(s) covered") != std::string::npos);
    CHECK(out.find("    absent: 0 mention(s), 0/0 chunk(s) covered -- database missing") !=
          std::string::npos);
    CHECK(out.find("membership changed") == std::string::npos);

    REQUIRE(fixture.run({"graph", "show", "work", "atlas", "--chunks", "0"}, &out, &err) == 0);
    INFO(out);
    CHECK(out.find("Atlas (system) -- 3 mention(s), no vector") != std::string::npos);
    CHECK(out.find("  notes: a.md [chunk 0]") != std::string::npos);
    CHECK(out.find("  meetings: c.md [chunk 0]") != std::string::npos);

    // Retrieval precedence: a turn over notes expands through work, with
    // the named graph's section header -- one turn, one graph.
    const apogee::harness::Config config = apogee::harness::load_config(fixture.config_path);
    const apogee::harness::Harness harness{config};
    const apogee::agentloop::RagResult turn = apogee::commands::retrieve_for_collection(
        harness, config, "notes", "field probes readings", 4, "", "", {});
    CHECK(turn.chunks == 1);
    CHECK(turn.graph_entities == 1);  // Atlas, named by "readings" at hop 0
    CHECK(turn.prefix.front().content.plain_text().find("[Knowledge graph: work]") !=
          std::string::npos);

    // Membership drift is noted; a rebuild converges.
    REQUIRE(fixture.run({"config", "add-graph", "work", "--collections", "notes", "--force"}, &out,
                        &err) == 0);
    REQUIRE(fixture.run({"graph", "stats", "work"}, &out, &err) == 0);
    CHECK(out.find("membership changed since the last build (was [absent, meetings, notes])") !=
          std::string::npos);
    REQUIRE(fixture.run({"graph", "build", "work"}, &out, &err) == 0);
    CHECK(out.find("Reconciled stale rows: 2 mention(s)") != std::string::npos);
    {
        const Store work{apogee::agentloop::graph_db_path("work")};
        CHECK(work.find_nodes("Atlas").front().mention_count == 2);
    }

    REQUIRE(fixture.run({"graph", "delete", "work"}, &out, &err) == 0);
    CHECK(out.find("Deleted graph \"work\": 2 node(s)") != std::string::npos);
    CHECK(out.find("config entry is untouched") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(apogee::agentloop::graph_db_path("work")));
    CHECK(apogee::harness::load_config(fixture.config_path).find_graph("work") != nullptr);
    // A graph name without a database and not a collection: refused by name.
    REQUIRE(fixture.run({"config", "delete-graph", "work"}, &out, &err) == 0);
    CHECK(fixture.run({"graph", "stats", "work"}, &out, &err) == 1);
    CHECK(err.find("no graph or collection named 'work'") != std::string::npos);
}

TEST_CASE(
    "under a hand-made collision every subcommand resolves the graphs: entry first, and "
    "the doctor fails it",
    "[commands][graph][named][collision]") {
    const Fixture fixture{"graphs:\n  notes:\n    collections: [meetings]\n"};
    fixture.ingest();
    std::string out;
    std::string err;
    REQUIRE(fixture.run({"graph", "stats", "notes"}, &out, &err) == 0);
    CHECK(out.find("No graph built for \"notes\"") != std::string::npos);  // the graph
    CHECK(fixture.run({"graph", "show", "notes", "atlas"}, &out, &err) == 1);
    CHECK(err.find("has not been built yet") != std::string::npos);
    REQUIRE(fixture.run({"graph", "delete", "notes"}, &out, &err) == 0);
    CHECK(out.find("No graph to delete") != std::string::npos);
    CHECK(fixture.store().chunk_count() == 2);
    CHECK(fixture.run({"check"}, &out, &err) != 0);
    CHECK(out.find("named graph: notes") != std::string::npos);
    CHECK(out.find("collides with a collection name") != std::string::npos);
}

TEST_CASE(
    "graph communities detects, summarises once, lists, skips unchanged, and the summary "
    "is a retrievable chunk",
    "[commands][graph][communities]") {
    const Fixture fixture;
    fixture.ingest();
    write(fixture.home.path() / "summarizer.json",
          script_of({"The main themes here are Atlas and the warehouse it writes to."}));
    std::string out;
    std::string err;
    REQUIRE(fixture.run({"config", "add-backend", "summarizer", "--type", "mock", "--model-path",
                         (fixture.home.path() / "summarizer.json").string()},
                        &out, &err) == 0);
    CHECK(fixture.run({"graph", "communities", "notes"}, &out, &err) == 1);
    CHECK(err.find("no relations to cluster") != std::string::npos);
    REQUIRE(fixture.run({"graph", "build", "notes"}, &out, &err) == 0);
    REQUIRE(fixture.run({"graph", "communities", "notes", "--list"}, &out, &err) == 0);
    CHECK(out.find("No communities stored for \"notes\"") != std::string::npos);
    // Two entities: below the default size, nothing summarised, no call.
    REQUIRE(fixture.run({"graph", "communities", "notes"}, &out, &err) == 0);
    CHECK(out.find("Communities for \"notes\": 0 detected -- 0 summarised, 0 unchanged, 0 "
                   "pruned.") != std::string::npos);
    REQUIRE(fixture.run({"graph", "communities", "notes", "--min-size", "2", "-m", "summarizer"},
                        &out, &err) == 0);
    INFO(out);
    CHECK(out.find("Detecting and summarising communities for \"notes\" with summarizer...") !=
          std::string::npos);
    CHECK(out.find("1 detected -- 1 summarised, 0 unchanged, 0 pruned.") != std::string::npos);
    CHECK(out.find("summary vectors skipped") != std::string::npos);  // no embedder
    CHECK(out.find("Community #1 -- 2 entities (top: Atlas, Vault)") != std::string::npos);
    CHECK(out.find("  The main themes here are Atlas") != std::string::npos);
    CHECK(err.find("[graph] summarising community 1/1") != std::string::npos);
    // Unchanged membership: no summariser call, the same row.
    REQUIRE(fixture.run({"graph", "communities", "notes", "--min-size", "2", "-m", "summarizer"},
                        &out, &err) == 0);
    CHECK(out.find("1 detected -- 0 summarised, 1 unchanged, 0 pruned.") != std::string::npos);
    CHECK(out.find("Community #1 -- 2 entities") != std::string::npos);  // the same row
    // The summary is an ordinary chunk: the top lexical hit for the question.
    REQUIRE(fixture.run({"embed", "query", "notes", "main themes"}, &out, &err) == 0);
    CHECK(out.find("graph://community/1") != std::string::npos);
    CHECK(fixture.store().graph_stats().communities == 1);
    REQUIRE(fixture.run({"graph", "stats", "notes"}, &out, &err) == 0);
    CHECK(out.find("Communities: 1 summarised") != std::string::npos);
    // A metered default is refused for the summariser too.
    const Fixture paid{{}, true};
    paid.ingest();
    REQUIRE(paid.run({"graph", "build", "notes", "-m", "extractor"}, &out, &err) == 0);
    CHECK(paid.run({"graph", "communities", "notes", "--min-size", "2"}, &out, &err) == 1);
    CHECK(err.find("community summary run never runs on a metered backend") != std::string::npos);
}

TEST_CASE("graph dedupe previews with --dry-run, merges on request, and is idempotent",
          "[commands][graph][dedupe]") {
    const Fixture fixture;
    fixture.ingest();
    std::string out;
    std::string err;
    REQUIRE(fixture.run({"graph", "build", "notes"}, &out, &err) == 0);
    // No vectors: nothing is considered.
    REQUIRE(fixture.run({"graph", "dedupe", "notes", "--dry-run"}, &out, &err) == 0);
    CHECK(out.find("nothing to merge") != std::string::npos);
    CHECK(fixture.run({"graph", "dedupe", "notes", "--threshold", "0"}, &out, &err) == 1);
    {
        Store store = fixture.store();
        const std::int64_t atlas = store.find_nodes("Atlas").front().id;
        const std::int64_t vault = store.find_nodes("Vault").front().id;
        store.update_node_embedding(atlas, {1.0F, 0.0F, 0.0F});
        store.update_node_embedding(vault, {0.0F, 1.0F, 0.0F});
        const std::int64_t twin = store.upsert_node("Atlas Prime", "system", "").id;
        store.update_node_embedding(twin, {0.99F, 0.1F, 0.0F});
        (void)store.add_mention(twin, 777);  // a mention only the twin has
    }
    REQUIRE(fixture.run({"graph", "dedupe", "notes", "--dry-run"}, &out, &err) == 0);
    CHECK(out.find("Would merge into Atlas (system): Atlas Prime") != std::string::npos);
    CHECK(out.find("Dry run -- nothing was changed.") != std::string::npos);
    CHECK(fixture.store().find_nodes("Atlas Prime").size() == 1);
    REQUIRE(fixture.run({"graph", "dedupe", "notes"}, &out, &err) == 0);
    CHECK(out.find("Merged into Atlas (system): Atlas Prime") != std::string::npos);
    CHECK(out.find("1 group(s), 1 entity(ies) merged at threshold 0.92.") != std::string::npos);
    CHECK(fixture.store().find_nodes("Atlas Prime").empty());
    CHECK(fixture.store().find_nodes("Atlas").front().mention_count == 3);
    REQUIRE(fixture.run({"graph", "dedupe", "notes"}, &out, &err) == 0);
    CHECK(out.find("nothing to merge") != std::string::npos);
}

TEST_CASE(
    "embed ingest --graph builds the covering graph after a successful ingest, and never "
    "after a failed one",
    "[commands][graph][ingest]") {
    const Fixture fixture;
    std::string out;
    std::string err;
    // A failed ingest never chains: nothing to build from.
    CHECK(fixture.run(
              {"embed", "ingest", "notes", (fixture.home.path() / "nowhere").string(), "--graph"},
              &out, &err) == 1);
    CHECK(out.find("Graph build") == std::string::npos);
    CHECK(err.find("apogee graph") == std::string::npos);  // no build was even attempted
    // The collection's own graph, in one command.
    REQUIRE(fixture.run(
                {"embed", "ingest", "notes", (fixture.home.path() / "docs").string(), "--graph"},
                &out, &err) == 0);
    INFO(out);
    CHECK(out.find("2 file(s), 2 chunk(s)") != std::string::npos);
    CHECK(out.find("registered 'notes'") != std::string::npos);
    CHECK(out.find("Graph build complete for \"notes\":") != std::string::npos);
    CHECK(out.find("graph.enabled set on 'notes'") != std::string::npos);
    CHECK(fixture.store().graph_stats().nodes == 2);
    // A named graph listing the collection is the chain's target, built
    // or not -- the first chained build is what creates it.
    REQUIRE(fixture.run({"config", "add-graph", "work", "--collections", "notes"}, &out, &err) ==
            0);
    REQUIRE(fixture.run(
                {"embed", "ingest", "notes", (fixture.home.path() / "docs").string(), "--graph"},
                &out, &err) == 0);
    INFO(out);
    CHECK(out.find("Building knowledge graph \"work\" over [notes]") != std::string::npos);
    CHECK(out.find("Graph build complete for \"work\":") != std::string::npos);
    CHECK(std::filesystem::exists(apogee::agentloop::graph_db_path("work")));
}
