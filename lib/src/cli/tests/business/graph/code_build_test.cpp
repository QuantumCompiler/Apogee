#include "graph/code_build.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <map>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/cancellation.h"
#include "embedstore/store.h"
#include "graph/code_languages.h"
#include "support/env_guard.h"

/// The code build over an injected file list (27k): incremental by content
/// hash -- an edit re-parses exactly that file and the graph converges on a
/// fresh build byte for byte -- every file it cannot use skipped by name,
/// vendored directories left out whole, the membership converged, a damaged
/// cache a re-parse, cancellation between files, and a dry run that stores
/// nothing. No model anywhere: the build takes no closure that could reach one.
namespace {

using apogee::embedstore::Store;
using apogee::graph::SourceBuildOptions;
using apogee::graph::SourceBuildResult;
using apogee::graph::SourceMember;

/// A source tree in memory: what the build reads, and how often.
struct Tree {
    std::map<std::string, std::string> files;
    std::map<std::string, int> reads;
    /// Paths whose read fails.
    std::vector<std::string> unreadable;

    [[nodiscard]] SourceMember member(std::string label) {
        SourceMember out;
        out.label = std::move(label);
        for (const auto& [path, unused] : files) {
            out.files.push_back(path);
        }
        for (const std::string& path : unreadable) {
            out.files.push_back(path);
        }
        out.read = [this](std::string_view path, std::string& error) -> std::optional<std::string> {
            ++reads[std::string{path}];
            if (std::ranges::find(unreadable, std::string{path}) != unreadable.end()) {
                error = "permission denied";
                return std::nullopt;
            }
            const auto it = files.find(std::string{path});
            if (it == files.end()) {
                error = "no such file";
                return std::nullopt;
            }
            return it->second;
        };
        return out;
    }
};

/// Two Python modules: `app` calls `lib.helper` and `lib.other`.
[[nodiscard]] Tree python_tree() {
    Tree tree;
    tree.files["pkg/lib.py"] = "def helper():\n    return 1\n\n\ndef other():\n    return 2\n";
    tree.files["pkg/app.py"] =
        "from .lib import helper, other\n\n\ndef run():\n    return helper() + other()\n";
    tree.files["pkg/util.py"] = "def unused():\n    return 0\n";
    return tree;
}

struct Scratch {
    apogee::testing::TempDir dir{"code-build-" + std::to_string(std::random_device{}())};
    Store store{dir.path() / "g.db"};
};

[[nodiscard]] SourceBuildResult build(Store& store, Tree& tree, SourceBuildOptions options = {}) {
    return apogee::graph::build_source(store, {tree.member("src")}, options);
}

/// The graph a fresh store holds after one full build of `tree`.
[[nodiscard]] std::string fresh_dump(Tree tree) {
    Scratch scratch;
    (void)build(scratch.store, tree);
    return scratch.store.graph_dump();
}

[[nodiscard]] bool has_reason(const SourceBuildResult& result, std::string_view file,
                              std::string_view reason) {
    return std::ranges::any_of(result.skipped, [&](const apogee::graph::SkippedFile& skipped) {
        return skipped.file == file && skipped.reason.find(reason) != std::string::npos;
    });
}

}  // namespace

TEST_CASE("a fresh build parses every supported file; an unchanged tree parses none",
          "[graph][code][build]") {
    Scratch scratch;
    Tree tree = python_tree();
    const SourceBuildResult first = build(scratch.store, tree);
    CHECK(first.files_offered == 3);
    CHECK(first.files_used == 3);
    CHECK(first.files_parsed == 3);
    CHECK(first.files_unchanged == 0);
    CHECK(first.files_by_language.at("python") == 3);
    CHECK(first.parsed ==
          std::vector<std::string>{"src/pkg/app.py", "src/pkg/lib.py", "src/pkg/util.py"});
    CHECK(first.counts.calls == 2);
    CHECK(first.counts.calls_resolved == 2);
    CHECK(first.sync.nodes_added == first.nodes);
    const std::string built = scratch.store.graph_dump();

    const SourceBuildResult again = build(scratch.store, tree);
    CHECK(again.files_parsed == 0);
    CHECK(again.files_unchanged == 3);
    CHECK(again.parsed.empty());
    CHECK(again.sync.nodes_added == 0);
    CHECK(again.sync.nodes_removed == 0);
    CHECK(again.sync.edges_added == 0);
    CHECK(scratch.store.graph_dump() == built);
}

TEST_CASE("editing one file re-parses exactly it, and the update converges on a fresh build",
          "[graph][code][build][update]") {
    Scratch scratch;
    Tree tree = python_tree();
    (void)build(scratch.store, tree);
    // `other` is renamed in lib.py: app.py's call to it no longer resolves,
    // though app.py itself did not change -- linking is global, parsing is
    // per file.
    tree.files["pkg/lib.py"] = "def helper():\n    return 1\n\n\ndef renamed():\n    return 2\n";
    tree.reads.clear();
    const SourceBuildResult update = build(scratch.store, tree);
    CHECK(update.files_parsed == 1);
    CHECK(update.parsed == std::vector<std::string>{"src/pkg/lib.py"});
    CHECK(update.files_unchanged == 2);
    CHECK(update.counts.calls_resolved == 1);
    CHECK(update.sync.nodes_added == 2);    // `renamed`, and `other` as an unresolved name
    CHECK(update.sync.nodes_removed == 1);  // the function `other`
    CHECK(scratch.store.graph_dump() == fresh_dump(tree));

    // Rewritten with the same bytes -- a `touch` -- nothing parses.
    const SourceBuildResult touched = build(scratch.store, tree);
    CHECK(touched.files_parsed == 0);
    CHECK(scratch.store.graph_dump() == fresh_dump(tree));
}

TEST_CASE("a file gone from the tree is forgotten, with every node only it stated",
          "[graph][code][build][update]") {
    Scratch scratch;
    Tree tree = python_tree();
    (void)build(scratch.store, tree);
    REQUIRE_FALSE(scratch.store.find_nodes("pkg.util.unused").empty());
    tree.files.erase("pkg/util.py");
    const SourceBuildResult update = build(scratch.store, tree);
    CHECK(update.files_removed == 1);
    CHECK(update.files_parsed == 0);
    CHECK(scratch.store.code_file_states("src").size() == 2);
    CHECK(scratch.store.find_nodes("pkg.util.unused").empty());
    CHECK(scratch.store.find_nodes("pkg/util.py").empty());
    CHECK(scratch.store.graph_dump() == fresh_dump(tree));
}

TEST_CASE("what cannot be used is skipped and named, and never fails the build",
          "[graph][code][build][skipped]") {
    Scratch scratch;
    Tree tree = python_tree();
    tree.files["notes.txt"] = "not code";
    tree.files["script.lua"] = "print('lua has no vendored grammar')";
    tree.files["Makefile"] = "all:\n\techo\n";
    tree.files["blob.py"] = std::string{"x = 1\n\0\0\0", 9};
    tree.files["huge.py"] = std::string(apogee::graph::kMaxSourceFileBytes + 1, '#');
    tree.unreadable.push_back("locked.py");
    const SourceBuildResult result = build(scratch.store, tree);
    CHECK(result.files_used == 3);
    CHECK(result.skipped.size() == 6);
    CHECK(has_reason(result, "notes.txt", "unsupported language (.txt)"));
    CHECK(has_reason(result, "script.lua", "unsupported language (.lua)"));
    CHECK(has_reason(result, "Makefile", "unsupported language (no extension)"));
    CHECK(has_reason(result, "blob.py", "binary"));
    CHECK(has_reason(result, "huge.py", "too large to parse"));
    CHECK(has_reason(result, "locked.py", "unreadable: permission denied"));
    // Sorted by (member, file): the report reads the same every run.
    CHECK(std::ranges::is_sorted(result.skipped,
                                 [](const auto& a, const auto& b) { return a.file < b.file; }));
    // An unsupported file is never read at all.
    CHECK_FALSE(tree.reads.contains("notes.txt"));
    CHECK(scratch.store.code_file_states("src").size() == 3);
}

TEST_CASE("the --lang filter leaves other languages out by name", "[graph][code][build][skipped]") {
    Scratch scratch;
    Tree tree = python_tree();
    tree.files["web/app.js"] = "export function start() { return 1; }\n";
    SourceBuildOptions options;
    options.languages = {"javascript"};
    const SourceBuildResult result = build(scratch.store, tree, options);
    CHECK(result.files_used == 1);
    CHECK(result.files_by_language.at("javascript") == 1);
    CHECK(has_reason(result, "pkg/app.py", "python left out by --lang"));
    CHECK(result.skipped.size() == 3);
    // By alias, as a hand-edited `languages:` may name it.
    options.languages = {"js"};
    CHECK(build(scratch.store, tree, options).files_used == 1);
}

TEST_CASE("a file parsed around a syntax error is used, and named partial",
          "[graph][code][build][partial]") {
    Scratch scratch;
    Tree tree = python_tree();
    tree.files["pkg/broken.py"] = "def fine():\n    return 1\n\n\ndef broken():\n    return )))\n";
    const SourceBuildResult result = build(scratch.store, tree);
    CHECK(result.files_used == 4);
    CHECK(result.partial == std::vector<std::string>{"src/pkg/broken.py"});
    CHECK_FALSE(scratch.store.find_nodes("pkg.broken.fine").empty());
}

TEST_CASE("vendored and build directories are left out whole, counted per directory",
          "[graph][code][build][vendored]") {
    using apogee::graph::vendored_directory;
    CHECK(vendored_directory("third_party/llama.cpp/ggml.c") == "third_party");
    CHECK(vendored_directory("lib/vendor/x.go") == "lib/vendor");
    CHECK(vendored_directory("web/node_modules/react/index.js") == "web/node_modules");
    CHECK(vendored_directory("build/_deps/catch2-src/x.cpp") == "build/_deps");
    CHECK(vendored_directory("cmake-build-debug/main.cpp") == "cmake-build-debug");
    CHECK(vendored_directory("src/third_party.py").empty());  // a file, not a directory
    CHECK(vendored_directory("src/vendors/x.py").empty());
    CHECK(vendored_directory("main.cpp").empty());

    Scratch scratch;
    Tree tree = python_tree();
    tree.files["third_party/dep/a.py"] = "def a(): pass\n";
    tree.files["third_party/dep/b.py"] = "def b(): pass\n";
    tree.files["web/node_modules/x/index.js"] = "export const x = 1;\n";
    const SourceBuildResult result = build(scratch.store, tree);
    CHECK(result.files_offered == 6);
    CHECK(result.files_used == 3);
    CHECK(result.excluded.at("third_party/") == 2);
    CHECK(result.excluded.at("web/node_modules/") == 1);
    CHECK(result.skipped.empty());
    CHECK_FALSE(tree.reads.contains("third_party/dep/a.py"));
}

TEST_CASE("a changed extractor, a damaged cache, or --force re-parses", "[graph][code][build]") {
    Scratch scratch;
    Tree tree = python_tree();
    (void)build(scratch.store, tree);
    std::map<std::string, apogee::embedstore::CodeFileState> states =
        scratch.store.code_file_states("src");

    // Facts from another extractor (a grammar pin moved) are not mixed in.
    apogee::embedstore::CodeFileState moved = states.at("pkg/lib.py");
    moved.extractor = "python:tree-sitter-python v0.0.1:extractor-0";
    scratch.store.set_code_file_state(moved);
    // A cache that no longer reads is a re-parse, never a failure.
    apogee::embedstore::CodeFileState damaged = states.at("pkg/app.py");
    damaged.facts = "{not json";
    scratch.store.set_code_file_state(damaged);
    const SourceBuildResult healed = build(scratch.store, tree);
    CHECK(healed.files_parsed == 2);
    CHECK(healed.files_unchanged == 1);
    CHECK(scratch.store.graph_dump() == fresh_dump(tree));

    SourceBuildOptions force;
    force.force = true;
    CHECK(build(scratch.store, tree, force).files_parsed == 3);
}

TEST_CASE("a dry run parses and links, and stores nothing", "[graph][code][build][dry-run]") {
    Scratch scratch;
    Tree tree = python_tree();
    SourceBuildOptions options;
    options.dry_run = true;
    const SourceBuildResult result = build(scratch.store, tree, options);
    CHECK(result.dry_run);
    CHECK(result.files_parsed == 3);
    CHECK(result.nodes > 0);
    CHECK(result.edges > 0);
    CHECK(scratch.store.graph_stats().nodes == 0);
    CHECK(scratch.store.code_file_states("src").empty());
}

TEST_CASE("cancellation stops between files; what was parsed is cached for the next build",
          "[graph][code][build][cancel]") {
    Scratch scratch;
    Tree tree = python_tree();
    const apogee::harness::CancellationToken token = apogee::harness::CancellationToken::create();
    SourceBuildOptions options;
    options.cancellation = token;
    int seen = 0;
    options.on_progress = [&](const apogee::graph::SourceProgress& progress) {
        CHECK(progress.count == 3);
        if (++seen == 2) {
            token.cancel();
        }
    };
    const SourceBuildResult cancelled = build(scratch.store, tree, options);
    CHECK(cancelled.cancelled);
    CHECK(cancelled.files_parsed == 2);
    CHECK(scratch.store.code_file_states("src").size() == 2);
    CHECK(scratch.store.graph_stats().nodes == 0);  // nothing linked or synced

    const SourceBuildResult resumed = build(scratch.store, tree);
    CHECK(resumed.files_parsed == 1);
    CHECK(resumed.files_unchanged == 2);
    CHECK(scratch.store.graph_dump() == fresh_dump(python_tree()));
}

TEST_CASE("members: two trees into one graph, and the membership converges",
          "[graph][code][build][members]") {
    Scratch scratch;
    Tree server = python_tree();
    Tree client;
    client.files["web/app.js"] = "export function start() { return 1; }\n";
    const SourceBuildResult both = apogee::graph::build_source(
        scratch.store, {server.member("server"), client.member("client")}, {});
    CHECK(both.files_used == 4);
    CHECK(scratch.store.code_members() == std::vector<std::string>{"client", "server"});
    CHECK(scratch.store.graph_dump().find("at client:web/app.js:1-1 definition") !=
          std::string::npos);

    // A build that does not own the membership links the other from its cache.
    SourceBuildOptions keep;
    keep.prune_other_members = false;
    const SourceBuildResult partial =
        apogee::graph::build_source(scratch.store, {server.member("server")}, keep);
    CHECK(partial.files_parsed == 0);
    CHECK(scratch.store.code_members().size() == 2);
    CHECK_FALSE(scratch.store.find_nodes("web/app.js::start").empty());

    // One that does forgets the member it was not handed.
    const SourceBuildResult owned =
        apogee::graph::build_source(scratch.store, {server.member("server")}, {});
    CHECK(owned.files_removed == 1);
    CHECK(scratch.store.code_members() == std::vector<std::string>{"server"});
    CHECK(scratch.store.find_nodes("web/app.js::start").empty());

    CHECK_THROWS_AS(apogee::graph::build_source(scratch.store, {server.member("")}, {}),
                    std::invalid_argument);
    CHECK_THROWS_AS(
        apogee::graph::build_source(scratch.store, {server.member("x"), client.member("x")}, {}),
        std::invalid_argument);
}

TEST_CASE("the same files, built or only linked, make the same graph", "[graph][code][build]") {
    Tree tree = python_tree();
    const apogee::graph::CodeGraph linked =
        apogee::graph::parse_and_resolve({tree.member("src")}, {});
    Scratch scratch;
    const SourceBuildResult built = build(scratch.store, tree);
    CHECK(static_cast<int>(linked.nodes.size()) == built.nodes);
    CHECK(static_cast<int>(linked.edges.size()) == built.edges);
    CHECK(linked.counts.resolved() == built.counts.resolved());
}
