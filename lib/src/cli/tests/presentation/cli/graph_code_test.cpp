#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "agentloop/graph_context.h"
#include "cli/command.h"
#include "cli/complete_sources.h"
#include "cli/graph.h"
#include "cli/registry.h"
#include "cli/root.h"
#include "contracts/config.h"
#include "embedstore/store.h"
#include "support/env_guard.h"

/// `apogee graph` over source trees (27k) on a sandbox with **no backend at
/// all** -- no model, no key, no embedder: `build --source` makes the named
/// graph and records it through the config editor, `stats` says what was
/// parsed and what a model asserted, `show` walks callers and callees across
/// files to the line, `update` re-parses only what changed, `communities`
/// clusters with the summaries reported absent, `dedupe` says what each layer
/// had to merge -- and the refusals.
namespace {

using apogee::embedstore::Store;

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
    apogee::testing::TempDir home{"graph-code-cli-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    std::filesystem::path config_path = home.path() / "config" / "config.yaml";
    std::filesystem::path tree = home.path() / "repo" / "app";

    Fixture() {
        // Zero backends: the code graph needs none.
        write(config_path, "# no backends -- the code graph needs none\n");
        write(tree / "pkg" / "__init__.py", "");
        write(tree / "pkg" / "lib.py",
              "def helper(x: int) -> int:\n    return x + 1\n\n\ndef other() -> int:\n"
              "    return helper(2)\n");
        write(tree / "pkg" / "app.py",
              "import json\n\nfrom .lib import helper, other\n\n\ndef run() -> str:\n"
              "    return json.dumps(helper(1) + other())\n");
        write(tree / "README.notes", "not code\n");
        write(tree / "third_party" / "dep.py", "def vendored():\n    pass\n");
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

    void build() const {
        std::string out;
        std::string err;
        INFO(out << err);
        REQUIRE(run({"graph", "build", "--source", tree.string(), "--graph", "code", "-q"}, &out,
                    &err) == 0);
    }

    [[nodiscard]] static std::filesystem::path db() {
        return apogee::agentloop::graph_db_path("code");
    }
};

}  // namespace

TEST_CASE("graph build --source makes the named graph with no backend, and records it once",
          "[commands][graph][code]") {
    const Fixture fixture;
    const std::string before = bytes(fixture.config_path);
    std::string out;
    std::string err;
    REQUIRE(fixture.run({"graph", "build", "--source", fixture.tree.string(), "--graph", "code"},
                        &out, &err) == 0);
    INFO(out << err);
    CHECK(out.find("added graph 'code'") != std::string::npos);
    CHECK(out.find("Code graph for \"code\" (no model):") != std::string::npos);
    CHECK(out.find("Files used:       3 (python 3) -- 3 parsed, 0 unchanged") != std::string::npos);
    CHECK(out.find("Excluded:         third_party/ (1 file(s)") != std::string::npos);
    // The unsupported file is named and counted; the build still succeeds.
    CHECK(out.find("Skipped:          1 file(s)") != std::string::npos);
    CHECK(out.find("unsupported language (.notes): 1") != std::string::npos);
    CHECK(out.find("app/README.notes") != std::string::npos);
    CHECK(err.find("[graph] parsing app/pkg/app.py (2/3)") != std::string::npos);
    CHECK(std::filesystem::exists(Fixture::db()));

    // The one config write: the entry appended, the source recorded absolute.
    const std::string after = bytes(fixture.config_path);
    CHECK(after.starts_with(before));
    const apogee::harness::Config config = apogee::harness::load_config(fixture.config_path);
    const apogee::harness::NamedGraphConfig* graph = config.find_graph("code");
    REQUIRE(graph != nullptr);
    CHECK(graph->collections.empty());
    REQUIRE(graph->sources.size() == 1);
    CHECK(std::filesystem::path{graph->sources.front()} == fixture.tree);

    // A tree given relative to where apogee runs is recorded absolute: the
    // config is read from anywhere. (No relative path exists across Windows
    // drives -- a temp directory on one, the build on another.)
    const std::filesystem::path relative =
        std::filesystem::relative(fixture.tree, std::filesystem::current_path());
    if (!relative.empty()) {
        REQUIRE(
            fixture.run({"graph", "build", "--source", relative.string(), "--graph", "rel", "-q"},
                        &out, &err) == 0);
        const apogee::harness::Config reread = apogee::harness::load_config(fixture.config_path);
        REQUIRE(reread.find_graph("rel") != nullptr);
        CHECK(std::filesystem::path{reread.find_graph("rel")->sources.front()}.is_absolute());
        CHECK(std::filesystem::equivalent(reread.find_graph("rel")->sources.front(), fixture.tree));
    }
    const std::string recorded = bytes(fixture.config_path);

    // Built again: nothing new to record, nothing to parse.
    REQUIRE(fixture.run({"graph", "build", "code", "-q"}, &out, &err) == 0);
    CHECK(bytes(fixture.config_path) == recorded);
    CHECK(out.find("0 parsed, 3 unchanged") != std::string::npos);
}

TEST_CASE("graph stats reports the code layer and the extracted/inferred split",
          "[commands][graph][code]") {
    const Fixture fixture;
    fixture.build();
    std::string out;
    REQUIRE(fixture.run({"graph", "stats", "code"}, &out) == 0);
    INFO(out);
    CHECK(out.find("Knowledge graph \"code\" from source tree(s) [app]:") != std::string::npos);
    const Store store{Fixture::db()};
    const apogee::embedstore::GraphStats stats = store.graph_stats();
    CHECK(stats.edges > 0);
    CHECK(stats.edges_extracted == stats.edges);
    CHECK(out.find("Origin:    " + std::to_string(stats.edges) +
                   " extracted (parsed from source), 0 inferred (asserted by a model)") !=
          std::string::npos);
    CHECK(out.find("Code:      3 file(s) parsed (python 3)") != std::string::npos);
    CHECK(out.find("prose entities embedded (code entities never are)") != std::string::npos);
}

TEST_CASE("graph show lists callers and callees across files, each at its file:line",
          "[commands][graph][code]") {
    const Fixture fixture;
    fixture.build();
    std::string out;
    REQUIRE(fixture.run({"graph", "show", "code", "pkg.lib.helper"}, &out) == 0);
    INFO(out);
    CHECK(out.find("pkg.lib.helper (function)") != std::string::npos);
    CHECK(out.find("def helper(x: int) -> int") != std::string::npos);
    CHECK(out.find("definition  app: pkg/lib.py:1-2") != std::string::npos);
    // Called from the other file, and from its own: each site named.
    CHECK(out.find("<- pkg.app.run (function) -- extracted") != std::string::npos);
    CHECK(out.find("at pkg/app.py:7") != std::string::npos);
    CHECK(out.find("<- pkg.lib.other (function) -- extracted") != std::string::npos);
    CHECK(out.find("at pkg/lib.py:6") != std::string::npos);

    // An unresolved call is a name node, counted -- never a guess.
    REQUIRE(fixture.run({"graph", "show", "code", "pkg.app.run"}, &out) == 0);
    CHECK(out.find("-> json.dumps (name) -- extracted") != std::string::npos);
    CHECK(out.find("-> pkg.lib.other (function) -- extracted") != std::string::npos);
    REQUIRE(fixture.run({"graph", "show", "code", "json.dumps"}, &out) == 0);
    CHECK(out.find("Referenced at:") != std::string::npos);
    CHECK(out.find("reference  app: pkg/app.py:7") != std::string::npos);
}

TEST_CASE("graph update re-parses only the edited file and matches a fresh build",
          "[commands][graph][code][update]") {
    const Fixture fixture;
    fixture.build();
    write(fixture.tree / "pkg" / "lib.py",
          "def helper(x: int) -> int:\n    return x + 2\n\n\ndef other() -> int:\n"
          "    return helper(3)\n");
    std::string out;
    std::string err;
    REQUIRE(fixture.run({"graph", "update", "code"}, &out, &err) == 0);
    INFO(out << err);
    CHECK(out.find("Updating the code of \"code\"") != std::string::npos);
    CHECK(out.find("1 parsed, 2 unchanged") != std::string::npos);
    CHECK(err.find("[graph] parsing app/pkg/lib.py") != std::string::npos);
    CHECK(err.find("pkg/app.py") == std::string::npos);
    const std::string updated = Store{Fixture::db()}.graph_dump();

    // A fresh build of the same tree, from nothing, stores the same graph.
    REQUIRE(fixture.run({"graph", "delete", "code"}, &out, &err) == 0);
    REQUIRE_FALSE(std::filesystem::exists(Fixture::db()));
    REQUIRE(fixture.run({"graph", "build", "code", "-q"}, &out, &err) == 0);
    CHECK(out.find("3 parsed, 0 unchanged") != std::string::npos);
    CHECK(Store{Fixture::db()}.graph_dump() == updated);
}

TEST_CASE(
    "communities cluster with no model and say the summaries are absent; dedupe says what "
    "each layer had",
    "[commands][graph][code][model-free]") {
    const Fixture fixture;
    fixture.build();
    std::string out;
    std::string err;
    REQUIRE(fixture.run({"graph", "communities", "code", "--min-size", "2", "-q"}, &out, &err) ==
            0);
    INFO(out << err);
    CHECK(out.find("-- no model; summaries absent (no generation backend is configured)") !=
          std::string::npos);
    CHECK(out.find("0 summarised") != std::string::npos);
    CHECK(out.find("clustered without a summary") != std::string::npos);
    CHECK(out.find("Summaries absent:") != std::string::npos);
    CHECK(Store{Fixture::db()}.graph_stats().communities > 0);

    REQUIRE(fixture.run({"graph", "dedupe", "code"}, &out, &err) == 0);
    CHECK(out.find("Code entities: ") != std::string::npos);
    CHECK(out.find("merged by exact qualified name as they were built") != std::string::npos);
    CHECK(out.find("Prose entities: vector dedupe skipped -- no prose entities") !=
          std::string::npos);
}

TEST_CASE("the --lang flag records the grammars on the entry and leaves the rest out by name",
          "[commands][graph][code]") {
    const Fixture fixture;
    write(fixture.tree / "web" / "app.js", "export function start() { return 1; }\n");
    std::string out;
    std::string err;
    REQUIRE(fixture.run({"graph", "build", "--source", fixture.tree.string(), "--graph", "code",
                         "--lang", "javascript", "-q"},
                        &out, &err) == 0);
    INFO(out << err);
    CHECK(out.find("Files used:       1 (javascript 1)") != std::string::npos);
    CHECK(out.find("python left out by --lang: 3") != std::string::npos);
    const apogee::harness::Config config = apogee::harness::load_config(fixture.config_path);
    CHECK(config.find_graph("code")->languages == std::vector<std::string>{"javascript"});
    CHECK(fixture.run({"graph", "build", "--source", fixture.tree.string(), "--graph", "code",
                       "--lang", "cobol"},
                      &out, &err) != 0);
}

TEST_CASE("a dry run parses and writes nothing: no entry, no database", "[commands][graph][code]") {
    const Fixture fixture;
    const std::string before = bytes(fixture.config_path);
    std::string out;
    std::string err;
    REQUIRE(fixture.run({"graph", "build", "--source", fixture.tree.string(), "--graph", "code",
                         "--dry-run", "-q"},
                        &out, &err) == 0);
    INFO(out << err);
    CHECK(out.find("Dry run -- code graph for \"code\" (no model):") != std::string::npos);
    CHECK(out.find("nothing was stored") != std::string::npos);
    CHECK(bytes(fixture.config_path) == before);
    CHECK_FALSE(std::filesystem::exists(Fixture::db()));
}

TEST_CASE("the refusals: a collection's name, no graph, no tree, and update off a code graph",
          "[commands][graph][code][refusal]") {
    const Fixture fixture;
    std::string out;
    std::string err;
    write(fixture.home.path() / "docs" / "a.md", "Atlas collects readings.");
    // `embed ingest` with no embedder registers a lexical collection.
    REQUIRE(fixture.run({"embed", "ingest", "notes", (fixture.home.path() / "docs").string()}, &out,
                        &err) == 0);
    CHECK(fixture.run({"graph", "build", "--source", fixture.tree.string(), "--graph", "notes"},
                      &out, &err) == 1);
    CHECK(err.find("'notes' is a collection -- a source tree builds into a named graph") !=
          std::string::npos);
    CHECK(fixture.run({"graph", "build", "--source", fixture.tree.string()}, &out, &err) == 1);
    CHECK(err.find("which graph?") != std::string::npos);
    CHECK(fixture.run({"graph", "build", "--source", (fixture.home.path() / "nope").string(),
                       "--graph", "code"},
                      &out, &err) == 1);
    CHECK(err.find("not a directory") != std::string::npos);
    CHECK(fixture.run({"graph", "update", "notes"}, &out, &err) == 1);
    CHECK(err.find("is not a named graph") != std::string::npos);
    REQUIRE(fixture.run({"config", "add-graph", "docs", "--collections", "notes"}, &out, &err) ==
            0);
    CHECK(fixture.run({"graph", "update", "docs"}, &out, &err) == 1);
    CHECK(err.find("has no source trees") != std::string::npos);
}

TEST_CASE("graph update completes the named graphs it refreshes, and no other",
          "[commands][graph][code][update][completion]") {
    // ADR 0007: `update` refuses a named graph with no source trees, so Tab
    // never offers one -- the same test, read from the config.
    const Fixture fixture;
    fixture.build();
    std::string out;
    std::string err;
    write(fixture.home.path() / "docs" / "a.md", "Atlas collects readings.");
    REQUIRE(fixture.run({"embed", "ingest", "notes", (fixture.home.path() / "docs").string()}, &out,
                        &err) == 0);
    REQUIRE(fixture.run({"config", "add-graph", "docs", "--collections", "notes"}, &out, &err) ==
            0);
    const apogee::harness::Config config = apogee::harness::load_config(fixture.config_path);
    apogee::commands::CompletionContext context;
    context.config = &config;
    const std::vector<std::string> offered =
        apogee::commands::list_names(apogee::commands::kSourcedGraphValue, context).names;
    CHECK(offered == std::vector<std::string>{"code"});
    for (const std::string& name : config.graph_names()) {
        INFO(name);
        const bool taken = fixture.run({"graph", "update", name, "-q"}, &out, &err) == 0;
        CHECK(taken == (std::ranges::find(offered, name) != offered.end()));
    }
}
