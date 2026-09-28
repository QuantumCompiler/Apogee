#include "tools/fs.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>

#include "agent/tool.h"
#include "support/env_guard.h"

/// The filesystem toolset: the sandbox above all, then each tool's contract.
namespace {

using apogee::agent::ToolOutcome;
using apogee::agent::ToolRegistry;
using apogee::tools::resolve_in_root;
using apogee::tools::wildcard_match;

struct Sandbox {
    apogee::testing::TempDir temp{"tools-fs-" + std::to_string(std::random_device{}())};
    std::filesystem::path root = temp.path() / "root";
    std::filesystem::path outside = temp.path() / "rootX";
    ToolRegistry registry;

    Sandbox() {
        std::filesystem::create_directories(root / "sub");
        std::filesystem::create_directories(root / ".hidden");
        std::filesystem::create_directories(outside);
        std::ofstream{root / "a.md"} << "alpha";
        std::ofstream{root / "sub" / "b.md"} << "beta";
        std::ofstream{root / ".hidden" / "c.md"} << "gamma";
        std::ofstream{outside / "secret.md"} << "not yours";
        apogee::tools::register_fs_tools(registry, root, /*read_limit=*/16);
    }

    [[nodiscard]] ToolOutcome run(std::string_view tool, const std::string& arguments) const {
        return registry.find(tool)->run(arguments);
    }
};

}  // namespace

TEST_CASE("the sandbox refuses every spelling of a path outside the root", "[tools][fs][sandbox]") {
    const Sandbox box;
    std::string error;
    // Inside, three ways.
    CHECK(resolve_in_root(box.root, "a.md", error).has_value());
    CHECK(resolve_in_root(box.root, (box.root / "sub" / "b.md").string(), error).has_value());
    CHECK(resolve_in_root(box.root, "sub/../a.md", error).has_value());

    // Outside: dot-dot, an absolute path, and the prefix trick -- `rootX`
    // shares every character of `root` and is not inside it.
    CHECK_FALSE(resolve_in_root(box.root, "../rootX/secret.md", error).has_value());
    CHECK(error.find("outside the allowed root") != std::string::npos);
    CHECK_FALSE(resolve_in_root(box.root, (box.outside / "secret.md").string(), error).has_value());
    CHECK_FALSE(resolve_in_root(box.root, box.outside.string(), error).has_value());

    // A symlink inside that points outside is judged by where it points.
    std::error_code code;
    std::filesystem::create_symlink(box.outside / "secret.md", box.root / "link.md", code);
    if (!code) {
        CHECK_FALSE(resolve_in_root(box.root, "link.md", error).has_value());
        CHECK(box.run("read_file", R"({"path":"link.md"})").is_error);
        CHECK(box.run("edit_file", R"({"path":"link.md","old_string":"not","new_string":"now"})")
                  .is_error);
        CHECK(box.run("grep_files", R"({"pattern":"yours","path":"link.md"})").is_error);
        // Found by a search of the root, it is still judged where it points:
        // the secret's text never comes back.
        const ToolOutcome searched = box.run("grep_files", R"({"pattern":"yours"})");
        CHECK(searched.content.find("not yours") == std::string::npos);
        CHECK(searched.content.starts_with("No matches"));
    }
    // A directory link out of the root is not walked either.
    std::filesystem::create_directory_symlink(box.outside, box.root / "linked", code);
    if (!code) {
        CHECK(box.run("grep_files", R"({"pattern":"yours"})").content.starts_with("No matches"));
        CHECK(box.run("grep_files", R"({"pattern":"yours","path":"linked"})").is_error);
    }

    // The tools themselves refuse, naming the root, and touch nothing.
    for (const char* tool : {"read_file", "delete_file", "list_directory"}) {
        INFO(tool);
        const ToolOutcome outcome = box.run(tool, R"({"path":"../rootX/secret.md"})");
        CHECK(outcome.is_error);
        CHECK(outcome.content.find("outside the allowed root") != std::string::npos);
    }
    CHECK(box.run("write_file", R"({"path":"../rootX/new.md","content":"x"})").is_error);
    CHECK_FALSE(std::filesystem::exists(box.outside / "new.md"));
    CHECK(std::filesystem::exists(box.outside / "secret.md"));
    CHECK(box.run("search_files", R"({"pattern":"*.md","root":"../rootX"})").is_error);

    // The two tools 25d added, every spelling: dot-dot, absolute, the prefix.
    for (const std::string& path :
         {std::string{"../rootX/secret.md"}, (box.outside / "secret.md").generic_string(),
          box.outside.generic_string()}) {
        INFO(path);
        const ToolOutcome edited =
            box.run("edit_file",
                    R"({"path":")" + path + R"(","old_string":"not yours","new_string":"mine"})");
        CHECK(edited.is_error);
        CHECK(edited.content.find("outside the allowed root") != std::string::npos);
        const ToolOutcome grepped =
            box.run("grep_files", R"({"pattern":"yours","path":")" + path + R"("})");
        CHECK(grepped.is_error);
        CHECK(grepped.content.find("outside the allowed root") != std::string::npos);
    }
    std::ifstream secret{box.outside / "secret.md"};
    CHECK(std::string{std::istreambuf_iterator<char>{secret}, {}} == "not yours");
}

TEST_CASE("read_file returns the text, and a file past the cap as its size instead",
          "[tools][fs]") {
    const Sandbox box;
    CHECK(box.run("read_file", R"({"path":"a.md"})").content == "alpha");
    std::ofstream{box.root / "cap.txt"} << std::string(16, 'c');
    CHECK(box.run("read_file", R"({"path":"cap.txt"})").content == std::string(16, 'c'));
    // Past the cap, none of it: its size and length, and how to read it.
    std::ofstream{box.root / "big.txt"} << "xxxxxxxxxx\nxxxxxxxxxx\nxxxxxxxxxx";
    const ToolOutcome capped = box.run("read_file", R"({"path":"big.txt"})");
    CHECK(capped.is_error);
    CHECK(capped.content.find("is 32 bytes, 3 lines") != std::string::npos);
    CHECK(capped.content.find("xxx") == std::string::npos);
    CHECK(capped.content.find("offset and limit") != std::string::npos);
    CHECK(capped.content.find("grep_files") != std::string::npos);
    CHECK(box.run("read_file", R"({"path":"missing.md"})").is_error);
    CHECK(box.run("read_file", R"({"path":"sub"})").is_error);
    CHECK(box.run("read_file", "not json").is_error);
    CHECK(box.run("read_file", R"({"path":null})").is_error);
}

TEST_CASE("write_file and delete_file are gated, create parents, and refuse directories",
          "[tools][fs][permission]") {
    const Sandbox box;
    const apogee::agent::Tool* write = box.registry.find("write_file");
    const apogee::agent::Tool* remove = box.registry.find("delete_file");
    REQUIRE(write != nullptr);
    REQUIRE(remove != nullptr);
    CHECK(write->writes);
    CHECK(remove->writes);
    CHECK_FALSE(box.registry.find("read_file")->writes);
    CHECK(write->describe_target(R"({"path":"new/x.txt"})") == "new/x.txt");

    const ToolOutcome wrote = box.run("write_file", R"({"path":"new/deep/x.txt","content":"hi"})");
    CHECK_FALSE(wrote.is_error);
    CHECK(wrote.content.find("Wrote 2 bytes") != std::string::npos);
    CHECK(box.run("read_file", R"({"path":"new/deep/x.txt"})").content == "hi");
    CHECK(box.run("write_file", R"({"path":"new/y.txt"})").is_error);  // no content

    CHECK(box.run("delete_file", R"({"path":"sub"})").is_error);  // a directory
    CHECK(std::filesystem::exists(box.root / "sub"));
    CHECK_FALSE(box.run("delete_file", R"({"path":"new/deep/x.txt"})").is_error);
    CHECK_FALSE(std::filesystem::exists(box.root / "new" / "deep" / "x.txt"));
    CHECK(box.run("delete_file", R"({"path":"new/deep/x.txt"})").is_error);  // gone
}

TEST_CASE("list_directory puts directories first and search_files skips hidden ones",
          "[tools][fs]") {
    const Sandbox box;
    const ToolOutcome listed = box.run("list_directory", R"({"path":"."})");
    REQUIRE_FALSE(listed.is_error);
    CHECK(listed.content.find("DIR   .hidden") < listed.content.find("DIR   sub"));
    CHECK(listed.content.find("DIR   sub") < listed.content.find("FILE  a.md"));
    CHECK(listed.content.find("FILE  a.md  5 B") != std::string::npos);
    CHECK(box.run("list_directory", R"({"path":"a.md"})").is_error);
    std::filesystem::create_directories(box.root / "empty");
    CHECK(box.run("list_directory", R"({"path":"empty"})").content.starts_with("(empty directory"));

    const ToolOutcome found = box.run("search_files", R"({"pattern":"*.md"})");
    REQUIRE_FALSE(found.is_error);
    CHECK(found.content == "a.md\nsub/b.md");  // .hidden/c.md skipped, sorted
    CHECK(box.run("search_files", R"({"pattern":"*.md","root":"sub"})").content == "b.md");
    CHECK(
        box.run("search_files", R"({"pattern":"*.zzz"})").content.starts_with("No files matching"));
    CHECK(box.run("search_files", R"({})").is_error);
}

TEST_CASE("wildcard_match covers the shell's file patterns", "[tools][fs]") {
    CHECK(wildcard_match("*.md", "notes.md"));
    CHECK_FALSE(wildcard_match("*.md", "notes.mdx"));
    CHECK(wildcard_match("a?c", "abc"));
    CHECK_FALSE(wildcard_match("a?c", "ac"));
    CHECK(wildcard_match("[ab]*", "bee"));
    CHECK_FALSE(wildcard_match("[ab]*", "cee"));
    CHECK(wildcard_match("[!ab]*", "cee"));
    CHECK(wildcard_match("*", ""));
    CHECK(wildcard_match("x*y*z", "xaybz"));
    CHECK_FALSE(wildcard_match("x*y*z", "xazby"));
}

namespace {

/// A sandbox with a read cap large enough for line ranges, and helpers to
/// write and read files byte for byte.
struct Files {
    apogee::testing::TempDir temp{"tools-fs-r-" + std::to_string(std::random_device{}())};
    std::filesystem::path root = temp.path();
    ToolRegistry registry;

    explicit Files(std::size_t read_limit = apogee::tools::kDefaultReadLimit) {
        apogee::tools::register_fs_tools(registry, root, read_limit);
    }

    void write(const std::string& name, const std::string& bytes) const {
        std::filesystem::create_directories((root / name).parent_path());
        std::ofstream{root / name, std::ios::binary} << bytes;
    }

    [[nodiscard]] std::string read(const std::string& name) const {
        std::ifstream in{root / name, std::ios::binary};
        return std::string{std::istreambuf_iterator<char>{in}, {}};
    }

    [[nodiscard]] ToolOutcome run(std::string_view tool, const std::string& arguments) const {
        return registry.find(tool)->run(arguments);
    }
};

std::string numbered_lines(int count) {
    std::string text;
    for (int i = 1; i <= count; ++i) {
        text += "row " + std::to_string(i) + "\n";
    }
    return text;
}

}  // namespace

TEST_CASE("read_file reads a range of lines, numbered, exact at the file's edges",
          "[tools][fs][range]") {
    const Files files;
    files.write("ten.txt", numbered_lines(10));

    SECTION("a range in the middle says where to read on") {
        const ToolOutcome read =
            files.run("read_file", R"({"path":"ten.txt","offset":3,"limit":2})");
        REQUIRE_FALSE(read.is_error);
        CHECK(read.content ==
              "     3\trow 3\n     4\trow 4\n[lines 3-4 of 10. Read on with offset 5]");
    }
    SECTION("the first line and the last are reachable exactly") {
        CHECK(files.run("read_file", R"({"path":"ten.txt","offset":1,"limit":1})").content ==
              "     1\trow 1\n[lines 1-1 of 10. Read on with offset 2]");
        CHECK(files.run("read_file", R"({"path":"ten.txt","offset":10})").content ==
              "    10\trow 10\n[lines 10-10 of 10]");
        // A limit past the end stops at the end, and says nothing more is left.
        CHECK(files.run("read_file", R"({"path":"ten.txt","offset":9,"limit":50})").content ==
              "     9\trow 9\n    10\trow 10\n[lines 9-10 of 10]");
        // A limit alone starts at the top.
        CHECK(files.run("read_file", R"({"path":"ten.txt","limit":1})")
                  .content.starts_with("     1\trow 1\n[lines 1-1 of 10."));
    }
    SECTION("past the end, and nonsense, are errors a model can act on") {
        const ToolOutcome past = files.run("read_file", R"({"path":"ten.txt","offset":11})");
        CHECK(past.is_error);
        CHECK(past.content.find("which has 10 lines") != std::string::npos);
        CHECK(files.run("read_file", R"({"path":"ten.txt","offset":0})").is_error);
        CHECK(files.run("read_file", R"({"path":"ten.txt","limit":0})").is_error);
        CHECK(files.run("read_file", R"({"path":"ten.txt","offset":"three"})").is_error);
        // A number sent as a string still reads.
        CHECK(files.run("read_file", R"({"path":"ten.txt","offset":"10"})")
                  .content.starts_with("    10\trow 10"));
    }
    SECTION("a last line with no newline is still a line; an empty file says so") {
        files.write("open.txt", "one\ntwo");
        CHECK(files.run("read_file", R"({"path":"open.txt","offset":2})").content ==
              "     2\ttwo\n[lines 2-2 of 2]");
        files.write("empty.txt", "");
        CHECK(
            files.run("read_file", R"({"path":"empty.txt","offset":1})").content.find("is empty") !=
            std::string::npos);
    }
    SECTION("a plain read stays byte for byte, unnumbered") {
        files.write("crlf.txt", "a\r\nb\r\n");
        CHECK(files.run("read_file", R"({"path":"crlf.txt"})").content == "a\r\nb\r\n");
        CHECK(files.run("read_file", R"({"path":"ten.txt","offset":null,"limit":null})").content ==
              numbered_lines(10));
    }
}

TEST_CASE("a ranged read stops at the byte cap on a whole line and says where to go on",
          "[tools][fs][range]") {
    const Files files{/*read_limit=*/64};
    files.write("ten.txt", numbered_lines(10));
    const ToolOutcome read = files.run("read_file", R"({"path":"ten.txt","offset":2})");
    REQUIRE_FALSE(read.is_error);
    // Each numbered row is 13 bytes: four fit under 64, the fifth would not.
    CHECK(read.content.starts_with(
        "     2\trow 2\n     3\trow 3\n     4\trow 4\n     5\trow 5\n[lines 2-5 of 10; "));
    CHECK(read.content.ends_with("Read on with offset 6]"));

    // One line longer than the whole cap: its start, and the fact.
    files.write("wide.txt", std::string(200, 'w') + "\nshort\n");
    const ToolOutcome wide = files.run("read_file", R"({"path":"wide.txt","offset":1})");
    CHECK(wide.content.starts_with("     1\t" + std::string(57, 'w') + "\n[line 1 is longer than"));
    CHECK(wide.content.ends_with("Read on with offset 2]"));

    // A plain read of the same file points at ranges; a ranged one reads it.
    files.write("big.txt", numbered_lines(20));
    CHECK(files.run("read_file", R"({"path":"big.txt"})").content.find("20 lines") !=
          std::string::npos);
    CHECK(files.run("read_file", R"({"path":"big.txt","offset":20})").content ==
          "    20\trow 20\n[lines 20-20 of 20]");
}

TEST_CASE("edit_file replaces exactly what it is given and nothing else",
          "[tools][fs][edit][permission]") {
    const Files files;
    const apogee::agent::Tool* edit = files.registry.find("edit_file");
    REQUIRE(edit != nullptr);
    CHECK(edit->writes);
    CHECK(edit->describe_target(R"({"path":"x.cpp","old_string":"a","new_string":"b"})") ==
          "x.cpp");

    SECTION("one line in two thousand: the rest byte-identical") {
        std::string text = numbered_lines(2000);
        files.write("long.txt", text);
        const ToolOutcome edited = files.run(
            "edit_file",
            R"({"path":"long.txt","old_string":"row 1234\n","new_string":"ROW 1234 (changed)\n"})");
        REQUIRE_FALSE(edited.is_error);
        CHECK(edited.content.find("Replaced 1 occurrence") != std::string::npos);
        CHECK(edited.content.ends_with("at line 1234"));
        text.replace(text.find("row 1234\n"), 9, "ROW 1234 (changed)\n");
        CHECK(files.read("long.txt") == text);
    }
    SECTION("absent: refused, the file untouched") {
        files.write("f.txt", "alpha\nbeta\n");
        const ToolOutcome missing =
            files.run("edit_file", R"({"path":"f.txt","old_string":"gamma","new_string":"x"})");
        CHECK(missing.is_error);
        CHECK(missing.content.find("was not found") != std::string::npos);
        CHECK(files.read("f.txt") == "alpha\nbeta\n");
        // Whitespace is part of the match, never trimmed away.
        CHECK(files.run("edit_file", R"({"path":"f.txt","old_string":" alpha","new_string":"x"})")
                  .is_error);
        CHECK(files.read("f.txt") == "alpha\nbeta\n");
    }
    SECTION("ambiguous: refused with the count, the file untouched; replace_all takes them all") {
        files.write("dup.txt", "x = 1\ny = 2\nx = 1\n");
        const ToolOutcome twice = files.run(
            "edit_file", R"({"path":"dup.txt","old_string":"x = 1","new_string":"x = 3"})");
        CHECK(twice.is_error);
        CHECK(twice.content.find("occurs 2 times") != std::string::npos);
        CHECK(files.read("dup.txt") == "x = 1\ny = 2\nx = 1\n");

        const ToolOutcome all = files.run(
            "edit_file",
            R"({"path":"dup.txt","old_string":"x = 1","new_string":"x = 3","replace_all":true})");
        REQUIRE_FALSE(all.is_error);
        CHECK(all.content.find("Replaced 2 occurrences") != std::string::npos);
        CHECK(all.content.ends_with("at lines 1, 3"));
        CHECK(files.read("dup.txt") == "x = 3\ny = 2\nx = 3\n");
    }
    SECTION("replace_all with one match, and new text that contains the old") {
        files.write("grow.txt", "ab ab");
        REQUIRE_FALSE(
            files
                .run(
                    "edit_file",
                    R"({"path":"grow.txt","old_string":"ab","new_string":"abab","replace_all":true})")
                .is_error);
        CHECK(files.read("grow.txt") == "abab abab");
        REQUIRE_FALSE(
            files
                .run("edit_file", R"({"path":"grow.txt","old_string":"abab abab","new_string":""})")
                .is_error);
        CHECK(files.read("grow.txt").empty());
    }
    SECTION("text copied without a Windows file's carriage returns still matches, and keeps them") {
        files.write("win.txt", "one\r\ntwo\r\nthree\r\n");
        REQUIRE_FALSE(files
                          .run("edit_file",
                               R"({"path":"win.txt","old_string":"one\ntwo","new_string":"1\n2"})")
                          .is_error);
        CHECK(files.read("win.txt") == "1\r\n2\r\nthree\r\n");
    }
    SECTION("what it will not do") {
        files.write("f.txt", "alpha");
        CHECK(files.run("edit_file", R"({"path":"f.txt","old_string":"","new_string":"x"})")
                  .is_error);
        CHECK(
            files.run("edit_file", R"({"path":"f.txt","old_string":"alpha","new_string":"alpha"})")
                .is_error);
        CHECK(files.run("edit_file", R"({"path":"f.txt","old_string":"alpha"})").is_error);
        const ToolOutcome absent =
            files.run("edit_file", R"({"path":"nope.txt","old_string":"a","new_string":"b"})");
        CHECK(absent.is_error);
        CHECK(absent.content.find("write_file") != std::string::npos);
        CHECK_FALSE(std::filesystem::exists(files.root / "nope.txt"));
        CHECK(files.read("f.txt") == "alpha");
    }
#ifndef _WIN32
    SECTION("an executable stays executable") {
        files.write("run.sh", "#!/bin/sh\necho old\n");
        std::filesystem::permissions(files.root / "run.sh", std::filesystem::perms{0755});
        REQUIRE_FALSE(
            files.run("edit_file", R"({"path":"run.sh","old_string":"old","new_string":"new"})")
                .is_error);
        CHECK(files.read("run.sh") == "#!/bin/sh\necho new\n");
        CHECK((std::filesystem::status(files.root / "run.sh").permissions() &
               std::filesystem::perms::owner_exec) != std::filesystem::perms::none);
    }
#endif
}

TEST_CASE("grep_files finds lines by content under the root, and stops at its caps",
          "[tools][fs][grep]") {
    const Files files;
    files.write("src/main.cpp", "#include <x>\nint main() {\n    return run();\n}\n");
    files.write("src/run.cpp", "int run() {\r\n    return 0;\r\n}\r\n");
    files.write("docs/notes.md", "Run the tests.\n");
    files.write(".git/config", "int main() in a hidden directory\n");
    files.write("build/app.o", std::string("int main() \0 object", 19));

    SECTION("matches as path:line: text, relative to the root, sorted") {
        const ToolOutcome found = files.run("grep_files", R"({"pattern":"int \\w+\\("})");
        REQUIRE_FALSE(found.is_error);
        CHECK(found.content ==
              "src/main.cpp:2: int main() {\nsrc/run.cpp:1: int run() {\n[1 binary file skipped]");
    }
    SECTION("a folder, one file, a glob, and case") {
        CHECK(files.run("grep_files", R"({"pattern":"return","path":"src/run.cpp"})").content ==
              "src/run.cpp:2:     return 0;");
        CHECK(files.run("grep_files", R"({"pattern":"run","glob":"*.md"})")
                  .content.starts_with("No matches"));
        CHECK(files.run("grep_files", R"({"pattern":"run","glob":"*.md","ignore_case":true})")
                  .content == "docs/notes.md:1: Run the tests.");
        CHECK(files.run("grep_files", R"({"pattern":"return","path":"docs"})")
                  .content.starts_with("No matches"));
    }
    SECTION("what it refuses") {
        const ToolOutcome bad = files.run("grep_files", R"({"pattern":"(unclosed"})");
        CHECK(bad.is_error);
        CHECK(bad.content.find("not a regular expression") != std::string::npos);
        CHECK(files.run("grep_files", R"({})").is_error);
        CHECK(files.run("grep_files", R"({"pattern":"x","path":"missing"})").is_error);
    }
    SECTION("past 100 matches: the first hundred, and how many more") {
        std::string many;
        for (int i = 1; i <= 150; ++i) {
            many += "hit " + std::to_string(i) + "\n";
        }
        files.write("many.txt", many);
        const ToolOutcome capped =
            files.run("grep_files", R"({"pattern":"^hit","path":"many.txt"})");
        CHECK(capped.content.find("many.txt:100: hit 100\n") != std::string::npos);
        CHECK(capped.content.find("many.txt:101:") == std::string::npos);
        CHECK(capped.content.find("[... 50 more of 150 matches not shown") != std::string::npos);
    }
    SECTION("past 16 KB: whole rows only, and how many more") {
        std::string wide;
        for (int i = 1; i <= 90; ++i) {
            wide += "hit " + std::string(230, 'w') + "\n";
        }
        // Short matches after the wide ones would still fit: they are not
        // shown either, so what comes back has no holes in it.
        for (int i = 1; i <= 5; ++i) {
            wide += "hit short\n";
        }
        files.write("wide.txt", wide);
        const ToolOutcome capped =
            files.run("grep_files", R"({"pattern":"^hit","path":"wide.txt"})");
        const std::size_t footer = capped.content.find("[... ");
        REQUIRE(footer != std::string::npos);
        CHECK(footer <= apogee::tools::kGrepMaxBytes);
        CHECK(capped.content.substr(0, footer).ends_with("\n"));
        CHECK(capped.content.find("more of 95 matches") != std::string::npos);
        CHECK(capped.content.find("hit short") == std::string::npos);
    }
    SECTION("a long line shows its start") {
        files.write("min.js", "var a=" + std::string(1000, 'x') + ";\n");
        const ToolOutcome found = files.run("grep_files", R"({"pattern":"var a","path":"min.js"})");
        CHECK(found.content == "min.js:1: var a=" + std::string(234, 'x') + " ...");
    }
}
