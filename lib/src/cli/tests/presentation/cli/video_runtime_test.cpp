#include "cli/video_runtime.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "support/env_guard.h"
#include "training/python_env.h"

/// The video set installed on demand, with consent (49b): asked at a
/// terminal with what it installs and how big it is, installed through the
/// environment's own pip on a yes and recorded, refused on a no and on a
/// pipe -- each refusal naming `apogee train setup --with video`.
namespace {

using Catch::Matchers::ContainsSubstring;

/// An environment whose pip is the test's: an install of the video set
/// lands torch and diffusers in its site-packages, as pip would.
struct Env {
    apogee::testing::TempDir root{"video-consent-" + std::to_string(std::random_device{}())};
    std::vector<apogee::platform::ChildCommand> commands;
    int exit_code = 0;

    /// The environment exists already: its interpreter, and its record.
    [[nodiscard]] apogee::training::PythonEnv env() {
        const std::filesystem::path venv = root.path() / "venv";
        const std::filesystem::path interpreter = apogee::training::PythonEnv{venv}.interpreter();
        std::filesystem::create_directories(interpreter.parent_path());
        std::ofstream{interpreter} << "#!fake";
        std::ofstream{venv / "apogee.json"}
            << R"({"base_python": "python3", "created_at": "2026-10-19T00:00:00Z", "sets": []})";
        return apogee::training::PythonEnv{
            venv, [this, venv](const apogee::platform::ChildCommand& command,
                               const apogee::harness::CancellationToken&) {
                commands.push_back(command);
                apogee::training::CommandResult result;
                result.exit_code = exit_code;
                if (exit_code != 0) {
                    result.err = "ERROR: No matching distribution found for diffusers>=0.35";
                    return result;
                }
                for (const char* package : {"torch", "diffusers"}) {
                    const std::filesystem::path dir =
                        venv / "lib" / "python3.13" / "site-packages" / package;
                    std::filesystem::create_directories(dir);
                    std::ofstream{dir / "__init__.py"};
                }
                return result;
            }};
    }
};

}  // namespace

TEST_CASE("a yes at a terminal installs the video set, saying what and how big",
          "[cli][video][consent]") {
    Env fixture;
    apogee::training::PythonEnv env = fixture.env();
    std::istringstream in{"y\n"};
    std::ostringstream out;
    REQUIRE(apogee::commands::install_video_set(env, true, in, out, "video generate").empty());
    CHECK_THAT(out.str(), ContainsSubstring("video generate needs torch and diffusers in Apogee's "
                                            "Python environment at\n  " +
                                            env.dir().string()));
    CHECK_THAT(out.str(), ContainsSubstring("(the 'video' set: torch>=2.6 diffusers>=0.35 "
                                            "transformers>=4.46 accelerate>=1.0 ftfy>=6.1 "
                                            "sentencepiece>=0.2 peft>=0.13 -- several gigabytes"));
    CHECK_THAT(out.str(), ContainsSubstring("Install it now? [y/N] "));
    CHECK_THAT(out.str(), ContainsSubstring("installing the 'video' set: "));
    CHECK(out.str().ends_with("installed\n"));
    // The environment's own pip, never the system's.
    REQUIRE(fixture.commands.size() == 1);
    CHECK(fixture.commands.front().program == env.interpreter());
    CHECK(fixture.commands.front().arguments.at(0) == "-m");
    CHECK(fixture.commands.front().arguments.at(1) == "pip");
    CHECK(std::ranges::find(fixture.commands.front().arguments, "diffusers>=0.35") !=
          fixture.commands.front().arguments.end());
    CHECK(env.status().has(apogee::training::RequirementSet::Video));
}

TEST_CASE("a no, a pipe or a failed pip installs nothing and names the command that would",
          "[cli][video][consent]") {
    Env fixture;
    apogee::training::PythonEnv env = fixture.env();
    std::ostringstream out;
    SECTION("no") {
        std::istringstream in{"n\n"};
        CHECK(apogee::commands::install_video_set(env, true, in, out, "video generate") ==
              "not installed. Run 'apogee train setup --with video' when ready");
        CHECK(fixture.commands.empty());
    }
    SECTION("an empty answer is a no") {
        std::istringstream in{"\n"};
        CHECK_FALSE(
            apogee::commands::install_video_set(env, true, in, out, "video generate").empty());
        CHECK(fixture.commands.empty());
    }
    SECTION("a pipe is never asked") {
        std::istringstream in{"y\n"};
        CHECK(apogee::commands::install_video_set(env, false, in, out, "video generate") ==
              "torch and diffusers are not installed in Apogee's Python environment -- apogee "
              "train setup --with video");
        CHECK(out.str().empty());
        CHECK(fixture.commands.empty());
    }
    SECTION("pip fails: its words") {
        fixture.exit_code = 1;
        std::istringstream in{"yes\n"};
        const std::string failure =
            apogee::commands::install_video_set(env, true, in, out, "video generate");
        CHECK_THAT(failure, ContainsSubstring("pip install for the 'video' set failed"));
        CHECK_THAT(failure, ContainsSubstring("No matching distribution found for diffusers"));
        CHECK_FALSE(env.status().has(apogee::training::RequirementSet::Video));
    }
}

TEST_CASE("a set the environment's Python cannot run is refused before it is offered",
          "[cli][video][consent][m19]") {
    // Nothing is offered that pip would only refuse: the rebuild is named,
    // nobody is asked, nothing runs.
    Env fixture;
    apogee::training::PythonEnv env = fixture.env();
    std::ofstream{env.dir() / "pyvenv.cfg"} << "home = /usr/bin\nversion = 3.9.6\n";
    std::istringstream in{"y\n"};
    std::ostringstream out;
    CHECK(apogee::commands::install_video_set(env, true, in, out, "video generate") ==
          "the Python environment at " + env.dir().string() +
              " is on Python 3.9.6, and the 'video' set runs on >=3.10 -- rebuild it on a Python "
              "that fits: apogee train setup --rebuild --with video");
    CHECK(out.str().empty());
    CHECK(fixture.commands.empty());
}
