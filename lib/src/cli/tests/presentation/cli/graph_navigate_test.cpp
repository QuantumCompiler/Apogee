#include "cli/graph_navigate.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "agent/tool.h"
#include "agentloop/graph_context.h"
#include "cli/chat_attachments.h"
#include "cli/registry.h"
#include "cli/root.h"
#include "contracts/config.h"
#include "contracts/layout.h"
#include "embedstore/store.h"
#include "harness/harness.h"
#include "httpserver/admin_graph_navigate.h"
#include "logger/session.h"
#include "support/env_guard.h"
#include "support/graph_fixture.h"
#include "tools/graph_nav.h"

/// `apogee graph path|explain|neighbors|query` (27l) over the committed
/// fixture graph, in-process with no backend: **one core** -- for the same
/// question, the CLI's `--output-format json`, the `graph` tool's result and
/// the admin plane's read twin are the same bytes; the human renderings
/// (each hop `a -[calls·extracted]-> b`, the card, the capped groups, the
/// query as the section a turn injects); `--graph`/`--collection`; and every
/// refusal a user error naming what to do.
namespace {

void write(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << content;
}

/// A home with the named graph `work` over `notes`, and `docs` holding a
/// graph of its own -- two graphs, so a selection is needed.
struct Fixture {
    apogee::testing::TempDir home{"graph-navigate-cli-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    std::filesystem::path config_path = home.path() / "config" / "config.yaml";

    explicit Fixture(bool with_docs = true) {
        write(config_path,
              "# no backends -- navigation needs none\ngraphs:\n  work:\n"
              "    collections: [notes]\n");
        apogee::embedstore::Store notes{apogee::harness::embeddings_dir() / "notes.db"};
        apogee::embedstore::Store work{apogee::agentloop::graph_db_path("work")};
        apogee::testing::build_navigation_graph(work, notes, "notes");
        if (with_docs) {
            apogee::embedstore::Store docs{apogee::harness::embeddings_dir() / "docs.db"};
            apogee::testing::build_navigation_graph(docs, docs, "");
        }
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

    /// The tool's result for `arguments`, over the same config.
    [[nodiscard]] apogee::agent::ToolOutcome tool(std::string_view name,
                                                  const std::string& arguments) const {
        const apogee::harness::Config config = apogee::harness::load_config(config_path);
        apogee::agent::ToolRegistry registry;
        apogee::tools::register_graph_tools(registry, {.config = &config, .scope = {}});
        return registry.find(name)->run(arguments);
    }

    /// The scoped instance's result (27o): the set built over `graph`'s store
    /// alone, `arguments` without a selection.
    [[nodiscard]] apogee::agent::ToolOutcome scoped(std::string_view graph, std::string_view name,
                                                    const std::string& arguments) const {
        const apogee::harness::Config config = apogee::harness::load_config(config_path);
        apogee::agent::ToolRegistry registry;
        apogee::tools::register_graph_tools(
            registry, {.config = nullptr,
                       .scope = apogee::graph::resolve_graph_target(
                           config, apogee::graph::GraphSelection{.graph = std::string{graph}}),
                       .scope_note = " Reads the code attached to this chat."});
        return registry.find(name)->run(arguments);
    }

    [[nodiscard]] apogee::httpserver::HttpResponse served(
        std::string_view verb, std::string_view graph,
        std::map<std::string, std::string> query) const {
        apogee::httpserver::HttpRequest request;
        request.method = "GET";
        request.query = std::move(query);
        const apogee::httpserver::AdminConfigContext context{.config_path = config_path};
        if (verb == "path") {
            return apogee::httpserver::admin_graph_path(context, graph, request);
        }
        if (verb == "explain") {
            return apogee::httpserver::admin_graph_explain(context, graph, request);
        }
        if (verb == "neighbors") {
            return apogee::httpserver::admin_graph_neighbors(context, graph, request);
        }
        return apogee::httpserver::admin_graph_query(context, graph, request);
    }
};

[[nodiscard]] bool contains(std::string_view text, std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

}  // namespace

TEST_CASE("one core: the CLI's JSON, the tool's result and the served read are one document",
          "[commands][graph][navigate][parity]") {
    const Fixture fixture;

    struct Golden {
        std::vector<std::string> args;
        std::string tool;
        std::string arguments;
        std::string verb;
        std::map<std::string, std::string> query;
    };

    const std::vector<Golden> goldens{
        {{"graph", "path", "system:Atlas", "pkg.app.main", "--graph", "work"},
         "graph_path",
         R"({"from":"system:Atlas","to":"pkg.app.main","graph":"work"})",
         "path",
         {{"from", "system:Atlas"}, {"to", "pkg.app.main"}}},
        {{"graph", "path", "main", "pkg.lib.helper", "--graph", "work", "--directed", "--relation",
          "calls", "--max-hops", "4"},
         "graph_path",
         R"({"from":"main","to":"pkg.lib.helper","graph":"work","directed":true,"relations":["calls"],"max_hops":4})",
         "path",
         {{"from", "main"},
          {"to", "pkg.lib.helper"},
          {"directed", "true"},
          {"relations", "calls"},
          {"max_hops", "4"}}},
        {{"graph", "path", "pkg.app.main", "pkg.island.alone", "--graph", "work"},
         "graph_path",
         R"({"from":"pkg.app.main","to":"pkg.island.alone","graph":"work"})",
         "path",
         {{"from", "pkg.app.main"}, {"to", "pkg.island.alone"}}},
        {{"graph", "explain", "Vault", "--graph", "work"},
         "graph_explain",
         R"({"node":"Vault","graph":"work"})",
         "explain",
         {{"node", "Vault"}}},
        {{"graph", "explain", "pkg/lib.py:11", "--graph", "work", "--max-neighbors", "2"},
         "graph_explain",
         R"({"node":"pkg/lib.py:11","graph":"work","max_neighbors":2})",
         "explain",
         {{"node", "pkg/lib.py:11"}, {"max_neighbors", "2"}}},
        {{"graph", "neighbors", "pkg.lib.helper", "--graph", "work", "--relation", "calls",
          "--direction", "in", "--max-neighbors", "5"},
         "graph_neighbors",
         R"({"node":"pkg.lib.helper","graph":"work","relation":"calls","direction":"in","max_neighbors":5})",
         "neighbors",
         {{"node", "pkg.lib.helper"},
          {"relation", "calls"},
          {"direction", "in"},
          {"max_neighbors", "5"}}},
        {{"graph", "query", "where does the vault keep readings", "--graph", "work"},
         "graph_query",
         R"({"question":"where does the vault keep readings","graph":"work"})",
         "query",
         {{"q", "where does the vault keep readings"}}},
        {{"graph", "query", "pkg.lib.helper", "--graph", "work", "--hops", "2", "--max-entities",
          "50"},
         "graph_query",
         R"({"question":"pkg.lib.helper","graph":"work","hops":2,"max_entities":50})",
         "query",
         {{"q", "pkg.lib.helper"}, {"hops", "2"}, {"max_entities", "50"}}},
    };
    for (const Golden& golden : goldens) {
        INFO(golden.arguments);
        std::vector<std::string> args = golden.args;
        args.insert(args.end(), {"--output-format", "json"});
        std::string out;
        std::string err;
        REQUIRE(fixture.run(args, &out, &err) == 0);
        CHECK(err.empty());
        // One document on one line, and nothing else on stdout.
        REQUIRE(out.ends_with("\n"));
        CHECK(out.find('\n') == out.size() - 1);
        const std::string document = out.substr(0, out.size() - 1);
        CHECK_FALSE(nlohmann::json::parse(document, nullptr, false).is_discarded());

        const apogee::agent::ToolOutcome tool = fixture.tool(golden.tool, golden.arguments);
        CHECK_FALSE(tool.is_error);
        CHECK(tool.content == document);

        // The scoped instance (27o), handed the same store: the same bytes,
        // the selection gone from its arguments.
        nlohmann::json unselected = nlohmann::json::parse(golden.arguments);
        unselected.erase("graph");
        const apogee::agent::ToolOutcome scoped =
            fixture.scoped("work", golden.tool, unselected.dump());
        CHECK_FALSE(scoped.is_error);
        CHECK(scoped.content == document);

        const apogee::httpserver::HttpResponse served =
            fixture.served(golden.verb, "work", golden.query);
        CHECK(served.status == 200);
        CHECK(served.body == document);
    }
}

TEST_CASE("graph path prints each hop with its relation and origin, or says there is none",
          "[commands][graph][navigate]") {
    const Fixture fixture;
    std::string out;
    REQUIRE(fixture.run({"graph", "path", "main", "pkg.lib.helper", "--graph", "work"}, &out) == 0);
    INFO(out);
    CHECK(contains(out, "('main' is pkg.app.main, by its unqualified name)\n"));
    CHECK(contains(out,
                   "Path from pkg.app.main (function, pkg/app.py:12) to pkg.lib.helper "
                   "(function, pkg/lib.py:1) in graph \"work\" -- 2 hop(s), undirected:\n"));
    CHECK(contains(out, "  pkg.app.main -[calls·extracted]-> pkg.app.run  at pkg/app.py:13\n"));
    CHECK(contains(out, "  pkg.app.run -[calls·extracted]-> pkg.lib.helper  at pkg/app.py:6\n"));

    REQUIRE(fixture.run({"graph", "path", "pkg.lib.helper", "pkg.app.main", "--graph", "work"},
                        &out) == 0);
    CHECK(contains(out, "  pkg.lib.helper <-[calls·extracted]- pkg.app.run  at pkg/app.py:6\n"));

    REQUIRE(fixture.run({"graph", "path", "system:Atlas", "Vault", "--graph", "work"}, &out) == 0);
    CHECK(contains(out,
                   "  Atlas -[stores readings in·inferred]-> Vault -- Atlas writes its "
                   "readings to Vault\n"));

    REQUIRE(fixture.run({"graph", "path", "pkg.app.main", "pkg.island.alone", "--graph", "work"},
                        &out) == 0);
    CHECK(out ==
          "No path within 8 hops between pkg.app.main (function, pkg/app.py:12) and "
          "pkg.island.alone (function, pkg/island.py:1) in graph \"work\" (undirected).\n");

    REQUIRE(fixture.run({"graph", "path", "pkg.chain.n0", "pkg.chain.n10", "--graph", "work",
                         "--directed", "--relation", "calls"},
                        &out) == 0);
    CHECK(contains(out, "No path within 8 hops"));
    CHECK(contains(out, "(directed, relations: calls)"));
    REQUIRE(fixture.run({"graph", "path", "pkg.chain.n0", "pkg.chain.n10", "--graph", "work",
                         "--directed", "--max-hops", "10"},
                        &out) == 0);
    CHECK(contains(out, "-- 10 hop(s), directed:"));
}

TEST_CASE("graph explain and neighbors render the card and the capped groups",
          "[commands][graph][navigate]") {
    const Fixture fixture;
    std::string out;
    REQUIRE(fixture.run({"graph", "explain", "pkg.lib.helper", "--graph", "work"}, &out) == 0);
    INFO(out);
    CHECK(
        out.starts_with("pkg.lib.helper (function, pkg/lib.py:1) in graph \"work\"\n"
                        "  def helper(x: int) -> int\n"
                        "  Degree: 18 edge(s): 1 out, 17 in; 1 mention(s)\n"
                        "Stated at:\n"
                        "  definition  app: pkg/lib.py:1-3\n"
                        "Relations:\n"
                        "  calls <- (17)\n"
                        "    pkg.app.run (function, pkg/app.py:5) x2  at pkg/app.py:6\n"));
    CHECK(contains(out, "    ... and 5 more\n"));
    CHECK(contains(out,
                   "  defined_in -> (1)\n    pkg/lib.py (file, pkg/lib.py:1)  at pkg/lib.py:1\n"));

    REQUIRE(fixture.run({"graph", "explain", "Vault", "--collection", "notes"}, &out) == 0);
    CHECK(contains(out, "Vault (system) in graph \"work\"\n"));
    CHECK(contains(out,
                   "Mentioned in:\n  notes: docs/atlas.md [chunk 0]\n  notes: docs/vault.md "
                   "[chunk 0]\n  notes: docs/log.md [chunk 0]\n"));
    CHECK(contains(out, "  ... and 2 more\n"));
    CHECK(contains(out,
                   "Community #1 (3 members): Atlas and the Vault it writes its readings to.\n"));
    CHECK(contains(out, "Decisions:\n  kr-0001 (decision, shipped): Keep every reading in Vault"));

    REQUIRE(fixture.run(
                {"graph", "neighbors", "pkg.lib.helper", "--graph", "work", "--direction", "out"},
                &out) == 0);
    CHECK(out ==
          "Neighbours of pkg.lib.helper (function, pkg/lib.py:1) in graph \"work\" -- 18 "
          "edge(s): 1 out, 17 in\n  defined_in -> (1)\n    pkg/lib.py (file, pkg/lib.py:1)  "
          "at pkg/lib.py:1\n");
    REQUIRE(fixture.run({"graph", "neighbors", "pkg.lib.helper", "--graph", "work", "--relation",
                         "imports"},
                        &out) == 0);
    CHECK(contains(out, "  none over 'imports'\n"));
}

TEST_CASE("graph query prints the section a turn would inject", "[commands][graph][navigate]") {
    const Fixture fixture;
    std::string out;
    REQUIRE(fixture.run({"graph", "query", "where does the vault keep readings", "--graph", "docs"},
                        &out) == 0);
    INFO(out);
    CHECK(contains(out,
                   "Matched 1 entity by name: Vault -- 1 hop(s), at most 8 entities\n"
                   "[Knowledge graph: docs]\nVault (system): the warehouse Atlas stores "
                   "readings in\n"));
    CHECK(
        contains(out, "Atlas —[stores readings in]→ Vault: Atlas writes its readings to Vault\n"));
    CHECK(contains(out, "Vault —[implemented by]→ pkg.lib.Store\n"));

    REQUIRE(fixture.run({"graph", "query", "pkg.lib.helper", "--graph", "docs", "--hops", "2",
                         "--max-entities", "50"},
                        &out) == 0);
    CHECK(contains(out, "Matched 1 entity by exact name: pkg.lib.helper"));
    CHECK(contains(out, "pkg.app.run —[calls·extracted]→ pkg.lib.helper\n"));
    CHECK(contains(out, "(cut at the 1500-codepoint budget a turn's graph section keeps)\n"));

    REQUIRE(fixture.run({"graph", "query", "zzqx", "--graph", "docs"}, &out) == 0);
    CHECK(out == "Nothing in graph \"docs\" is named by \"zzqx\".\n");
}

TEST_CASE("navigation refusals are user errors, on stderr, naming what to do",
          "[commands][graph][navigate]") {
    const Fixture fixture;

    struct Row {
        std::vector<std::string> args;
        std::string part;
    };

    const std::vector<Row> rows{
        {{"graph", "explain", "helper", "--graph", "work", "--output-format", "json"},
         "apogee graph: 'helper' names 2 nodes in graph 'work' -- name one: "
         "function:pkg.lib.helper (pkg/lib.py:1), function:pkg.util.helper (pkg/util.py:1)\n"},
        {{"graph", "explain", "zzqx", "--graph", "work"},
         "apogee graph: no node named 'zzqx' in graph 'work' -- nothing has a name near it\n"},
        {{"graph", "explain", "Vault"}, "2 graphs are built -- name one: work, docs"},
        {{"graph", "explain", "Vault", "--graph", "work", "--collection", "notes"},
         "name a graph or a collection, not both"},
        {{"graph", "explain", "Vault", "--graph", "nope"},
         "no graph or collection named 'nope' -- built: work, docs"},
        {{"graph", "path", "main", "pkg.app.run", "--graph", "work", "--max-hops", "40"},
         "the hop cap must be between 1 and 32 (got 40)"},
        {{"graph", "query", "vault", "--graph", "work", "--hops", "3"},
         "a query walks 1 or 2 hops (got 3)"},
        {{"graph", "neighbors", "Vault", "--graph", "work", "--max-neighbors", "0"},
         "the neighbour cap must be between 1 and 100 per relation (got 0)"},
    };
    for (const Row& row : rows) {
        INFO(row.part);
        std::string out;
        std::string err;
        CHECK(fixture.run(row.args, &out, &err) == 1);
        CHECK(out.empty());
        CHECK(contains(err, row.part));
    }
    // A turn's stream is not a read's format.
    std::string out;
    std::string err;
    CHECK(fixture.run(
              {"graph", "explain", "Vault", "--graph", "work", "--output-format", "stream-json"},
              &out, &err) != 0);
    CHECK(out.empty());
}

TEST_CASE("with one graph built, the verbs need no selection", "[commands][graph][navigate]") {
    const Fixture fixture{/*with_docs=*/false};
    std::string out;
    REQUIRE(fixture.run({"graph", "explain", "kr-0001"}, &out) == 0);
    CHECK(out.starts_with("kr-0001 (decision, shipped) in graph \"work\"\n"));
    CHECK(contains(out, "  Discipline: engineering\n"));
}

TEST_CASE("one core, scoped: a chat's attachment graph answers as the CLI does over the same tree",
          "[commands][graph][navigate][parity][attachments]") {
    // 27o: the folder attached to a chat holds, in the chat's index, the graph
    // `graph build --source` builds over the same tree (27n); the scoped
    // tools over the index and the verbs over that build print one document.
    const Fixture fixture{/*with_docs=*/false};
    const std::filesystem::path tree = fixture.home.path() / "work" / "app";
    std::filesystem::create_directories(tree.parent_path());
    std::filesystem::copy(std::filesystem::path{APOGEE_TEST_FIXTURES} / "code_graph" / "python",
                          tree, std::filesystem::copy_options::recursive);
    REQUIRE(fixture.run({"graph", "build", "--source", tree.string(), "--graph", "attachments"}) ==
            0);

    apogee::harness::Harness harness{apogee::harness::parse_config("# no backends\n", "<test>")};
    harness.use_default_router();
    apogee::logger::Session session;
    session.chat_id = "golden";
    apogee::commands::ChatAttachments attached{
        harness, session, apogee::commands::ChatAttachments::index_for("golden"),
        apogee::commands::ChatAttachments::Hooks{.say = [](const std::string&, bool) {},
                                                 .progress = {},
                                                 .confirm_large = {},
                                                 .save = false,
                                                 .code_graph = true}};
    REQUIRE(attached.attach("app", tree.parent_path()));
    attached.settle();
    const std::optional<apogee::commands::AttachmentGraphScope> scope = attached.graph_scope();
    REQUIRE(scope.has_value());
    apogee::agent::ToolRegistry registry;
    apogee::tools::register_graph_tools(
        registry, {.config = nullptr,
                   .scope = apogee::commands::attachment_graph_target(*scope),
                   .scope_note = apogee::commands::attachment_graph_note(*scope)});

    struct Golden {
        std::vector<std::string> args;
        std::string tool;
        std::string arguments;
    };

    const std::vector<Golden> goldens{
        {{"graph", "explain", "make_user"}, "graph_explain", R"({"node":"make_user"})"},
        {{"graph", "explain", "pkg/models.py:11", "--max-neighbors", "2"},
         "graph_explain",
         R"({"node":"pkg/models.py:11","max_neighbors":2})"},
        {{"graph", "path", "main", "make_user", "--directed", "--relation", "calls"},
         "graph_path",
         R"({"from":"main","to":"make_user","directed":true,"relations":["calls"]})"},
        {{"graph", "neighbors", "pkg.models.User", "--direction", "in"},
         "graph_neighbors",
         R"({"node":"pkg.models.User","direction":"in"})"},
        {{"graph", "query", "greet"}, "graph_query", R"({"question":"greet"})"},
    };
    for (const Golden& golden : goldens) {
        INFO(golden.arguments);
        std::vector<std::string> args = golden.args;
        args.insert(args.end(), {"--graph", "attachments", "--output-format", "json"});
        std::string out;
        std::string err;
        REQUIRE(fixture.run(args, &out, &err) == 0);
        CHECK(err.empty());
        REQUIRE(out.ends_with("\n"));
        const apogee::agent::ToolOutcome tool = registry.find(golden.tool)->run(golden.arguments);
        CHECK_FALSE(tool.is_error);
        CHECK(tool.content == out.substr(0, out.size() - 1));
    }
}
