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

#include "commands/registry.h"
#include "commands/root.h"
#include "embedstore/store.h"
#include "harness/config.h"
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
    CHECK(err.find("no collection named 'nothing'") != std::string::npos);
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
