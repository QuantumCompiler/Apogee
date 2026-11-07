#include "platform/platform.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "support/env_guard.h"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

// The exact six names shared by `cicd.sh --platform`, the CMake presets, the
// CI matrix, and the binary. This test is what keeps them one vocabulary --
// drift here means a preset the script cannot select, or a CI leg that builds
// something other than what it claims.
constexpr std::array<std::string_view, 6> kReleaseTargets{
    "linux-x64", "linux-arm64", "macos-arm64", "windows-x64", "windows-arm64",
};

}  // namespace

TEST_CASE("host_target is one of the five release targets", "[platform]") {
    const std::string target = apogee::platform::host_target();

    const bool known = std::ranges::find(kReleaseTargets, target) != kReleaseTargets.end();
    INFO("host_target() returned: " << target);
    REQUIRE(known);
}

TEST_CASE("host_target is the OS and architecture joined by a hyphen", "[platform]") {
    const std::string expected =
        std::string{apogee::platform::to_string(apogee::platform::host_os())} + "-" +
        std::string{apogee::platform::to_string(apogee::platform::host_architecture())};

    REQUIRE(apogee::platform::host_target() == expected);
}

TEST_CASE("every OS and architecture has a name", "[platform]") {
    using apogee::platform::Architecture;
    using apogee::platform::OperatingSystem;

    REQUIRE(apogee::platform::to_string(OperatingSystem::Linux) == "linux");
    REQUIRE(apogee::platform::to_string(OperatingSystem::MacOS) == "macos");
    REQUIRE(apogee::platform::to_string(OperatingSystem::Windows) == "windows");

    REQUIRE(apogee::platform::to_string(Architecture::X64) == "x64");
    REQUIRE(apogee::platform::to_string(Architecture::Arm64) == "arm64");
}

TEST_CASE("host detection is stable across calls", "[platform]") {
    REQUIRE(apogee::platform::host_os() == apogee::platform::host_os());
    REQUIRE(apogee::platform::host_architecture() == apogee::platform::host_architecture());
    REQUIRE(apogee::platform::host_target() == apogee::platform::host_target());
}

TEST_CASE("a process is running while it runs, and an id nothing holds is not", "[platform]") {
    // What tells a live `models convert`'s staging directory from one an
    // interrupted run left behind.
    CHECK(apogee::platform::process_running(apogee::platform::current_process_id()));
    CHECK_FALSE(apogee::platform::process_running(0));
    CHECK_FALSE(apogee::platform::process_running(-1));
    CHECK_FALSE(apogee::platform::process_running(999999999));
}

TEST_CASE("local_date is the calendar day at the offset it reports", "[platform][environment]") {
    // Checked against the standard calendar rather than a fixed zone: the
    // day, the month, the year and the weekday must all be the UTC moment
    // moved by the offset the same call reported.
    using namespace std::chrono;
    for (const system_clock::time_point when :
         {system_clock::time_point{}, system_clock::time_point{seconds{1'790'000'000}},
          system_clock::now()}) {
        const apogee::platform::LocalDate date = apogee::platform::local_date(when);
        const sys_days day = floor<days>(when + minutes{date.utc_offset_minutes});
        const year_month_day calendar{day};
        CHECK(static_cast<int>(calendar.year()) == date.year);
        CHECK(static_cast<unsigned>(calendar.month()) == static_cast<unsigned>(date.month));
        CHECK(static_cast<unsigned>(calendar.day()) == static_cast<unsigned>(date.day));
        CHECK(weekday{day}.c_encoding() == static_cast<unsigned>(date.weekday));
        CHECK(date.utc_offset_minutes >= -12 * 60);
        CHECK(date.utc_offset_minutes <= 14 * 60);
    }
}

TEST_CASE("a standard stream given another buffer is not a terminal", "[platform][terminal]") {
    // What every in-process command test does to feed input and capture
    // output: the stream's buffer swapped. Then it is answered as a pipe,
    // whatever the descriptor is -- so a test run from a developer's terminal
    // neither paints into the captured output nor waits on the real keyboard.
    using apogee::platform::is_terminal;
    using apogee::platform::StandardStream;
    std::istringstream in;
    std::ostringstream out;
    std::ostringstream err;
    std::streambuf* const old_in = std::cin.rdbuf(in.rdbuf());
    std::streambuf* const old_out = std::cout.rdbuf(out.rdbuf());
    std::streambuf* const old_err = std::cerr.rdbuf(err.rdbuf());
    const bool in_terminal = is_terminal(StandardStream::In);
    const bool out_terminal = is_terminal(StandardStream::Out);
    const bool err_terminal = is_terminal(StandardStream::Err);
    std::cin.rdbuf(old_in);
    std::cout.rdbuf(old_out);
    std::cerr.rdbuf(old_err);
    CHECK_FALSE(in_terminal);
    CHECK_FALSE(out_terminal);
    CHECK_FALSE(err_terminal);
}

TEST_CASE("echo is neither hidden nor paused when standard input is no terminal",
          "[platform][terminal]") {
    // A pipe's bytes are the input, not typeahead: nothing to silence, so
    // a question asked over it has no echo to turn back on.
    std::istringstream in;
    std::streambuf* const old_in = std::cin.rdbuf(in.rdbuf());
    bool hidden = false;
    bool paused = false;
    {
        const apogee::platform::TypeaheadGuard quiet;
        const apogee::platform::EchoPause visible;
        hidden = quiet.active();
        paused = visible.paused();
    }
    std::cin.rdbuf(old_in);
    CHECK_FALSE(hidden);
    CHECK_FALSE(paused);
}

TEST_CASE("a terminal that cannot place the cursor is refused a full screen, with why",
          "[platform][terminal]") {
    if (apogee::platform::host_os() == apogee::platform::OperatingSystem::Windows) {
        // The console is asked, not TERM: under ctest stdout is no console,
        // and that is said rather than drawn into.
        CHECK_FALSE(apogee::platform::full_screen_refusal().empty());
        return;
    }
    {
        const apogee::testing::EnvGuard term{"TERM", "dumb"};
        CHECK(apogee::platform::full_screen_refusal() ==
              "this terminal (TERM=dumb) cannot place the cursor");
    }
    {
        const apogee::testing::EnvUnsetGuard term{"TERM"};
        CHECK(apogee::platform::full_screen_refusal().find("TERM is not set") == 0);
    }
    {
        const apogee::testing::EnvGuard term{"TERM", "xterm-256color"};
        CHECK(apogee::platform::full_screen_refusal().empty());
    }
}

TEST_CASE("a private directory is made where nothing was, its owner's alone, never found",
          "[platform][private]") {
    // 57a: a private chat's folder is claimed, never taken over -- a
    // directory, file or link already under the name is refused, and what is
    // made is 0700 from its first instant.
    const apogee::testing::TempDir root{"platform-private-" +
                                        std::to_string(std::random_device{}())};
    const std::filesystem::path made = root.path() / "made";
    bool exists = true;
    REQUIRE(apogee::platform::create_private_directory(made, exists));
    CHECK_FALSE(exists);
    CHECK(std::filesystem::is_directory(made));
    CHECK(apogee::platform::owned_by_this_user(made));
#if !defined(_WIN32)
    CHECK((std::filesystem::status(made).permissions() & std::filesystem::perms::all) ==
          std::filesystem::perms::owner_all);
#endif

    // Something already there -- a directory, a file -- is never this one's.
    CHECK_FALSE(apogee::platform::create_private_directory(made, exists));
    CHECK(exists);
    const std::filesystem::path file = root.path() / "file";
    std::ofstream{file} << "planted";
    CHECK_FALSE(apogee::platform::create_private_directory(file, exists));
    CHECK(exists);
    CHECK(std::filesystem::is_regular_file(file));

    // Nowhere to make it: refused, and not for being there.
    CHECK_FALSE(apogee::platform::create_private_directory(root.path() / "no" / "such", exists));
    CHECK_FALSE(exists);
    CHECK_FALSE(apogee::platform::owned_by_this_user(root.path() / "no" / "such"));
}

namespace {

/// `path`'s UTF-8 bytes, as `std::string`.
[[nodiscard]] std::string utf8_of(const std::filesystem::path& path) {
    const std::u8string bytes = path.u8string();
    return {bytes.begin(), bytes.end()};
}

/// The whole of `path`'s text, or empty.
[[nodiscard]] std::string slurp(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

#if defined(_WIN32)
/// A variable set through the Win32 call alone -- never the C runtime's copy
/// -- for the life of the object, put back after (57j's guardrail).
class Win32EnvGuard {
public:
    Win32EnvGuard(const wchar_t* name, const std::wstring& value) : name_{name} {
        std::wstring previous(32767, L'\0');
        const DWORD got =
            GetEnvironmentVariableW(name_, previous.data(), static_cast<DWORD>(previous.size()));
        if (got > 0 && got < previous.size()) {
            previous.resize(got);
            previous_ = previous;
            had_ = true;
        }
        (void)SetEnvironmentVariableW(name_, value.c_str());
    }

    ~Win32EnvGuard() {
        (void)SetEnvironmentVariableW(name_, had_ ? previous_.c_str() : nullptr);
    }

    Win32EnvGuard(const Win32EnvGuard&) = delete;
    Win32EnvGuard& operator=(const Win32EnvGuard&) = delete;
    Win32EnvGuard(Win32EnvGuard&&) = delete;
    Win32EnvGuard& operator=(Win32EnvGuard&&) = delete;

private:
    const wchar_t* name_;
    std::wstring previous_;
    bool had_ = false;
};
#endif

}  // namespace

// 57j: Windows read the environment and argv in the ANSI code page, and named
// files through ANSI calls -- a profile folder named `C:\Users\José` made
// every command exit 1. These hold the seam to UTF-8 on every platform; on
// CI's Windows rows they are the proof, since no Windows host runs here.

TEST_CASE("this process reads its text as UTF-8", "[platform][utf8][57j]") {
    // The manifest every executable carries -- the test binary's too --
    // names UTF-8 the code page, so the Windows test runs read text as users'
    // runs do. Elsewhere there is no code page to get wrong.
    CHECK(apogee::platform::text_encoding_gap().empty());
#if defined(_WIN32)
    CHECK(GetACP() == CP_UTF8);
#endif
}

TEST_CASE("the manifest names UTF-8 and parses", "[platform][utf8][57j]") {
    // Windows refuses to start a process whose manifest does not parse, and
    // XML forbids two hyphens inside a comment -- a typo here would stop
    // every Windows run, and only a Windows run would say so.
    const std::filesystem::path source{APOGEE_SOURCE_DIR};
    const std::string manifest = slurp(source / "apogee.manifest");
    REQUIRE_FALSE(manifest.empty());
    CHECK(manifest.find("<activeCodePage xmlns=\"http://schemas.microsoft.com/SMI/2019/"
                        "WindowsSettings\">UTF-8</activeCodePage>") != std::string::npos);
    std::size_t comments = 0;
    for (std::size_t open = manifest.find("<!--"); open != std::string::npos;
         open = manifest.find("<!--", open + 4)) {
        const std::size_t close = manifest.find("-->", open + 4);
        REQUIRE(close != std::string::npos);
        const std::string body = manifest.substr(open + 4, close - open - 4);
        INFO("comment: " << body);
        CHECK(body.find("--") == std::string::npos);
        CHECK((body.empty() || body.back() != '-'));
        ++comments;
    }
    CHECK(comments > 0);
    // The resource script builds that file in, as the manifest resource.
    CHECK(slurp(source / "apogee.rc").find("1 24 \"apogee.manifest\"") != std::string::npos);
}

TEST_CASE("an environment value comes back as UTF-8", "[platform][utf8][57j]") {
    // "¿Qué tal el café?", written as its bytes.
    const std::string value = "\xC2\xBFQu\xC3\xA9 tal el caf\xC3\xA9?";
    {
        const apogee::testing::EnvGuard set{"APOGEE_TEST_UTF8", value};
        CHECK(apogee::platform::utf8_environment("APOGEE_TEST_UTF8") == value);
    }
    CHECK_FALSE(apogee::platform::utf8_environment("APOGEE_TEST_UTF8").has_value());
}

TEST_CASE("a path built from UTF-8 names those characters", "[platform][utf8][57j]") {
    const std::filesystem::path name = apogee::platform::path_from_utf8("Jos\xC3\xA9");
#if defined(_WIN32)
    CHECK(name.wstring() == L"Jos\u00e9");
#else
    CHECK(name.string() == "Jos\xC3\xA9");
#endif
    CHECK(utf8_of(name) == "Jos\xC3\xA9");
}

TEST_CASE("an exclusive file is created under its non-ASCII name", "[platform][utf8][57j]") {
    // The owner marker beside a staging folder: `CreateFileA` named it in the
    // ANSI code page, so on Windows it landed under a mis-decoded name.
    const apogee::testing::TempDir root{"platform-exclusive-" +
                                        std::to_string(std::random_device{}())};
    const std::filesystem::path owner =
        root.path() / apogee::platform::path_from_utf8("na\xC3\xAFve-owner");
    bool exists = true;
    REQUIRE(apogee::platform::create_exclusive_file(owner, "1234\n", exists));
    CHECK_FALSE(exists);
    CHECK(std::filesystem::exists(owner));
    CHECK(slurp(owner) == "1234\n");
    // That name and no other: the folder holds exactly it.
    std::vector<std::filesystem::path> entries;
    for (const auto& entry : std::filesystem::directory_iterator{root.path()}) {
        entries.push_back(entry.path().filename());
    }
    REQUIRE(entries.size() == 1);
    CHECK(utf8_of(entries.front()) == "na\xC3\xAFve-owner");
    // A second claim finds it there.
    CHECK_FALSE(apogee::platform::create_exclusive_file(owner, "5678\n", exists));
    CHECK(exists);
    CHECK(slurp(owner) == "1234\n");
}

#if defined(_WIN32)
TEST_CASE("a non-ASCII profile folder comes back as its UTF-8 bytes", "[platform][utf8][57j]") {
    // Set through the Win32 call, never the C runtime's copy: the home
    // directory is read from the process's environment itself, wide.
    const std::wstring profile = std::filesystem::temp_directory_path().wstring() + L"\\Jos\u00e9";
    const Win32EnvGuard guard{L"USERPROFILE", profile};
    const std::optional<std::string> home = apogee::platform::home_directory();
    REQUIRE(home.has_value());
    CHECK(home->ends_with("\\Jos\xC3\xA9"));
    CHECK(apogee::platform::path_from_utf8(*home).wstring() == profile);
}
#endif

TEST_CASE("the console is left alone where no standard stream is one", "[platform][console][57j]") {
    // Under ctest every standard stream is a pipe: nothing to set up, and
    // nothing put back. A scope inside a live one never changes anything --
    // the outermost owns the console -- and a restore with nothing changed
    // is safe, from anywhere.
    std::istringstream in;
    std::streambuf* const old_in = std::cin.rdbuf(in.rdbuf());
    bool outer_changed = true;
    bool inner_changed = true;
    {
        const apogee::platform::ConsoleScope outer;
        outer_changed = outer.changed();
        {
            const apogee::platform::ConsoleScope inner;
            inner_changed = inner.changed();
        }
    }
    std::cin.rdbuf(old_in);
    CHECK_FALSE(inner_changed);
    if (apogee::platform::host_os() != apogee::platform::OperatingSystem::Windows) {
        CHECK_FALSE(outer_changed);
    }
    apogee::platform::restore_console();
    // Escape sequences render wherever no Windows console says otherwise.
    using apogee::platform::renders_escape_sequences;
    using apogee::platform::StandardStream;
    CHECK(renders_escape_sequences(StandardStream::In));
    if (apogee::platform::host_os() != apogee::platform::OperatingSystem::Windows) {
        CHECK(renders_escape_sequences(StandardStream::Out));
        CHECK(renders_escape_sequences(StandardStream::Err));
    }
}
