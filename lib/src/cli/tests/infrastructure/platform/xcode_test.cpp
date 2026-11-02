#include "platform/xcode.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <filesystem>
#include <optional>
#include <random>
#include <string>

#include "platform/child_process.h"
#include "support/env_guard.h"
#include "support/fake_xcode.h"
#include "support/terminal_output.h"

/// The Xcode probe (56d): what `xcodebuild -version` says read, versions
/// compared, a developer directory told from the Command Line Tools', and the
/// probe itself over stub `xcode-select` and `xcrun` scripts on a `PATH`
/// without `/usr/bin` -- every case's calls logged, nothing past the bound,
/// `xcrun` never run where no Xcode is selected, and nothing the stubs say on
/// stderr reaching this process's own descriptors.
namespace {

using apogee::platform::CapturedLimits;
using apogee::platform::XcodeFacts;
using apogee::platform::XcodeInstall;
using apogee::testing::FakeXcode;
using apogee::testing::terminal_output;
using Catch::Matchers::ContainsSubstring;

[[nodiscard]] std::optional<XcodeFacts> probe(
    std::string& error, std::chrono::milliseconds bound = std::chrono::seconds{5}) {
    return apogee::platform::xcode_facts(CapturedLimits{.timeout = bound}, error);
}

}  // namespace

TEST_CASE("xcodebuild -version is read for its version and build", "[platform][xcode]") {
    const auto read =
        apogee::platform::parse_xcodebuild_version("Xcode 27.1\nBuild version 27A9275\n");
    REQUIRE(read.has_value());
    CHECK(read->version == "27.1");
    CHECK(read->build == "27A9275");
    // CRLF, white space, a line of noise before it.
    const auto spaced = apogee::platform::parse_xcodebuild_version(
        "note: something\r\n  Xcode 26.4.1  \r\nBuild version 17E202\r\n");
    REQUIRE(spaced.has_value());
    CHECK(spaced->version == "26.4.1");
    CHECK(spaced->build == "17E202");
    // No version, or one with no number: nothing read.
    CHECK_FALSE(apogee::platform::parse_xcodebuild_version("Build version 27A9275\n"));
    CHECK_FALSE(apogee::platform::parse_xcodebuild_version("Xcode beta\n"));
    CHECK_FALSE(apogee::platform::parse_xcodebuild_version(""));
}

TEST_CASE("Xcode versions compare by number, never as text", "[platform][xcode]") {
    using apogee::platform::xcode_at_least;
    CHECK(xcode_at_least("27.1", 26, 6));
    CHECK(xcode_at_least("26.6", 26, 6));
    CHECK(xcode_at_least("26.10", 26, 6));
    CHECK_FALSE(xcode_at_least("26.4", 26, 6));
    CHECK(xcode_at_least("26.4", 26, 3));
    CHECK(xcode_at_least("26.3.1", 26, 3));
    CHECK_FALSE(xcode_at_least("26.2", 26, 3));
    CHECK(xcode_at_least("27", 26, 6));
    CHECK_FALSE(xcode_at_least("26", 26, 3));
    CHECK_FALSE(xcode_at_least("9.4", 26, 3));
    CHECK_FALSE(xcode_at_least("", 0, 0));
    CHECK_FALSE(xcode_at_least("beta", 0, 0));
}

TEST_CASE("an Xcode's developer directory is told from the Command Line Tools'",
          "[platform][xcode]") {
    using apogee::platform::is_xcode_developer_dir;
    CHECK(is_xcode_developer_dir("/Applications/Xcode.app/Contents/Developer"));
    CHECK(is_xcode_developer_dir("/Applications/Xcode-beta.app/Contents/Developer/"));
    CHECK_FALSE(is_xcode_developer_dir("/Library/Developer/CommandLineTools"));
    CHECK_FALSE(is_xcode_developer_dir("/Applications/Xcode.app"));
    CHECK_FALSE(is_xcode_developer_dir("/opt/Contents/Developer"));
}

TEST_CASE("the probe asks xcode-select first, then xcrun for an Xcode's version and bridge",
          "[platform][xcode][child]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    const FakeXcode fake;
    fake.xcode("27.1", "27A9275", true);
    std::string error;
    const std::optional<XcodeFacts> facts = probe(error);
    REQUIRE(facts.has_value());
    CHECK(error.empty());
    CHECK(facts->install == XcodeInstall::Xcode);
    CHECK(facts->developer_dir == fake.developer_dir());
    CHECK(facts->version == "27.1");
    CHECK(facts->build == "27A9275");
    CHECK(facts->mcpbridge == fake.developer_dir() / "usr" / "bin" / "mcpbridge");
    CHECK(fake.logged() == "xcode-select -p\nxcrun xcodebuild -version\nxcrun --find mcpbridge\n");

    // An Xcode before 26.3: no bridge, still an Xcode.
    fake.xcode("26.2", "17C52", false);
    const std::optional<XcodeFacts> old = probe(error);
    REQUIRE(old.has_value());
    CHECK(old->install == XcodeInstall::Xcode);
    CHECK(old->version == "26.2");
    CHECK(old->mcpbridge.empty());
}

TEST_CASE("the probe's children say nothing on this process's descriptors",
          "[platform][xcode][child]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    const FakeXcode fake;
    fake.xcode("27.1", "27A9275", true);
    // The control: a stub run with this process's descriptors inherited is
    // seen there, so a probe child that inherited them would be too.
    apogee::platform::ChildCommand inherited;
    inherited.program = (fake.bin() / "xcrun").string();
    inherited.arguments = {"--find", "mcpbridge"};
    std::string error;
    const std::string control =
        terminal_output([&] { (void)apogee::platform::run_foreground(inherited, error); });
    CHECK_THAT(control, ContainsSubstring("xcrun noise nobody should see"));

    // Every one of the probe's three children speaks on stderr: none of it
    // reaches descriptor 1 or 2, an Xcode's probe or an absent one's.
    std::optional<XcodeFacts> facts;
    const std::string said = terminal_output([&] { facts = probe(error); });
    REQUIRE(facts.has_value());
    CHECK(facts->install == XcodeInstall::Xcode);
    CHECK(fake.logged() ==
          "xcrun --find mcpbridge\n"
          "xcode-select -p\nxcrun xcodebuild -version\nxcrun --find mcpbridge\n");
    CHECK_THAT(said, !ContainsSubstring("noise nobody should see"));

    const FakeXcode none;
    const std::string absent = terminal_output([&] { facts = probe(error); });
    REQUIRE(facts.has_value());
    CHECK(facts->install == XcodeInstall::None);
    CHECK(none.logged() == "xcode-select -p\n");
    CHECK_THAT(absent, !ContainsSubstring("noise nobody should see"));
}

TEST_CASE("with no Xcode selected, xcrun is never run", "[platform][xcode][child]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    std::string error;
    {
        // Nothing selected: xcode-select fails, and that is an answer.
        const FakeXcode fake;
        const std::optional<XcodeFacts> none = probe(error);
        REQUIRE(none.has_value());
        CHECK(none->install == XcodeInstall::None);
        CHECK(fake.logged() == "xcode-select -p\n");
    }
    {
        // The Command Line Tools: never an xcrun, which would refuse.
        const FakeXcode fake;
        fake.command_line_tools();
        const std::optional<XcodeFacts> tools = probe(error);
        REQUIRE(tools.has_value());
        CHECK(tools->install == XcodeInstall::CommandLineTools);
        CHECK_THAT(tools->developer_dir.string(), ContainsSubstring("CommandLineTools"));
        CHECK(fake.logged() == "xcode-select -p\n");
    }
    {
        // An Xcode deleted while still selected: the directory is checked
        // before any xcrun, whose shims would offer to install the tools.
        const FakeXcode fake;
        fake.deleted();
        const std::optional<XcodeFacts> gone = probe(error);
        REQUIRE(gone.has_value());
        CHECK(gone->install == XcodeInstall::Missing);
        CHECK(fake.logged() == "xcode-select -p\n");
    }
}

TEST_CASE("a tool that never answers is stopped at the bound, and said",
          "[platform][xcode][child]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    for (const char* program : {"xcode-select", "xcrun"}) {
        INFO(program);
        const FakeXcode fake;
        fake.xcode("27.1", "27A9275", true);
        fake.slow(program);
        std::string error;
        const auto started = std::chrono::steady_clock::now();
        const std::optional<XcodeFacts> facts = probe(error, std::chrono::seconds{1});
        const auto took = std::chrono::steady_clock::now() - started;
        CHECK_FALSE(facts.has_value());
        CHECK_THAT(error, ContainsSubstring(std::string{program} + " took longer than 1 seconds"));
        CHECK(took < std::chrono::seconds{4});
    }
}

TEST_CASE("an xcode-select not on PATH is a probe that failed, said by name",
          "[platform][xcode][child]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    const apogee::testing::TempDir empty{"no-xcode-" + std::to_string(std::random_device{}())};
    const apogee::testing::EnvGuard path{"PATH", empty.path().string()};
    std::string error;
    CHECK_FALSE(probe(error).has_value());
    CHECK_THAT(error, ContainsSubstring("xcode-select"));
    CHECK_THAT(error, ContainsSubstring("not found on PATH"));
}

TEST_CASE("a captured run keeps a child's stderr as a tail and its status", "[platform][child]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    // The seam's one bounded run (56d), which ffmpeg's runs and the Xcode
    // probe share: stdout whole, stderr a tail never written anywhere, the
    // status, and a failure's last line in the error.
    apogee::platform::ChildCommand command;
    command.program = "/bin/sh";
    command.arguments = {"-c", "echo out; echo first >&2; echo last >&2; exit 3"};
    const apogee::platform::CapturedRun ran =
        apogee::platform::run_captured(command, CapturedLimits{.timeout = std::chrono::seconds{5}});
    CHECK(ran.out == "out\n");
    CHECK(ran.tail == "first\nlast\n");
    REQUIRE(ran.status.has_value());
    CHECK(*ran.status == 3);
    CHECK(ran.error == "/bin/sh failed: last");

    command.arguments = {"-c", "printf ok"};
    const apogee::platform::CapturedRun clean =
        apogee::platform::run_captured(command, CapturedLimits{.timeout = std::chrono::seconds{5}});
    CHECK(clean.out == "ok");
    CHECK(clean.status == 0);
    CHECK(clean.error.empty());
}
