#pragma once

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "agentloop/content.h"
#include "cli/command.h"
#include "contracts/types.h"
#include "harness/harness.h"
#include "logger/session.h"

/// `apogee chats` — list, inspect, and rename saved conversations.
namespace apogee::commands {

/// One saved conversation as `chats list --output-format json` carries it
/// (28h): the row's facts -- `id`, `updated`, `turns`, `name` -- and since
/// 52a the `backend` it ran on and the roster `model` it pinned there (empty
/// for the backend's own), as its session file records them.
[[nodiscard]] nlohmann::json session_row_view(const logger::Session& session);

/// One saved conversation as `chats info --output-format json` carries it
/// (M31): `session_row_view`'s facts, then what the text says beside them --
/// `{object: "chat.info", id, name, updated, turns, backend, model,
/// custom_name, title, started, messages, compactions, temperature,
/// max_tokens, schema_version}`, `backend` empty for the default,
/// `temperature` and `max_tokens` null when unset, `schema_version` 0 for a
/// file written before the schema existed.
[[nodiscard]] nlohmann::json session_info_view(const logger::Session& session);

/// One row of `apogee chats list`.
[[nodiscard]] std::string format_session_row(const logger::Session& session);

/// The body of `apogee chats info <name>`.
[[nodiscard]] std::string format_session_info(const logger::Session& session);

/// A saved conversation as `chats show --output-format json` carries it
/// (M27): `{object: "chat", id, name, title, backend, model, attachments,
/// turns, warnings}` -- `id`, `name`, `backend` and `model` as `chats list`'s
/// row says them -- each entry of `turns` one of `logger::saved_turns`,
/// numbered as a resumed session numbers on from it. Attachments named, never
/// their bytes. `warnings` (55b) is what of the file the loader could not read
/// -- `{kind, message}` for each of `warnings`' `FieldDropped` -- `[]` when
/// it read everything.
[[nodiscard]] nlohmann::json saved_chat_view(
    const logger::Session& session, const std::vector<logger::ResumeWarning>& warnings = {});

/// The body of `apogee chats show <name>`: the same turns, each as the chat
/// drew it, plainly -- the user's words, the tools called, the answer.
[[nodiscard]] std::string format_saved_chat(const logger::Session& session);

/// Deletes the conversation `name` names (its id or its name) -- its file, its
/// attachments' index, its recall summary -- what `chats delete` runs, and
/// the full-screen shell's (32d). Refused while a live task writes it.
/// Returns what was done: `deleted <id>`.
[[nodiscard]] std::string delete_chat(const std::string& name);

/// What `compact_conversation` did (55b): the backend asked, and the
/// compaction's outcome and failure as `agentloop::Compaction` says them.
struct CompactionDone {
    std::string compactor{};
    agentloop::Compaction::Outcome outcome = agentloop::Compaction::Outcome::Nothing;
    std::string failure{};

    [[nodiscard]] bool compacted() const noexcept {
        return outcome == agentloop::Compaction::Outcome::Compacted;
    }
};

/// Compacts `session`'s history in memory (52d): the utility model when one
/// is named, else the chat's own backend, folds the earlier exchanges into a
/// summary, and the session counts it -- only when it did (55b): with no
/// exchange to fold, or a summariser that failed, the history and the count
/// stay as they were. The one compaction `/compact`, a driver's `compact`
/// line and `chats compact` run -- each caller settles what it holds and
/// saves what changed.
[[nodiscard]] CompactionDone compact_conversation(const harness::Harness& harness,
                                                  logger::Session& session);

/// What a live session says of a compaction (55b) -- `/compact`, a driver's
/// `compact` line and one the context's fill started alike: `history
/// compacted[ by <backend>]`, `nothing to compact -- no exchange to fold
/// yet`, or `compaction by <backend> failed: <why> -- the history is
/// unchanged`. `conversation` is the chat's own backend, never named.
[[nodiscard]] std::string compaction_line(const CompactionDone& done,
                                          std::string_view conversation);

/// `chats compact <name>`'s work (52d), on a saved chat: compacted, its
/// attachments told, saved -- what the verb runs once asked, and the shell's
/// Chats view once confirmed. Refused while a live task writes the chat.
/// Returns what was done: `compacted <id>: 12 messages into 3, by <backend>`
/// -- or, with no exchange to fold, `nothing to compact in <id> -- no
/// exchange to fold yet`, the chat untouched (55b). A summariser that fails
/// is refused naming why, the chat untouched.
[[nodiscard]] std::string compact_saved_chat(const std::string& name,
                                             const std::filesystem::path& config_path);

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
