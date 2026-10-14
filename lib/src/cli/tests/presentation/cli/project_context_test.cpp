#include "cli/project_context.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "support/env_guard.h"

/// Execute's project opening (41b): what makes a folder a project, which
/// context file is read, the cap that keeps its head and says the cut, the
/// text mended where it enters, and what the opening attaches.
namespace {

namespace fs = std::filesystem;
namespace c = apogee::commands;
using Catch::Matchers::ContainsSubstring;

void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << text;
}

std::string words(std::size_t count) {
    std::string out;
    for (std::size_t i = 0; i < count; ++i) {
        out += (i == 0 ? "" : (i % 10 == 0 ? "\n" : " ")) + std::string{"w"} + std::to_string(i);
    }
    return out;
}

}  // namespace

TEST_CASE("a folder is a project when it is a repository or holds a context file",
          "[cli][execute][project]") {
    const apogee::testing::TempDir root{"project-" + std::to_string(std::random_device{}())};
    SECTION("neither: not a project, and nothing is read") {
        const c::ProjectOpening opening = c::read_project_opening(root.path());
        CHECK_FALSE(opening.project);
        CHECK_FALSE(opening.context.has_value());
    }
    SECTION("a repository with no context file: a project, nothing to read") {
        fs::create_directories(root.path() / ".git");
        const c::ProjectOpening opening = c::read_project_opening(root.path());
        CHECK(opening.project);
        CHECK_FALSE(opening.context.has_value());
    }
    SECTION("a context file alone makes it one, and is read whole under the cap") {
        write(root.path() / "AGENTS.md", "Use tabs.\nRun make test before done.\n");
        const c::ProjectOpening opening = c::read_project_opening(root.path());
        CHECK(opening.project);
        REQUIRE(opening.context.has_value());
        CHECK(opening.context->file.filename() == "AGENTS.md");
        CHECK(opening.context->text == "Use tabs.\nRun make test before done.\n");
        CHECK(opening.context->words == 7);
        CHECK_FALSE(opening.context->truncated());
        CHECK(c::project_context_line(*opening.context) ==
              "project context: AGENTS.md, 7 words, in every request");
        const std::string note = c::project_context_note(*opening.context);
        CHECK_THAT(note, ContainsSubstring("Project context, from AGENTS.md in the folder this "
                                           "session opened in"));
        CHECK_THAT(note, ContainsSubstring("Run make test before done."));
        CHECK_THAT(note, !ContainsSubstring("left out"));
    }
    SECTION("AGENTS.md first; CLAUDE.md honoured when it is the only one") {
        write(root.path() / "CLAUDE.md", "Claude's rules.\n");
        REQUIRE(c::read_project_opening(root.path()).context.has_value());
        CHECK(c::read_project_opening(root.path()).context->file.filename() == "CLAUDE.md");
        write(root.path() / "AGENTS.md", "The agents' rules.\n");
        CHECK(c::read_project_opening(root.path()).context->text == "The agents' rules.\n");
    }
    SECTION("past the cap: the head kept, the cut said in the note and on the line") {
        write(root.path() / "AGENTS.md", words(c::kProjectContextWords + 100));
        const c::ProjectOpening opening = c::read_project_opening(root.path());
        REQUIRE(opening.context.has_value());
        CHECK(opening.context->truncated());
        CHECK(opening.context->words == c::kProjectContextWords);
        CHECK(opening.context->total_words == c::kProjectContextWords + 100);
        CHECK(opening.context->text.ends_with("w3999"));
        CHECK(c::project_context_line(*opening.context) ==
              "project context: AGENTS.md, the first 4,000 of 4,100 words, in every request -- "
              "the rest left out");
        CHECK_THAT(c::project_context_note(*opening.context),
                   ContainsSubstring("[AGENTS.md goes on: 100 more words were left out, past the "
                                     "4,000-word cap. Read the file itself for the rest.]"));
    }
    SECTION("text where it enters: a Latin-1 file is mended to UTF-8") {
        write(root.path() / "AGENTS.md", "caf\xe9 rules\n");
        const c::ProjectOpening opening = c::read_project_opening(root.path());
        REQUIRE(opening.context.has_value());
        CHECK(opening.context->text == "caf\xEF\xBF\xBD rules\n");
    }
}

TEST_CASE("the opening attaches the folder, unless --attach says otherwise",
          "[cli][execute][project]") {
    CHECK(c::opening_attaches({}, true) == std::vector<std::string>{"."});
    CHECK(c::opening_attaches({}, false).empty());
    // `--attach ""` skips it, and is never attached itself.
    CHECK(c::opening_attaches({""}, true).empty());
    CHECK(c::opening_attaches({""}, false).empty());
    // Anything given replaces it.
    CHECK(c::opening_attaches({"notes.md"}, true) == std::vector<std::string>{"notes.md"});
    CHECK(c::opening_attaches({"", "notes.md"}, true) == std::vector<std::string>{"notes.md"});
    CHECK(c::opening_attaches({"a", "b"}, false) == std::vector<std::string>{"a", "b"});
}
