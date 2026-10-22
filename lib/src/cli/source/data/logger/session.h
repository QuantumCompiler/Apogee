#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/todo.h"
#include "contracts/types.h"

/// Persisted chat sessions.
///
/// **The file is rewritten after every completed turn.** Crash safety is the
/// requirement, not a nicety: a `kill -9` mid-conversation must leave every
/// finished turn on disk. That is a property of when the write happens, which
/// is why persistence was specced with the REPL rather than bolted on after.
///
/// The write goes through the same temp-file-then-rename path the config engine
/// uses, so an interrupted write leaves the previous session intact rather than
/// a truncated file that will not parse.
namespace apogee::logger {

/// Schema version stamped on every session written.
///
/// A file without one is a **legacy** session: it is resumed best-effort with
/// current defaults and flagged, rather than refused. Refusing would make a
/// version bump destroy conversations.
///
/// 2 (26d): `attachments`. An absent list means none, so a version 1 file
/// reads as a chat with nothing attached.
inline constexpr int kCurrentSchemaVersion = 2;

/// Per-request settings, saved so a resumed session continues as it began.
struct InferenceParams {
    std::optional<double> temperature;
    std::optional<std::int64_t> max_tokens;
    std::string system_prompt;
    /// Whether the model thinks first, and for how long (26i), as `--think`
    /// or `/think` set them. Absent from an older file: unset, the backend's.
    /// Saved as `think` and `think_budget` -- a setting, never thinking itself,
    /// and named so a file holding any reasoning still stands out.
    std::optional<harness::ThinkingMode> thinking;
    std::optional<std::int64_t> thinking_budget;
    /// The level `--effort` or `/effort` chose (39a), saved as `effort`; held
    /// instead of a budget, never with one. Absent from an older file.
    std::optional<harness::Effort> effort;
};

/// Why a resume was imperfect.
///
/// Typed rather than free strings so a surface can decide how loudly to say
/// each, and so a new kind cannot be added without a name.
enum class WarningKind : std::uint8_t {
    /// Written before the schema existed; resumed best-effort.
    SchemaLegacy,
    /// The saved backend is no longer in the config.
    BackendMissing,
    /// The saved rerank judge is no longer in the config; resumed without it.
    RerankBackendMissing,
    /// The saved suite is no longer in the config (27d); resumed under the
    /// config's default suite, if it has one.
    SuiteMissing,
    /// The file parsed but a field was the wrong shape; a default was used.
    FieldDropped,
};

struct ResumeWarning {
    WarningKind kind = WarningKind::SchemaLegacy;
    /// The dependency's name; empty for SchemaLegacy.
    std::string subject;
    /// Ready to show the user.
    std::string message;
};

[[nodiscard]] std::string_view to_string(WarningKind kind) noexcept;

/// One file of an attachment (26d).
struct AttachedFile {
    /// What the model and the user call it: its path relative to the folder
    /// the chat was in when it was attached, `/`-separated -- or its absolute
    /// path, when it lies outside that folder. Citations use it.
    std::string name;
    /// Its absolute path when it was attached.
    std::string path;
    /// Its content's sha256: the key its chunks are indexed under, and what
    /// another chat's index is searched for before anything is embedded.
    std::string sha256;
    /// How its text was read: `text`, `pdftotext` or `html`.
    std::string reader;
    std::uint64_t bytes = 0;
};

/// A folder attachment's part of the chat's code graph (27n): the graph lives
/// in the chat's index beside the chunks, and this is what the session keeps
/// of it -- which part is whose, and what the notice said. Additive: an older
/// file has none, and reads as an attachment with no graph.
struct AttachmentGraph {
    /// The source member its files are recorded under in the index -- the
    /// folder's name, made unique among the chat's attachments. Empty when
    /// the graph is absent.
    std::string label;
    /// The files the build used, by language (`cpp`, `python`), and what it
    /// left out, by extension, reason or vendored directory (`.md`,
    /// `binary`, `third_party/`) -- the notice's parentheses.
    std::map<std::string, std::int64_t> supported;
    std::map<std::string, std::int64_t> skipped;
    /// Why there is no graph although one was asked for -- `cancelled`, or
    /// the error that stopped it -- so `/attachments` says the absence too.
    /// Empty when it was built.
    std::string absent;
};

/// A file, folder or glob attached to the chat (26d).
struct Attachment {
    /// As the user named it: `report.pdf`, `src`, `docs/*.md`.
    std::string name;
    std::vector<AttachedFile> files;
    /// The message its text rides, whole, when it was inlined; nullopt when
    /// its excerpts are retrieved instead. The text itself is never saved --
    /// it is rebuilt from the chat's index -- so the transcript keeps the
    /// message as typed.
    std::optional<std::size_t> inline_at;
    /// The message its map card rides (26q) -- a folder's or a glob's, never
    /// one file's -- or nullopt when it has none. Rebuilt from `files`, like
    /// the inlined text, so the transcript keeps the message as typed.
    std::optional<std::size_t> map_at;
    /// Its part of the chat's code graph (27n): a folder whose files include
    /// a supported language; nullopt for a file, a glob, or a folder with no
    /// code.
    std::optional<AttachmentGraph> graph;
};

/// One saved conversation.
struct Session {
    /// **Immutable.** Renaming goes through `custom_name`. An id that changed
    /// would break every reference to the session that already exists — a
    /// resume by id, a log line, something the user wrote down.
    std::string chat_id;

    /// User-chosen display name. Free to change.
    std::string custom_name;

    /// Model-generated one-liner, filled in after the first exchange.
    std::string title;

    std::string backend;
    /// The vendor roster model the chat runs on `backend` in place of the
    /// model its entry names (33), as `-m` or `/model` pinned it -- re-pinned
    /// when the chat resumes. Empty for a chat on its backend's own model,
    /// which writes nothing, so it saves exactly as before.
    std::string model;
    InferenceParams params;

    /// The conversation, in neutral IR.
    ///
    /// **Never contains thinking or injected context** — not by filtering here,
    /// but because the loop never puts them in history in the first place. The
    /// cleanliness test locks that property from this end.
    std::vector<harness::ChatMessage> messages;

    /// A provider-side session id, when the backend keeps one. For every
    /// backend Apogee's transcript is authoritative and this is only a resume
    /// optimization.
    std::string provider_session_id;

    std::string started_at;
    std::string updated_at;

    /// 0 means legacy — written before the schema existed.
    int schema_version = kCurrentSchemaVersion;

    /// Completed user↔assistant exchanges.
    int turns = 0;

    /// How many times this session has been compacted.
    int compactions = 0;

    /// Marked with `/private` (26l): never summarised for recall, its summary
    /// removed if it had one. Absent from an older file: false.
    bool private_chat = false;

    /// Opened with `chat --private` (52d): never written at all -- `save` is
    /// a no-op for it, so not one byte of the conversation lands under
    /// `sessions/`, not even to be deleted later. Never serialised: a file
    /// that exists was, by definition, not this.
    bool never_written = false;

    /// The session's `--retriever` SETTING (`lexical` / `vector` / `hybrid`;
    /// empty means auto) and its `--rerank` setting (a backend, `off`, or
    /// empty for the collections' pins). What the user chose, never what auto
    /// resolved to on some turn -- persisting a resolution would freeze an
    /// adaptive choice. Updated when `/retriever` or `/rerank` changes it, so
    /// a resumed session continues as it was last set.
    std::string retriever;
    std::string rerank;
    /// The suite the chat runs under (27d), as `--suite` or `/suite` left it:
    /// a suite's name, "" for one turned off, and unset for a chat that never
    /// had one -- which follows the config's default suite, and writes
    /// nothing, so a chat with no suite saves exactly as it did before.
    std::optional<std::string> suite;
    /// The task that drives this chat (27h): its id, or empty for a chat a
    /// person drives -- which writes nothing, so it saves exactly as before.
    /// The ledger names the chat and the chat names its task; `chats delete`
    /// refuses a chat whose task is live.
    std::string task;
    /// The plan the model keeps with `todo_write` (41d), as it last left it
    /// -- shown again when the chat resumes. Empty writes nothing, so a chat
    /// that never kept one saves exactly as before.
    harness::TodoList todo;
    /// What is attached to this chat, in the order attached (26d). Its index
    /// is `attachments/<chat_id>.db`, deleted with the chat.
    std::vector<Attachment> attachments;

    /// The name shown in listings: `custom_name` when set, else `title`, else
    /// the id.
    [[nodiscard]] std::string display_name() const;
};

/// What currently exists, so a resume can flag what does not.
///
/// Plain names rather than a `Config`, so this package stays free of a harness
/// config include. An empty list disables backend checking.
struct KnownDependencies {
    std::vector<std::string> backends;
    /// The configured suites (27d). A saved suite missing from it is a
    /// warning and dropped; with `check_suites` false nothing is checked.
    std::vector<std::string> suites{};
    bool check_suites = false;
};

struct LoadedSession {
    Session session;
    std::vector<ResumeWarning> warnings;
};

/// The directory sessions live in: `<APOGEE_HOME>/sessions`.
[[nodiscard]] std::filesystem::path sessions_dir();

/// The file for `chat_id`.
[[nodiscard]] std::filesystem::path session_path(const std::string& chat_id);

/// A fresh, unique chat id.
[[nodiscard]] std::string new_chat_id();

/// Serializes a session. Exposed so tests can assert on the bytes without
/// touching a filesystem.
[[nodiscard]] std::string serialize(const Session& session);

/// Parses a session, collecting warnings rather than failing.
///
/// **Resume degrades, never fails.** A missing field, an unknown shape, a
/// legacy schema — each produces a warning and a working session. A
/// conversation that cannot be reopened because one key moved is worse than one
/// that reopens with a note.
[[nodiscard]] LoadedSession deserialize(std::string_view text, const KnownDependencies& known);

/// Writes `session` atomically. Called after every completed turn. A
/// `never_written` session (52d) writes nothing.
void save(const Session& session);

/// Loads by chat id or by custom name.
/// Throws std::runtime_error only when the session cannot be found or read at
/// all -- every recoverable problem is a warning.
[[nodiscard]] LoadedSession load(const std::string& name, const KnownDependencies& known);

/// What renaming a backend did to the saved chats (46a).
struct SessionSweep {
    /// How many chats now name the new backend.
    std::size_t renamed = 0;
    /// The chats that named the old one and could not be rewritten: left as
    /// they were, their resume the orphan path's (`BackendMissing`).
    std::vector<std::string> failed;
};

/// Every saved chat that names backend `old_name` -- its backend, its rerank
/// judge -- rewritten to `new_name`, each file atomically and its
/// `updated_at` kept: a rename is no new turn, and the chats keep their
/// order. A file that does not parse is never rewritten; one that cannot be
/// written is counted in `failed`.
[[nodiscard]] SessionSweep rename_session_backend(std::string_view old_name,
                                                  std::string_view new_name);

/// Every saved session, newest first; a tie in `updated_at` by `chat_id`,
/// descending.
[[nodiscard]] std::vector<Session> list_sessions();

/// The most recently updated session, or nullopt when there are none.
/// What `--continue` resolves to.
[[nodiscard]] std::optional<Session> most_recent();

/// A conversation as plain text for a reader that is not a model client --
/// the knowledge clerk, above all: `User: ...` and `Assistant: ...` turns
/// separated by blank lines, system messages, tool results and empty turns
/// left out. Only the participants' words are the participants' words.
/// Rendered from the same grouping `saved_turns` reads (M27).
[[nodiscard]] std::string transcript_text(const std::vector<harness::ChatMessage>& messages);

/// How a compaction's summary message opens: `agentloop::compact_history`
/// writes it, and a read of a saved history knows a compaction by it (M27)
/// -- one declaration for both, so the two cannot drift.
inline constexpr std::string_view kCompactionPreamble =
    "The following is a summary of the conversation so far. Earlier messages have been "
    "compacted to preserve context window space. Use this summary as the authoritative "
    "record of what has been discussed:\n\n";

/// A tool call's arguments as one line: compact JSON (the text as written
/// when it is not JSON, its line breaks made spaces), clipped on a character
/// boundary. What a saved turn lists a call by, and what a stubbed tool
/// result repeats of the call it answers.
[[nodiscard]] std::string tool_call_summary(const harness::ToolCall& call);

/// A tool the model called in a saved turn (M27): its name, its arguments in
/// one line, and how much its result held -- never the result itself.
struct SavedTool {
    std::string name{};
    std::string summary{};
    /// The result's size in bytes; unset when the history keeps no result for
    /// the call.
    std::optional<std::size_t> result_bytes{};
};

/// Something attached, named -- never its bytes (M27).
struct SavedAttachment {
    /// As the user named it (`report.pdf`, `src`, `docs/*.md`); for media a
    /// message carries itself, its media type (`image/png`) or its URL.
    std::string name{};
    std::size_t files = 0;
    /// Its size in bytes; unset where the history does not say (an image
    /// named by URL).
    std::optional<std::uint64_t> bytes{};
};

/// One entry of a saved conversation (M27): a turn -- a user message and
/// everything after it up to the next -- or a compaction, where it happened,
/// with what the history kept after it up to the next user message.
struct SavedTurn {
    /// The conversation's turn count once the entry ended: the count a
    /// resumed session numbers on from (`next_turn` is the last entry's plus
    /// one), so an answered turn carries the number a machine session gave
    /// it. A turn left unanswered and a compaction add nothing: each carries
    /// the count before it.
    std::int64_t turn = 0;
    /// The user's words, trimmed; empty for a compaction.
    std::string user{};
    /// What rode the user's message: the media it carries itself, then what
    /// the chat attached on it, in the order attached.
    std::vector<SavedAttachment> attachments{};
    /// The model's words, a part per message in order -- what it said before
    /// a tool call too -- trimmed, empty ones left out.
    std::vector<std::string> answer{};
    /// The tools it called, in order.
    std::vector<SavedTool> tools{};
    /// Whether the history holds the model's answer: false for a turn
    /// cancelled at a terminal or failed, and for a compaction that kept no
    /// answer.
    bool answered = false;
    /// The summary, without its preamble, when the entry is a compaction.
    std::optional<std::string> compacted{};
};

/// `session`'s conversation as entries, in order (M27): what `chats show`
/// prints. Its system prompt is a setting, not a turn, and is left out; the
/// history holds no thinking and no injected context to leave out.
[[nodiscard]] std::vector<SavedTurn> saved_turns(const Session& session);

/// Every attachment `session` holds, named, in the order attached (M27).
[[nodiscard]] std::vector<SavedAttachment> saved_attachments(const Session& session);

}  // namespace apogee::logger
