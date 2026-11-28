#include "cli/notes.h"

#include <catch2/catch_test_macros.hpp>

#include <iostream>
#include <sstream>
#include <string>

/// The one writer of a finished run's notes (CLI-M58): one stderr line in the
/// house voice, nothing on stdout, whatever the words.
namespace {

/// stdout and stderr captured for one block, restored however it ends.
struct Captured {
    std::ostringstream out;
    std::ostringstream err;
    std::streambuf* old_out = std::cout.rdbuf(out.rdbuf());
    std::streambuf* old_err = std::cerr.rdbuf(err.rdbuf());

    Captured() = default;
    Captured(const Captured&) = delete;
    Captured& operator=(const Captured&) = delete;
    Captured(Captured&&) = delete;
    Captured& operator=(Captured&&) = delete;

    ~Captured() {
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
    }
};

}  // namespace

TEST_CASE("the house voice is apogee, the verb and a colon", "[commands][notes][M58]") {
    CHECK(apogee::commands::house_voice("graph") == "apogee graph: ");
    CHECK(apogee::commands::house_voice("knowledge") == "apogee knowledge: ");
    CHECK(apogee::commands::house_voice("mcp serve") == "apogee mcp serve: ");
}

TEST_CASE("a note is exactly one stderr line in the house voice, and nothing on stdout",
          "[commands][notes][M58]") {
    const Captured captured;
    apogee::commands::say_note("embed",
                               "warning: 2 source(s) keep vectors from 'mock-embed-a' -- the "
                               "collection searches lexically");
    apogee::commands::say_note("agents",
                               "could not remove /x/prompts/probe.txt: Permission denied");
    CHECK(captured.out.str().empty());
    CHECK(captured.err.str() ==
          "apogee embed: warning: 2 source(s) keep vectors from 'mock-embed-a' -- the collection "
          "searches lexically\n"
          "apogee agents: could not remove /x/prompts/probe.txt: Permission denied\n");
}
