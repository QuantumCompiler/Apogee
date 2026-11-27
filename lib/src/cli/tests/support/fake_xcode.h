#pragma once

#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <string_view>

#include "support/env_guard.h"

/// An `xcode-select` and an `xcrun` on `PATH` that answer as a Mac's would
/// for an Xcode of any version, the Command Line Tools, a deleted Xcode or
/// none -- and say something on stderr every time (56d). For the platform
/// seam's probe (`platform/xcode`) and `check`'s Xcode rows through it, with
/// no Xcode in sight. POSIX only: the stubs are shell scripts, and Windows has
/// no child processes here yet.
///
/// **`PATH` holds the stubs and `/bin`, never `/usr/bin`**, where a Mac's real
/// `xcrun` and `xcode-select` live: the fake must not fall through to them.
/// `/bin` gives the stubs `cat` and `sleep`.
namespace apogee::testing {

class FakeXcode {
public:
    /// Nothing selected: `xcode-select -p` fails, as on a Mac with no
    /// developer tools.
    FakeXcode()
        : dir_{"fake-xcode-" + std::to_string(std::random_device{}())},
          path_{"PATH", (dir_.path() / "bin").string() + ":/bin"} {
        std::filesystem::create_directories(dir_.path() / "bin");
        const std::string d = dir_.path().string();
        script("xcode-select",
               "echo \"xcode-select $*\" >> '" + log().string() +
                   "'\n"
                   "echo 'xcode-select noise nobody should see' >&2\n"
                   "[ -f '" +
                   d + "/slow-xcode-select' ] && exec sleep 30\n" + "if [ -f '" + d +
                   "/selected' ]; then cat '" + d +
                   "/selected'; exit 0; fi\n"
                   "echo 'xcode-select: error: unable to get active developer directory' >&2\n"
                   "exit 2\n");
        script("xcrun", "echo \"xcrun $*\" >> '" + log().string() +
                            "'\n"
                            "echo 'xcrun noise nobody should see' >&2\n"
                            "[ -f '" +
                            d + "/slow-xcrun' ] && exec sleep 30\n" +
                            "case \"$*\" in\n"
                            "  'xcodebuild -version')\n"
                            "    if [ -f '" +
                            d + "/version' ]; then cat '" + d +
                            "/version'; exit 0; fi\n"
                            "    echo \"xcode-select: error: tool 'xcodebuild' requires Xcode\" "
                            ">&2; exit 1 ;;\n"
                            "  '--find mcpbridge')\n"
                            "    if [ -f '" +
                            d + "/bridge' ]; then cat '" + d +
                            "/bridge'; exit 0; fi\n"
                            "    echo 'xcrun: error: unable to find utility \"mcpbridge\"' >&2; "
                            "exit 72 ;;\n"
                            "esac\n"
                            "echo \"xcrun: error: unexpected $*\" >&2\nexit 64\n");
    }

    /// An Xcode selected at `<temp>/Xcode.app/Contents/Developer`, answering
    /// `version` and `build`, with its stdio bridge when `bridge`.
    void xcode(const std::string& version, const std::string& build, bool bridge) const {
        std::filesystem::create_directories(developer_dir());
        write("selected", developer_dir().string() + "\n");
        write("version", "Xcode " + version + "\nBuild version " + build + "\n");
        if (bridge) {
            write("bridge", (developer_dir() / "usr" / "bin" / "mcpbridge").string() + "\n");
        } else {
            std::filesystem::remove(dir_.path() / "bridge");
        }
    }

    /// The Command Line Tools selected, a directory of their own.
    void command_line_tools() const {
        const std::filesystem::path tools = dir_.path() / "CommandLineTools";
        std::filesystem::create_directories(tools);
        write("selected", tools.string() + "\n");
    }

    /// An Xcode selected that is no longer there.
    void deleted() const {
        write("selected", (dir_.path() / "Gone.app" / "Contents" / "Developer").string() + "\n");
    }

    /// `program` -- `xcode-select` or `xcrun` -- sleeps thirty seconds from
    /// now on, as a tool that never answers.
    void slow(std::string_view program) const {
        write("slow-" + std::string{program}, "1");
    }

    [[nodiscard]] std::filesystem::path developer_dir() const {
        return dir_.path() / "Xcode.app" / "Contents" / "Developer";
    }

    /// The stubs' folder: `<bin>/xcrun` is the stub `xcrun`.
    [[nodiscard]] std::filesystem::path bin() const {
        return dir_.path() / "bin";
    }

    /// Every stub command line, one a line: `xcode-select -p`, `xcrun ...`.
    [[nodiscard]] std::filesystem::path log() const {
        return dir_.path() / "calls.log";
    }

    [[nodiscard]] std::string logged() const {
        std::ifstream in{log()};
        return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    }

private:
    void script(const std::string& name, const std::string& body) const {
        const std::filesystem::path path = bin() / name;
        std::ofstream{path} << "#!/bin/sh\n" << body;
        std::filesystem::permissions(path, std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::replace);
    }

    void write(const std::string& name, const std::string& text) const {
        std::ofstream{dir_.path() / name, std::ios::binary | std::ios::trunc} << text;
    }

    TempDir dir_;
    EnvGuard path_;
};

}  // namespace apogee::testing
