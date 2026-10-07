#include "operations/graph_sources.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "agentloop/attachments.h"
#include "support/env_guard.h"

/// The bridge from a walk to a source build (27k, 27n): the files a walk
/// found as a build takes them, the one disk reader, and the test a chat's
/// folder attachment takes before its code graph is built -- whether
/// anything in it is a language a vendored grammar parses, outside a vendored
/// directory.
namespace {

using apogee::commands::offers_code;
using apogee::commands::source_files_under;
using apogee::commands::source_member;

}  // namespace

TEST_CASE("a folder offers code when a vendored grammar claims a file outside a vendored directory",
          "[operations][graph][attachments]") {
    struct Row {
        std::vector<std::string> files;
        bool code;
    };

    const std::vector<Row> rows{
        {.files = {}, .code = false},
        {.files = {"README.md", "notes.txt", "data.json", "Makefile", "build.cmake"},
         .code = false},
        {.files = {"main.py"}, .code = true},
        {.files = {"docs/guide.md", "src/lib.rs"}, .code = true},
        // A header alone is code: C, or C++ when the tree holds C++.
        {.files = {"include/api.h"}, .code = true},
        // An extension is read whatever its case.
        {.files = {"tools/RUN.SH"}, .code = true},
        {.files = {"script.lua", "style.css", "page.html"}, .code = false},
        // Vendored or built, wherever it sits: someone else's code.
        {.files = {"third_party/zlib/inflate.c", "README.md"}, .code = false},
        {.files = {"web/node_modules/left-pad/index.js"}, .code = false},
        {.files = {"vendor/github.com/x/y.go", "cmake-build-debug/gen.cpp"}, .code = false},
        {.files = {"third_party/dep.py", "app/main.go"}, .code = true},
        // A name that only looks vendored is the tree's own.
        {.files = {"vendored/tool.py"}, .code = true},
        {.files = {"every/language/Program.cs"}, .code = true},
        {.files = {"web/App.tsx", "web/index.ts", "web/legacy.js"}, .code = true},
        {.files = {"pkg/Main.java", "lib/thing.rb"}, .code = true},
    };
    for (const Row& row : rows) {
        INFO(row.files.size() << " files, first: " << (row.files.empty() ? "" : row.files[0]));
        CHECK(offers_code(row.files) == row.code);
    }
}

TEST_CASE("a walk's files become a build's: relative to the folder, a file outside it left out",
          "[operations][graph][attachments]") {
    const std::filesystem::path root{"/work/repo/src"};
    const std::vector<apogee::agentloop::FoundFile> found{
        {.path = "/work/repo/src/main.py", .name = "src/main.py", .bytes = 10},
        {.path = "/work/repo/src/pkg/util.py", .name = "src/pkg/util.py", .bytes = 10},
        {.path = "/work/repo/other/x.py", .name = "other/x.py", .bytes = 10},
        {.path = "/work/repo/src/./pkg/../doc.md", .name = "src/doc.md", .bytes = 10},
    };
    const std::vector<std::string> expected{"main.py", "pkg/util.py", "doc.md"};
    CHECK(source_files_under(found, root) == expected);
    // `/work/repo/src/` is the same folder.
    CHECK(source_files_under(found, "/work/repo/src/") == expected);
    // A folder's walk, as the attach and `graph build --source` both make it.
    const apogee::testing::TempDir dir{"graph-sources-" + std::to_string(std::random_device{}())};
    const std::filesystem::path tree = dir.path() / "tree";
    std::filesystem::create_directories(tree / "pkg");
    std::ofstream{tree / "a.py"} << "x = 1\n";
    std::ofstream{tree / "pkg" / "b.py"} << "y = 2\n";
    const apogee::agentloop::FoundFiles walked =
        apogee::agentloop::find_attachment_files("tree", dir.path());
    CHECK(source_files_under(walked.files, tree) == std::vector<std::string>{"a.py", "pkg/b.py"});
    CHECK(source_files_under(apogee::agentloop::find_attachment_files(".", tree).files, tree) ==
          std::vector<std::string>{"a.py", "pkg/b.py"});
}

TEST_CASE("a source member reads its files from its root, byte for byte, or says why not",
          "[operations][graph][attachments]") {
    const apogee::testing::TempDir dir{"graph-member-" + std::to_string(std::random_device{}())};
    std::filesystem::create_directories(dir.path() / "pkg");
    const std::string bytes = std::string{"def f():\r\n    return '\xc3\xa9'\n"} + '\0' + "tail";
    std::ofstream{dir.path() / "pkg" / "a.py", std::ios::binary} << bytes;
    const apogee::graph::SourceMember member =
        source_member("tree", dir.path(), {"pkg/a.py", "gone.py"});
    CHECK(member.label == "tree");
    CHECK(member.files == std::vector<std::string>{"pkg/a.py", "gone.py"});
    std::string error;
    CHECK(member.read("pkg/a.py", error) == std::optional<std::string>{bytes});
    CHECK_FALSE(member.read("gone.py", error).has_value());
    CHECK(error == "cannot open");
}
