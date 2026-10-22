#include "training/python_env.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "platform/platform.h"
#include "support/env_guard.h"

/// The environment Apogee owns: created from a base interpreter with
/// `-m venv`, requirement sets installed with its own pip and recorded,
/// the record read back, every failure named -- all through an injectable
/// runner, so no test here runs Python or pip.
namespace {

using apogee::training::choose_base_python;
using apogee::training::CommandResult;
using apogee::training::locate_base_python;
using apogee::training::packages_for;
using apogee::training::python_range;
using apogee::training::PythonEnv;
using apogee::training::PythonEnvStatus;
using apogee::training::PythonVersion;
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
    /// What each interpreter, by file name, says its version is (M19); a
    /// `venv` made by one writes it into `pyvenv.cfg`, as Python does.
    std::map<std::string, std::string> versions;
    /// The pip install, counted from 1, that fails; 0 for none.
    int failing_pip = 0;
    int pips = 0;

    [[nodiscard]] apogee::training::CommandRunner runner() {
        return
            [this](const apogee::platform::ChildCommand& command,
                   const apogee::harness::CancellationToken& token) { return run(command, token); };
    }

    [[nodiscard]] PythonEnv env() {
        return PythonEnv{root.path() / "venv", runner()};
    }

    CommandResult run(const apogee::platform::ChildCommand& command,
                      const apogee::harness::CancellationToken&) {
        commands.push_back(command);
        CommandResult result;
        result.exit_code = exit_code;
        result.err = stderr_text;
        const std::string name = std::filesystem::path{command.program}.filename().string();
        if (!command.arguments.empty() && command.arguments[0] == "-c") {
            const auto said = versions.find(name);
            result.exit_code = said == versions.end() ? 1 : 0;
            result.out = said == versions.end() ? "" : said->second + "\n";
            return result;
        }
        if (command.arguments.size() >= 2 && command.arguments[1] == "pip") {
            ++pips;
            if (pips == failing_pip) {
                result.exit_code = 1;
                result.err = "ERROR: a scripted failure\n";
                return result;
            }
        }
        // The venv directory is the third argument; a
        // command without one creates nothing (a mutant
        // that dropped it once wrote fake interpreters
        // into the working directory).
        if (exit_code == 0 && command.arguments.size() >= 3 && command.arguments[1] == "venv") {
            const std::filesystem::path venv{command.arguments[2]};
            std::filesystem::create_directories(venv / "bin");
            std::ofstream{venv / "bin" / "python"} << "#!fake";
            std::filesystem::create_directories(venv / "Scripts");
            std::ofstream{venv / "Scripts" / "python.exe"} << "fake";
            if (const auto said = versions.find(name); said != versions.end()) {
                std::ofstream{venv / "pyvenv.cfg"} << "home = /x\nversion = " << said->second
                                                   << "\n";
            }
        }
        return result;
    }
};

/// Fake interpreters, by name, in a directory that is the whole PATH -- a
/// scripted machine: each answers its version through the fixture's runner.
struct ScriptedPath {
    apogee::testing::TempDir dir{"python-path-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard path{"PATH", dir.path().string()};

    explicit ScriptedPath(Fixture& fixture, const std::map<std::string, std::string>& pythons) {
        for (const auto& [name, version] : pythons) {
            const std::filesystem::path file = dir.path() / name;
            std::ofstream{file} << "#!/bin/sh\n";
            std::filesystem::permissions(file, std::filesystem::perms::owner_all);
            fixture.versions[name] = version;
        }
    }
};

/// Every set an Apple-silicon host can install, in the table's order.
const std::vector<RequirementSet> kAppleSets{RequirementSet::Prepare,      RequirementSet::Mlx,
                                             RequirementSet::Convert,      RequirementSet::MlxVlm,
                                             RequirementSet::Video,        RequirementSet::Voice,
                                             RequirementSet::MlxEmbeddings};

}  // namespace

TEST_CASE("requirement sets are named and carry packages", "[training][python]") {
    CHECK(requirement_set_names().size() == 8);
    // The speech driver's stack (50a).
    CHECK(requirement_set_from_string("voice") == RequirementSet::Voice);
    CHECK(packages_for(RequirementSet::Voice)[1] == "kokoro>=0.9.4");
    CHECK(requirement_set_from_string("prepare") == RequirementSet::Prepare);
    CHECK(requirement_set_from_string("convert") == RequirementSet::Convert);
    // The mlx backend's vision path, installed on demand (27c).
    CHECK(requirement_set_from_string("mlx-vlm") == RequirementSet::MlxVlm);
    CHECK(packages_for(RequirementSet::MlxVlm)[0] == "mlx-vlm>=0.6.1");
    // 53f: the first release whose models/ builds every towered set the
    // store holds, read on a recorded date.
    CHECK(apogee::training::mlx_vlm_floor_reviewed() == "2026-10-21");
    // 53h: the embedding route's, on demand -- its floor the first release
    // whose gemma3_text pools first and converts its padding mask, read on a
    // recorded date, and Apple silicon's alone.
    CHECK(requirement_set_from_string("mlx-embeddings") == RequirementSet::MlxEmbeddings);
    CHECK(to_string(RequirementSet::MlxEmbeddings) == "mlx-embeddings");
    CHECK(packages_for(RequirementSet::MlxEmbeddings).size() == 1);
    CHECK(packages_for(RequirementSet::MlxEmbeddings)[0] == "mlx-embeddings>=0.1.0");
    CHECK(apogee::training::mlx_embeddings_floor_reviewed() == "2026-10-21");
    CHECK(apogee::training::set_unfit_reason(RequirementSet::MlxEmbeddings, true, false).empty());
    CHECK(apogee::training::set_unfit_reason(RequirementSet::MlxEmbeddings, false, true) ==
          "for Apple silicon, which this host is not");
    // The video driver's stack (49b), never installed with anything else.
    CHECK(requirement_set_from_string("video") == RequirementSet::Video);
    CHECK(to_string(RequirementSet::Video) == "video");
    CHECK(packages_for(RequirementSet::Video)[0] == "torch>=2.6");
    CHECK(packages_for(RequirementSet::Video)[1] == "diffusers>=0.35");
    // What diffusers loads a LoRA through (49e).
    CHECK(packages_for(RequirementSet::Video).back() == "peft>=0.13");
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

TEST_CASE("an installed package is found by its files, on either layout, with its version",
          "[training][python][video]") {
    // 49b: the video ladder's probe -- never an import.
    Fixture fixture;
    const std::filesystem::path venv = fixture.root.path() / "venv";
    CHECK(apogee::training::find_site_package(venv, "torch").empty());
    const std::filesystem::path posix = venv / "lib" / "python3.13" / "site-packages";
    std::filesystem::create_directories(posix / "torch");
    std::ofstream{posix / "torch" / "__init__.py"};
    std::filesystem::create_directories(posix / "torch-2.9.1.dist-info");
    const std::filesystem::path torch = apogee::training::find_site_package(venv, "torch");
    CHECK(torch == posix / "torch");
    CHECK(apogee::training::installed_package_version(torch) == "2.9.1");
    // A directory without __init__.py is no package.
    std::filesystem::create_directories(posix / "diffusers");
    CHECK(apogee::training::find_site_package(venv, "diffusers").empty());
    // Windows keeps one site-packages under Lib/.
    const std::filesystem::path windows = venv / "Lib" / "site-packages" / "diffusers";
    std::filesystem::create_directories(windows);
    std::ofstream{windows / "__init__.py"};
    CHECK(apogee::training::find_site_package(venv, "diffusers") == windows);
    CHECK(apogee::training::installed_package_version(windows).empty());
}

TEST_CASE("each set's Python range is carried in the sets' table, with the date it was read",
          "[training][python][m19]") {
    // PyPI's requires_python for each set's packages, read 2026-10-21: only
    // voice is bounded above (kokoro, misaki <3.13), mlx's floor is mlx-lm's.
    CHECK(apogee::training::python_ranges_reviewed() == "2026-10-21");
    CHECK(python_range(RequirementSet::Voice).describe() == ">=3.10,<3.13");
    CHECK(python_range(RequirementSet::Mlx).describe() == ">=3.11");
    for (const RequirementSet set :
         {RequirementSet::Prepare, RequirementSet::Peft, RequirementSet::Convert,
          RequirementSet::MlxVlm, RequirementSet::Video, RequirementSet::MlxEmbeddings}) {
        CHECK(python_range(set).describe() == ">=3.10");
    }
    // An Apple-silicon host's sets fit 3.11 and 3.12 alone.
    CHECK(python_range(kAppleSets).describe() == ">=3.11,<3.13");
    CHECK(python_range(kAppleSets).contains(*PythonVersion::parse("3.12.13")));
    CHECK_FALSE(python_range(kAppleSets).contains(*PythonVersion::parse("3.14.5")));
    CHECK_FALSE(python_range(kAppleSets).contains(*PythonVersion::parse("3.10.20")));

    CHECK(PythonVersion::parse("3.11.4.final.0")->str() == "3.11.4");
    CHECK(PythonVersion::parse("3.12")->str() == "3.12.0");
    CHECK_FALSE(PythonVersion::parse("").has_value());
    CHECK_FALSE(PythonVersion::parse("python").has_value());
}

TEST_CASE("an environment on a Python a set does not run on is refused before pip",
          "[training][python][m19]") {
    // The user's environment (2026-10-21): made from Homebrew's 3.14.5, its
    // version only in pyvenv.cfg -- before M19 the record did not say it.
    Fixture fixture;
    fixture.versions["python3"] = "3.14.5";
    PythonEnv env = fixture.env();
    REQUIRE(env.create("/opt/homebrew/bin/python3").empty());
    std::ofstream{env.record_path()}
        << R"({"base_python": "/opt/homebrew/bin/python3", "created_at": "x", "sets": ["prepare"]})";
    CHECK(env.status().base_version.str() == "3.14.5");

    const std::size_t before = fixture.commands.size();
    const std::string refused = env.install(RequirementSet::Voice);
    CHECK(refused.find("is on Python 3.14.5, and the 'voice' set runs on >=3.10,<3.13") !=
          std::string::npos);
    CHECK(refused.ends_with("apogee train setup --rebuild --with voice"));
    CHECK(fixture.commands.size() == before);           // no pip
    CHECK(env.install(RequirementSet::Video).empty());  // what fits still installs
}

TEST_CASE("a rebuild reinstalls every recorded set in order, and a failed one puts the old back",
          "[training][python][m19]") {
    Fixture fixture;
    fixture.versions["python3"] = "3.14.5";
    PythonEnv env = fixture.env();
    REQUIRE(env.create("/opt/homebrew/bin/python3", {}, *PythonVersion::parse("3.14.5")).empty());
    for (const RequirementSet set :
         {RequirementSet::Prepare, RequirementSet::Mlx, RequirementSet::Convert,
          RequirementSet::MlxVlm, RequirementSet::Video}) {
        REQUIRE(env.install(set).empty());
    }
    const std::vector<std::string> recorded = env.status().sets;

    // A pip that fails midway: what was there is put back as it was.
    fixture.versions["python3.12"] = "3.12.13";
    fixture.pips = 0;
    fixture.failing_pip = 3;
    const std::string failed =
        env.rebuild("/opt/homebrew/bin/python3.12", *PythonVersion::parse("3.12.13"));
    CHECK(failed.find("put back as it was") != std::string::npos);
    CHECK(env.status().base_version.str() == "3.14.5");
    CHECK(env.status().sets == recorded);
    CHECK_FALSE(std::filesystem::exists(env.dir().string() + ".previous"));

    fixture.commands.clear();
    fixture.pips = 0;
    fixture.failing_pip = 0;
    REQUIRE(env.rebuild("/opt/homebrew/bin/python3.12", *PythonVersion::parse("3.12.13")).empty());
    REQUIRE(fixture.commands.size() == 6);
    CHECK(fixture.commands[0].program == "/opt/homebrew/bin/python3.12");
    CHECK(fixture.commands[0].arguments[1] == "venv");
    std::vector<std::string> installed;
    for (std::size_t i = 1; i < fixture.commands.size(); ++i) {
        installed.push_back(fixture.commands[i].arguments.at(4));
    }
    CHECK(installed == std::vector<std::string>{"datasets>=3.0", "mlx-lm>=0.21", "torch>=2.11",
                                                "mlx-vlm>=0.6.1", "torch>=2.6"});
    CHECK(env.status().base_version.str() == "3.12.13");
    CHECK(env.status().sets == recorded);
    CHECK_FALSE(std::filesystem::exists(env.dir().string() + ".previous"));
    CHECK(env.install(RequirementSet::Voice).empty());  // now it fits
}

TEST_CASE("the choice is the newest Python every set the host can install runs on",
          "[training][python][m19]") {
    if (apogee::platform::host_target().starts_with("windows")) {
        SKIP("the scripted interpreters are POSIX files on a POSIX PATH");
    }
    // The user's Mac: python3 is Homebrew's 3.14.5, 3.13 and 3.12 beside it.
    Fixture fixture;
    const ScriptedPath machine{
        fixture, {{"python3", "3.14.5"}, {"python3.13", "3.13.13"}, {"python3.12", "3.12.13"}}};
    const apogee::training::PythonChoice chosen =
        choose_base_python("", kAppleSets, std::vector{RequirementSet::Prepare}, fixture.runner());
    CHECK(chosen.error.empty());
    CHECK(chosen.path.filename() == "python3.12");
    CHECK(chosen.version.str() == "3.12.13");
    CHECK(chosen.reason ==
          "the newest Python every set this host can install runs on (>=3.11,<3.13); passed over "
          "python3 (3.14.5), python3.13 (3.13.13): the 'voice' set runs on >=3.10,<3.13");

    // The person's word stands: never swapped, refused before anything runs.
    const apogee::training::PythonChoice named = choose_base_python(
        "python3", kAppleSets, std::vector{RequirementSet::Voice}, fixture.runner(), "--python");
    CHECK(named.path.empty());
    CHECK(named.error.find("is Python 3.14.5, and the 'voice' set runs on >=3.10,<3.13") !=
          std::string::npos);
    const apogee::training::PythonChoice fits = choose_base_python(
        "python3.12", kAppleSets, std::vector{RequirementSet::Voice}, fixture.runner());
    CHECK(fits.path.filename() == "python3.12");
    CHECK(fits.reason == "as named");
}

TEST_CASE("with no Python for every set, the requested ones decide; with none for them, a refusal",
          "[training][python][m19]") {
    if (apogee::platform::host_target().starts_with("windows")) {
        SKIP("the scripted interpreters are POSIX files on a POSIX PATH");
    }
    Fixture fixture;
    const ScriptedPath machine{fixture, {{"python3", "3.14.5"}, {"python3.13", "3.13.13"}}};
    const apogee::training::PythonChoice prepare =
        choose_base_python("", kAppleSets, std::vector{RequirementSet::Prepare}, fixture.runner());
    CHECK(prepare.path.filename() == "python3");
    CHECK(prepare.reason.find("so the 'voice' set cannot go in this environment") !=
          std::string::npos);

    const apogee::training::PythonChoice voice =
        choose_base_python("", kAppleSets, std::vector{RequirementSet::Voice}, fixture.runner());
    CHECK(voice.path.empty());
    CHECK(voice.error ==
          "no Python here runs the 'voice' set (>=3.10,<3.13): found python3 "
          "(3.14.5), python3.13 (3.13.13) -- install one, such as 'brew install "
          "python@3.12', and name it with --python or training.python");
    // Nothing was created or installed: version questions only.
    for (const apogee::platform::ChildCommand& command : fixture.commands) {
        CHECK(command.arguments.at(0) == "-c");
    }
}
