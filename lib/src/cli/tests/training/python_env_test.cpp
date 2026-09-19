#include "training/python_env.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "support/env_guard.h"

/// The environment Apogee owns: created from a base interpreter with
/// `-m venv`, requirement sets installed with its own pip and recorded,
/// the record read back, every failure named -- all through an injectable
/// runner, so no test here runs Python or pip.
namespace {

using apogee::training::CommandResult;
using apogee::training::locate_base_python;
using apogee::training::packages_for;
using apogee::training::PythonEnv;
using apogee::training::PythonEnvStatus;
using apogee::training::requirement_set_from_string;
using apogee::training::requirement_set_names;
using apogee::training::RequirementSet;

struct Fixture {
    apogee::testing::TempDir root{"python-env-" + std::to_string(std::random_device{}())};
    std::vector<apogee::platform::ChildCommand> commands;
    /// Exit code the next commands return; the runner also creates the
    /// interpreter on a `venv` call, as the real one would.
    int exit_code = 0;
    std::string stderr_text;

    [[nodiscard]] PythonEnv env() {
        return PythonEnv{root.path() / "venv", [this](const apogee::platform::ChildCommand& command,
                                                      const apogee::harness::CancellationToken&) {
                             commands.push_back(command);
                             CommandResult result;
                             result.exit_code = exit_code;
                             result.err = stderr_text;
                             // The venv directory is the third argument; a
                             // command without one creates nothing (a mutant
                             // that dropped it once wrote fake interpreters
                             // into the working directory).
                             if (exit_code == 0 && command.arguments.size() >= 3 &&
                                 command.arguments[1] == "venv") {
                                 const std::filesystem::path venv{command.arguments[2]};
                                 std::filesystem::create_directories(venv / "bin");
                                 std::ofstream{venv / "bin" / "python"} << "#!fake";
                                 std::filesystem::create_directories(venv / "Scripts");
                                 std::ofstream{venv / "Scripts" / "python.exe"} << "fake";
                             }
                             return result;
                         }};
    }
};

}  // namespace

TEST_CASE("requirement sets are named and carry packages", "[training][python]") {
    CHECK(requirement_set_names().size() == 4);
    CHECK(requirement_set_from_string("prepare") == RequirementSet::Prepare);
    CHECK(requirement_set_from_string("convert") == RequirementSet::Convert);
    CHECK_FALSE(requirement_set_from_string("nope").has_value());
    for (const std::string_view name : requirement_set_names()) {
        const auto set = requirement_set_from_string(name);
        REQUIRE(set.has_value());
        CHECK_FALSE(packages_for(*set).empty());
        CHECK(apogee::training::to_string(*set) == name);
    }
    CHECK(packages_for(RequirementSet::Prepare)[0] == "datasets>=3.0");
}

TEST_CASE("create runs the base interpreter's venv module and records the origin",
          "[training][python]") {
    Fixture fixture;
    PythonEnv env = fixture.env();
    CHECK_FALSE(env.exists());
    CHECK_FALSE(env.status().exists);

    REQUIRE(env.create("/usr/bin/python3").empty());
    REQUIRE(fixture.commands.size() == 1);
    CHECK(fixture.commands[0].program == "/usr/bin/python3");
    REQUIRE(fixture.commands[0].arguments.size() == 3);
    CHECK(fixture.commands[0].arguments[0] == "-m");
    CHECK(fixture.commands[0].arguments[1] == "venv");
    CHECK(fixture.commands[0].arguments[2] == env.dir().string());

    CHECK(env.exists());
    const PythonEnvStatus status = env.status();
    CHECK(status.exists);
    CHECK(status.error.empty());
    CHECK(status.base_python == "/usr/bin/python3");
    CHECK_FALSE(status.created_at.empty());
    CHECK(status.sets.empty());

    // A second create is a no-op: the environment is the user's now.
    REQUIRE(env.create("/other/python").empty());
    CHECK(fixture.commands.size() == 1);
}

TEST_CASE("install runs the environment's own pip with the set's floors and records the set",
          "[training][python]") {
    Fixture fixture;
    PythonEnv env = fixture.env();
    REQUIRE(env.create("/usr/bin/python3").empty());
    REQUIRE(env.install(RequirementSet::Prepare).empty());
    REQUIRE(fixture.commands.size() == 2);
    const apogee::platform::ChildCommand& pip = fixture.commands[1];
    CHECK(pip.program == env.interpreter().string());
    REQUIRE(pip.arguments.size() >= 5);
    CHECK(pip.arguments[0] == "-m");
    CHECK(pip.arguments[1] == "pip");
    CHECK(pip.arguments[2] == "install");
    CHECK(pip.arguments[3] == "--disable-pip-version-check");
    CHECK(pip.arguments[4] == "datasets>=3.0");
    CHECK(env.status().has(RequirementSet::Prepare));
    CHECK_FALSE(env.status().has(RequirementSet::Mlx));

    // Installing again records the set once.
    REQUIRE(env.install(RequirementSet::Prepare).empty());
    CHECK(env.status().sets.size() == 1);
}

TEST_CASE("a failing command is named with its exit code and stderr", "[training][python]") {
    Fixture fixture;
    PythonEnv env = fixture.env();
    fixture.exit_code = 1;
    fixture.stderr_text = "Error: Command '/usr/bin/python3 -m venv' returned non-zero\n";
    const std::string error = env.create("/usr/bin/python3");
    CHECK(error.find("exit code 1") != std::string::npos);
    CHECK(error.find("returned non-zero") != std::string::npos);
    CHECK_FALSE(env.exists());
}

TEST_CASE("install before create names the setup command", "[training][python]") {
    Fixture fixture;
    PythonEnv env = fixture.env();
    const std::string error = env.install(RequirementSet::Prepare);
    CHECK(error.find("apogee train setup") != std::string::npos);
    CHECK(fixture.commands.empty());
}

TEST_CASE("a configured interpreter that does not exist is refused by name", "[training][python]") {
    std::string error;
    const std::filesystem::path found = locate_base_python("/definitely/not/here/python3", error);
    CHECK(found.empty());
    CHECK(error.find("/definitely/not/here/python3") != std::string::npos);
    CHECK(error.find("training.python") != std::string::npos);
}

TEST_CASE("a configured interpreter that exists is used as given", "[training][python]") {
    const apogee::testing::TempDir root{"python-locate-" + std::to_string(std::random_device{}())};
    const std::filesystem::path fake = root.path() / "py";
    std::ofstream{fake} << "#!fake";
    std::string error;
    CHECK(locate_base_python(fake.string(), error) == fake);
    CHECK(error.empty());
}
