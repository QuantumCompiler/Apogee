#include "cli/graph_artifacts.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "agentloop/graph_context.h"
#include "cli/registry.h"
#include "cli/root.h"
#include "contracts/layout.h"
#include "embedstore/store.h"
#include "graph/export_graphml.h"
#include "graph/navigate.h"
#include "graph/report.h"
#include "render/graph_report.h"
#include "support/env_guard.h"
#include "support/graph_fixture.h"

/// `apogee graph report` and `graph export html|graphml|mermaid` (27m),
/// in-process with **no backend configured**: the report printed and the
/// same bytes at `--out`; `--graph`/`--collection`; the exports at `--out`,
/// on stdout for `-`, and under their default names in the working
/// directory; failures as user errors; and the privacy sweep -- a canary
/// planted in a chunk's text and in every private layout row is in no
/// artifact, and the data directory is byte for byte what it was.
namespace {

void write(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << content;
}

[[nodiscard]] std::string slurp(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

[[nodiscard]] bool contains(std::string_view text, std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

/// A home with the named graph `work` over `notes` -- no backends at all.
struct Fixture {
    apogee::testing::TempDir home{"graph-artifacts-cli-" + std::to_string(std::random_device{}())};
    apogee::testing::TempDir elsewhere{"graph-artifacts-out-" +
                                       std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    std::filesystem::path config_path = home.path() / "config" / "config.yaml";

    explicit Fixture(bool built = true) {
        write(config_path,
              "# no backends -- the artifacts need none\ngraphs:\n  work:\n"
              "    collections: [notes]\n");
        if (!built) {
            return;
        }
        apogee::embedstore::Store notes{apogee::harness::embeddings_dir() / "notes.db"};
        apogee::embedstore::Store work{apogee::agentloop::graph_db_path("work")};
        apogee::testing::build_navigation_graph(work, notes, "notes");
        apogee::testing::add_report_orphans(work, notes, "notes");
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

    /// The report as the store makes it, rendered.
    [[nodiscard]] static std::string expected_report() {
        const apogee::embedstore::Store work{apogee::agentloop::graph_db_path("work")};
        return apogee::render::render_graph_report(
            apogee::graph::to_json(apogee::graph::build_report(work, "work")));
    }

    /// Every file under the home: its path and its bytes.
    [[nodiscard]] std::map<std::string, std::string> snapshot() const {
        std::map<std::string, std::string> out;
        for (const auto& entry : std::filesystem::recursive_directory_iterator{home.path()}) {
            if (entry.is_regular_file()) {
                out[std::filesystem::relative(entry.path(), home.path()).generic_string()] =
                    slurp(entry.path());
            }
        }
        return out;
    }
};

/// Moves the working directory for the life of the object.
class WorkingDirectory {
public:
    explicit WorkingDirectory(const std::filesystem::path& to)
        : previous_{std::filesystem::current_path()} {
        std::filesystem::current_path(to);
    }

    ~WorkingDirectory() {
        std::error_code ignored;
        std::filesystem::current_path(previous_, ignored);
    }

    WorkingDirectory(const WorkingDirectory&) = delete;
    WorkingDirectory& operator=(const WorkingDirectory&) = delete;
    WorkingDirectory(WorkingDirectory&&) = delete;
    WorkingDirectory& operator=(WorkingDirectory&&) = delete;

private:
    std::filesystem::path previous_;
};

}  // namespace

TEST_CASE("graph report prints the architecture summary, and --out writes the same bytes",
          "[commands][graph][artifacts][report]") {
    const Fixture fixture;
    std::string out;
    std::string err;
    REQUIRE(fixture.run({"graph", "report", "--graph", "work"}, &out, &err) == 0);
    CHECK(err.empty());
    CHECK(out == Fixture::expected_report());
    CHECK(out.starts_with("# Graph report: work\n"));

    const std::filesystem::path file = fixture.elsewhere.path() / "report.md";
    std::string said;
    REQUIRE(fixture.run({"graph", "report", "--graph", "work", "--out", file.string()}, &said) ==
            0);
    CHECK(slurp(file) == out);
    CHECK(said == "Wrote the report on graph \"work\" to " + file.string() + " (" +
                      std::to_string((out.size() + 512) / 1024) + " KB).\n");

    // The covering graph of a collection, and the one graph built: the same.
    std::string by_collection;
    REQUIRE(fixture.run({"graph", "report", "--collection", "notes"}, &by_collection) == 0);
    CHECK(by_collection == out);
    std::string by_default;
    REQUIRE(fixture.run({"graph", "report"}, &by_default) == 0);
    CHECK(by_default == out);
}

TEST_CASE("graph report --output-format json prints the report's one document",
          "[commands][graph][artifacts][report]") {
    const Fixture fixture;
    std::string out;
    REQUIRE(fixture.run({"graph", "report", "--output-format", "json"}, &out) == 0);
    REQUIRE(out.ends_with("\n"));
    CHECK(out.find('\n') == out.size() - 1);
    const nlohmann::json document = nlohmann::json::parse(out);
    CHECK(document["object"] == "graph.report");
    const apogee::embedstore::Store work{apogee::agentloop::graph_db_path("work")};
    CHECK(document == apogee::graph::to_json(apogee::graph::build_report(work, "work")));
    CHECK(document["human_summary"].get<std::string>().starts_with("Graph \"work\" holds"));
    CHECK(fixture.run({"graph", "report", "--output-format", "stream-json"}) != 0);
}

TEST_CASE("the exports land at --out, on stdout for -, or under their default names here",
          "[commands][graph][artifacts][export]") {
    const Fixture fixture;
    const std::filesystem::path html = fixture.elsewhere.path() / "page.html";
    std::string out;
    REQUIRE(fixture.run({"graph", "export", "html", "--graph", "work", "-o", html.string()},
                        &out) == 0);
    CHECK(contains(slurp(html), "<script type=\"application/json\" id=\"graph-data\">"));
    CHECK(out.starts_with("Exported graph \"work\" to " + html.string() + ": "));
    CHECK(contains(out, " entities and "));
    CHECK(contains(out, "\nShowing all "));

    std::string xml;
    REQUIRE(fixture.run({"graph", "export", "graphml", "--out", "-"}, &xml) == 0);
    const apogee::embedstore::Store work{apogee::agentloop::graph_db_path("work")};
    CHECK(xml == apogee::graph::export_graphml(work, "work").xml);

    const std::filesystem::path mermaid = fixture.elsewhere.path() / "calls.mmd";
    REQUIRE(fixture.run({"graph", "export", "mermaid", "--collection", "notes", "--out",
                         mermaid.string(), "--max-nodes", "5"},
                        &out) == 0);
    CHECK(slurp(mermaid).starts_with("flowchart LR\n"));
    CHECK(contains(out, "the call flow of 5 functions and "));
    CHECK(contains(out, ", the highest-degree 5 of 30."));

    // No --out: the working directory, under the default names.
    CHECK(apogee::commands::default_artifact_name("work", "html") == "work.html");
    CHECK(apogee::commands::default_artifact_name("work", "graphml") == "work.graphml");
    CHECK(apogee::commands::default_artifact_name("work", "mermaid") == "work.mmd");
    const apogee::testing::TempDir here{"graph-artifacts-cwd-" +
                                        std::to_string(std::random_device{}())};
    {
        const WorkingDirectory moved{here.path()};
        for (const std::string_view kind : {"html", "graphml", "mermaid"}) {
            REQUIRE(fixture.run({"graph", "export", std::string{kind}}) == 0);
        }
    }
    for (const std::string_view name : {"work.html", "work.graphml", "work.mmd"}) {
        INFO(name);
        CHECK(std::filesystem::is_regular_file(here.path() / name));
    }
}

TEST_CASE("an artifact of nothing, or past a cap, is a user error naming what to do",
          "[commands][graph][artifacts]") {
    {
        const Fixture unbuilt{false};
        std::string out;
        std::string err;
        CHECK(unbuilt.run({"graph", "report"}, &out, &err) == 1);
        CHECK(out.empty());
        CHECK(contains(err, "apogee graph: no graph is built yet"));
        CHECK(unbuilt.run({"graph", "export", "html", "--graph", "work"}, &out, &err) == 1);
        CHECK(contains(err, "graph 'work' has not been built yet"));
    }
    const Fixture fixture;
    std::string err;
    CHECK(fixture.run({"graph", "export", "html", "--max-nodes", "2001", "-o", "-"}, nullptr,
                      &err) == 1);
    CHECK(contains(err, "the node cap must be between 1 and 2000"));
    CHECK(fixture.run({"graph", "export", "mermaid", "--max-nodes", "151", "-o", "-"}, nullptr,
                      &err) == 1);
    CHECK(contains(err, "between 1 and 150"));
    CHECK(fixture.run({"graph", "report", "--graph", "nothing"}, nullptr, &err) == 1);
    CHECK(contains(err, "no graph or collection named 'nothing'"));
    CHECK(fixture.run({"graph", "export"}) != 0);
}

TEST_CASE(
    "the privacy sweep: no artifact holds private-row content or a chunk's text, and the "
    "data directory is unchanged",
    "[commands][graph][artifacts][privacy]") {
    const Fixture fixture;
    const std::string canary = "PRIVATE-CANARY-27m-a91f";
    {
        // A chunk's text, mentioned by an entity whose card lists it.
        apogee::embedstore::Store notes{apogee::harness::embeddings_dir() / "notes.db"};
        apogee::embedstore::Store work{apogee::agentloop::graph_db_path("work")};
        notes.replace_source("docs/secret.md", {"The ledger's key is " + canary + "."});
        const std::int64_t chunk = notes.chunks_by_source("docs/secret.md").front().id;
        (void)work.add_mention(apogee::graph::resolve_node(work, "work", "Ledger").node.id, "notes",
                               chunk);
    }
    // Every private layout row holds it too.
    const std::filesystem::path home = fixture.home.path();
    write(apogee::harness::knowledge_raw_dir() / "kr-0001.md", "raw conversation: " + canary);
    write(apogee::harness::sessions_dir() / "chat-1.jsonl", "{\"text\":\"" + canary + "\"}\n");
    write(apogee::harness::memory_dir() / "notes.txt", canary);
    write(apogee::harness::attachments_dir() / "chat-1" / "a.txt", canary);
    write(apogee::harness::tasks_dir() / "task-1" / "task.json", "{\"goal\":\"" + canary + "\"}");
    write(apogee::harness::training_datasets_dir() / "mined.jsonl", canary);
    const std::map<std::string, std::string> before = fixture.snapshot();

    std::vector<std::string> artifacts;
    std::string out;
    REQUIRE(fixture.run({"graph", "report"}, &out) == 0);
    artifacts.push_back(out);
    REQUIRE(fixture.run({"graph", "report", "--output-format", "json"}, &out) == 0);
    artifacts.push_back(out);
    for (const std::string_view kind : {"html", "graphml", "mermaid"}) {
        REQUIRE(fixture.run({"graph", "export", std::string{kind}, "-o", "-"}, &out) == 0);
        artifacts.push_back(out);
    }
    const std::filesystem::path file = fixture.elsewhere.path() / "page.html";
    REQUIRE(fixture.run({"graph", "export", "html", "-o", file.string()}) == 0);
    artifacts.push_back(slurp(file));

    for (const std::string& artifact : artifacts) {
        CHECK_FALSE(contains(artifact, canary));
        CHECK_FALSE(contains(artifact, "The ledger's key is"));
    }
    // The mention is surfaced as the source it is, and nothing more.
    CHECK(contains(artifacts[2], R"(docs\/secret.md)"));
    // Nothing was written into the data directory, nothing changed there.
    CHECK(fixture.snapshot() == before);
}
