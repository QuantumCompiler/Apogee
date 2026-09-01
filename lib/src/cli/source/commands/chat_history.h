#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "commands/command.h"
#include "logger/session.h"

/// `apogee chats` — list, inspect, and rename saved conversations.
namespace apogee::commands {

/// One row of `apogee chats list`.
[[nodiscard]] std::string format_session_row(const logger::Session& session);

/// The body of `apogee chats info <name>`.
[[nodiscard]] std::string format_session_info(const logger::Session& session);

/// The auto-title prompt sent after the first exchange.
[[nodiscard]] std::string title_prompt();

/// Cleans a model-generated title: one line, no quotes, bounded length.
///
/// A model asked for a title will sometimes answer with a sentence, a quoted
/// phrase, or a paragraph explaining its choice. This makes any of those
/// usable rather than letting a listing wrap across three rows.
[[nodiscard]] std::string sanitize_title(std::string_view raw);

class ChatsCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
