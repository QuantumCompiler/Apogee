#pragma once

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <string_view>
#include <vector>

#include "cli/command.h"
#include "contracts/types.h"
#include "logger/session.h"

/// `apogee chats` — list, inspect, and rename saved conversations.
namespace apogee::commands {

/// One row of `apogee chats list`.
/// One saved conversation as `chats list --output-format json` carries it
/// (28h): the row's facts -- `id`, `updated`, `turns`, `name`.
[[nodiscard]] nlohmann::json session_row_view(const logger::Session& session);

[[nodiscard]] std::string format_session_row(const logger::Session& session);

/// The body of `apogee chats info <name>`.
[[nodiscard]] std::string format_session_info(const logger::Session& session);

/// Deletes the conversation `name` names (its id or its name) -- its file, its
/// attachments' index, its recall summary -- what `chats delete` runs, and
/// the full-screen shell's (32d). Refused while a live task writes it.
/// Returns what was done: `deleted <id>`.
[[nodiscard]] std::string delete_chat(const std::string& name);

/// The auto-title prompt sent after the first exchange.
[[nodiscard]] std::string title_prompt();

/// The request that titles `session`: a side request carrying the user's
/// messages (not the answers), a small token cap, and no reasoning first --
/// cheap enough to run once in the background after the first exchange.
/// `backend` is the one the title is asked of -- the utility model (26b) --
/// and the session's own when empty.
[[nodiscard]] harness::ChatRequest title_request(const logger::Session& session,
                                                 std::string_view backend = {});

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
