#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// The translator (56a), the heart of `apogee acp`: pure functions between
/// the Agent Client Protocol and machine mode, both ways. An ACP session is a
/// captured child of the binary's own `execute` in machine mode, so ACP can
/// say only what machine mode says -- and `cli.machine_schema_conformance`
/// pins that.
///
///   - **Out:** each machine event becomes ACP session updates, a prompt's
///     answer, or a request to the host -- or is dropped, deliberately. Every
///     event type machine mode has is in `event_rules()`, and the unit suite
///     fails a type with no rule, as the parity laws do.
///   - **In:** a prompt's blocks become one `user` line, a cancel a `cancel`
///     line, a model or mode change a `set` line, the host's answer to a
///     permission sheet an `answer` line.
namespace apogee::acp {

/// The ACP major version this agent speaks, and the day it was last checked
/// against the protocol's schema (agentclientprotocol.com, the v1 schema):
/// re-checked whenever ACP work is touched and at every release roll, as the
/// carried model lists are (ADR 0009). A host asking for another version is
/// answered this one, as the protocol says.
inline constexpr int kProtocolVersion = 1;
inline constexpr std::string_view kProtocolReviewed = "2026-11-01";

/// What a machine event becomes.
enum class Disposition : std::uint8_t {
    /// The child's first event: the session is open, and `session/new` is
    /// answered.
    SessionStart,
    /// One or more `session/update`s (`Translator::updates`).
    Update,
    /// A notice: ACP's `notice` to a host that takes them, else the turn's
    /// thought stream -- the next turn's, when none is running.
    Notice,
    /// A question: the permission sheet, or `ask_user`.
    Question,
    /// The turn's end: the prompt's answer, with its stop reason.
    PromptEnd,
    /// The turn failed: the prompt's error, in Apogee's words.
    PromptError,
    /// Nothing reaches the host, for the reason the rule says.
    Dropped,
};

/// One machine event type's rule, and why.
struct EventRule {
    std::string_view type;
    Disposition disposition;
    std::string_view why;
};

/// Every machine event type's rule, in the protocol's order.
[[nodiscard]] std::span<const EventRule> event_rules() noexcept;

/// The rule for `type`, or null for a type this build does not know -- a
/// later child's event, dropped as a driver must.
[[nodiscard]] const EventRule* event_rule(std::string_view type) noexcept;

/// The ACP kind a tool is drawn as: one row per native tool.
struct KindRow {
    std::string_view tool;
    std::string_view kind;
    /// Whether the target the tool names is a path, so it is drawn as the
    /// call's location: the file tools'. Decided per tool, never by kind --
    /// a note tool is an edit or a delete whose target is the note's key.
    bool path = false;
};

/// The native tools' kinds. A tool no row names -- an MCP tool, a symphony
/// played as a tool -- is `other`.
[[nodiscard]] std::span<const KindRow> tool_kinds() noexcept;
[[nodiscard]] std::string_view tool_kind(std::string_view tool) noexcept;

/// Whether `tool`'s target is a path, so it is the call's location:
/// `read_file`, `write_file`, `edit_file`, `delete_file`, `list_directory`.
/// No tool a row does not name.
[[nodiscard]] bool tool_takes_path(std::string_view tool) noexcept;

/// A `result`'s `finish_reason` as a stop reason: `stop` → `end_turn`,
/// `length` → `max_tokens`, `content_filter` → `refusal`, `cancelled` →
/// `cancelled`, anything else → `end_turn`.
[[nodiscard]] std::string_view stop_reason(std::string_view finish_reason) noexcept;

/// A todo item's state as a plan entry's status: `pending` → `pending`,
/// `active` → `in_progress`, `done` → `completed`.
[[nodiscard]] std::string_view plan_status(std::string_view state) noexcept;

/// A `tool_call` event's state as a tool call's status: `pending` →
/// `pending`, `running` → `in_progress`, `done` → `completed`, `failed` →
/// `failed`.
[[nodiscard]] std::string_view tool_call_status(std::string_view state) noexcept;

/// A thought chunk: `text` on the reasoning channel.
[[nodiscard]] nlohmann::json thought_update(std::string_view text);

/// ACP's `notice` update, for a host that advertised `session.notices`.
[[nodiscard]] nlohmann::json notice_update(std::string_view text);

/// One session's translation state: the tool calls it has drawn, so a call's
/// first report is a `tool_call` and every later one an update.
///
/// **A card per call, whatever the model's ids.** A call's `pending` report
/// opens its card; a later report, and a question naming the call, reach the
/// card its id last opened. An id that repeats -- a provider whose wire gives
/// none, as Gemini's API, whose id is the tool's name -- opens a card of its
/// own under a fresh id (`write_file-2`), never the earlier call's again.
class Translator {
public:
    /// `cwd` is the session's folder: a relative path a tool names is a
    /// location under it. `home` is the folder a `~` path names, as the file
    /// tools expand it; empty, a `~` target is drawn with no location.
    explicit Translator(std::filesystem::path cwd, std::filesystem::path home = {});

    /// The session updates `event` becomes, in order -- the `update` of each
    /// `session/update` -- for an event of disposition `Update`; empty for
    /// one that says nothing to a host (a tool's own status line, which its
    /// `tool_call` already draws).
    [[nodiscard]] std::vector<nlohmann::json> updates(const nlohmann::json& event);

    /// The permission sheet's `toolCall` for a permission `question`: the
    /// card of the call it names, titled and located by the question's own
    /// tool and target -- what is being asked about -- its `detail` (a diff,
    /// a URL) as the call's text.
    [[nodiscard]] nlohmann::json permission_tool_call(const nlohmann::json& question);

private:
    struct Card {
        std::string name;
        std::string target;
    };

    /// The ACP id of the card a `tool_call` event reports on; `opened` when
    /// the event opens it.
    [[nodiscard]] std::string card_for(const nlohmann::json& event, bool& opened);
    /// `base` when no card has it, else `base-2`, `base-3`, ...
    [[nodiscard]] std::string fresh_id(const std::string& base) const;
    [[nodiscard]] nlohmann::json drawn(std::string_view id, const Card& call) const;
    /// A path target as the file tools resolve it -- `~` under the home
    /// folder, a relative path under the session's -- or empty: no target,
    /// or a `~` one with no home known.
    [[nodiscard]] std::filesystem::path location(std::string_view target) const;

    std::filesystem::path cwd_;
    std::filesystem::path home_;
    /// Every card drawn, by its ACP id.
    std::map<std::string, Card, std::less<>> cards_;
    /// The model's id for a call → the card it last opened.
    std::map<std::string, std::string, std::less<>> latest_;
    /// The card of the last call that came with no id.
    std::string anonymous_;
    std::size_t minted_ = 0;
};

/// The permission sheet's four options, each the answer line it sends:
/// `yes` → allow_once; `session` → allow_always, for this session; `always`
/// → allow_always, remembered in the config; `no` → reject_once.
[[nodiscard]] nlohmann::json permission_options(bool outbound);

/// The answer the host's response gives: the chosen option's word, or `no`
/// for a cancelled outcome, an error, an option no sheet offered, or no
/// response at all.
[[nodiscard]] std::string permission_answer(const nlohmann::json* result);

/// A prompt's blocks as one `user` line's text: text as written, a
/// `resource_link` (or a `resource` naming a file) as an `@"path"` mention,
/// which the session attaches through its own mention path. Empty with
/// `error` filled for a block it cannot carry -- an image, audio, embedded
/// content that is no file -- or a prompt with nothing in it.
[[nodiscard]] std::string prompt_text(const nlohmann::json& prompt, std::string& error);

/// A `file://` URI's path, percent-decoded; empty for any other URI.
[[nodiscard]] std::string file_uri_path(std::string_view uri);

/// A permission mode a host may choose: its id -- the word
/// `--permission-mode` takes -- and how it reads.
struct ModeChoice {
    std::string_view id;
    std::string_view name;
    std::string_view description;
};

/// The modes offered: manual, accept-edits and plan; bypass when the launch
/// allowed it; and the mode the session started in, when it is none of those
/// (dont-ask, chosen at launch). Never dont-ask otherwise: a host is always
/// there to ask.
[[nodiscard]] std::vector<ModeChoice> offered_modes(bool bypass_allowed,
                                                    std::string_view started_in);

/// ACP's `modes`: the current one, and those offered.
[[nodiscard]] nlohmann::json modes_state(const std::vector<ModeChoice>& modes,
                                         std::string_view current);

/// A model a host may choose: a configured backend that chats.
struct ModelChoice {
    std::string value;
    std::string name;
    std::string description{};
};

/// ACP's `configOptions`: the model, then the permission mode -- each a
/// `select` with its current value.
[[nodiscard]] nlohmann::json config_options(const std::vector<ModelChoice>& models,
                                            std::string_view model,
                                            const std::vector<ModeChoice>& modes,
                                            std::string_view mode);

/// `elicitation/create`'s params for an `ask_user` question (form mode): one
/// text field per question, `answer_1` on.
[[nodiscard]] nlohmann::json elicitation_params(const nlohmann::json& question,
                                                std::string_view session_id);

/// The answer lines' texts for `count` questions from the host's response
/// to the form: each field as given; for a declined or cancelled form, or
/// none, a line saying so -- the model goes on and says what it chose.
[[nodiscard]] std::vector<std::string> elicitation_answers(const nlohmann::json* result,
                                                           std::size_t count);

/// What `ask_user` is answered with where the host takes no questions.
[[nodiscard]] std::string nobody_to_ask();

/// The lines a session's child reads, each one JSON object without its
/// newline: a turn, a cancel, a setting, an answer, the host's hello.
[[nodiscard]] std::string user_line(std::string_view text);
[[nodiscard]] std::string cancel_line();
[[nodiscard]] std::string set_line(std::string_view key, std::string_view value);
[[nodiscard]] std::string answer_line(std::string_view text);
[[nodiscard]] std::string hello_line(std::string_view client, std::string_view version);

/// The setting a child's notice refuses, when it is the answer to a `set`
/// line that did not take -- `set model: ...` names `model` -- or empty.
[[nodiscard]] std::string refused_setting(std::string_view notice);

/// The hidden `execute` options a new session's child is started with (56c):
/// who began the chat -- the host's `clientInfo.name`, `acp` when it gave
/// none -- and the folder its `session/new` named, as it named it. The chat
/// saves them as its `origin` and `cwd`. Plumbing, never completed.
inline constexpr std::string_view kOriginOption = "--acp-origin";
inline constexpr std::string_view kCwdOption = "--acp-cwd";

/// The origin a chat is saved with when the host named itself nothing.
inline constexpr std::string_view kNoClientOrigin = "acp";

/// A saved chat as `session/list` reads it (56c): `chats list`'s row, with
/// who began it and where.
struct ListedChat {
    /// The chat's id: the session's id, as `chats list`, `chats show` and
    /// `--resume` take it.
    std::string id{};
    /// The name `chats list` shows -- the id itself while it has none.
    std::string name{};
    /// Its `updated_at`, ISO 8601 UTC.
    std::string updated_at{};
    /// Who began it, and in which folder; empty for a chat no host began.
    std::string origin{};
    std::string cwd{};
};

/// `session/list`'s `sessions` (56c): every chat an ACP host began -- one
/// with an origin and a folder -- in the order given (newest first, as
/// `chats list`), each its `sessionId`, `cwd`, `title` (the name, left out
/// while that is the bare id) and `updatedAt`. With `cwd` non-empty, only the
/// chats begun in exactly that folder.
[[nodiscard]] nlohmann::json listed_sessions(const std::vector<ListedChat>& chats,
                                             std::string_view cwd);

/// One entry of a saved chat as a replay sends it (56c): `logger::
/// saved_turns`' entry, in the words `chats show` prints it.
struct SavedEntry {
    /// The user's words, with what rode them named as `chats show` names
    /// them (`attached: report.pdf (1 file, 9 KiB)`); empty for a
    /// compaction.
    std::string user{};

    /// Each tool called, as `chats show` lists it: its name and its
    /// arguments in one line.
    struct Tool {
        std::string name{};
        std::string summary{};
    };

    std::vector<Tool> tools{};
    /// The model's words, a part per message.
    std::vector<std::string> answer{};
    /// The summary, when the entry is a compaction.
    std::optional<std::string> compacted{};
};

/// The `session/update`s a `session/load` replays (56c), in order: for each
/// entry, a compaction as an `agent_message_chunk` holding `chats show`'s
/// "history compacted" line and its summary; the user's words as one
/// `user_message_chunk`; each tool call as a `tool_call`, `completed`,
/// titled by its name and arguments, with no result; each answer part as an
/// `agent_message_chunk`. Ids come from the entry's index in `entries` --
/// the index of `chats show --output-format json`'s `turns` -- so every
/// `messageId` is distinct: `entry-<i>-user`, `entry-<i>-compaction`,
/// `entry-<i>-agent-<n>` and `toolCallId` `entry-<i>-tool-<n>`, `n` from 1.
/// Nothing hidden is replayed: a saved chat holds no thinking and no
/// injected context.
[[nodiscard]] std::vector<nlohmann::json> replay_updates(const std::vector<SavedEntry>& entries);

}  // namespace apogee::acp
