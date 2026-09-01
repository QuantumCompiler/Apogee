#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "harness/types.h"

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
inline constexpr int kCurrentSchemaVersion = 1;

/// Per-request settings, saved so a resumed session continues as it began.
struct InferenceParams {
    std::optional<double> temperature;
    std::optional<std::int64_t> max_tokens;
    std::string system_prompt;
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

}  // namespace apogee::logger
