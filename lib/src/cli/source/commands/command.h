#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace CLI {
class App;
}

namespace apogee::commands {

/// Root-level state every subcommand can read.
///
/// Populated by CLI11 while it parses, which means a command must read these
/// fields inside its callback and never at bind time -- at bind time they are
/// still empty.
struct RootContext {
    /// Value of the persistent `--config` flag; empty when the user did not
    /// pass one, in which case the config engine's default resolution applies.
    std::string config_path;

    /// Every registered subcommand name, in registration order.
    ///
    /// Here so the completion protocol can offer the real command set rather
    /// than a list it maintains separately -- a second list would go stale the
    /// first time a command is added, and the symptom (tab-completion quietly
    /// missing a command) is one nobody files a bug about.
    std::vector<std::string> command_names;
};

/// One `apogee <name>` subcommand.
///
/// A command owns its own flags and its own behavior, and nothing outside it
/// knows what those are -- adding a command touches exactly two places: the
/// command's own files, and the one line in `default_registry()` that
/// constructs it. That is the self-registration property the skeleton exists
/// to establish, and it is why the CLI can grow to Ommi's ~25 commands without
/// main.cpp growing at all.
///
/// Signal failure from a callback by throwing `CLI::RuntimeError(code)`; the
/// root command turns it into that process exit code.
class Command {
public:
    Command() = default;
    virtual ~Command() = default;

    Command(const Command&) = delete;
    Command& operator=(const Command&) = delete;
    Command(Command&&) = delete;
    Command& operator=(Command&&) = delete;

    /// The subcommand word, e.g. "version". Must be unique in a registry.
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    /// One-line description, shown in `apogee --help`.
    [[nodiscard]] virtual std::string_view summary() const noexcept = 0;

    /// Attaches this command's subcommand, flags, and callback to `root`.
    /// Called once, by CommandRegistry::bind_all. `context` outlives the app.
    virtual void bind(CLI::App& root, const RootContext& context) = 0;
};

}  // namespace apogee::commands
