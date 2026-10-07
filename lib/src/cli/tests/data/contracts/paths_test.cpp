#include "contracts/paths.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/layout.h"
#include "support/channel_guard.h"
#include "support/env_guard.h"

using apogee::harness::apogee_home;
using apogee::testing::EnvGuard;
using apogee::testing::EnvUnsetGuard;

TEST_CASE("APOGEE_HOME relocates the whole tree", "[paths]") {
    const EnvGuard home{"APOGEE_HOME", "/tmp/apogee-somewhere"};

    CHECK(apogee_home() == std::filesystem::path{"/tmp/apogee-somewhere"});
    CHECK(apogee::harness::config_dir() == std::filesystem::path{"/tmp/apogee-somewhere/config"});
    CHECK(apogee::harness::default_config_path() ==
          std::filesystem::path{"/tmp/apogee-somewhere/config/config.yaml"});
}

TEST_CASE("without the override the tree is ~/.apogee", "[paths]") {
    const EnvUnsetGuard no_override{"APOGEE_HOME"};
    const EnvGuard home{"HOME", "/home/tester"};

#if !defined(_WIN32)
    CHECK(apogee_home() == std::filesystem::path{"/home/tester/.apogee"});
    CHECK(apogee::harness::default_config_path() ==
          std::filesystem::path{"/home/tester/.apogee/config/config.yaml"});
#else
    SUCCEED("HOME is not how Windows reports a home directory");
#endif
}

TEST_CASE("an empty APOGEE_HOME is ignored rather than rooting at the filesystem root", "[paths]") {
    // Treating "" as a valid root would put the config at /config/config.yaml.
    const EnvGuard empty{"APOGEE_HOME", ""};
    const EnvGuard home{"HOME", "/home/tester"};

#if !defined(_WIN32)
    CHECK(apogee_home() == std::filesystem::path{"/home/tester/.apogee"});
#else
    SUCCEED("HOME is not how Windows reports a home directory");
#endif
}

TEST_CASE("no home and no override is an error, not a guess", "[paths]") {
    // The caller is about to write here. A plausible-but-wrong directory is
    // worse than a message naming the fix.
    const EnvUnsetGuard no_override{"APOGEE_HOME"};
    const EnvUnsetGuard no_home{"HOME"};
    const EnvUnsetGuard no_profile{"USERPROFILE"};
    const EnvUnsetGuard no_drive{"HOMEDRIVE"};

    try {
        (void)apogee_home();
        FAIL("expected a runtime_error");
    } catch (const std::runtime_error& e) {
        CHECK(std::string{e.what()}.find("APOGEE_HOME") != std::string::npos);
    }
}

TEST_CASE("an explicit --config wins over the default location", "[paths]") {
    const EnvGuard home{"APOGEE_HOME", "/tmp/apogee-somewhere"};

    CHECK(apogee::harness::resolve_config_path("/elsewhere/mine.yaml") ==
          std::filesystem::path{"/elsewhere/mine.yaml"});
    CHECK(apogee::harness::resolve_config_path("") ==
          std::filesystem::path{"/tmp/apogee-somewhere/config/config.yaml"});
}

// --- The root chain (M10) ---------------------------------------------------
//
// Every rung alone and every pairing, through the pure resolver; then the
// process-level pins -- the re-root, the scope, uninstall's root -- against
// throwaway homes.

namespace {

using apogee::harness::Channel;
using apogee::harness::RootFlag;
using apogee::harness::RootInputs;
using apogee::harness::RootResolution;
using apogee::harness::RootRung;

[[nodiscard]] RootFlag flag_for(Channel channel) {
    return RootFlag{.channel = channel, .custom_config = {}};
}

[[nodiscard]] RootFlag custom_flag(const std::filesystem::path& config) {
    return RootFlag{.channel = std::nullopt, .custom_config = config};
}

[[nodiscard]] RootInputs at_home(Channel baked) {
    return RootInputs{.flag = std::nullopt,
                      .environment = {},
                      .channel = baked,
                      .home_directory = "/home/tester"};
}

[[nodiscard]] std::filesystem::path home_root(Channel channel) {
    return std::filesystem::path{"/home/tester"} /
           std::string{apogee::harness::channel_directory(channel)};
}

[[nodiscard]] bool says(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

/// Whether `path` sits inside `root`.
[[nodiscard]] bool under(const std::filesystem::path& path, const std::filesystem::path& root) {
    const std::filesystem::path relative = path.lexically_relative(root);
    return !relative.empty() && *relative.begin() != "..";
}

/// A throwaway home directory and no APOGEE_HOME, so the baked rung decides --
/// and a second directory for a flag to point somewhere else.
struct Sandbox {
    apogee::testing::TempDir home{"paths-home-" + std::to_string(std::random_device{}())};
    apogee::testing::TempDir elsewhere{"paths-elsewhere-" + std::to_string(std::random_device{}())};
    EnvUnsetGuard no_override{"APOGEE_HOME"};
    EnvGuard user_home{"HOME", home.path().string()};
    EnvGuard profile{"USERPROFILE", home.path().string()};
};

}  // namespace

TEST_CASE("the channel names are the three the build and MODE take", "[paths][channels]") {
    // Compile-tested: main.cpp holds its stamp to these with the same call.
    static_assert(apogee::harness::parse_channel("release") == Channel::Release);
    static_assert(apogee::harness::parse_channel("dev") == Channel::Dev);
    static_assert(apogee::harness::parse_channel("test") == Channel::Test);
    static_assert(!apogee::harness::parse_channel("Dev").has_value());
    static_assert(!apogee::harness::parse_channel("").has_value());

    for (const Channel channel : apogee::harness::kChannels) {
        CHECK(apogee::harness::parse_channel(apogee::harness::channel_name(channel)) == channel);
    }
    CHECK(apogee::harness::channel_directory(Channel::Release) == ".apogee");
    CHECK(apogee::harness::channel_directory(Channel::Dev) == ".apogee-dev");
    CHECK(apogee::harness::channel_directory(Channel::Test) == ".apogee-test");
    CHECK(apogee::harness::channel_flag(Channel::Release) == "--release");
    CHECK(apogee::harness::channel_flag(Channel::Dev) == "--dev");
    CHECK(apogee::harness::channel_flag(Channel::Test) == "--test");
}

TEST_CASE("with no flag and no APOGEE_HOME each channel's build owns its own root",
          "[paths][channels]") {
    for (const Channel channel : apogee::harness::kChannels) {
        INFO(apogee::harness::channel_name(channel));
        const RootResolution resolved = apogee::harness::resolve_root(at_home(channel));
        REQUIRE(resolved.ok());
        CHECK(resolved.root == home_root(channel));
        CHECK(resolved.config == home_root(channel) / "config" / "config.yaml");
        CHECK(resolved.rung == RootRung::Baked);
        CHECK(resolved.channel == channel);
        CHECK(apogee::harness::root_reason(resolved) ==
              "the " + std::string{apogee::harness::channel_name(channel)} +
                  " channel's own root, baked into this build");
    }
}

TEST_CASE("APOGEE_HOME beats the baked channel, whichever it is", "[paths][channels]") {
    for (const Channel channel : apogee::harness::kChannels) {
        INFO(apogee::harness::channel_name(channel));
        RootInputs inputs = at_home(channel);
        inputs.environment = "/srv/apogee";
        const RootResolution resolved = apogee::harness::resolve_root(inputs);
        REQUIRE(resolved.ok());
        // Exactly as before channels existed: the value, as given.
        CHECK(resolved.root == std::filesystem::path{"/srv/apogee"});
        CHECK(resolved.rung == RootRung::Environment);
        CHECK(apogee::harness::root_reason(resolved) == "set by APOGEE_HOME");
    }
}

TEST_CASE("a channel flag beats the baked channel, every flag against every build",
          "[paths][channels]") {
    for (const Channel baked : apogee::harness::kChannels) {
        for (const Channel asked : apogee::harness::kChannels) {
            INFO(apogee::harness::channel_name(baked)
                 << " build, " << apogee::harness::channel_flag(asked));
            RootInputs inputs = at_home(baked);
            inputs.flag = flag_for(asked);
            const RootResolution resolved = apogee::harness::resolve_root(inputs);
            REQUIRE(resolved.ok());
            CHECK(resolved.root == home_root(asked));
            CHECK(resolved.config == home_root(asked) / "config" / "config.yaml");
            CHECK(resolved.rung == RootRung::Flag);
            // The channel is the build's, whatever root a flag points it at.
            CHECK(resolved.channel == baked);
            CHECK(apogee::harness::root_reason(resolved) ==
                  "set by " + std::string{apogee::harness::channel_flag(asked)});
        }
    }
}

TEST_CASE("a flag and an APOGEE_HOME naming the same root agree, and the flag is credited",
          "[paths][channels]") {
    for (const std::string& environment :
         {home_root(Channel::Dev).string(), home_root(Channel::Dev).string() + "/"}) {
        INFO(environment);
        RootInputs inputs = at_home(Channel::Release);
        inputs.flag = flag_for(Channel::Dev);
        inputs.environment = environment;
        const RootResolution resolved = apogee::harness::resolve_root(inputs);
        REQUIRE(resolved.ok());
        CHECK(resolved.root == home_root(Channel::Dev));
        CHECK(resolved.rung == RootRung::Flag);
    }

    const std::filesystem::path config = std::filesystem::absolute("/elsewhere/config/config.yaml");
    RootInputs inputs = at_home(Channel::Release);
    inputs.flag = custom_flag(config);
    inputs.environment = config.parent_path().parent_path().string();
    const RootResolution resolved = apogee::harness::resolve_root(inputs);
    REQUIRE(resolved.ok());
    CHECK(resolved.rung == RootRung::Flag);
}

TEST_CASE("a flag and an APOGEE_HOME that disagree are refused, both named", "[paths][channels]") {
    std::vector<RootFlag> flags;
    flags.reserve(apogee::harness::kChannels.size() + 1);
    for (const Channel channel : apogee::harness::kChannels) {
        flags.push_back(flag_for(channel));
    }
    flags.push_back(custom_flag("/elsewhere/config/config.yaml"));

    for (const RootFlag& flag : flags) {
        INFO(flag.spelling());
        RootInputs inputs = at_home(Channel::Release);
        inputs.flag = flag;
        inputs.environment = "/srv/other";
        const RootResolution resolved = apogee::harness::resolve_root(inputs);
        CHECK_FALSE(resolved.ok());
        CHECK(resolved.root.empty());
        CHECK(says(resolved.error, flag.spelling()));
        CHECK(says(resolved.error, "APOGEE_HOME"));
        CHECK(says(resolved.error, "/srv/other"));
    }
}

TEST_CASE("an empty APOGEE_HOME is no override beside a flag either", "[paths][channels]") {
    RootInputs inputs = at_home(Channel::Release);
    inputs.flag = flag_for(Channel::Test);
    inputs.environment = "";
    const RootResolution resolved = apogee::harness::resolve_root(inputs);
    REQUIRE(resolved.ok());
    CHECK(resolved.root == home_root(Channel::Test));
}

TEST_CASE("a channel's root needs a home directory, and the custom flag and APOGEE_HOME do not",
          "[paths][channels]") {
    RootInputs inputs = at_home(Channel::Dev);
    inputs.home_directory.reset();

    const RootResolution baked = apogee::harness::resolve_root(inputs);
    CHECK_FALSE(baked.ok());
    CHECK(says(baked.error, "APOGEE_HOME"));

    inputs.flag = flag_for(Channel::Test);
    const RootResolution flagged = apogee::harness::resolve_root(inputs);
    CHECK_FALSE(flagged.ok());
    CHECK(says(flagged.error, "--test"));
    CHECK(says(flagged.error, "--custom"));

    inputs.flag = custom_flag("/elsewhere/config/config.yaml");
    CHECK(apogee::harness::resolve_root(inputs).ok());

    inputs.flag.reset();
    inputs.environment = "/srv/apogee";
    CHECK(apogee::harness::resolve_root(inputs).ok());
}

TEST_CASE("the custom flag derives its root from the config file's layout position",
          "[paths][channels][custom]") {
    const std::filesystem::path root = std::filesystem::absolute("/elsewhere");

    for (const std::string_view file : {"config.yaml", "mine.yaml"}) {
        INFO(file);
        const apogee::harness::CustomRoot custom =
            apogee::harness::custom_root(root / "config" / std::string{file});
        REQUIRE(custom.error.empty());
        CHECK(custom.root == root);
        CHECK(custom.config == root / "config" / std::string{file});
    }

    // Normalized, and made absolute against the working directory.
    const apogee::harness::CustomRoot dotted =
        apogee::harness::custom_root(root / "sub" / ".." / "config" / "config.yaml");
    REQUIRE(dotted.error.empty());
    CHECK(dotted.root == root);

    const apogee::harness::CustomRoot relative =
        apogee::harness::custom_root(std::filesystem::path{"x"} / "config" / "config.yaml");
    REQUIRE(relative.error.empty());
    CHECK(relative.root == std::filesystem::current_path() / "x");

    // The flag's resolution reads the file it named, not config.yaml beside it.
    RootInputs inputs = at_home(Channel::Release);
    inputs.flag = custom_flag(root / "config" / "mine.yaml");
    const RootResolution resolved = apogee::harness::resolve_root(inputs);
    REQUIRE(resolved.ok());
    CHECK(resolved.root == root);
    CHECK(resolved.config == root / "config" / "mine.yaml");
    CHECK(apogee::harness::root_reason(resolved) ==
          "set by --custom " + (root / "config" / "mine.yaml").string());
}

TEST_CASE("a custom config file off the layout's shape is refused, naming the shape",
          "[paths][channels][custom]") {
    const std::filesystem::path root = std::filesystem::absolute("/elsewhere");

    const apogee::harness::CustomRoot loose = apogee::harness::custom_root(root / "mine.yaml");
    CHECK(loose.root.empty());
    CHECK(says(loose.error, "<root>/config/<file>"));
    // And where it would have to sit to root what it seems to mean, spelled
    // as the refusal spells it: absolute and lexically normal. `root` alone
    // is not always that -- on Windows libc++ makes "/elsewhere" absolute as
    // "C:/elsewhere", so appending gives "C:/elsewhere\config\mine.yaml",
    // which normal form writes with one separator throughout.
    CHECK(says(loose.error, (root / "config" / "mine.yaml").lexically_normal().string()));

    const apogee::testing::TempDir directory{"paths-custom-dir-" +
                                             std::to_string(std::random_device{}())};
    const apogee::harness::CustomRoot folder = apogee::harness::custom_root(directory.path());
    CHECK(says(folder.error, "directory"));

    const apogee::harness::CustomRoot top =
        apogee::harness::custom_root(std::filesystem::absolute("/config/config.yaml"));
    CHECK(says(top.error, "top of the filesystem"));

    CHECK_FALSE(apogee::harness::custom_root("").error.empty());

    RootInputs inputs = at_home(Channel::Release);
    inputs.flag = custom_flag(root / "mine.yaml");
    const RootResolution resolved = apogee::harness::resolve_root(inputs);
    CHECK_FALSE(resolved.ok());
    CHECK(resolved.root.empty());  // never half-rooted
}

TEST_CASE("a root flag in force re-roots every layout row", "[paths][channels][custom]") {
    // The re-root pin: enumerated from layout.h's own rows, so a row added
    // later cannot half-escape a flag -- the no-second-list discipline.
    const Sandbox sandbox;
    const std::filesystem::path config = sandbox.elsewhere.path() / "config" / "mine.yaml";
    const apogee::harness::RootFlagScope scope{custom_flag(config)};

    CHECK(apogee::harness::same_path(apogee_home(), sandbox.elsewhere.path()));
    CHECK(apogee::harness::same_path(apogee::harness::default_config_path(), config));
    CHECK(apogee::harness::same_path(apogee::harness::resolve_config_path(""), config));
    CHECK(apogee::harness::same_path(apogee::harness::config_dir(), config.parent_path()));

    REQUIRE(apogee::harness::seed_data_directory().ok());
    for (const apogee::harness::LayoutEntry& entry : apogee::harness::data_directories()) {
        INFO(entry.relative_path);
        CHECK(std::filesystem::is_directory(sandbox.elsewhere.path() / entry.relative_path));
    }
    // Nothing reached the baked root.
    CHECK_FALSE(std::filesystem::exists(sandbox.home.path() / ".apogee"));

    for (const std::filesystem::path& row :
         {apogee::harness::sessions_dir(), apogee::harness::models_dir(),
          apogee::harness::cache_dir(), apogee::harness::knowledge_dir(),
          apogee::harness::training_dir(), apogee::harness::memory_dir()}) {
        INFO(row.string());
        CHECK(under(row, apogee_home()));
    }
}

TEST_CASE("a channel flag in force roots the run at that channel's directory",
          "[paths][channels]") {
    const Sandbox sandbox;
    {
        const apogee::harness::RootFlagScope scope{flag_for(Channel::Test)};
        CHECK(apogee_home() == sandbox.home.path() / ".apogee-test");
        CHECK(apogee::harness::default_config_path() ==
              sandbox.home.path() / ".apogee-test" / "config" / "config.yaml");
        CHECK(apogee::harness::current_root().rung == RootRung::Flag);
    }
    // The scope gone, the flag is gone: a run's choice is never persisted.
    CHECK_FALSE(apogee::harness::root_flag().has_value());
    CHECK(apogee_home() == sandbox.home.path() / ".apogee");
}

TEST_CASE("a flag scope puts back the flag it replaced", "[paths][channels]") {
    const Sandbox sandbox;
    const apogee::harness::RootFlagScope outer{flag_for(Channel::Dev)};
    {
        const apogee::harness::RootFlagScope inner{flag_for(Channel::Test)};
        CHECK(apogee_home() == sandbox.home.path() / ".apogee-test");
    }
    CHECK(apogee_home() == sandbox.home.path() / ".apogee-dev");
}

TEST_CASE("the process's chain reads the baked channel", "[paths][channels]") {
    const Sandbox sandbox;
    const apogee::testing::BakedChannelGuard dev{Channel::Dev};

    CHECK(apogee::harness::baked_channel() == Channel::Dev);
    CHECK(apogee_home() == sandbox.home.path() / ".apogee-dev");
    CHECK(apogee::harness::current_root().channel == Channel::Dev);
    CHECK(apogee::harness::current_root().rung == RootRung::Baked);
}

TEST_CASE("a flag APOGEE_HOME disagrees with makes the root throw rather than guess",
          "[paths][channels]") {
    const Sandbox sandbox;
    const EnvGuard elsewhere{"APOGEE_HOME", sandbox.elsewhere.path().string()};
    const apogee::harness::RootFlagScope scope{flag_for(Channel::Dev)};

    try {
        (void)apogee_home();
        FAIL("expected a runtime_error");
    } catch (const std::runtime_error& e) {
        CHECK(says(e.what(), "--dev"));
        CHECK(says(e.what(), sandbox.elsewhere.path().string()));
    }
}

TEST_CASE("uninstall's root is the build's own, whatever flag is in force",
          "[paths][channels][uninstall]") {
    const Sandbox sandbox;
    for (const Channel baked : apogee::harness::kChannels) {
        const apogee::testing::BakedChannelGuard build{baked};
        const std::filesystem::path own =
            sandbox.home.path() / std::string{apogee::harness::channel_directory(baked)};
        INFO(apogee::harness::channel_name(baked) << " build");

        CHECK(apogee::harness::install_home() == own);
        for (const Channel asked : apogee::harness::kChannels) {
            INFO(apogee::harness::channel_flag(asked));
            const apogee::harness::RootFlagScope scope{flag_for(asked)};
            CHECK(apogee::harness::install_home() == own);
        }
        const apogee::harness::RootFlagScope custom{
            custom_flag(sandbox.elsewhere.path() / "config" / "config.yaml")};
        CHECK(apogee::harness::install_home() == own);
    }

    // APOGEE_HOME is still honored: every sandboxed uninstall stays hermetic.
    const EnvGuard override_root{"APOGEE_HOME", sandbox.elsewhere.path().string()};
    CHECK(apogee::harness::install_home() == sandbox.elsewhere.path());
}
