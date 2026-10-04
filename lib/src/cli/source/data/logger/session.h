#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

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
    InferenceParams params;

    /// The conversation, in neutral IR.
    ///
    /// **Never contains thinking or injected context** — not by filtering here,
    /// but because the loop never puts them in history in the first place. The
    /// cleanliness test locks that property from this end.
    std::vector<harness::ChatMessage> messages;

    /// A provider-side session id, when the backend keeps one. Generalizes
    /// Ommi's Claude-specific field: for every backend Apogee's transcript is
    /// authoritative and this is only a resume optimization.
    std::string provider_session_id;

    std::string started_at;
    std::string updated_at;

    /// 0 means legacy — written before the schema existed.
    int schema_version = kCurrentSchemaVersion;

    /// Completed user↔assistant exchanges.
    int turns = 0;

    /// How many times this session has been compacted.
    int compactions = 0;

    /// The session's `--retriever` SETTING (`lexical` / `vector` / `hybrid`;
    /// empty means auto) and its `--rerank` setting (a backend, `off`, or
    /// empty for the collections' pins). What the user chose, never what auto
    /// resolved to on some turn -- persisting a resolution would freeze an
    /// adaptive choice. Updated when `/retriever` or `/rerank` changes it, so
    /// a resumed session continues as it was last set.
    std::string retriever;
    std::string rerank;
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

/// Writes `session` atomically. Called after every completed turn.
void save(const Session& session);

/// Loads by chat id or by custom name.
/// Throws std::runtime_error only when the session cannot be found or read at
/// all -- every recoverable problem is a warning.
[[nodiscard]] LoadedSession load(const std::string& name, const KnownDependencies& known);

/// Every saved session, newest first.
[[nodiscard]] std::vector<Session> list_sessions();

/// The most recently updated session, or nullopt when there are none.
/// What `--continue` resolves to.
[[nodiscard]] std::optional<Session> most_recent();

/// A conversation as plain text for a reader that is not a model client --
/// the knowledge clerk, above all: `User: ...` and `Assistant: ...` turns
/// separated by blank lines, system messages, tool results and empty turns
/// left out. Only the participants' words are the participants' words.
[[nodiscard]] std::string transcript_text(const std::vector<harness::ChatMessage>& messages);

}  // namespace apogee::logger
