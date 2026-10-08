#pragma once

#include <array>
#include <memory>
#include <string>

#include "cli/command.h"
#include "cli/registry.h"

namespace CLI {
class Option;
}  // namespace CLI

namespace apogee::harness {
class RootFlagScope;
}  // namespace apogee::harness

namespace apogee::commands {

/// Every command under `app` whose `--output-format` takes `json` -- the
/// reads (27j, 28h) -- by its full name, comma-separated: what the refusal of
/// a JSON face asked of a command without one names.
[[nodiscard]] std::string json_readers(const CLI::App& app);

/// The `apogee` root command: persistent flags, the registered subcommands,
/// and argv -> exit code.
///
/// Constructible with an arbitrary registry so tests can drive the real
/// parsing path over a command set they control, without linking the CLI
/// executable (see cmake/ApogeeLinkPolicy.cmake for why that matters).
class RootCommand {
public:
    /// Builds the root over `registry`, binding every command in it.
    explicit RootCommand(CommandRegistry registry = default_registry());
    ~RootCommand();

    RootCommand(const RootCommand&) = delete;
    RootCommand& operator=(const RootCommand&) = delete;
    RootCommand(RootCommand&&) = delete;
    RootCommand& operator=(RootCommand&&) = delete;

    /// The configured CLI11 app -- help text, subcommands, flags.
    [[nodiscard]] CLI::App& app() noexcept;
    [[nodiscard]] const CLI::App& app() const noexcept;

    /// Root-level flag values. Meaningful only after run() has parsed.
    [[nodiscard]] const RootContext& context() const noexcept;

    /// Parses `argv` and runs the selected command.
    /// Returns the process exit code. A bare `apogee` prints help and
    /// returns 0.
    [[nodiscard]] int run(int argc, const char* const* argv);

private:
    /// Runs once parsing completes and before any command: puts the root flag
    /// in force for this run -- or refuses one that disagrees with
    /// `APOGEE_HOME` or `--config` -- then answers `--version`, which names
    /// the root and so must come after it.
    void apply_root_flags();

    // Declaration order is destruction order reversed: app_ holds callbacks
    // that reference context_, custom_config_ and the commands owned by
    // registry_, so it must be declared last and therefore destroyed first.
    RootContext context_;
    /// `--custom`'s file as parsed (M10).
    std::string custom_config_;
    /// The root flag this run put in force, until the root is destroyed.
    std::unique_ptr<harness::RootFlagScope> root_scope_;
    CommandRegistry registry_;
    std::unique_ptr<CLI::App> app_;
    /// Owned by `app_`, and read for what THIS parse was given: a bound value
    /// keeps an earlier parse's answer when a later one omits the flag.
    CLI::Option* version_option_ = nullptr;
    CLI::Option* config_option_ = nullptr;
    /// `--release`, `--dev`, `--test`, in `harness::kChannels` order.
    std::array<CLI::Option*, 3> channel_options_{};
    CLI::Option* custom_option_ = nullptr;
};

}  // namespace apogee::commands
