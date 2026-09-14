#include "tools/notes.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <random>
#include <string>

#include "agent/tool.h"
#include "harness/layout.h"
#include "support/env_guard.h"

/// The notes toolset: keys that can never be paths, and the four tools.
namespace {

using apogee::agent::ToolOutcome;
using apogee::agent::ToolRegistry;
using apogee::tools::valid_note_key;

}  // namespace

TEST_CASE("note keys are names, never paths", "[tools][notes]") {
    CHECK(valid_note_key("todo"));
    CHECK(valid_note_key("Meeting_2026-09-13"));
    CHECK_FALSE(valid_note_key(""));
    CHECK_FALSE(valid_note_key("../etc/passwd"));
    CHECK_FALSE(valid_note_key("a/b"));
    CHECK_FALSE(valid_note_key("with space"));
    CHECK_FALSE(valid_note_key(std::string(129, 'a')));
}

TEST_CASE("notes round-trip, list sorted, and delete; the writers are gated",
          "[tools][notes][permission]") {
    const apogee::testing::TempDir temp{"tools-notes-" + std::to_string(std::random_device{}())};
    const std::filesystem::path dir = temp.path() / "notes";  // not created yet
    ToolRegistry registry;
    apogee::tools::register_notes_tools(registry, dir);
    CHECK(registry.find("write_note")->writes);
    CHECK(registry.find("delete_note")->writes);
    CHECK_FALSE(registry.find("read_note")->writes);
    CHECK_FALSE(registry.find("list_notes")->writes);
    CHECK(registry.find("write_note")->describe_target(R"({"key":"todo"})") == "todo");

    const auto run = [&](std::string_view tool, const std::string& arguments) {
        return registry.find(tool)->run(arguments);
    };
    CHECK(run("list_notes", "{}").content.starts_with("No notes yet"));
    CHECK(run("read_note", R"({"key":"todo"})").is_error);

    CHECK(run("write_note", R"({"key":"todo","content":"buy milk"})").content ==
          "Note 'todo' saved (8 bytes)");
    CHECK(
        run("write_note", R"({"key":"alpha","content":"a"})").content.starts_with("Note 'alpha'"));
    CHECK(run("read_note", R"({"key":"todo"})").content == "buy milk");
    CHECK(run("write_note", R"({"key":"todo","content":"new"})").content ==
          "Note 'todo' saved (3 bytes)");
    CHECK(run("read_note", R"({"key":"todo"})").content == "new");
    CHECK(run("list_notes", "{}").content == "  alpha  (1 B)\n  todo  (3 B)");

    CHECK(run("write_note", R"({"key":"../escape","content":"x"})").is_error);
    CHECK_FALSE(std::filesystem::exists(temp.path() / "escape.txt"));
    CHECK(run("write_note", R"({"key":"nocontent"})").is_error);

    CHECK(run("delete_note", R"({"key":"todo"})").content == "Note 'todo' deleted");
    CHECK(run("delete_note", R"({"key":"todo"})").is_error);
    CHECK(run("list_notes", "{}").content == "  alpha  (1 B)");
}

TEST_CASE("notes/ is declared once, in the layout, so every install path seeds it",
          "[tools][notes][layout]") {
    bool declared = false;
    for (const apogee::harness::LayoutEntry& entry : apogee::harness::data_directories()) {
        if (entry.relative_path == "notes") {
            declared = true;
            CHECK_FALSE(entry.private_mode);
        }
    }
    CHECK(declared);
    CHECK(apogee::harness::notes_dir().filename() == "notes");
}
