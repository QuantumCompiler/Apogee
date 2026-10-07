// The root command's contract, driven at library level.
//
// These exercise the same RootCommand the `apogee` binary runs, without
// linking the executable -- which is the whole reason for the library/CLI
// split. The complementary end-to-end checks that actually run the binary are
// the cli.* tests registered in ../CMakeLists.txt.

#include "cli/root.h"

#include <CLI/CLI.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "cli/registry.h"
#include "contracts/paths.h"
#include "support/channel_guard.h"
#include "support/env_guard.h"
#include "support/fake_command.h"
#include "version/version.h"

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

TEST_CASE("a config flag may name a file that does not exist yet", "[commands][root]") {
    // Deliberately NOT rejected at parse time. `apogee config init --config
    // <new path>` has to name a file it is about to create, and a command that
    // needs an existing config reports the miss itself -- with a message that
    // names the fix ("run 'apogee config init'") rather than CLI11's generic
    // "does not exist".
    RootCommand root;

    REQUIRE(run_with(root, {"--config", "/definitely/not/a/real/apogee.yaml", "version"}) == 0);
    CHECK(root.context().config_path == "/definitely/not/a/real/apogee.yaml");
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

// --- The root flags (M10) ---------------------------------------------------
//
// `--dev`, `--test`, `--release` and `--custom <config file>`: the first rung
// of the chain that chooses the data directory, for one run. The chain's own
// table is tests/data/contracts/paths_test.cpp; these are the flags as the
// command line meets them -- the parse, the refusals, the wording.

namespace {

using apogee::harness::Channel;

/// A throwaway home directory and no APOGEE_HOME: the baked rung decides, and
/// nothing a run writes can reach the developer's own install.
struct Sandbox {
    apogee::testing::TempDir home{"root-home-" + std::to_string(std::random_device{}())};
    apogee::testing::TempDir elsewhere{"root-elsewhere-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvUnsetGuard no_override{"APOGEE_HOME"};
    apogee::testing::EnvUnsetGuard no_config{"APOGEE_CONFIG"};
    apogee::testing::EnvGuard user_home{"HOME", home.path().string()};
    apogee::testing::EnvGuard profile{"USERPROFILE", home.path().string()};
};

/// What a run printed, and how it ended.
struct Ran {
    int code = -1;
    std::string out;
    std::string err;
};

/// Runs `args` through `root` with stdout and stderr captured.
Ran run_captured(RootCommand& root, const std::vector<std::string>& args) {
    const std::ostringstream out;
    const std::ostringstream err;
    std::streambuf* old_out = std::cout.rdbuf(out.rdbuf());
    std::streambuf* old_err = std::cerr.rdbuf(err.rdbuf());
    Ran ran;
    try {
        ran.code = run_with(root, args);
    } catch (...) {
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
        throw;
    }
    std::cout.rdbuf(old_out);
    std::cerr.rdbuf(old_err);
    ran.out = out.str();
    ran.err = err.str();
    return ran;
}

/// The real command set, run once.
Ran run_default(const std::vector<std::string>& args) {
    RootCommand root;
    return run_captured(root, args);
}

/// A root over one fake command, `probe`, which records the root it saw.
struct Probed {
    const FakeCommand* probe = nullptr;
    std::unique_ptr<RootCommand> root;

    Probed() {
        auto owned = std::make_unique<FakeCommand>("probe", "records the root");
        probe = owned.get();
        CommandRegistry registry;
        registry.add(std::move(owned));
        root = std::make_unique<RootCommand>(std::move(registry));
    }
};

[[nodiscard]] bool says(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

/// Whether `text` holds `line` as a whole line.
[[nodiscard]] bool has_line(const std::string& text, const std::string& line) {
    std::istringstream lines{text};
    for (std::string each; std::getline(lines, each);) {
        if (each == line) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("the root flags are offered, and help says how they relate to the config flag",
          "[commands][root][channels]") {
    const RootCommand root;
    const std::string help = root.app().help();

    for (const std::string_view flag : {"--release", "--dev", "--test", "--custom"}) {
        INFO(flag);
        CHECK(says(help, std::string{flag}));
    }
    CHECK(says(help, "~/.apogee-dev"));
    CHECK(says(help, "<root>/config/<file>"));
    // `--config` names a file; `--custom` names a file and re-roots to match.
    CHECK(says(help, "Names a file only"));
    CHECK(says(help, "--custom names both"));
}

TEST_CASE("the root flags exclude one another", "[commands][root][channels]") {
    const Sandbox sandbox;
    const std::string custom = (sandbox.elsewhere.path() / "config" / "config.yaml").string();
    const std::vector<std::vector<std::string>> pairs{{"--dev", "--test"},
                                                      {"--dev", "--release"},
                                                      {"--test", "--release"},
                                                      {"--dev", "--custom", custom},
                                                      {"--release", "--custom", custom}};
    for (const std::vector<std::string>& pair : pairs) {
        INFO(pair.front() << " " << pair.at(1));
        Probed probed;
        std::vector<std::string> args = pair;
        args.emplace_back("probe");
        CHECK(run_captured(*probed.root, args).code != 0);
        CHECK(probed.probe->invocations() == 0);
    }
}

TEST_CASE("a channel flag roots the run at that channel's directory, and only that run",
          "[commands][root][channels]") {
    const Sandbox sandbox;
    for (const Channel channel : apogee::harness::kChannels) {
        const std::string flag{apogee::harness::channel_flag(channel)};
        INFO(flag);
        {
            Probed probed;
            REQUIRE(run_captured(*probed.root, {flag, "probe"}).code == 0);
            const apogee::harness::RootResolution& seen = probed.probe->observed_root();
            REQUIRE(seen.ok());
            CHECK(seen.root ==
                  sandbox.home.path() / std::string{apogee::harness::channel_directory(channel)});
            CHECK(seen.rung == apogee::harness::RootRung::Flag);
        }
        // The run over, the flag is gone, and it was written nowhere.
        CHECK_FALSE(apogee::harness::root_flag().has_value());
        CHECK(std::filesystem::is_empty(sandbox.home.path()));
    }
}

TEST_CASE("a second parse of the same root starts from the chain again",
          "[commands][root][channels]") {
    // The flag is what this parse was given, never a value an earlier parse
    // left bound.
    const Sandbox sandbox;
    Probed probed;
    REQUIRE(run_captured(*probed.root, {"--test", "probe"}).code == 0);
    CHECK(probed.probe->observed_root().rung == apogee::harness::RootRung::Flag);

    REQUIRE(run_captured(*probed.root, {"probe"}).code == 0);
    CHECK(probed.probe->observed_root().rung == apogee::harness::RootRung::Baked);
    CHECK(probed.probe->observed_root().root == sandbox.home.path() / ".apogee");
    CHECK_FALSE(apogee::harness::root_flag().has_value());
}

TEST_CASE("a root flag and a disagreeing APOGEE_HOME are refused before any command runs",
          "[commands][root][channels]") {
    const Sandbox sandbox;
    const apogee::testing::EnvGuard other{"APOGEE_HOME", sandbox.elsewhere.path().string()};

    Probed probed;
    const Ran ran = run_captured(*probed.root, {"--test", "probe"});
    CHECK(ran.code == 1);
    CHECK(probed.probe->invocations() == 0);
    // Both named: the flag with its root, and the variable with its value.
    CHECK(says(ran.err, "--test"));
    CHECK(says(ran.err, (sandbox.home.path() / ".apogee-test").string()));
    CHECK(says(ran.err, "APOGEE_HOME"));
    CHECK(says(ran.err, sandbox.elsewhere.path().string()));
}

TEST_CASE("an APOGEE_HOME agreeing with the flag is no conflict", "[commands][root][channels]") {
    const Sandbox sandbox;
    const apogee::testing::EnvGuard same{"APOGEE_HOME",
                                         (sandbox.home.path() / ".apogee-dev").string()};
    Probed probed;
    REQUIRE(run_captured(*probed.root, {"--dev", "probe"}).code == 0);
    CHECK(probed.probe->observed_root().rung == apogee::harness::RootRung::Flag);
}

TEST_CASE("the custom flag roots the run where its config file sits, and reads that file",
          "[commands][root][channels][custom]") {
    const Sandbox sandbox;
    const std::filesystem::path config = sandbox.elsewhere.path() / "config" / "mine.yaml";

    Probed probed;
    REQUIRE(run_captured(*probed.root, {"--custom", config.string(), "probe"}).code == 0);
    const apogee::harness::RootResolution& seen = probed.probe->observed_root();
    REQUIRE(seen.ok());
    CHECK(apogee::harness::same_path(seen.root, sandbox.elsewhere.path()));
    CHECK(apogee::harness::same_path(seen.config, config));
    // `--config` keeps its one meaning: the flag's file is the default the
    // root resolves, not a value in the context.
    CHECK(probed.probe->observed_config_path().empty());
}

TEST_CASE("the custom flag and a config flag naming another file are refused, both named",
          "[commands][root][channels][custom]") {
    const Sandbox sandbox;
    const std::filesystem::path config = sandbox.elsewhere.path() / "config" / "config.yaml";
    const std::filesystem::path other = sandbox.home.path() / "other.yaml";

    Probed refused;
    const Ran ran = run_captured(
        *refused.root, {"--custom", config.string(), "--config", other.string(), "probe"});
    CHECK(ran.code == 1);
    CHECK(refused.probe->invocations() == 0);
    CHECK(says(ran.err, "--custom"));
    CHECK(says(ran.err, "--config"));
    CHECK(says(ran.err, other.string()));

    // The same file twice is one answer, not two.
    Probed agreed;
    CHECK(run_captured(*agreed.root,
                       {"--custom", config.string(), "--config", config.string(), "probe"})
              .code == 0);
}

TEST_CASE("an APOGEE_CONFIG naming another file than the custom flag is refused by name",
          "[commands][root][channels][custom]") {
    const Sandbox sandbox;
    const std::filesystem::path config = sandbox.elsewhere.path() / "config" / "config.yaml";
    const apogee::testing::EnvGuard env_config{"APOGEE_CONFIG",
                                               (sandbox.home.path() / "z.yaml").string()};
    Probed probed;
    const Ran ran = run_captured(*probed.root, {"--custom", config.string(), "probe"});
    CHECK(ran.code == 1);
    CHECK(says(ran.err, "APOGEE_CONFIG"));
}

TEST_CASE("a channel flag beside the config flag reads that file over the channel's root",
          "[commands][root][channels]") {
    // A channel flag claims a root and no file, so a `--config` beside it is
    // no second answer: the file is read, the data lives in the channel's
    // root -- as `--config` has always composed with APOGEE_HOME.
    const Sandbox sandbox;
    const std::filesystem::path config = sandbox.elsewhere.path() / "mine.yaml";

    Probed probed;
    REQUIRE(run_captured(*probed.root, {"--dev", "--config", config.string(), "probe"}).code == 0);
    CHECK(probed.probe->observed_root().root == sandbox.home.path() / ".apogee-dev");
    CHECK(probed.probe->observed_config_path() == config.string());
}

TEST_CASE("a custom config file off the layout's shape is refused naming the shape",
          "[commands][root][channels][custom]") {
    const Sandbox sandbox;
    Probed probed;
    const Ran ran = run_captured(
        *probed.root, {"--custom", (sandbox.elsewhere.path() / "mine.yaml").string(), "probe"});
    CHECK(ran.code == 1);
    CHECK(probed.probe->invocations() == 0);
    CHECK(says(ran.err, "<root>/config/<file>"));
}

TEST_CASE("version names the channel, the root and the rung that chose it",
          "[commands][root][channels][version]") {
    const Sandbox sandbox;
    const std::string first = "apogee " + std::string{apogee::version::semantic()};

    SECTION("a release build, no flag") {
        const Ran ran = run_default({"version"});
        REQUIRE(ran.code == 0);
        CHECK(ran.out.starts_with(first));  // line one stays what the release scripts read
        CHECK(has_line(ran.out, "channel: release"));
        CHECK(has_line(ran.out, "root: " + (sandbox.home.path() / ".apogee").string() +
                                    " (the release channel's own root, baked into this build)"));
    }
    SECTION("a dev build, no flag") {
        const apogee::testing::BakedChannelGuard dev{Channel::Dev};
        const Ran ran = run_default({"version"});
        CHECK(has_line(ran.out, "channel: dev"));
        CHECK(has_line(ran.out, "root: " + (sandbox.home.path() / ".apogee-dev").string() +
                                    " (the dev channel's own root, baked into this build)"));
    }
    SECTION("a test build, no flag") {
        const apogee::testing::BakedChannelGuard test{Channel::Test};
        const Ran ran = run_default({"version"});
        CHECK(has_line(ran.out, "channel: test"));
        CHECK(has_line(ran.out, "root: " + (sandbox.home.path() / ".apogee-test").string() +
                                    " (the test channel's own root, baked into this build)"));
    }
    SECTION("a channel flag") {
        const Ran ran = run_default({"--test", "version"});
        CHECK(has_line(ran.out, "channel: release"));
        CHECK(has_line(ran.out, "root: " + (sandbox.home.path() / ".apogee-test").string() +
                                    " (set by --test)"));
    }
    SECTION("APOGEE_HOME") {
        const apogee::testing::EnvGuard override_root{"APOGEE_HOME",
                                                      sandbox.elsewhere.path().string()};
        const Ran ran = run_default({"version"});
        CHECK(has_line(ran.out,
                       "root: " + sandbox.elsewhere.path().string() + " (set by APOGEE_HOME)"));
    }
    SECTION("the custom flag, and the file it reads") {
        const std::filesystem::path config = sandbox.elsewhere.path() / "config" / "config.yaml";
        const Ran ran = run_default({"--custom", config.string(), "version"});
        const apogee::harness::CustomRoot custom = apogee::harness::custom_root(config);
        CHECK(has_line(ran.out, "root: " + custom.root.string() + " (set by --custom " +
                                    config.string() + ")"));
        CHECK(has_line(ran.out, "config: " + custom.config.string()));
    }
    SECTION("the version flag names the root the run's flag chose") {
        const Ran ran = run_default({"--dev", "--version"});
        REQUIRE(ran.code == 0);
        CHECK(ran.out.starts_with(first));
        CHECK(has_line(ran.out, "root: " + (sandbox.home.path() / ".apogee-dev").string() +
                                    " (set by --dev)"));
    }
    // Saying which root is not touching it.
    CHECK(std::filesystem::is_empty(sandbox.home.path()));
}
