#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "platform/child_process.h"

/// What Xcode's command-line tools say of the Xcode on this Mac (56d): the
/// facts `apogee check`'s Xcode section reads.
///
/// **Asked, never opened.** The probe runs three of Xcode's own commands --
/// `xcode-select -p`, `xcrun xcodebuild -version`, `xcrun --find mcpbridge`
/// -- each a captured child through the seam's one bounded run
/// (`run_captured`): stdin closed, stderr a tail, a deadline. It reads none of
/// Xcode's settings, its defaults or anything under the user's Library: what
/// those hold (whether external agents may use Xcode's tools) is said to the
/// person in words, never looked up.
///
/// **Never a developer-tools shim on a Mac with none.** `xcode-select -p`
/// comes first, and `xcrun` runs only when the directory it names exists and
/// is an Xcode's: on a Mac with no developer directory, the `/usr/bin` shims
/// offer to install the tools, and a doctor must never raise a dialog.
///
/// Every program is resolved from `PATH`, so stub scripts on a `PATH` fake a
/// whole Xcode in a test.
namespace apogee::platform {

/// What the selected developer directory is.
enum class XcodeInstall : std::uint8_t {
    /// None is selected: `xcode-select -p` answers an error.
    None,
    /// One is selected, but it is not there -- an Xcode moved or deleted.
    Missing,
    /// The Command Line Tools', not an Xcode's: no `xcodebuild`, no
    /// `mcpbridge`, no agents.
    CommandLineTools,
    /// An Xcode's: its version and build read.
    Xcode,
};

/// The facts.
struct XcodeFacts {
    XcodeInstall install = XcodeInstall::None;
    /// What `xcode-select -p` named; empty when it named nothing.
    std::filesystem::path developer_dir{};
    /// An Xcode the person could select instead, when the selected directory
    /// is not one -- `/Applications/Xcode.app` when it exists -- else empty.
    std::filesystem::path xcode_app{};
    /// `xcodebuild -version`'s `Xcode 27.1` and `Build version 27A9275`.
    std::string version{};
    std::string build{};
    /// `xcrun --find mcpbridge`'s answer: the stdio bridge to Xcode's tools,
    /// shipped since Xcode 26.3. Empty when this Xcode has none.
    std::filesystem::path mcpbridge{};
};

/// The bound on each of the probe's children: a hundred times the measured
/// 0.05 s of `xcodebuild -version` (2026-10-31).
inline constexpr std::chrono::seconds kXcodeProbeTimeout{5};

/// The facts, or nullopt with `error` saying why Xcode could not be asked: a
/// command that would not start, ran past `limits`, or answered nothing a
/// version can be read from. An Xcode that is simply absent is facts
/// (`XcodeInstall::None`), never an error.
[[nodiscard]] std::optional<XcodeFacts> xcode_facts(const CapturedLimits& limits,
                                                    std::string& error);

/// `xcodebuild -version`'s answer read: the version and build, or nullopt when
/// it names no version.
struct XcodeVersion {
    std::string version{};
    std::string build{};
};

[[nodiscard]] std::optional<XcodeVersion> parse_xcodebuild_version(std::string_view text);

/// Whether `version` (`27.1`, `26.6`, `26.3.1`) is at least `major.minor`.
/// A version with no number in it is not.
[[nodiscard]] bool xcode_at_least(std::string_view version, int major, int minor);

/// Whether `developer_dir` is an Xcode's -- `<name>.app/Contents/Developer`
/// -- rather than the Command Line Tools'.
[[nodiscard]] bool is_xcode_developer_dir(const std::filesystem::path& developer_dir);

}  // namespace apogee::platform
