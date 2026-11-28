#include "cli/notes.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "cli/job_stream.h"

/// The one writer of a finished run's notes (CLI-M58): one stderr line in the
/// house voice, nothing on stdout, whatever the words -- and on an open job
/// stream (CLI-M60), a `notice` carrying the words alone, nothing on stderr.
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

TEST_CASE("on an open job stream a note is a notice with its words alone, and stderr has none",
          "[commands][notes][job][M60]") {
    // CLI-M60: instead, never also -- a driver reading both channels would
    // get the note twice, as machine-mode sessions' suite warnings never are.
    const Captured captured;
    {
        apogee::commands::JobStream job{"models pull"};
        std::cout << "copying from your Ollama store (1 KiB)\n";
        apogee::commands::say_note(
            "models",
            "warning: architecture 'bert' is not known to run in Apogee's llama.cpp -- pulling "
            "anyway is fine; pass --yes to skip this notice.");
        std::cout << "the next line\n";
    }
    CHECK(captured.err.str().empty());
    std::vector<nlohmann::json> events;
    std::istringstream lines{captured.out.str()};
    for (std::string line; std::getline(lines, line);) {
        events.push_back(nlohmann::json::parse(line));
    }
    REQUIRE(events.size() == 4);
    CHECK(events[0].at("type") == "job");
    CHECK(events[1] ==
          nlohmann::json{{"type", "notice"}, {"text", "copying from your Ollama store (1 KiB)"}});
    CHECK(events[2] == nlohmann::json{{"type", "notice"},
                                      {"text",
                                       "warning: architecture 'bert' is not known to run in "
                                       "Apogee's llama.cpp -- pulling anyway is fine; pass --yes "
                                       "to skip this notice."}});
    CHECK(events[3] == nlohmann::json{{"type", "notice"}, {"text", "the next line"}});

    // Closed, the writer is the terminal's again.
    apogee::commands::say_note("models", "note: after the stream");
    CHECK(captured.err.str() == "apogee models: note: after the stream\n");
}
