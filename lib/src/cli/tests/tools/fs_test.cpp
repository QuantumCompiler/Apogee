#include "tools/fs.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
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
}

TEST_CASE("read_file returns the text, caps it, and says so", "[tools][fs]") {
    const Sandbox box;
    CHECK(box.run("read_file", R"({"path":"a.md"})").content == "alpha");
    std::ofstream{box.root / "big.txt"} << std::string(40, 'x');
    const ToolOutcome capped = box.run("read_file", R"({"path":"big.txt"})");
    CHECK_FALSE(capped.is_error);
    CHECK(capped.content.starts_with(std::string(16, 'x')));
    CHECK(capped.content.find("truncated -- 24 bytes omitted") != std::string::npos);
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
