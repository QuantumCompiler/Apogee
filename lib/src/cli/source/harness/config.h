#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

/// The config engine's read path.
///
/// Loading is typed and total: a config either parses into these structs or
/// throws ConfigError with a message naming the offending key. There is no
/// half-loaded state and no silent default for a value the user got wrong --
/// SPEC.md's "fail loud on install/parity, degrade gracefully at runtime"
/// applies here on the loud side, because a misread config misroutes every
/// request that follows.
///
/// WRITING is deliberately not here. See harness/config_edit.h: mutations are
/// line-oriented text surgery on the file, never a marshal of these structs,
/// because marshaling strips the comments that carry most of a config file's
/// documentation.
namespace apogee::harness {

/// Raised for every user-facing config failure: unreadable file, malformed
/// YAML, unknown backend type, name collision, bad scalar.
class ConfigError : public std::runtime_error {
public:
    explicit ConfigError(const std::string& message) : std::runtime_error(message) {}
};

/// The backend kinds v0.1.0 accepts.
///
/// Deliberately closed. Later items widen it -- `claude-cli` and the vendor CLI
/// family, and eventually a SafeTensors type for training -- and each widening
/// is a recorded event in that item's document, never a silent edit. Adding one
/// means adding a row to kBackendTypeNames in config.cpp and a case here; the
/// loader dispatches through that table, so nothing else changes.
enum class BackendType : std::uint8_t {
    Anthropic,
    OpenAI,
    Google,
    LlamaCpp,
    ClaudeCli,
    CodexCli,
    GeminiCli,
    OllamaCli,
    Mock
};

/// The spelling of `type:` for `value`, e.g. "anthropic".
[[nodiscard]] std::string_view to_string(BackendType value) noexcept;

/// Parses a `type:` value. Returns nullopt for an unrecognized spelling; the
/// loader turns that into a ConfigError listing every accepted name.
[[nodiscard]] std::optional<BackendType> backend_type_from_string(std::string_view name) noexcept;

/// Every accepted `type:` spelling, in declaration order. Used to build error
/// messages and to drive tests that must stay honest as the enum widens.
[[nodiscard]] std::span<const std::string_view> backend_type_names() noexcept;

/// One entry under `backends:`.
///
/// Which fields matter depends on the type -- api_key for the cloud types,
/// model_path for llamacpp -- but the struct is flat rather than a variant
/// because the file is hand-edited: a user who moves an entry from `llamacpp`
/// to `anthropic` should get a clear "this field does nothing here" story
/// later, not a parse failure now.
struct BackendConfig {
    BackendType type = BackendType::Mock;

    /// Stored literally, `${ENV_VAR}` and all. Expanded on read -- see
    /// expand_env. Never logged: SPEC.md -> secrets are 0600, never logged.
    std::string api_key;

    std::string model;
    std::string model_path;

    /// The model used when this entry EMBEDS rather than chats.
    ///
    /// A cloud vendor serves chat and embeddings from different models behind
    /// one key, so one entry can do both: `model` answers questions,
    /// `embedding_model` turns text into vectors. Empty means the vendor's
    /// documented default (`text-embedding-3-small`, `gemini-embedding-001`).
    /// A local entry ignores it -- a GGUF embeds with whatever it is.
    std::string embedding_model;

    /// Path to the multimodal projector (an "mmproj" GGUF), for a local model
    /// that can read images.
    ///
    /// A field of its own rather than inferred from `model_path`, because
    /// inference guesses and a field states: projectors are separate files with
    /// no reliable naming relationship to their model, and picking the wrong
    /// one produces nonsense rather than an error. Unset means text-only, which
    /// is the common case.
    std::string mmproj_path;

    std::string system_prompt;

    /// Context window in tokens. Unlike Ommi -- whose claude entries omitted
    /// this because the claude CLI managed its own context -- Apogee owns
    /// context tracking for every backend, so cloud entries carry a window
    /// too. The model->window fallback table is harness-core's, not this
    /// item's: unset here means "ask the fallback", not "unlimited".
    std::optional<std::int64_t> context_size;

    std::optional<std::int64_t> max_tokens;
    std::optional<double> temperature;

    /// Vendor-CLI backends: the binary to spawn, resolved from PATH when it
    /// has no separator. Apogee never installs, bundles, or modifies it -- the
    /// user installs and logs in themselves (SPEC.md -> Principles).
    std::string binary;

    /// Where a vendor CLI's own server lives, when it has one.
    ///
    /// Only the Ollama backend uses this today: its CLI is a client of a local
    /// HTTP server, and Apogee checks that the server is already running before
    /// spawning anything -- because the CLI would otherwise START one, making
    /// Apogee the cause of a listening socket.
    std::string host;

    /// Vendor-CLI backends: which credentials the child may use.
    ///
    /// `subscription` (default) uses whatever the user's CLI is logged into.
    /// `bare` passes `--bare`, which skips hook/MCP/CLAUDE.md discovery **and
    /// disables subscription auth** -- the child then needs an API key. Right
    /// for CI; wrong for someone on their own machine.
    std::string mode;

    /// Seconds of inactivity after which a local backend releases its model.
    ///
    /// Local only, and unset means "stay resident" -- a loaded model is the
    /// point of in-process inference, so giving it back has to be asked for.
    /// It exists because that model is the largest thing the process holds:
    /// a long-lived `apogee chat` that has switched to a cloud backend should
    /// not keep 16GB pinned for a conversation it is no longer having.
    std::optional<std::int64_t> idle_unload_seconds;
};

/// The `models:` role pointers.
///
/// Each names a key in `backends`, and they are resolved through ONE shared
/// resolver -- `harness/roles.h` -- because CLI and HTTP must never grow
/// independent resolution chains, which is a real Ommi bug class. Read them
/// directly only to display or validate the raw value; to decide which backend
/// to RUN, call `resolve_backend_key()`. `cli.one_role_resolver` enforces it.
struct ModelsConfig {
    std::string default_backend;
    std::string default_embedding;
    std::string default_extraction;
};

/// One entry under `embeddings:` -- a RAG collection the config knows about.
///
/// A collection exists on disk the moment `apogee embed ingest` creates it,
/// with or without an entry here. The entry is what lets it carry settings:
/// today its chunk sizes, so a corpus of ADRs can want 768 where prose wants
/// 512 without anyone re-typing that on every ingest -- which is how corpora
/// end up chunked inconsistently. Later, `embedding-clients` hangs a
/// per-collection backend here; that field is deliberately not declared until
/// something consumes it.
///
/// Keyed by name in a map, like `backends:`, rather than Ommi's list of
/// `- name:` items: the edit helpers, the loader's collision check, and
/// `config get`'s dotted keys are all map-shaped, and a list would have needed
/// a second copy of every one of them.
struct EmbeddingConfig {
    /// Codepoints per chunk. Unset means the ingest default.
    std::optional<std::int64_t> chunk_size;
    /// Codepoints of overlap between chunks. Unset means the ingest default.
    std::optional<std::int64_t> chunk_overlap;
    /// Free text, for a listing. Never interpreted.
    std::string description;

    /// Which backend embeds this collection. Empty means the embedding role
    /// (`models.default_embedding`, then `models.default`).
    std::string backend;

    /// How this collection is searched when no flag says: `lexical`, `vector`,
    /// `hybrid`, or empty/`auto`. A pin, not a preference: a `vector` pin is
    /// never searched lexically, and is excluded with a note when its vectors
    /// do not qualify. Validated by `apogee check`, so a typo cannot silently
    /// mean auto.
    std::string retriever;

    /// The backend that reranks this collection's hits with one generation
    /// call, or `off`. Empty means no reranking.
    std::string rerank;
};

/// Optional search roots that pre-populate path prompts. All optional; a
/// missing value means "no default", not an error.
struct PathsConfig {
    std::string gguf_dir;
    std::string hf_dir;
    std::string mcp_dir;
    std::string embeddings_dir;
};

/// How operational status output is displayed. Consumed by the terminal UX
/// layer (chat-cli); parsed here so an invalid value fails at load rather than
/// three commands later.
enum class StatusMode : std::uint8_t { Line, Verbose, Quiet };

[[nodiscard]] std::string_view to_string(StatusMode value) noexcept;
[[nodiscard]] std::optional<StatusMode> status_mode_from_string(std::string_view name) noexcept;

/// Case-insensitive ordering for backend names.
///
/// Transparent, so a `std::string_view` looks up without allocating. Making
/// the MAP itself case-insensitive is what turns Ommi's silent-merge bug into
/// a detectable one: a second key differing only by case fails to insert, and
/// the loader reports the collision by name instead of dropping an entry.
struct CaseInsensitiveLess {
    using is_transparent = void;
    [[nodiscard]] bool operator()(std::string_view lhs, std::string_view rhs) const noexcept;
};

/// A whole config file, loaded.
struct Config {
    /// Backend entries keyed by their name AS WRITTEN in the file.
    ///
    /// Ordered by the case-folded name so iteration is deterministic across
    /// platforms. Two names differing only by case are rejected at load rather
    /// than merged -- Ommi's Viper-lowercasing lesson, made explicit: there,
    /// "Qwen3" and "qwen3" silently became one entry.
    std::map<std::string, BackendConfig, CaseInsensitiveLess> backends;

    ModelsConfig models;
    PathsConfig paths;

    /// Collection entries keyed by their name AS WRITTEN, compared
    /// case-insensitively for the same reason `backends` is.
    std::map<std::string, EmbeddingConfig, CaseInsensitiveLess> embeddings;

    /// A collection to retrieve from on EVERY turn, with no `--rag` flag.
    ///
    /// One name, not several: merging two collections' scores means merging
    /// incomparable scales, which is `vector-hybrid-rerank`'s problem and not
    /// this key's. Empty means off. The flag still wins -- `--rag other` for
    /// one run, `--rag ""` to switch it off for one run -- because a key that
    /// cannot be overridden per invocation is a key people stop using.
    ///
    /// Read at turn build, not at startup, so a mid-session edit takes effect
    /// on the next turn. It names a collection, which need not appear under
    /// `embeddings:` -- retrieval never depended on registration and does not
    /// start to here.
    std::string auto_rag;

    StatusMode status_mode = StatusMode::Line;
    bool color = true;

    /// Case-insensitive lookup, mirroring how the file's keys are compared at
    /// load. Returns nullptr when absent.
    [[nodiscard]] const BackendConfig* find_backend(std::string_view name) const noexcept;

    /// Backend names as written, in the map's (case-folded) order.
    [[nodiscard]] std::vector<std::string> backend_names() const;

    /// Case-insensitive lookup of a collection entry. nullptr when absent --
    /// which is not an error: an unregistered collection still works.
    [[nodiscard]] const EmbeddingConfig* find_embedding(std::string_view name) const noexcept;

    /// Collection names as written, in the map's (case-folded) order.
    [[nodiscard]] std::vector<std::string> embedding_names() const;
};

/// Expands `${VAR}` references against the process environment.
///
/// An undefined variable expands to the empty string, matching Ommi and the
/// shell: a config referencing ${ANTHROPIC_API_KEY} on a machine that has none
/// must still LOAD -- "local by default, cloud by choice" means a missing key
/// is a runtime message from the backend, not a parse error here. `$$` is a
/// literal `$`; a `${` with no closing brace is left untouched.
[[nodiscard]] std::string expand_env(std::string_view input);

/// Parses `content` as a config. Used directly by tests and by the edit
/// helpers' re-parse validation, which is why it takes text rather than a path.
///
/// `origin` names the source in error messages (a path, or something like
/// "<edit result>").
[[nodiscard]] Config parse_config(std::string_view content, std::string_view origin);

/// Reads and parses the file at `path`.
/// Throws ConfigError when it cannot be read or does not parse.
[[nodiscard]] Config load_config(const std::filesystem::path& path);

/// The starter config shipped by `apogee config init`, as bytes.
///
/// The checked-in sample at `lib/src/cli/assets/config.yaml` must byte-match
/// this exactly; a test enforces it, so the two can never drift (Ommi's
/// template-drift test, ported).
[[nodiscard]] std::string_view config_template() noexcept;

/// Writes config_template() to `path`, creating parent directories.
///
/// Refuses to overwrite an existing file unless `force`, and writes atomically
/// so an interrupted init cannot leave a truncated config behind.
void save_config_template(const std::filesystem::path& path, bool force);

}  // namespace apogee::harness
