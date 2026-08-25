#pragma once

#include <memory>

#include "commands/command.h"
#include "commands/registry.h"

namespace apogee::commands {

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
    // Declaration order is destruction order reversed: app_ holds callbacks
    // that reference context_ and the commands owned by registry_, so it must
    // be declared last and therefore destroyed first.
    RootContext context_;
    CommandRegistry registry_;
    std::unique_ptr<CLI::App> app_;
};

}  // namespace apogee::commands
