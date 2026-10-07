#include "cli/attachment_graph.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "agentloop/attachments.h"
#include "contracts/config.h"
#include "embedstore/store.h"
#include "operations/graph_sources.h"
#include "support/env_guard.h"

/// The attachment code graph's pass (27n), over the fixture mini-repos 27k
/// commits: a folder's files built into a chat's index with 27k's build,
/// incremental by content hash; a cancel that syncs nothing and keeps what it
/// parsed, and the forgetting that makes the graph honestly absent; members
/// no attachment owns forgotten before a build; a storage error said, never
/// thrown; and the one line.
namespace {

using apogee::commands::attachment_graph_line;
using apogee::commands::AttachmentGraphJob;
using apogee::commands::AttachmentGraphOutcome;
using apogee::commands::build_attachment_graph;
using apogee::commands::forget_attachment_graph;
using apogee::commands::skipped_kinds;
using apogee::embedstore::Store;

[[nodiscard]] std::filesystem::path fixtures() {
    return std::filesystem::path{APOGEE_TEST_FIXTURES} / "code_graph";
}

struct Scratch {
    apogee::testing::TempDir dir{"attachment-graph-" + std::to_string(std::random_device{}())};

    /// A copy of one language's fixture tree at `dir/<as>`.
    [[nodiscard]] std::filesystem::path tree(const std::string& language,
                                             const std::string& as) const {
        const std::filesystem::path to = dir.path() / as;
        std::filesystem::create_directories(to);
        std::filesystem::copy(fixtures() / language, to,
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::overwrite_existing);
        return to;
    }

    [[nodiscard]] std::filesystem::path store(const std::string& name) const {
        return dir.path() / (name + ".db");
    }
};

/// The job an attach of `root` makes: its walk, labelled by its name.
[[nodiscard]] AttachmentGraphJob job_for(const std::filesystem::path& root,
                                         std::set<std::string> keep = {}) {
    AttachmentGraphJob job;
    job.name = root.filename().string();
    job.label = apogee::commands::source_member_label(root.generic_string());
    job.root = root;
    job.files = apogee::commands::source_files_under(
        apogee::agentloop::find_attachment_files(root.filename().string(), root.parent_path())
            .files,
        root);
    job.keep = std::move(keep);
    return job;
}

/// What `graph build --source` builds over `trees`: 27k's own entry.
[[nodiscard]] std::string direct_dump(const std::filesystem::path& store_path,
                                      const std::vector<std::filesystem::path>& trees) {
    apogee::harness::NamedGraphConfig named;
    for (const std::filesystem::path& tree : trees) {
        named.sources.push_back(tree.generic_string());
    }
    Store store{store_path};
    (void)apogee::commands::build_graph_sources(store, named, {});
    return store.graph_dump();
}

}  // namespace

TEST_CASE("a folder's pass builds what graph build --source builds, and counts its part",
          "[commands][attachments][graph]") {
    const Scratch scratch;
    const std::filesystem::path tree = scratch.tree("python", "app");
    std::ofstream{tree / "NOTES.md"} << "not code\n";
    std::filesystem::create_directories(tree / "third_party");
    std::ofstream{tree / "third_party" / "dep.py"} << "def vendored():\n    pass\n";

    const AttachmentGraphOutcome built =
        build_attachment_graph(scratch.store("chat"), job_for(tree), {});
    REQUIRE(built.state == AttachmentGraphOutcome::State::Built);
    CHECK(built.record.label == "app");
    CHECK(built.record.supported == std::map<std::string, std::int64_t>{{"python", 4}});
    CHECK(built.record.skipped ==
          std::map<std::string, std::int64_t>{{".md", 1}, {"third_party/", 1}});
    CHECK(built.record.absent.empty());
    CHECK(built.parsed == 4);
    CHECK(built.unchanged == 0);

    const Store store{scratch.store("chat")};
    const std::string direct = direct_dump(scratch.store("direct"), {tree});
    CHECK_FALSE(direct.empty());
    CHECK(store.graph_dump() == direct);
    // One folder: its part is the whole graph.
    const apogee::embedstore::GraphStats stats = store.graph_stats();
    CHECK(built.nodes == stats.nodes);
    CHECK(built.edges == stats.edges);
    CHECK(store.code_member_counts("app").files == 4);
    CHECK(store.code_member_counts("elsewhere").nodes == 0);
}

TEST_CASE("re-attached, only the edited file is parsed, and the graph is a fresh build's",
          "[commands][attachments][graph]") {
    const Scratch scratch;
    const std::filesystem::path tree = scratch.tree("python", "app");
    REQUIRE(build_attachment_graph(scratch.store("chat"), job_for(tree), {}).parsed == 4);

    std::ofstream{tree / "pkg" / "service.py", std::ios::app}
        << "\n\ndef audit(user: User) -> str:\n    return user.greet()\n";
    const AttachmentGraphOutcome updated =
        build_attachment_graph(scratch.store("chat"), job_for(tree), {});
    REQUIRE(updated.state == AttachmentGraphOutcome::State::Built);
    CHECK(updated.parsed == 1);
    CHECK(updated.unchanged == 3);
    CHECK(Store{scratch.store("chat")}.graph_dump() == direct_dump(scratch.store("fresh"), {tree}));

    // The same bytes written again: a touch parses nothing.
    const std::string same = [&] {
        std::ifstream in{tree / "main.py", std::ios::binary};
        return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    }();
    std::ofstream{tree / "main.py", std::ios::binary | std::ios::trunc} << same;
    const AttachmentGraphOutcome touched =
        build_attachment_graph(scratch.store("chat"), job_for(tree), {});
    CHECK(touched.parsed == 0);
    CHECK(touched.unchanged == 4);
}

TEST_CASE(
    "cancelled between files: nothing synced, the parsed cached; forgotten, the next completes",
    "[commands][attachments][graph]") {
    const Scratch scratch;
    const std::filesystem::path shapes = scratch.tree("cpp", "shapes");
    const std::filesystem::path app = scratch.tree("python", "app");
    const std::filesystem::path chat = scratch.store("chat");
    REQUIRE(build_attachment_graph(chat, job_for(shapes), {}).state ==
            AttachmentGraphOutcome::State::Built);
    const std::string shapes_only = Store{chat}.graph_dump();

    // Ctrl-C while the second folder is parsed: checked between files.
    const apogee::harness::CancellationToken token = apogee::harness::CancellationToken::create();
    const AttachmentGraphOutcome cancelled = build_attachment_graph(
        chat, job_for(app, {"shapes"}), token, [&](const apogee::graph::SourceProgress& progress) {
            if (progress.index == 2) {
                token.cancel();
            }
        });
    CHECK(cancelled.state == AttachmentGraphOutcome::State::Cancelled);
    CHECK(cancelled.record.absent == "cancelled");
    CHECK(cancelled.record.label.empty());
    CHECK(cancelled.parsed == 2);
    {
        const Store store{chat};
        // Nothing synced: the graph is the first folder's, as it was.
        CHECK(store.graph_dump() == shapes_only);
        // What was parsed is cached.
        CHECK(store.code_file_states("app").size() == 2);
    }

    // Forgotten -- the absence honest -- the other folder re-linked as it was.
    forget_attachment_graph(chat, "app", {"shapes"});
    {
        const Store store{chat};
        CHECK(store.code_members() == std::vector<std::string>{"shapes"});
        CHECK(store.graph_dump() == shapes_only);
    }

    // The next attach completes it: both folders, as one build of both.
    const AttachmentGraphOutcome again = build_attachment_graph(chat, job_for(app, {"shapes"}), {});
    CHECK(again.state == AttachmentGraphOutcome::State::Built);
    CHECK(again.parsed == 4);
    CHECK(Store{chat}.graph_dump() == direct_dump(scratch.store("both"), {shapes, app}));
    // Each folder counts its own part.
    CHECK(again.nodes == Store{chat}.code_member_counts("app").nodes);
    CHECK(again.nodes < Store{chat}.graph_stats().nodes);
    CHECK(again.edges < Store{chat}.graph_stats().edges);
    CHECK(again.edges + Store{chat}.code_member_counts("shapes").edges ==
          Store{chat}.graph_stats().edges);

    // Forgetting what is not there does nothing.
    const std::string both = Store{chat}.graph_dump();
    forget_attachment_graph(chat, "nothing", {"shapes", "app"});
    CHECK(Store{chat}.graph_dump() == both);
}

TEST_CASE("a member no attachment owns is forgotten before a pass, the rest re-linked",
          "[commands][attachments][graph]") {
    const Scratch scratch;
    const std::filesystem::path ghost = scratch.tree("go", "ghost");
    const std::filesystem::path app = scratch.tree("python", "app");
    const std::filesystem::path chat = scratch.store("chat");
    // A folder detached while its forget failed, or a build a crash cut short.
    REQUIRE(build_attachment_graph(chat, job_for(ghost), {}).state ==
            AttachmentGraphOutcome::State::Built);
    REQUIRE(build_attachment_graph(chat, job_for(app), {}).state ==
            AttachmentGraphOutcome::State::Built);
    const Store store{chat};
    CHECK(store.code_members() == std::vector<std::string>{"app"});
    CHECK(store.graph_dump() == direct_dump(scratch.store("direct"), {app}));
}

TEST_CASE("a storage error is a failed pass, said, never a throw",
          "[commands][attachments][graph]") {
    const Scratch scratch;
    const std::filesystem::path app = scratch.tree("python", "app");
    std::ofstream{scratch.dir.path() / "plain"} << "a file, not a folder\n";
    const AttachmentGraphOutcome failed =
        build_attachment_graph(scratch.dir.path() / "plain" / "index.db", job_for(app), {});
    CHECK(failed.state == AttachmentGraphOutcome::State::Failed);
    CHECK_FALSE(failed.record.absent.empty());
    CHECK(failed.record.label.empty());
}

TEST_CASE("what a build left out, by kind, and the one line", "[commands][attachments][graph]") {
    apogee::graph::SourceBuildResult result;
    result.skipped = {
        {.member = "app", .file = "README.md", .reason = "unsupported language (.md)"},
        {.member = "app", .file = "docs/a.md", .reason = "unsupported language (.md)"},
        {.member = "app", .file = "Makefile", .reason = "unsupported language (no extension)"},
        {.member = "app", .file = "blob.c", .reason = "binary"},
        {.member = "app", .file = "gen.cpp", .reason = "too large to parse"},
        {.member = "app", .file = "gone.py", .reason = "unreadable: cannot open"},
        {.member = "app", .file = "bad.py", .reason = "did not parse (timeout)"},
    };
    result.excluded = {{"third_party/", 120}, {"web/node_modules/", 3}};
    CHECK(skipped_kinds(result) == std::map<std::string, std::int64_t>{{".md", 2},
                                                                       {"binary", 1},
                                                                       {"no extension", 1},
                                                                       {"third_party/", 120},
                                                                       {"too large", 1},
                                                                       {"unparseable", 1},
                                                                       {"unreadable", 1},
                                                                       {"web/node_modules/", 3}});

    apogee::logger::AttachmentGraph graph;
    graph.label = "source";
    graph.supported = {{"python", 2}, {"cpp", 465}};
    graph.skipped = {{".md", 2}};
    CHECK(attachment_graph_line(graph, 9840, 63742) ==
          "graph: 9840 nodes, 63742 edges (supported: cpp 465, python 2; skipped: .md 2)");
    graph.supported = {{"python", 1}};
    graph.skipped = {};
    CHECK(attachment_graph_line(graph, 1, 1) ==
          "graph: 1 node, 1 edge (supported: python 1; skipped: none)");
    // Past eight kinds, the rest are counted; ties keep their name order.
    graph.skipped = skipped_kinds(result);
    graph.skipped["z"] = 1;
    CHECK(attachment_graph_line(graph, 0, 0) ==
          "graph: 0 nodes, 0 edges (supported: python 1; skipped: third_party/ 120, "
          "web/node_modules/ 3, .md 2, binary 1, no extension 1, too large 1, unparseable 1, "
          "unreadable 1, and 1 more)");
    // Absent, and said.
    const apogee::logger::AttachmentGraph cancelled{
        .label = {}, .supported = {}, .skipped = {}, .absent = "cancelled"};
    CHECK(attachment_graph_line(cancelled, 0, 0) ==
          "graph: not built -- cancelled; attach it again to build it");
}
