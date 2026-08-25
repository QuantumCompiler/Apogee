// The root command's contract, driven at library level.
//
// These exercise the same RootCommand the `apogee` binary runs, without
// linking the executable -- which is the whole reason for the library/CLI
// split. The complementary end-to-end checks that actually run the binary are
// the cli.* tests registered in ../CMakeLists.txt.

#include "commands/root.h"

#include <CLI/CLI.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "commands/registry.h"
#include "support/fake_command.h"

using apogee::commands::CommandRegistry;
using apogee::commands::RootCommand;
using apogee::testing::FakeCommand;

namespace {

/// Runs `root` over an argv built from `args` (argv[0] is supplied here, as
/// the real process gets it from the OS).
int run_with(RootCommand& root, const std::vector<std::string>& args) {
    std::vector<const char*> argv;
    argv.reserve(args.size() + 1);
    argv.push_back("apogee");
    for (const std::string& arg : args) {
        argv.push_back(arg.c_str());
    }
    return root.run(static_cast<int>(argv.size()), argv.data());
}

/// A config file that exists, so the root's ExistingFile check passes. Written
/// under the OS temp directory and removed by the caller -- tests stay
/// hermetic and never touch a real ~/.apogee.
std::filesystem::path write_temp_config() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "apogee-root-test-config.yaml";
    std::ofstream out{path};
    out << "# fixture\n";
    return path;
}

}  // namespace

TEST_CASE("the root command binds the registry it was given", "[commands][root]") {
    CommandRegistry registry;
    registry.add(std::make_unique<FakeCommand>("alpha", "the first"));
    registry.add(std::make_unique<FakeCommand>("beta", "the second"));

    RootCommand root{std::move(registry)};

    REQUIRE(root.app().get_subcommand("alpha") != nullptr);
    REQUIRE(root.app().get_subcommand("beta") != nullptr);
}

TEST_CASE("the default root exposes the built-in commands", "[commands][root]") {
    RootCommand root;

    REQUIRE(root.app().get_subcommand("version") != nullptr);
}

TEST_CASE("bare invocation prints help and succeeds", "[commands][root]") {
    RootCommand root;

    // argc == 1: the program name and nothing else. Help, exit 0 -- running
    // `apogee` with no arguments is a question, not a usage error.
    const std::array<const char*, 1> argv{"apogee"};
    REQUIRE(root.run(1, argv.data()) == 0);
}

TEST_CASE("help text lists the registered subcommands", "[commands][root]") {
    CommandRegistry registry;
    registry.add(std::make_unique<FakeCommand>("distinctive-name", "a summary line"));
    RootCommand root{std::move(registry)};

    const std::string help = root.app().help();

    REQUIRE(help.find("distinctive-name") != std::string::npos);
    REQUIRE(help.find("a summary line") != std::string::npos);
}

TEST_CASE("naming a subcommand runs exactly that command", "[commands][root]") {
    auto owned_alpha = std::make_unique<FakeCommand>("alpha", "the first");
    auto owned_beta = std::make_unique<FakeCommand>("beta", "the second");
    const FakeCommand* alpha = owned_alpha.get();
    const FakeCommand* beta = owned_beta.get();

    CommandRegistry registry;
    registry.add(std::move(owned_alpha));
    registry.add(std::move(owned_beta));
    RootCommand root{std::move(registry)};

    REQUIRE(run_with(root, {"alpha"}) == 0);

    REQUIRE(alpha->invocations() == 1);
    REQUIRE(beta->invocations() == 0);
}

TEST_CASE("an unknown subcommand is an error, not a silent no-op", "[commands][root]") {
    RootCommand root;

    REQUIRE(run_with(root, {"no-such-command"}) != 0);
}

// Test names must not begin with "-": ctest invokes each case by passing its
// name as an argument, and Catch2's own parser would read it as a flag.
TEST_CASE("the config flag reaches the command at callback time", "[commands][root]") {
    const std::filesystem::path config = write_temp_config();

    auto owned = std::make_unique<FakeCommand>("alpha", "the first");
    const FakeCommand* alpha = owned.get();

    CommandRegistry registry;
    registry.add(std::move(owned));
    RootCommand root{std::move(registry)};

    REQUIRE(run_with(root, {"--config", config.string(), "alpha"}) == 0);
    REQUIRE(alpha->observed_config_path() == config.string());
    REQUIRE(root.context().config_path == config.string());

    std::filesystem::remove(config);
}

TEST_CASE("a config flag pointing at a missing file is rejected", "[commands][root]") {
    RootCommand root;

    REQUIRE(run_with(root, {"--config", "/definitely/not/a/real/apogee.yaml", "version"}) != 0);
}

TEST_CASE("an absent config flag leaves the context empty for default resolution",
          "[commands][root]") {
    auto owned = std::make_unique<FakeCommand>("alpha", "the first");
    const FakeCommand* alpha = owned.get();

    CommandRegistry registry;
    registry.add(std::move(owned));
    RootCommand root{std::move(registry)};

    REQUIRE(run_with(root, {"alpha"}) == 0);
    REQUIRE(alpha->observed_config_path().empty());
}
