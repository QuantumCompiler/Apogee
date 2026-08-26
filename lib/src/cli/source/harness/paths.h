#pragma once

#include <filesystem>
#include <string>

/// Apogee's on-disk layout.
///
/// One identical tree on all six targets (SPEC.md -> "One install contract:
/// identical on-disk layout from every install path"), rooted at `~/.apogee`:
///
///     ~/.apogee/
///       config/config.yaml    the config engine's file
///       sessions/             persisted chats        (chat-cli)
///       logs/                 operational log        (chat-cli)
///       cache/                downloads, prompt KV   (llamacpp-backend)
///
/// The root is overridable with the `APOGEE_HOME` environment variable, which
/// relocates the whole tree. That override is not a convenience -- it is what
/// makes tests hermetic (CLAUDE.md requires no writes outside a test's own
/// temp directory), and it is the only reason the config helpers need no
/// injected-path parameter threaded through every entry point.
namespace apogee::harness {

/// Name of the environment variable that relocates the tree.
inline constexpr const char* kHomeEnvVar = "APOGEE_HOME";

/// Apogee's root directory: $APOGEE_HOME when set and non-empty, else
/// `<home>/.apogee`.
///
/// Throws std::runtime_error when APOGEE_HOME is unset AND the platform cannot
/// report a home directory -- the caller is about to read or write there, so
/// guessing would be worse than failing.
[[nodiscard]] std::filesystem::path apogee_home();

/// `<apogee_home()>/config`.
[[nodiscard]] std::filesystem::path config_dir();

/// `<apogee_home()>/config/config.yaml` -- the default when the user passes no
/// `--config`. Resolution order for a command is: `--config` flag, then this.
[[nodiscard]] std::filesystem::path default_config_path();

/// Resolves the config path a command should use: `flag_value` when non-empty,
/// otherwise default_config_path().
[[nodiscard]] std::filesystem::path resolve_config_path(const std::string& flag_value);

}  // namespace apogee::harness
