#include "commands/train.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "commands/registry.h"
#include "commands/root.h"
#include "support/env_guard.h"

/// `apogee train setup` refusals -- an interpreter named in the config that
/// does not exist, an unknown requirement set, a bad trainer name -- each
/// before anything is created. The real creation runs in the shell
/// lifecycle test against the host's python3.
namespace {

struct Fixture {
    apogee::testing::TempDir home{"train-cli-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    std::filesystem::path config_path = home.path() / "config" / "config.yaml";

    explicit Fixture(std::string_view config = "backends:\n  local:\n    type: mock\n") {
        std::filesystem::create_directories(config_path.parent_path());
        std::ofstream{config_path, std::ios::binary} << config;
    }

    int run(const std::vector<std::string>& args, std::string* err) const {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        std::streambuf* old_out = std::cout.rdbuf(captured_out.rdbuf());
        std::streambuf* old_err = std::cerr.rdbuf(captured_err.rdbuf());
        int code = -1;
        try {
            apogee::commands::RootCommand root{apogee::commands::default_registry()};
            std::vector<std::string> full{"--config", config_path.string()};
            full.insert(full.end(), args.begin(), args.end());
            std::vector<const char*> argv{"apogee"};
            for (const std::string& arg : full) {
                argv.push_back(arg.c_str());
            }
            code = root.run(static_cast<int>(argv.size()), argv.data());
        } catch (...) {
            std::cout.rdbuf(old_out);
            std::cerr.rdbuf(old_err);
            throw;
        }
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
        *err = captured_err.str();
        return code;
    }
};

}  // namespace

TEST_CASE(
    "train setup refuses a configured interpreter that does not exist, before creating "
    "anything",
    "[commands][train][setup]") {
    const Fixture fixture{
        "training:\n  python: /no/such/python3\nbackends:\n  local:\n    type: mock\n"};
    std::string err;
    CHECK(fixture.run({"train", "setup"}, &err) == 1);
    CHECK(err.find("/no/such/python3") != std::string::npos);
    CHECK(err.find("training.python") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(fixture.home.path() / "training" / "venv"));
}

TEST_CASE("train setup refuses an unknown requirement set and a bad trainer by name",
          "[commands][train][setup]") {
    const Fixture fixture;
    std::string err;
    CHECK(fixture.run({"train", "setup", "--with", "nope"}, &err) == 1);
    CHECK(err.find("unknown requirement set 'nope'") != std::string::npos);
    CHECK(err.find("prepare, mlx, peft, convert") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(fixture.home.path() / "training" / "venv"));

    // The interpreter is located before the trainer name is judged, so a
    // host with no python3 still refuses -- with one message or the other.
    CHECK(fixture.run({"train", "setup", "--trainer", "bogus"}, &err) == 1);
    CHECK((err.find("--trainer must be") != std::string::npos ||
           err.find("python") != std::string::npos));
}

TEST_CASE("the trainer detection names its reason when nothing fits", "[commands][train][setup]") {
    std::string reason;
    const std::string trainer = apogee::commands::detect_trainer(reason);
    if (trainer.empty()) {
        CHECK(reason.find("nvidia-smi") != std::string::npos);
    } else {
        CHECK((trainer == "mlx" || trainer == "peft"));
        CHECK(reason.empty());
    }
}
