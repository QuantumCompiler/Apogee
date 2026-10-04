#pragma once

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

#include "commands/command.h"

namespace apogee::commands {

/// Owns the set of subcommands the CLI exposes.
///
/// Held separately from the root command so tests can assemble a registry of
/// their own -- the injectable-seam discipline, starting at the first
/// interface. Retrofitting that later in C++ is far more painful than in Go,
/// which is why it is here on day one rather than when it is first needed.
class CommandRegistry {
public:
    CommandRegistry() = default;

    /// Takes ownership of `command`.
    /// Throws std::invalid_argument if its name is already registered -- a
    /// silently shadowed subcommand is a bug found months later, if ever.
    void add(std::unique_ptr<Command> command);

    /// The command registered under `name`, or nullptr. Non-owning.
    [[nodiscard]] const Command* find(std::string_view name) const noexcept;

    /// Registered names, in registration order.
    [[nodiscard]] std::vector<std::string_view> names() const;

    [[nodiscard]] std::size_t size() const noexcept;

    [[nodiscard]] bool empty() const noexcept;

    /// Binds every registered command to `root`.
    void bind_all(CLI::App& root, const RootContext& context);

private:
    std::vector<std::unique_ptr<Command>> commands_;
};

/// Apogee's built-in command set.
///
/// This is the one place a new subcommand is wired in. Later backlog items add
/// their command here (`complete`, `chat`, `config`, `check`, ...) and it
/// appears in `apogee --help` with no other edit anywhere.
[[nodiscard]] CommandRegistry default_registry();

}  // namespace apogee::commands
