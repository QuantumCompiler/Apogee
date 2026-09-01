#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

/// How close a conversation is to filling its context window.
struct ContextUsage {
    std::int64_t used_tokens = 0;
    std::int64_t window = 0;
    /// True when the count came from the provider rather than an estimate.
    bool exact = false;

    /// 0.0 when the window is unknown -- and an unknown window must NOT be
    /// treated as full.
    [[nodiscard]] double fraction() const noexcept {
        return window > 0 ? static_cast<double>(used_tokens) / static_cast<double>(window) : 0.0;
    }

    [[nodiscard]] bool should_warn() const noexcept {
        return window > 0 && fraction() >= 0.80;
    }

    [[nodiscard]] bool should_compact() const noexcept {
        return window > 0 && fraction() >= 0.90;
    }
};

/// Measures `messages` against the window `model` resolves to.
///
/// Falls back to an estimate when the provider offers no exact count, and says
/// which it used. **A context warning that fires at the wrong point is worse
/// than none**, so the distinction is carried rather than smoothed over.
[[nodiscard]] ContextUsage measure_context(const harness::Harness& harness,
                                           const std::vector<harness::ChatMessage>& messages,
                                           const std::string& model);

/// `apogee chat` — a persistent, resumable conversation.
class ChatCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
