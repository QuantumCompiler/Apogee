#include "cli/tui_knowledge.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "agentloop/graph_context.h"
#include "cli/root.h"
#include "contracts/layout.h"
#include "contracts/paths.h"
#include "embedstore/store.h"
#include "knowledge/record.h"
#include "knowledge/store.h"
#include "support/cli_home.h"
#include "support/graph_fixture.h"
#include "tui/list_view.h"
#include "tui/pump.h"
#include "tui/shell.h"

/// The shell's knowledge views (37c) held to their commands, the 32d way: the
/// Knowledge view's rows are `knowledge list --output-format json`'s, its
/// card `knowledge info`'s and its query `knowledge query`'s output, byte for
/// byte; the Collections view's rows are `embed list`'s document, its query
/// `embed query`'s words -- the retriever that ran, a demotion said the same;
/// the Graph view's rows are `graph stats`'s documents and its card `graph
/// explain`'s. Every delete asks, and a no writes nothing.
namespace {

namespace fs = std::filesystem;
using apogee::tui::Key;

constexpr const char* kConfig = R"(backends:
  local:
    type: mock
models:
  default: local
)";

constexpr const char* kEmbedderConfig = R"(backends:
  local:
    type: mock
  embed:
    type: mock
    embedding_model: mock-space
models:
  default: local
  default_embedding: embed
)";

[[nodiscard]] apogee::commands::RootContext context_of(const apogee::testing::CliHome& home) {
    apogee::commands::RootContext context;
    context.config_path = home.config_path().string();
    return context;
}

/// One view on a shell over a manual pump, as the shell draws it.
struct Stage {
    explicit Stage(apogee::tui::ListOptions options)
        : view{pump, apogee::tui::Theme{.color = false}, std::move(options)} {
        shell.add(view.view());
        shell.activate(0);
    }

    [[nodiscard]] std::string frame() {
        for (int i = 0; i < 3; ++i) {
            view.settle();
            (void)pump.drain();
        }
        return shell.render_text(200, 40);
    }

    void press(const Key& key) {
        (void)shell.press(key);
        (void)frame();
    }

    void type(const std::string& text) {
        for (const char c : text) {
            press(Key::character(std::string(1, c)));
        }
    }

    apogee::tui::ManualPump pump;
    apogee::tui::Shell shell{{.title = "apogee test", .theme = {.color = false}}};
    apogee::tui::ListView view;
};

[[nodiscard]] bool has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

[[nodiscard]] std::string joined(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& line : lines) {
        out += line + "\n";
    }
    return out;
}

void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << text;
}

/// Two records in the default collection: one shipped, one rejected.
void seed_records() {
    apogee::knowledge::Store store{apogee::harness::embeddings_dir() / "knowledge.db",
                                   apogee::harness::knowledge_raw_dir()};
    apogee::knowledge::Record shipped;
    shipped.id = "kr-20260913T120010Z-00000a";
    shipped.intent = "the cancel button was removed after testing\nsecond line";
    shipped.decision = "remove it";
    shipped.status = "shipped";
    shipped.discipline = "ux";
    shipped.timestamp = "2026-09-13T12:00:10.000000Z";
    store.put(shipped, {}, "Ada: drop the cancel button? Bob: yes");
    apogee::knowledge::Record rejected;
    rejected.id = "kr-20260913T120020Z-00000b";
    rejected.intent = "keep the cancel button";
    rejected.status = "rejected";
    rejected.discipline = "eng";
    rejected.timestamp = "2026-09-13T12:00:20.000000Z";
    store.put(rejected, {}, "raw");
}

}  // namespace

TEST_CASE("the Knowledge view draws knowledge list's document, info's card and query's words",
          "[cli][tui][knowledge]") {
    const apogee::testing::CliHome home{kConfig};
    const apogee::commands::RootContext context = context_of(home);
    {
        // No collection yet: the command's words, no rows.
        const auto [heading, rows] = apogee::commands::knowledge_view_options(context).load();
        CHECK(rows.empty());
        std::string out;
        std::string err;
        CHECK(home.run({"knowledge", "list"}, &out, &err) != 0);
        REQUIRE(heading.size() == 1);
        CHECK(has(err, heading.front()));
    }
    seed_records();
    std::string out;
    std::string err;
    REQUIRE(home.run({"knowledge", "list", "--output-format", "json"}, &out, &err) == 0);
    const nlohmann::json document = nlohmann::json::parse(out);
    const apogee::tui::ListOptions options = apogee::commands::knowledge_view_options(context);
    const auto [heading, rows] = options.load();
    REQUIRE(rows.size() == document["data"].size());
    REQUIRE(rows.size() == 2);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const nlohmann::json& record = document["data"].at(i);
        const std::string intent = record["intent"].get<std::string>();
        CHECK(rows.at(i).cells ==
              std::vector<std::string>{record["id"], record["status"], record["discipline"],
                                       record["timestamp"], intent.substr(0, intent.find('\n'))});
    }
    REQUIRE(home.run({"knowledge", "list"}, &out, &err) == 0);
    CHECK(heading == std::vector<std::string>{out.substr(0, out.find('\n'))});

    // Enter: the record as `knowledge info` prints it.
    const apogee::tui::ListRow& shipped = rows.at(1);
    REQUIRE(shipped.key == "kr-20260913T120010Z-00000a");
    REQUIRE(home.run({"knowledge", "info", shipped.key}, &out, &err) == 0);
    CHECK(joined(options.detail(shipped)) == out);

    // `/`: the query as `knowledge query` answers it, retriever and scores.
    REQUIRE(home.run({"knowledge", "query", "cancel button"}, &out, &err) == 0);
    CHECK(has(out, "[lexical]"));
    CHECK(options.ask(apogee::tui::ListRow{}, "cancel button") == out);
    REQUIRE(home.run({"knowledge", "query", "sqlite"}, &out, &err) == 0);
    CHECK(options.ask(apogee::tui::ListRow{}, "sqlite") == out);

    // The input row on the shell: typing, then Enter asks.
    Stage stage{apogee::commands::knowledge_view_options(context)};
    CHECK(has(stage.frame(), "/ query"));
    stage.press(Key::character("/"));
    CHECK(stage.view.view().takes_text());
    stage.type("cancel button");
    CHECK(has(stage.frame(), "query: cancel button"));
    stage.press(Key::named(Key::Name::Return));
    CHECK_FALSE(stage.view.view().takes_text());
    CHECK(has(stage.frame(), "Top 1 result(s) for \"cancel button\" in \"knowledge\" [lexical]:"));
}

TEST_CASE("deleting from the Knowledge view asks, and a no writes nothing",
          "[cli][tui][knowledge]") {
    const apogee::testing::CliHome home{kConfig};
    const apogee::commands::RootContext context = context_of(home);
    seed_records();
    const apogee::knowledge::Store store{apogee::harness::embeddings_dir() / "knowledge.db",
                                         apogee::harness::knowledge_raw_dir()};
    const std::string raw = store.get("kr-20260913T120020Z-00000b")->raw_ref;
    REQUIRE(fs::exists(raw));

    Stage stage{apogee::commands::knowledge_view_options(context)};
    CHECK(has(stage.frame(), "x delete"));
    stage.press(Key::character("x"));
    CHECK(has(stage.frame(),
              "Delete kr-20260913T120020Z-00000b and its archived conversation? [y/N]"));
    stage.press(Key::character("n"));
    CHECK(has(stage.frame(), "not done"));
    CHECK(store.get("kr-20260913T120020Z-00000b").has_value());

    stage.press(Key::character("x"));
    stage.press(Key::character("y"));
    CHECK(has(stage.frame(), "Deleted kr-20260913T120020Z-00000b"));
    CHECK_FALSE(store.get("kr-20260913T120020Z-00000b").has_value());
    CHECK_FALSE(fs::exists(raw));
    CHECK_FALSE(has(stage.frame(), "keep the cancel button"));
}

TEST_CASE("the Collections view draws embed list's document, info's text and query's words",
          "[cli][tui][knowledge]") {
    const apogee::testing::CliHome home{kEmbedderConfig};
    const apogee::commands::RootContext context = context_of(home);
    write(home.home() / "docs" / "heron.md",
          "Project Heron is the billing ledger. It deploys to Frankfurt.\n");
    std::string out;
    std::string err;
    REQUIRE(home.run({"embed", "ingest", "notes", (home.home() / "docs").string()}, &out, &err) ==
            0);

    REQUIRE(home.run({"embed", "list", "--output-format", "json"}, &out, &err) == 0);
    const nlohmann::json document = nlohmann::json::parse(out);
    const apogee::tui::ListOptions options = apogee::commands::collections_view_options(context);
    const auto [heading, rows] = options.load();
    REQUIRE(rows.size() == document["data"].size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const nlohmann::json& row = document["data"].at(i);
        CHECK(rows.at(i).cells ==
              std::vector<std::string>{row["name"], row["chunks"].dump(), row["sources"].dump()});
    }
    REQUIRE(rows.size() == 1);
    const apogee::tui::ListRow& notes = rows.front();
    REQUIRE(home.run({"embed", "info", "notes"}, &out, &err) == 0);
    CHECK(joined(options.detail(notes)) == out);

    // With the embedder the vectors were made by: the retriever the command
    // names, said the same.
    REQUIRE(home.run({"embed", "query", "notes", "Heron billing"}, &out, &err) == 0);
    const std::string with_embedder = out;
    CHECK(options.ask(notes, "Heron billing") == with_embedder);
    // Without it: the demotion to lexical, said the same.
    write(home.config_path(), kConfig);
    REQUIRE(home.run({"embed", "query", "notes", "Heron billing"}, &out, &err) == 0);
    CHECK(has(out, "lexical"));
    CHECK(out != with_embedder);
    CHECK(options.ask(notes, "Heron billing") == out);
}

TEST_CASE("deleting a collection from the view asks, and a no keeps it", "[cli][tui][knowledge]") {
    const apogee::testing::CliHome home{kConfig};
    const apogee::commands::RootContext context = context_of(home);
    write(home.home() / "docs" / "a.md", "alpha beta gamma\n");
    std::string out;
    std::string err;
    REQUIRE(home.run({"embed", "ingest", "notes", (home.home() / "docs").string()}, &out, &err) ==
            0);
    const fs::path db = apogee::harness::embeddings_dir() / "notes.db";
    REQUIRE(fs::exists(db));

    Stage stage{apogee::commands::collections_view_options(context)};
    CHECK(has(stage.frame(), "notes"));
    stage.press(Key::character("x"));
    CHECK(has(stage.frame(), "Delete the whole collection notes, every chunk in it? [y/N]"));
    stage.press(Key::character("n"));
    CHECK(fs::exists(db));
    stage.press(Key::character("x"));
    stage.press(Key::character("y"));
    CHECK(has(stage.frame(), "will delete the whole collection:"));
    CHECK_FALSE(fs::exists(db));
    CHECK(has(stage.frame(), "no collections yet"));
}

TEST_CASE("the Graph view draws graph stats' documents, and its card is graph explain's",
          "[cli][tui][knowledge]") {
    const apogee::testing::CliHome home{
        "graphs:\n  work:\n    collections: [notes]\n  idle:\n"
        "    collections: [notes]\n"};
    const apogee::commands::RootContext context = context_of(home);
    {
        apogee::embedstore::Store notes{apogee::harness::embeddings_dir() / "notes.db"};
        apogee::embedstore::Store work{apogee::agentloop::graph_db_path("work")};
        apogee::testing::build_navigation_graph(work, notes, "notes");
        apogee::embedstore::Store docs{apogee::harness::embeddings_dir() / "docs.db"};
        apogee::testing::build_navigation_graph(docs, docs, "");
        // A collection with no graph of its own is not a graph.
        (void)apogee::embedstore::Store{apogee::harness::embeddings_dir() / "plain.db"};
    }
    const apogee::tui::ListOptions options = apogee::commands::graph_view_options(context);
    const auto [heading, rows] = options.load();
    std::vector<std::string> keys;
    for (const apogee::tui::ListRow& row : rows) {
        keys.push_back(row.key);
    }
    // The named graphs as configured, then each collection holding a graph.
    CHECK(keys == std::vector<std::string>{"work", "idle", "docs"});
    std::string out;
    std::string err;
    for (const apogee::tui::ListRow& row : rows) {
        REQUIRE(home.run({"graph", "stats", row.key, "--output-format", "json"}, &out, &err) == 0);
        const nlohmann::json document = nlohmann::json::parse(out);
        INFO(document.dump());
        const bool built = document["built"].get<bool>();
        CHECK(row.cells ==
              std::vector<std::string>{document["graph"], document["kind"],
                                       built ? document["nodes"].dump() : std::string{"not built"},
                                       built ? document["edges"].dump() : std::string{}});
        REQUIRE(home.run({"graph", "stats", row.key}, &out, &err) == 0);
        CHECK(joined(options.detail(row)) == out);
    }
    CHECK(rows.at(1).cells.at(2) == "not built");

    // `/`: a node's card, the command's own bytes, for either kind of graph.
    for (const std::string& graph : {std::string{"work"}, std::string{"docs"}}) {
        REQUIRE(home.run({"graph", "explain", "Vault", "--graph", graph}, &out, &err) == 0);
        CHECK(options.ask(apogee::tui::ListRow{.key = graph}, "Vault") == out);
    }
    // A node that is not there: refused in the command's words.
    CHECK(home.run({"graph", "explain", "Nowhere", "--graph", "work"}, &out, &err) != 0);
    Stage stage{apogee::commands::graph_view_options(context)};
    CHECK(has(stage.frame(), "/ explain"));
    stage.press(Key::character("/"));
    stage.type("Nowhere");
    stage.press(Key::named(Key::Name::Return));
    const std::string refused = err.substr(err.find(": ") + 2);
    CHECK(has(stage.frame(), "could not ask: " + refused.substr(0, refused.find('\n'))));
}
