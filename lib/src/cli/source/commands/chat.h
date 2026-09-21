#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agentloop/content.h"
#include "commands/command.h"
#include "harness/harness.h"
#include "logger/session.h"

/// `apogee chat` — the interactive multi-turn surface.
namespace apogee::commands {

/// What a slash command asked the REPL to do.
enum class SlashOutcome : std::uint8_t {
    /// Not a slash command; treat the line as a prompt.
    NotACommand,
    /// Handled; carry on.
    Handled,
    /// Handled; leave the REPL.
    Exit,
};

/// One parsed slash command.
struct SlashCommand {
    std::string name;      ///< without the leading '/'
    std::string argument;  ///< everything after the first space, trimmed
};

/// Parses a line as a slash command, or nullopt when it is a prompt.
///
/// A line is a command only when it starts with `/` AND the word after it
/// contains no whitespace — so `/help` is a command and `/usr/bin/env is a
/// path` is a question about a path.
[[nodiscard]] std::optional<SlashCommand> parse_slash(std::string_view line);

/// `apogee chat` — a persistent, resumable conversation.
class ChatCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
