#pragma once

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "contracts/paths.h"

/// `apogee acp registration` (56d): the fields an ACP host's "add an agent"
/// takes -- Xcode 26.6 and later's Settings › Intelligence › Agents › Add an
/// Agent: a Name, an Executable, an optional Interpreter, Arguments (one a
/// line) and Environment Variables -- built once, here, and rendered as text
/// and as the `acp.registration` document the macOS app reads (the GUI's 5a).
///
/// **Apogee prints; the person types.** Nothing here writes, or reads, an
/// ACP host's settings: the person pastes the fields into the host's own
/// sheet.
///
/// **This binary's, and this install's.** The Executable is the running
/// binary's absolute path (`platform::executable_path()`, symlinks resolved),
/// never `argv[0]`, `~` or a bare name: Xcode looks nothing up. The root the
/// person printed it from goes with it -- a root flag or `--config` ahead of
/// `acp` in Arguments, any path made absolute; `APOGEE_HOME` and
/// `APOGEE_CONFIG`, when set, in Environment -- so the agent Xcode starts
/// opens that install, whatever environment Xcode hands it. And `PATH`, when
/// set: an app opened from the Dock gives its children launchd's `PATH`, on
/// which no subscription CLI is found.
namespace apogee::operations {

/// Where in Xcode the fields go.
inline constexpr std::string_view kAcpRegistrationWhere =
    "Settings › Intelligence › Agents › Add an Agent";

/// The first Xcode that hosts ACP agents.
inline constexpr std::string_view kAcpXcodeMinimum = "26.6";

/// Wall W3, said where a person sets Xcode up: `check`'s `approval` row and
/// the registration's last line. Read from Xcode 27.1's own strings, not yet
/// observed -- the user's run corrects it if Xcode does not ask again.
inline constexpr std::string_view kXcodeApproval =
    "Xcode approves an agent by its code signature and its binary's hash. Apogee's macOS builds "
    "are ad-hoc signed, so Xcode asks again after every install or update.";

/// What the fields are built from: this process's facts, gathered by the
/// command so the build is a pure function a test drives.
struct AcpRegistrationInputs {
    /// The running binary (`platform::executable_path()`); empty when the
    /// platform will not say.
    std::filesystem::path executable{};
    /// The build's channel: the Name.
    harness::Channel channel = harness::Channel::Release;
    /// The root flag in force for this run, if any.
    std::optional<harness::RootFlag> root_flag{};
    /// `--config` as typed on the line -- not `APOGEE_CONFIG`'s -- or empty.
    std::string config_flag{};
    /// `APOGEE_HOME`, `APOGEE_CONFIG` and `PATH` as this process has them;
    /// empty when unset.
    std::string apogee_home{};
    std::string apogee_config{};
    std::string path{};
    /// The flags `acp` itself was given (`commands::forwarded_flags`): they
    /// follow `acp` in Arguments, so every session has them.
    std::vector<std::string> acp_flags{};
};

/// The fields.
struct AcpRegistration {
    std::string where{kAcpRegistrationWhere};
    /// `Apogee`, `Apogee (dev)`, `Apogee (test)`.
    std::string name{};
    std::filesystem::path executable{};
    /// Always empty: the Executable is a binary, never a script.
    std::optional<std::string> interpreter{};
    std::vector<std::string> arguments{};
    /// In the order the text prints them.
    std::vector<std::pair<std::string, std::string>> environment{};
    /// `release`, `dev`, `test`.
    std::string channel{};
    std::string xcode_minimum{kAcpXcodeMinimum};
    std::string approval{kXcodeApproval};
};

/// The fields for `inputs`. Throws std::runtime_error when the binary's own
/// path is not known: a registration naming no executable is no registration.
[[nodiscard]] AcpRegistration acp_registration(const AcpRegistrationInputs& inputs);

/// `--output-format json`: `{"object": "acp.registration", "where", "name",
/// "executable", "interpreter": null, "arguments": [...], "environment":
/// {...}, "channel", "xcode_minimum", "approval"}`.
[[nodiscard]] nlohmann::json acp_registration_document(const AcpRegistration& registration);

/// The text: the menu path, a line a field (Arguments and Environment one
/// entry a line), then what `PATH` means when it is carried, and the approval.
[[nodiscard]] std::string render_acp_registration(const AcpRegistration& registration);

}  // namespace apogee::operations
