#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

/// Apogee's on-disk layout root, and the one chain that chooses it.
///
/// One identical tree on all five targets (SPEC.md -> "One install contract:
/// identical on-disk layout from every install path"), rooted at `~/.apogee`
/// for a release build:
///
///     ~/.apogee/
///       config/config.json    the config engine's file (JSONC, 28i; an
///                             older install's config.yaml still loads)
///       sessions/             persisted chats        (chat-cli)
///       logs/                 operational log        (chat-cli)
///       cache/                downloads, prompt KV   (llamacpp-backend)
///
/// **Which root (M10).** Three install channels build from source, each with
/// a root of its own -- release `~/.apogee`, dev `~/.apogee-dev`, test
/// `~/.apogee-test` -- and the channel is a compile fact of the executable
/// (`-DAPOGEE_CHANNEL=<name>`, `make install MODE=<name>`), so an installed
/// binary knows its root before it reads a single file. One chain, here,
/// chooses the root for a run, explicit before ambient before built-in:
///
///   1. a **root flag** -- `--dev`, `--test`, `--release`, or `--custom
///      <config file>`, whose position names the root -- for that run only,
///      written nowhere;
///   2. **`APOGEE_HOME`**, which relocates the whole tree. That override is
///      not a convenience -- it is what makes tests hermetic (CLAUDE.md
///      requires no writes outside a test's own temp directory), and it is
///      the only reason the config helpers need no injected-path parameter
///      threaded through every entry point;
///   3. the **baked channel's** root under the home directory.
///
/// A flag and an `APOGEE_HOME` naming different roots are refused, both
/// named: a root chosen two ways that disagree is never guessed. Every layout
/// row resolves through `apogee_home()`, so a flag re-roots everything --
/// models, chats, secrets, cache, config -- never only the config read.
namespace apogee::harness {

/// Name of the environment variable that relocates the tree.
inline constexpr const char* kHomeEnvVar = "APOGEE_HOME";

/// An install channel: which root a build owns, and the name it installs
/// under beside the others (`apogee`, `apogee-dev`, `apogee-test`).
enum class Channel : std::uint8_t { Release, Dev, Test };

/// Every channel, in the order help and messages name them.
inline constexpr std::array<Channel, 3> kChannels{Channel::Release, Channel::Dev, Channel::Test};

/// `release`, `dev`, `test` -- what `MODE=` and `-DAPOGEE_CHANNEL=` take.
[[nodiscard]] constexpr std::string_view channel_name(Channel channel) noexcept {
    switch (channel) {
        case Channel::Dev:
            return "dev";
        case Channel::Test:
            return "test";
        case Channel::Release:
            break;
    }
    return "release";
}

/// The channel `name` spells, or nothing. Constexpr, so the executable holds
/// its own compile-time stamp to the three names before it can be built.
[[nodiscard]] constexpr std::optional<Channel> parse_channel(std::string_view name) noexcept {
    for (const Channel channel : kChannels) {
        if (channel_name(channel) == name) {
            return channel;
        }
    }
    return std::nullopt;
}

/// The channel's root under the home directory: `.apogee`, `.apogee-dev`,
/// `.apogee-test`.
[[nodiscard]] constexpr std::string_view channel_directory(Channel channel) noexcept {
    switch (channel) {
        case Channel::Dev:
            return ".apogee-dev";
        case Channel::Test:
            return ".apogee-test";
        case Channel::Release:
            break;
    }
    return ".apogee";
}

/// The global flag pointing a run at the channel's root: `--release`,
/// `--dev`, `--test`. The root command declares them from here, and the
/// completion protocol reads a typed line with the same spellings.
[[nodiscard]] constexpr std::string_view channel_flag(Channel channel) noexcept {
    switch (channel) {
        case Channel::Dev:
            return "--dev";
        case Channel::Test:
            return "--test";
        case Channel::Release:
            break;
    }
    return "--release";
}

/// The global flag rooting a run at the install a config file sits in.
inline constexpr std::string_view kCustomFlag = "--custom";

/// The channel this executable was built for. `main()` sets it from the
/// compile-time stamp before anything runs; a library with no executable
/// around it -- a test -- is release unless it says otherwise.
[[nodiscard]] Channel baked_channel() noexcept;

/// Sets the baked channel. The executable's first act, and a test's way to be
/// another channel's build; nothing else calls it.
void set_baked_channel(Channel channel) noexcept;

/// A root flag as given on the command line: a channel's root, or `--custom`
/// and the config file whose position names the root.
struct RootFlag {
    /// The channel flag given; empty for `--custom`.
    std::optional<Channel> channel;
    /// The config file `--custom` named, as typed.
    std::filesystem::path custom_config;

    /// The flag as typed, for messages: `--dev`, `--custom <path>`.
    [[nodiscard]] std::string spelling() const;
};

/// Which rung of the chain chose a root.
enum class RootRung : std::uint8_t { Flag, Environment, Baked };

/// What the chain chose, and why.
struct RootResolution {
    std::filesystem::path root;
    /// The config file this root reads when `--config` names none:
    /// `config_file_in(<root>/config)`, or the file `--custom` named.
    std::filesystem::path config;
    RootRung rung = RootRung::Baked;
    /// The build's own channel -- a fact about the binary, whichever rung
    /// chose the root.
    Channel channel = Channel::Release;
    /// The flag, when one chose.
    std::optional<RootFlag> flag;
    /// Non-empty when the chain refused, saying why; nothing was chosen then.
    std::string error;

    [[nodiscard]] bool ok() const noexcept {
        return error.empty();
    }
};

/// Why `resolution.root` is the root, in a few words for `--version` and
/// `check`: `set by --dev`, `set by APOGEE_HOME`, `the dev channel's own root,
/// baked into this build`.
[[nodiscard]] std::string root_reason(const RootResolution& resolution);

/// Everything the chain reads, so it is a pure function a test drives rung by
/// rung.
struct RootInputs {
    std::optional<RootFlag> flag;
    /// `APOGEE_HOME`'s value. Empty -- unset, or set to "" -- is no override:
    /// treating "" as a root would put the config at /config/config.json.
    std::string environment;
    /// The build's channel.
    Channel channel = Channel::Release;
    /// The user's home directory, when the platform reports one.
    std::optional<std::string> home_directory;
};

/// The chain: the flag, then `APOGEE_HOME`, then the baked channel's root.
///
/// A flag and an `APOGEE_HOME` naming the same directory agree, and the flag
/// is credited; naming different ones is refused with both in the message. A
/// channel's root needs the home directory; without one the chain refuses
/// rather than guess.
[[nodiscard]] RootResolution resolve_root(const RootInputs& inputs);

/// This process's inputs: the root flag in force, `APOGEE_HOME`, the baked
/// channel, and the platform's home directory.
[[nodiscard]] RootInputs current_root_inputs();

/// `resolve_root(current_root_inputs())`. Never throws: a refusal is in
/// `error`, for the surfaces that report it rather than act on it.
[[nodiscard]] RootResolution current_root();

/// The root `--custom <config file>` names.
///
/// The one layout declaration already fixes where a config sits in a root --
/// `<root>/config/<file>` -- so the file's position names the root, through
/// `home_for_config()`, the one derivation. A file anywhere else is refused
/// with that shape named: an install half-rooted at a guess is worse than
/// none.
struct CustomRoot {
    std::filesystem::path root;
    /// The config file, absolute and normalized.
    std::filesystem::path config;
    /// Non-empty on refusal.
    std::string error;
};

[[nodiscard]] CustomRoot custom_root(const std::filesystem::path& config_file);

/// Whether two spellings name one place: made absolute, resolved through
/// symlinks as far as they exist (`/tmp` is `/private/tmp` on macOS), a
/// trailing separator ignored. How the chain tells agreement from
/// disagreement, and the root command tells `--custom` and `--config` apart.
[[nodiscard]] bool same_path(const std::filesystem::path& left, const std::filesystem::path& right);

/// The root flag in force for this run, if any.
[[nodiscard]] std::optional<RootFlag> root_flag();

/// Puts `flag` in force for the scope's lifetime and the previous one back
/// after: a run's flag is that run's affair, never persisted -- the next run
/// starts from the chain again. The root command holds one for the parse it
/// ran; the completion protocol for the line it was handed.
class RootFlagScope {
public:
    explicit RootFlagScope(std::optional<RootFlag> flag);
    ~RootFlagScope();

    RootFlagScope(const RootFlagScope&) = delete;
    RootFlagScope& operator=(const RootFlagScope&) = delete;
    RootFlagScope(RootFlagScope&&) = delete;
    RootFlagScope& operator=(RootFlagScope&&) = delete;

private:
    std::optional<RootFlag> previous_;
};

/// Apogee's root directory for this run: the chain's answer.
///
/// Throws std::runtime_error when the chain refuses -- a flag and
/// `APOGEE_HOME` disagreeing, or no home directory to put a channel's root
/// under -- because the caller is about to read or write there, and guessing
/// would be worse than failing.
[[nodiscard]] std::filesystem::path apogee_home();

/// The root `apogee uninstall` removes: `APOGEE_HOME` when set -- so every
/// sandboxed uninstall stays hermetic -- else the baked channel's root, and
/// **never** a root flag's. A binary removes the install it belongs to: a
/// release uninstall must not take `~/.apogee-dev` because `--dev` was on the
/// line. Throws as `apogee_home()` does.
[[nodiscard]] std::filesystem::path install_home();

/// `<apogee_home()>/config`.
[[nodiscard]] std::filesystem::path config_dir();

/// The config file's name in a root's `config/` directory (28i): JSONC.
inline constexpr std::string_view kConfigFileName = "config.json";

/// The name it had before: YAML, still read -- with a notice -- until the
/// user runs `apogee config migrate` (ADR backwards-compatibility).
inline constexpr std::string_view kLegacyConfigFileName = "config.yaml";

/// The config file a root's `config/` directory holds: `config.json`, or
/// `config.yaml` when that is the only one there -- the compat read. Where
/// neither exists, `config.json`: the file a fresh install writes.
[[nodiscard]] std::filesystem::path config_file_in(const std::filesystem::path& config_directory);

/// Whether `path` is the older YAML config the compat read is serving: named
/// `config.yaml` by the layout. A `--config` naming a file of another name is
/// the user's own choice, read by its content and never noticed.
[[nodiscard]] bool is_legacy_config_path(const std::filesystem::path& path);

/// Non-empty when `config_directory` holds both `config.json` and
/// `config.yaml`: the refusal, naming both. Which one is current is the
/// user's call, never guessed.
[[nodiscard]] std::string config_conflict(const std::filesystem::path& config_directory);

/// The config file a command reads when the user passes no `--config`:
/// `config_file_in(<apogee_home()>/config)`, or the file `--custom` named.
/// Resolution order for a command is: `--config` flag, then this. Throws
/// std::runtime_error when the chain refuses, or when the directory holds
/// both formats (`config_conflict`).
[[nodiscard]] std::filesystem::path default_config_path();

/// Resolves the config path a command should use: `flag_value` when non-empty,
/// otherwise default_config_path().
[[nodiscard]] std::filesystem::path resolve_config_path(const std::string& flag_value);

/// The data directory a config file lives in: the parent of its `config/`
/// directory. `~/.apogee/config/config.json` gives `~/.apogee`; a `--config`
/// temp tree gives that tree, which is what keeps every path derived from
/// it hermetic in tests. The scaffold cores and the agent loader use this
/// rather than `apogee_home()` for that reason, and `--custom` derives its
/// root with it.
[[nodiscard]] std::filesystem::path home_for_config(const std::filesystem::path& config_path);

}  // namespace apogee::harness
