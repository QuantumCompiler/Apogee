#include "cli/video_runtime.h"

#include <CLI/CLI.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "cli/job_stream.h"
#include "contracts/config.h"
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

namespace {

/// A runtime asked for on a pipe, as a streamed job asks it (M29): `ready`
/// run inside an open stream over a home whose environment holds nothing
/// when `bare`, and none at all when not. The stream's events, and stderr.
struct StreamedRefusal {
    std::vector<nlohmann::json> events;
    std::string err;
};

[[nodiscard]] StreamedRefusal refused_in_stream(
    bool bare, const std::function<void(const apogee::harness::Config&)>& ready) {
    const apogee::testing::TempDir home{"runtime-stream-" + std::to_string(std::random_device{}())};
    const apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    if (bare) {
        const std::filesystem::path interpreter =
            apogee::training::PythonEnv{home.path() / "training" / "venv"}.interpreter();
        std::filesystem::create_directories(interpreter.parent_path());
        std::ofstream{interpreter} << "#!fake";
    }
    std::ostringstream out;
    std::ostringstream err;
    std::streambuf* old_out = std::cout.rdbuf(out.rdbuf());
    std::streambuf* old_err = std::cerr.rdbuf(err.rdbuf());
    bool threw = false;
    {
        const apogee::commands::JobStream job{"video generate"};
        try {
            ready(apogee::harness::Config{});
        } catch (const CLI::RuntimeError& e) {
            threw = e.get_exit_code() == 1;
        }
    }
    std::cout.rdbuf(old_out);
    std::cerr.rdbuf(old_err);
    CHECK(threw);
    StreamedRefusal said;
    std::istringstream lines{out.str()};
    for (std::string line; std::getline(lines, line);) {
        said.events.push_back(nlohmann::json::parse(line));
    }
    said.err = err.str();
    return said;
}

}  // namespace

TEST_CASE("a runtime refused on a pipe is said as the open stream's error, and on stderr",
          "[cli][video][consent][stream]") {
    // M29: the refusal went to stderr alone, and the stream ended on `job`.
    const auto video = [](const apogee::harness::Config& config) {
        (void)apogee::commands::ready_video_runtime(config, "video generate");
    };
    const auto voice = [](const apogee::harness::Config& config) {
        (void)apogee::commands::ready_voice_runtime(config, "voice say");
    };
    for (const auto& [ready, purpose, refusal] :
         {std::tuple{std::function<void(const apogee::harness::Config&)>{video},
                     std::string{"video generate"},
                     std::string{"torch and diffusers are not installed in Apogee's Python "
                                 "environment -- apogee train setup --with video"}},
          std::tuple{std::function<void(const apogee::harness::Config&)>{voice},
                     std::string{"voice say"},
                     std::string{"torch and kokoro are not installed in Apogee's Python "
                                 "environment -- apogee train setup --with voice"}}}) {
        INFO(purpose);
        const StreamedRefusal said = refused_in_stream(true, ready);
        REQUIRE(said.events.size() == 2);
        CHECK(said.events.front().at("type") == "job");
        CHECK(said.events.back().at("type") == "error");
        CHECK(said.events.back().at("message") == refusal);
        CHECK(said.err == "apogee " + purpose + ": " + refusal + "\n");
    }

    // No environment at all (`require_python_env`, the sibling): the same.
    const StreamedRefusal none = refused_in_stream(false, video);
    REQUIRE(none.events.size() == 2);
    CHECK(none.events.back().at("type") == "error");
    const std::string message = none.events.back().at("message").get<std::string>();
    CHECK(
        message.starts_with("the Python environment has not been created yet. Run 'apogee "
                            "train setup' first"));
    CHECK(none.err == "apogee video generate: " + message + "\n");
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
