#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "contracts/types.h"

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
    /// Apple's MLX through a persistent Python child (27a): opt-in, Apple
    /// silicon only, refused at construction where its runtime is absent.
    Mlx,
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

/// How a local model stores its attention cache: the keys and values it keeps
/// for every position of the window, in every attention layer (26a).
///
/// `q8_0` is the default -- about half of `f16`'s memory, with flash attention
/// on, which a quantized cache needs. `q4_0` halves it again, at a cost to
/// long-context recall worth measuring before it is chosen.
enum class KvCacheType : std::uint8_t { F16, Q8_0, Q4_0 };

/// The spelling of `cache_type:` for `value`, e.g. "q8_0".
[[nodiscard]] std::string_view to_string(KvCacheType value) noexcept;

/// Parses a `cache_type:` value; nullopt for anything else.
[[nodiscard]] std::optional<KvCacheType> cache_type_from_string(std::string_view name) noexcept;

/// Whether `type` drives a vendor's official CLI on a personal subscription.
///
/// One predicate, in the harness, because two surfaces refuse these by type
/// for the same reason and must not disagree: `serve` never dispatches to
/// one, and `analyze` never runs an agent on one -- the CLI runs its own
/// tools outside Apogee's gate, so an agent's tool policy cannot hold there.
[[nodiscard]] bool is_vendor_cli(BackendType type) noexcept;

/// The largest `top_k` and `seed` a backend may name (26h). A vocabulary runs
/// to a few hundred thousand tokens, and llama.cpp reads the 32-bit maximum as
/// "draw a seed", so a fixed one stops just short of it.
inline constexpr std::int64_t kMaxTopK = 1'000'000;
inline constexpr std::int64_t kMaxSeed = 4'294'967'294;
/// The largest `thinking_budget` a backend may name (26i).
inline constexpr std::int64_t kMaxThinkingBudget = 1'000'000;

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

    /// Context window in tokens. Apogee owns context tracking for every
    /// backend, so cloud entries carry a window too. The model->window fallback
    /// table is harness-core's, not this item's: unset here means "ask the
    /// fallback", not "unlimited".
    std::optional<std::int64_t> context_size;

    /// A local backend's attention cache (26a). Unset is `q8_0`, and a model
    /// that cannot take it falls back to `f16`; a value written here is used
    /// as written or refused, never swapped.
    std::optional<KvCacheType> cache_type;

    std::optional<std::int64_t> max_tokens;
    std::optional<double> temperature;

    /// A local backend's sampling beyond the temperature (26h). Each unset
    /// takes the model file's own recommendation, then its family's published
    /// default, then llama.cpp's neutral value; a cloud backend ignores them,
    /// and `models info` says so.
    std::optional<double> top_p;
    std::optional<std::int64_t> top_k;
    std::optional<double> min_p;
    std::optional<double> repeat_penalty;
    std::optional<double> presence_penalty;
    /// A fixed sampling seed, so sampled output repeats. Unset draws a fresh
    /// one for every generation; at temperature 0 it does not matter.
    std::optional<std::int64_t> seed;

    /// Whether a reasoning model thinks, unless a request says (26i): `on`,
    /// `off` or `auto`. Unset is `on`, as before; `auto` decides per question.
    std::optional<ThinkingMode> thinking;
    /// The most tokens it may reason for before it must answer. Unset is no
    /// budget: a budget changes answers, so it is chosen, never implied.
    std::optional<std::int64_t> thinking_budget;

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
/// independent resolution chains, which is a real bug class. Read them
/// directly only to display or validate the raw value; to decide which backend
/// to RUN, call `resolve_backend_key()`. `cli.one_role_resolver` enforces it.
struct ModelsConfig {
    std::string default_backend;
    std::string default_embedding;
    std::string default_extraction;
    /// The helper roles (26b): a model that describes images for a chat model
    /// with no projector, one that transcribes audio, and one that does the
    /// chores -- titles, compaction, query rewriting, large tool results.
    std::string default_vision;
    std::string default_transcription;
    std::string default_utility;

    /// The suite every role resolves under (27d): a `suites:` name, or empty
    /// for none -- and none is exactly the chain as it was before suites.
    /// In the file it is the default; in the config a running session
    /// resolves against it is that session's ACTIVE suite, which `--suite`
    /// and `/suite` set in memory and never write back. A name with no
    /// `suites:` entry fails the load, as a typo here would otherwise run
    /// every command on the global pointers without a word.
    std::string default_suite;
};

/// The roles a suite names members for (27d), as `suites.<name>.members`
/// spells them: the six roles `harness/roles.h` resolves, called by the role
/// rather than by the pointer (`chat`, not `default`), in the order every
/// listing shows them.
[[nodiscard]] std::span<const std::string_view> suite_role_names() noexcept;

/// The words a suite member's `toolset:` takes (27d): the native toolsets
/// `tools/toolsets.h` registers (`fs`, `shell`, `git`, `notes`, `rag`, and
/// since 27l `graph`), `web`
/// (fetch_url and web_search) and `mcp` (every MCP server's tools). Declared
/// here, beside the parser that refuses any other word, as `lora_methods()`
/// is; a test holds it to the toolsets that exist.
[[nodiscard]] std::span<const std::string_view> suite_toolset_names() noexcept;

/// The word `--suite` and `/suite` take for "no suite" -- reserved, so no
/// suite may be named it.
inline constexpr std::string_view kSuiteOff = "off";

/// One member of a suite: the backend that answers for a role while the suite
/// is active, and the two knobs that make a small helper small (27d).
///
/// The knobs **pin the backend while the suite is active**, whichever role it
/// answers for at that moment: one backend is one loaded model with one
/// window, so a pin cannot follow the role. Two members of one suite pinning
/// the same backend two ways fail the load.
struct SuiteMember {
    /// A key under `backends:`, validated where it is used -- the existing
    /// "no backend named" wording at use, a `Fail` row in `check` -- never at
    /// load, exactly as a role pointer is.
    std::string backend;
    /// The window the backend runs at, in tokens: its `context_size` while
    /// the suite is active. Unset keeps the entry's own.
    std::optional<std::int64_t> context_size;
    /// The toolsets the backend is offered when it runs a conversation's
    /// tools, words of `suite_toolset_names()`. Unset offers every
    /// registered tool; an empty list offers none.
    std::optional<std::vector<std::string>> toolset;

    /// Whether either knob is set -- the long form is written for it.
    [[nodiscard]] bool pins() const noexcept {
        return context_size.has_value() || toolset.has_value();
    }

    bool operator==(const SuiteMember&) const = default;
};

/// How many member calls a turn may make, and how long a brief and an answer
/// may be (27f) -- the defaults a suite's `consult_caps:` adjusts. Named
/// constants, so every reader agrees on them.
inline constexpr std::int64_t kConsultsPerTurn = 4;
inline constexpr std::int64_t kConsultBriefTokens = 1024;
inline constexpr std::int64_t kConsultAnswerTokens = 512;

/// A suite's `consult_caps:` (27f): each unset takes its default above. The
/// member's own window stays the hard ceiling whatever these say.
struct ConsultCaps {
    std::optional<std::int64_t> per_turn;
    std::optional<std::int64_t> brief_tokens;
    std::optional<std::int64_t> answer_tokens;

    /// Whether any is set -- the block is written for it.
    [[nodiscard]] bool any() const noexcept {
        return per_turn.has_value() || brief_tokens.has_value() || answer_tokens.has_value();
    }

    bool operator==(const ConsultCaps&) const = default;
};

/// The caps as they hold: each set one, else its default.
struct ConsultLimits {
    std::int64_t per_turn = kConsultsPerTurn;
    std::int64_t brief_tokens = kConsultBriefTokens;
    std::int64_t answer_tokens = kConsultAnswerTokens;

    bool operator==(const ConsultLimits&) const = default;
};

[[nodiscard]] ConsultLimits consult_limits(const ConsultCaps& caps) noexcept;

/// The names `consult_caps:` takes, in the order it is written:
/// `per_turn`, `brief_tokens`, `answer_tokens`.
[[nodiscard]] std::span<const std::string_view> consult_cap_names() noexcept;

/// The roles a suite may name `consultable:` (27f): every role but `chat` --
/// the root does not consult itself -- and `embedding`, a model that turns
/// text into vectors and answers nothing.
[[nodiscard]] std::span<const std::string_view> consultable_role_names() noexcept;

/// The seams a suite's `validate:` block switches (27g), in the order it is
/// written: `tool_args` (a gated tool's arguments, before it runs),
/// `extraction` (a knowledge capture's record, against its source) and
/// `answers` (an answer, on request or always).
[[nodiscard]] std::span<const std::string_view> validate_seam_names() noexcept;

/// What `validate.answers` takes (27g): `request` -- `/check` only, the
/// default -- or `always`, a standing check after every answer.
[[nodiscard]] std::span<const std::string_view> answer_check_names() noexcept;

/// The role whose member checks when `validate.verifier` names none (27g):
/// the small helper a suite already has for the chores.
inline constexpr std::string_view kDefaultVerifier = "utility";

/// A suite's `validate:` block (27g): which seams a member checks, and which
/// member -- each unset field taking its default (the utility member, the
/// seams off, answers on request), and none set writing no block. Off by
/// default, opted into per seam.
struct ValidateConfig {
    /// The role whose member checks, one of `consultable_role_names()` with
    /// a member in the suite.
    std::optional<std::string> verifier;
    std::optional<bool> tool_args;
    std::optional<bool> extraction;
    /// One of `answer_check_names()`.
    std::optional<std::string> answers;

    /// Whether any is set -- the block is written for it.
    [[nodiscard]] bool any() const noexcept {
        return verifier.has_value() || tool_args.has_value() || extraction.has_value() ||
               answers.has_value();
    }

    bool operator==(const ValidateConfig&) const = default;
};

/// The block as it holds: each set field, else its default.
struct ValidatePolicy {
    std::string verifier{kDefaultVerifier};
    bool tool_args = false;
    bool extraction = false;
    bool answers_always = false;

    bool operator==(const ValidatePolicy&) const = default;
};

[[nodiscard]] ValidatePolicy validate_policy(const ValidateConfig& validate);

/// One entry under `suites:` -- a named bundle of models (27d): a member per
/// role it speaks for, every other role falling through the existing chain.
///
/// Later items hang their policy here, beside `members:` -- the consultable
/// members (27f), the validation seams (27g), orchestration (27t, not yet
/// declared) -- and none of them is declared until something consumes it.
struct SuiteConfig {
    /// Free text, for a listing. Never interpreted.
    std::string description;
    /// Keyed by role name (`suite_role_names()`); a role absent here is not
    /// spoken for.
    std::map<std::string, SuiteMember, std::less<>> members;
    /// The roles whose members the root may consult through the `consult`
    /// tool (27f), each one of `consultable_role_names()` with a member in
    /// this suite, as written. Empty offers no tool. Whether a member is local
    /// and unmetered is its provider's to say, so that is held where a
    /// provider can be asked -- the config verbs, and the tool at use.
    std::vector<std::string> consultable;
    /// What bounds them (27f) -- and validation too (27g), which spends from
    /// the same per-turn budget.
    ConsultCaps consult_caps;
    /// Which seams a member checks, and which member (27g). Its verifier must
    /// be a member of this suite; whether that member is local and unmetered
    /// is held where a provider can be asked, as for `consultable:`.
    ValidateConfig validate;

    bool operator==(const SuiteConfig&) const = default;
};

/// The roles a symphony stage may play (27q): every suite role that answers a
/// prompt -- `chat`, `extraction`, `vision`, `transcription`, `utility` --
/// and never `embedding`, which answers none. In listing order.
[[nodiscard]] std::span<const std::string_view> symphony_role_names() noexcept;

/// What a symphony takes in (27q): the text a play is given, described for
/// whoever plays it, and whether it carries an image too.
struct SymphonyInput {
    /// Free text: what the input is (`The passage to summarize.`). Shown by
    /// `symphonies show`; never interpreted.
    std::string description;
    /// The play takes an image beside its text (`play --image`), which a
    /// stage marked `image: true` is sent with its prompt.
    bool image = false;

    bool operator==(const SymphonyInput&) const = default;
};

/// One stage of a symphony (27q): a suite ROLE -- never a backend: the suite
/// decides placement, the symphony decides process -- given a prompt rendered
/// from the input and the earlier stages' answers, through the one member
/// call (`agentloop/member_call`).
///
/// **Or another symphony, played** (27r): a stage that names a symphony under
/// `play:` instead of a role is that symphony's whole walk, given `input` as
/// its `{{input}}` -- its stages the same member calls an inline stage makes
/// -- and its output is this stage's answer, threaded on exactly as a role
/// stage's is. A chain is a symphony; there is no other kind.
struct SymphonyStage {
    /// Unique in its symphony, letters, digits, `_` and `-`; what later
    /// stages name its answer by (`{{summarize}}`). Never `input`.
    std::string name;
    /// One of `symphony_role_names()`. Empty for a play stage.
    std::string role;
    /// The symphony this stage plays, by name (27r). Empty for a role stage:
    /// a stage is one kind or the other, never both.
    std::string play;
    /// A play stage's template for what the played symphony is given as its
    /// `{{input}}`, the prompt's grammar (27r). Empty means the previous
    /// stage's answer -- the symphony's own input for the first stage -- so
    /// stages that each play a symphony chain without a word. A role stage
    /// has none: its brief is its `prompt`.
    std::string input;
    /// The template, as written: `{{input}}` and `{{<an earlier stage>}}`
    /// are replaced at play time (`business/symphony/definition`), and
    /// nothing else in it is touched. Never `${ENV}`-expanded.
    std::string prompt;
    /// A JSON Schema the stage's answer is held to (26f's grammar on a local
    /// member), as its author wrote it -- the text rides the request as
    /// written, so a grammar writes the properties in the order it lists
    /// them. Empty for a plain-text answer.
    std::string schema;
    /// The input's image goes with this stage's prompt.
    bool image = false;
    /// This stage's caps; unset takes the symphony defaults
    /// (`business/symphony/definition`). The member's window stays the
    /// ceiling over both.
    std::optional<std::int64_t> brief_tokens;
    std::optional<std::int64_t> answer_tokens;

    /// Whether this stage plays a symphony rather than a role (27r).
    [[nodiscard]] bool plays() const noexcept {
        return !play.empty();
    }

    bool operator==(const SymphonyStage&) const = default;
};

/// A symphony (27q): a named, staged prompt process -- an input in, its
/// stages played in order, each one member call on a role of the active
/// suite, and the last stage's answer out. From a `symphonies:` config entry
/// or a spec file -- the same parser reads both (the `training.pipelines`
/// precedent) -- or a shipped starter, which is a spec file compiled in.
struct SymphonySpec {
    std::string name;
    /// Free text, for a listing.
    std::string description;
    SymphonyInput input;
    /// In play order; at least one.
    std::vector<SymphonyStage> stages;

    bool operator==(const SymphonySpec&) const = default;
};

/// How deep symphonies may nest (27r) when `symphony_caps.depth` does not
/// say: a symphony that plays none is 1 deep, one that plays it 2. Deep
/// enough for real composition, shallow enough that the serial latency of a
/// whole walk stays legible.
inline constexpr std::int64_t kSymphonyDepth = 4;

/// The `symphony_caps:` section (27r), hand-edited like `memory:`: how deep a
/// play may nest, and the whole walk's budget. The budget aggregates across
/// every symphony a play reaches -- a chain cannot multiply its way past a
/// cap a single symphony honors -- and a walk that reaches it stops there,
/// named, with no answer.
struct SymphonyCaps {
    /// The nesting cap, refused at definition time like a loop. Unset is
    /// `kSymphonyDepth`.
    std::optional<std::int64_t> depth;
    /// The member calls one whole play may make. Unset is the walk's own
    /// count -- each stage once -- so a play is never refused for its size.
    std::optional<std::int64_t> stage_calls;
    /// The answer tokens one whole play may produce. Unset is none.
    std::optional<std::int64_t> answer_tokens;

    [[nodiscard]] std::int64_t max_depth() const noexcept {
        return depth.value_or(kSymphonyDepth);
    }

    bool operator==(const SymphonyCaps&) const = default;
};

/// Whether two role-pointer sets are identical. Defined in `config.cpp` -- the
/// one place besides the resolver allowed to name the fields -- so a caller
/// comparing configs (the admin plane's `restart_required`) never reads a
/// pointer directly.
[[nodiscard]] bool operator==(const ModelsConfig& lhs, const ModelsConfig& rhs) noexcept;

/// The `graph:` block on an `embeddings:` entry -- the knowledge graph built
/// over a collection's chunks and walked at retrieval time. Every field is
/// optional; a bare `apogee graph build` needs none of them.
struct GraphConfig {
    /// Set by the first successful build, through the config editor. What
    /// gates retrieval-time expansion on every surface.
    bool enabled = false;
    /// The backend `graph build` extracts with when `-m` is not given. Empty
    /// means the extraction role, then the default.
    std::string extract_backend;
    /// Expansion depth at retrieval: 1 or 2 edge steps.
    int hops = 1;
    /// Neighbour entities an expansion injects, at most.
    int max_entities = 8;
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
/// Keyed by name in a map, like `backends:`, rather than a list of `- name:`
/// items: the edit helpers, the loader's collision check, and `config get`'s
/// dotted keys are all map-shaped, and a list would have needed a second copy
/// of every one of them.
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

    /// The knowledge graph over this collection, when one is configured.
    GraphConfig graph;
};

/// One entry under `graphs:` -- a **named knowledge graph spanning several
/// collections**, keyed by name like every other section. Its database is
/// derived data at `<embeddings_dir>/graphs/<name>.db`, created lazily by
/// the first `apogee graph build <name>` -- never by an installer -- with
/// one node per entity across every member, so expansion crosses collection
/// boundaries. There is no `enabled`: building is the enablement, and a
/// built named graph takes retrieval precedence for its members. A graph
/// name must never collide with a collection name -- resolution is
/// graphs-first everywhere, and `apogee check` fails the collision.
struct NamedGraphConfig {
    /// The member collections, each an `embeddings:` name. Removing one
    /// converges on the next build -- its rows reconcile away.
    std::vector<std::string> collections;
    /// Source trees parsed into the graph by the code build (27k), each an
    /// absolute directory (`~` and `${VAR}` expanded at load). Removing one
    /// converges on the next build or update -- its rows are forgotten.
    std::vector<std::string> sources;
    /// The code build's grammars, by roster name (`cpp`, `python`, ...);
    /// empty means every vendored one. Validated by `apogee check` and the
    /// writers, not at load: the roster is the graph package's.
    std::vector<std::string> languages;
    /// The backend `graph build <name>` extracts with when `-m` is not
    /// given. Empty means the extraction role, then the default.
    std::string extract_backend;
    /// Expansion depth at retrieval: 1 or 2 edge steps.
    int hops = 1;
    /// Neighbour entities an expansion injects, at most.
    int max_entities = 8;
};

/// One entry under `mcp_servers:` -- a stdio MCP server the loop connects
/// to at startup and whose tools appear as `mcp__<name>__<tool>`.
///
/// Keyed by name in a map like `backends:` and `embeddings:`, so the edit
/// helpers, the loader's collision check and `config get` all work the same
/// way. `command`, `args` and `env` have `${ENV_VAR}` and a leading `~`
/// expanded at load -- both, because expanding only the former while the
/// documentation shows the latter is a trap.
struct McpServerConfig {
    /// The executable, or a bare program name found on PATH.
    std::string command;
    std::vector<std::string> args;
    /// `KEY=VALUE` entries overlaying the inherited environment.
    std::vector<std::string> env;
    /// A disabled server is listed by `mcp list` and never dialled.
    bool enabled = true;
};

/// What an agent may call. **This is the agent's permission model**: a policy
/// is enforced by what is registered, never by asking the model to behave.
///
/// `ReadOnly` registers only tools that declare no `writes` -- nothing to
/// prompt for, so a read-only agent never blocks, on any surface. `All` puts
/// the agent under the same gate as `chat`. `None` registers nothing.
enum class AgentToolPolicy : std::uint8_t { ReadOnly, All, None };

[[nodiscard]] std::string_view to_string(AgentToolPolicy policy) noexcept;
[[nodiscard]] std::optional<AgentToolPolicy> agent_tool_policy_from_string(
    std::string_view name) noexcept;
/// The words `agent_tool_policy_from_string` accepts, for completion.
[[nodiscard]] std::vector<std::string_view> agent_tool_policy_names();

/// How an agent's schema shapes its answer.
///
/// `Auto`: the model is asked for JSON conforming to the schema, the answer
/// is validated, and it is rendered to Markdown in-process. `Json`: the same,
/// printed raw. `Markdown`: the schema is a checklist and the model writes
/// prose -- nothing is validated, and no provider is asked for JSON.
enum class AgentOutputFormat : std::uint8_t { Auto, Json, Markdown };

[[nodiscard]] std::string_view to_string(AgentOutputFormat format) noexcept;
[[nodiscard]] std::optional<AgentOutputFormat> agent_output_format_from_string(
    std::string_view name) noexcept;
/// The words `agent_output_format_from_string` accepts, for completion.
[[nodiscard]] std::vector<std::string_view> agent_output_format_names();

/// A fine-tune's `method`: a pipeline or regime stage's `method:`, and `train
/// run --method` -- one list, so the config and the flag accept the same.
[[nodiscard]] std::span<const std::string_view> lora_methods() noexcept;

/// One entry under `agents:` -- a named workflow `apogee analyze --agent`
/// runs: a persona assembled from prompt files, an optional output schema,
/// a tool policy, and the knobs below. Agents are data, executed by the same
/// loop every other surface runs.
///
/// Keyed by name in a map like every other section. Paths are read through
/// `expand_env_and_home`; a RELATIVE path resolves against the data directory
/// the config lives in (`prompts/x.txt`), which is what keeps an entry
/// portable across machines and a `--config` temp tree hermetic.
struct AgentConfig {
    std::string description;
    /// Backend override; empty means the chat role.
    std::string model;
    /// `.txt` files concatenated into the system prompt, in order.
    std::vector<std::string> prompts;
    /// JSON Schema files the answer must satisfy. More than one is joined
    /// in the instruction; the FIRST is what the answer is validated against.
    std::vector<std::string> schemas;
    AgentOutputFormat output_format = AgentOutputFormat::Auto;
    AgentToolPolicy tools = AgentToolPolicy::ReadOnly;
    /// Names of `mcp_servers:` entries this agent connects to. Empty means
    /// none: an agent is a curated workflow and names what it needs.
    std::vector<std::string> mcp;
    /// Opts the agent into `ask_user` on a terminal. Off by default: most
    /// agents are unattended report generators, for which a blocking prompt
    /// is the wrong default.
    bool questions = false;
    /// A collection retrieved from on every turn -- `auto_rag`, read from the
    /// agent instead of the top-level key. `--rag` still wins per run.
    std::string collection;
    /// Where a run's report is saved. Empty means the layout's `analyses/`.
    std::string save_dir;
    /// The saved file's base name; empty means the agent's name.
    std::string save_filename;
    /// A directory under `save_dir` this agent's reports nest in, so agents
    /// do not all glob into one directory. Empty means flat.
    std::string save_subdir;
};

/// The `ui:` section -- how a terminal shows what Apogee prints.
struct UiConfig {
    /// Render answers' Markdown on a terminal (bold, lists, tables, code
    /// blocks). A pipe, machine mode and the saved transcript always carry the
    /// model's text as written; this only chooses what a terminal shows.
    /// `--raw` turns it off for one run.
    bool markdown = true;
};

/// The `knowledge:` section -- the organizational knowledge layer's two
/// settings. Read-only here: hand-edited, like `auto_rag`.
struct KnowledgeConfig {
    /// Distil one record from an interactive chat when it ends cleanly. Off
    /// by default: a generation call per session, and most chats are
    /// low-signal.
    bool auto_capture = false;

    /// The collection records go into. Empty means the default, `knowledge`.
    std::string db;

    /// The default collection name.
    static constexpr std::string_view kDefaultCollection = "knowledge";

    /// `db`, or the default when unset.
    [[nodiscard]] std::string collection() const;
};

/// How an attached folder is represented beyond its chunks (27p): `code`
/// parses a folder of code into the chat's code graph (27n) -- its functions,
/// classes and the calls between them, with no model -- and `off` indexes
/// its chunks alone. Deliberately two words: richer methods wait until one
/// ships to name.
enum class AttachmentGraphMethod : std::uint8_t { Code, Off };

[[nodiscard]] std::string_view to_string(AttachmentGraphMethod method) noexcept;
[[nodiscard]] std::optional<AttachmentGraphMethod> attachment_graph_method_from_string(
    std::string_view name) noexcept;
/// The words `attachments.graph` and every `--graph` take, in order: `code`,
/// `off` -- for help, completion and the refusal of any other.
[[nodiscard]] std::vector<std::string_view> attachment_graph_method_names();
/// `<label>: unknown value '<got>' (accepted: code, off)` -- the one refusal
/// of a method word, wherever it was typed. An empty label omits the prefix
/// (CLI11 prints the option's name in front of a validator's message).
[[nodiscard]] std::string attachment_graph_values_message(std::string_view label,
                                                          std::string_view got);

/// The `attachments:` section (27p): the default an attach takes when nothing
/// on the line says otherwise. Read by the loader and written only by the
/// comment-preserving editor (`set_attachments_graph`).
struct AttachmentsConfig {
    /// Unset means each surface's built-in -- a chat builds a folder's code
    /// graph, `complete`'s one-shot store does not (27n) -- and set, it holds
    /// for both. A `--graph` on one attach beats it.
    std::optional<AttachmentGraphMethod> graph;
};

/// The `memory:` section (26l). Read-only here: hand-edited, like `auto_rag`.
struct MemoryConfig {
    /// Whether `chat` summarises its finished chats and recalls them in new
    /// ones. On by default (the user's call); `--no-recall` turns it off for
    /// a run and `/recall off` for a session. Never on `serve`, whatever
    /// this says.
    bool recall = true;
};

/// Optional search roots that pre-populate path prompts. All optional; a
/// missing value means "no default", not an error.
struct PathsConfig {
    std::string gguf_dir;
    std::string hf_dir;
    std::string mcp_dir;
    std::string embeddings_dir;
};

/// What the permission gate does before a destructive tool runs.
///
/// Three words: `ask` prompts the user every time and is the default when a
/// tool is not listed; `allow` never prompts; `deny` never runs. Kept as an
/// enum in the harness rather than a string so an unrecognised value fails at
/// load -- a typo that read as "not allow" and silently meant ask would be the
/// wrong kind of forgiving.
enum class PermissionLevel : std::uint8_t { Ask, Allow, Deny };

[[nodiscard]] std::string_view to_string(PermissionLevel level) noexcept;
[[nodiscard]] std::optional<PermissionLevel> permission_level_from_string(
    std::string_view name) noexcept;

/// The `permissions:` section: one level per **tool name**.
///
/// Keyed by tool name rather than by a fixed pair of fields (one for
/// `write_file`, one for `delete_file`) so the shell tool, the notes tools,
/// and a namespaced MCP tool all fit the same schema without a new key each.
/// Every destructive tool consults it through one checker; a read-only tool
/// never does, because prompting for reads trains the user to say yes.
struct PermissionsConfig {
    std::map<std::string, PermissionLevel, std::less<>> levels;

    /// The level for `tool`; `Ask` when it is not listed.
    [[nodiscard]] PermissionLevel level(std::string_view tool) const noexcept;
};

/// One stage of a training pipeline (`training.pipelines.<name>.stages[]`,
/// or a stage in a spec file). The dataset and the suite are NAMES OR PATHS
/// resolved the way `train run --dataset` and `train eval --suite` resolve
/// theirs; zero means the driver's default, as on `train run`.
struct PipelineStageSpec {
    std::string name;
    std::string dataset;
    /// `lora` (the default when empty) or `qlora`.
    std::string method;
    int iters = 0;
    int batch_size = 0;
    int num_layers = 0;
    bool grad_checkpoint = false;
    bool mask_prompt = false;
    /// Required: the stage's own suite, gated cumulatively with every prior
    /// stage's.
    std::string eval_suite;
    /// A deterministic sample of each prior stage's dataset mixed into this
    /// one, `0.1` for a tenth. 0 (the default) mixes nothing.
    double rehearsal_fraction = 0.0;
};

/// A multi-stage pipeline: the student snapshot and the ordered stages, each
/// a fresh LoRA on the previous stage's fused weights. From a YAML file or
/// a `training.pipelines:` entry -- the same parser reads both.
struct PipelineSpec {
    std::string name;
    /// The snapshot, as `train run` names one: a directory, or a name under
    /// `paths.hf_dir` or `models/`.
    std::string student;
    std::vector<PipelineStageSpec> stages;
};

/// A regime: a teacher distilling a dataset per kit, the kits as one
/// eval-gated pipeline over the student, the last passing stage promoted.
/// From flags, a `training.regimes:` entry, or a spec file; flags win.
struct RegimeSpec {
    std::string name;
    std::string teacher;
    std::string student;
    /// Ordered kit names; each becomes one stage.
    std::vector<std::string> kits;
    /// Examples per kit; 0 means each kit's own `synth.count`.
    int count = 0;
    /// The backend the last passing stage is promoted into; empty leaves
    /// promotion manual.
    std::string promote_as;
    /// Per-stage iterations; 0 means each kit's `train.iters`.
    int iters = 0;
    /// The teacher's sampling temperature; 0 means each kit's.
    double temperature = 0.0;
};

/// One source the cycle collects from.
struct CycleSourceConfig {
    /// `directory` or `sessions`.
    std::string type;
    /// `directory`: the queue scanned for `*.jsonl`; empty means
    /// `training/cycle/queue/`.
    std::string dir;
    /// `sessions`: MUST be true. The user's own chats are user data, and
    /// training a model on its own outputs reinforces its mistakes.
    bool log_consent = false;
    /// `sessions`: only chats that ran on this backend; empty means any.
    std::string backend;
    /// `sessions`: only chats on or after `YYYY-MM-DD`; empty means any.
    std::string since;
};

/// The `training.cycle:` block -- the unattended, scheduler-invoked pass.
struct CycleConfig {
    /// A `training.pipelines:` name or a spec file path.
    std::string pipeline;
    /// The backend a passing cycle promotes into.
    std::string backend;
    /// The promoted version pinned as the anchor; 0 means the first passing
    /// cycle's version becomes it.
    int anchor_version = 0;
    /// The score drop tolerated against the last passing cycle and against
    /// the anchor, in `[0, 1]`. 0 is strict no-regression.
    double regression_threshold = 0.0;
    /// Consecutive failed cycles that halt the loop until `cycle resume`; 0
    /// disables the breaker (not recommended unattended).
    int circuit_breaker_k = kDefaultCircuitBreakerK;
    std::vector<CycleSourceConfig> sources;
    /// Overrides `training.judge_backend` for the cycle's evals.
    std::string judge_backend;

    static constexpr int kDefaultCircuitBreakerK = 3;

    /// Whether the block says enough to run: a pipeline, a backend and at
    /// least one source.
    [[nodiscard]] bool configured() const noexcept {
        return !pipeline.empty() && !backend.empty() && !sources.empty();
    }
};

/// The `training:` section -- the training track's knobs. Declared field by
/// field as the items that read them land: today the Python boundary alone.
struct TrainingConfig {
    /// The interpreter the training environment (`training/venv/`) is seeded
    /// FROM by `apogee train setup`. `${ENV_VAR}` and a leading `~` are
    /// expanded at use. Empty means `python3` on PATH. Never the interpreter
    /// a script runs under -- that is always the environment's own.
    std::string python;

    /// Which trainer `train run` uses when `--trainer` is not given: `auto`
    /// (the default when empty -- `mlx` on Apple Silicon, `peft` where
    /// `nvidia-smi` is on PATH), `mlx`, `peft`, or `mock`.
    std::string trainer;

    /// The backend `train eval` asks to judge items without an `expected`
    /// substring, pairwise against the untuned base. Empty means such items
    /// skip and auto-pass, loudly. Named explicitly here or by `--judge`,
    /// never a metered default.
    std::string judge_backend;

    /// The suite `train eval` runs when `--suite` is not given.
    std::string eval_suite_path;

    /// Promoted GGUFs kept per backend; the oldest inactive ones are pruned
    /// on promote. 0 keeps every version.
    int retain_versions = kDefaultRetainVersions;

    /// `hard` (the default): promote refuses a run whose eval has not run or
    /// has not passed. `soft`: the same is a warning. `--force` skips the
    /// gate either way.
    std::string gate_mode;

    /// Named pipelines, `train pipeline run --pipeline <name>`.
    std::map<std::string, PipelineSpec, std::less<>> pipelines;
    /// Named regimes, `train regime run <name>`.
    std::map<std::string, RegimeSpec, std::less<>> regimes;
    /// The continuous cycle.
    CycleConfig cycle;

    static constexpr int kDefaultRetainVersions = 3;
    static constexpr std::string_view kGateHard = "hard";
    static constexpr std::string_view kGateSoft = "soft";

    /// `gate_mode`, or `hard` when unset.
    [[nodiscard]] std::string_view effective_gate_mode() const noexcept {
        return gate_mode.empty() ? kGateHard : std::string_view{gate_mode};
    }
};

/// The `tools:` section: where the native toolsets operate.
/// `tools.search`: the web search `web_search` answers from (25e). Absent,
/// no search tool is registered -- a model is never offered a tool that can
/// only fail. Kept as written: an unknown provider or an unusable URL loads,
/// registers nothing, and `check` names it.
struct SearchConfig {
    /// `searxng`, the one provider so far.
    std::string provider;
    /// The instance's address, such as `http://127.0.0.1:8888`. `${ENV_VAR}`
    /// references are expanded.
    std::string url;
    /// How many results a search returns, 1 to 20.
    int results = 5;

    /// Whether the section is present at all.
    [[nodiscard]] bool configured() const noexcept {
        return !provider.empty() || !url.empty();
    }
};

struct ToolsConfig {
    /// The directory the filesystem tools are sandboxed to. Empty means the
    /// folder Apogee was started in (2026-09-25; it was the home directory).
    /// `${ENV_VAR}` references are expanded.
    std::string fs_root;

    /// Toolsets switched off by name (`fs`, `shell`, `git`, `notes`, `rag`).
    /// Enablement, not safety: the permission gate is the safety.
    std::vector<std::string> disabled;

    /// The websites `fetch_url` reaches without asking, by exact host
    /// (`harness/host.h`). Any other host asks first, and where nobody can
    /// answer -- a pipe, `serve` -- it is refused. As written; an entry that
    /// is not a bare host matches nothing, and `check` reports it.
    std::vector<std::string> allowed_hosts;

    SearchConfig search;

    [[nodiscard]] bool is_disabled(std::string_view toolset) const noexcept;
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
/// the MAP itself case-insensitive is what makes a case-only duplicate
/// detectable rather than a silent merge: a second key differing only by case
/// fails to insert, and the loader reports the collision by name instead of
/// dropping an entry.
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
    /// than merged, so "Qwen3" and "qwen3" can never silently become one entry.
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

    PermissionsConfig permissions;
    ToolsConfig tools;
    KnowledgeConfig knowledge;
    MemoryConfig memory;
    AttachmentsConfig attachments;
    UiConfig ui;
    TrainingConfig training;

    /// MCP servers keyed by name AS WRITTEN, compared case-insensitively
    /// for the same reason `backends` is.
    std::map<std::string, McpServerConfig, CaseInsensitiveLess> mcp_servers;

    /// Agents keyed by name AS WRITTEN, compared case-insensitively. The
    /// bundled agents are not here unless the file overrides one: they are
    /// compiled in (see `harness/assets.h`) and a same-named entry wins.
    std::map<std::string, AgentConfig, CaseInsensitiveLess> agents;

    /// Named multi-collection graphs keyed by name AS WRITTEN, compared
    /// case-insensitively, in the file's order (the retrieval precedence
    /// rule reads them first to last, so the order is the user's).
    std::vector<std::pair<std::string, NamedGraphConfig>> graphs;

    /// Suites keyed by name AS WRITTEN, compared case-insensitively (27d).
    /// Which backend a member gives a role is `harness/roles.h`'s answer, the
    /// one chain; read the members directly only to display or validate them.
    std::map<std::string, SuiteConfig, CaseInsensitiveLess> suites;

    /// Symphonies keyed by name AS WRITTEN, compared case-insensitively
    /// (27q). The shipped starters are not here unless the file overrides
    /// one: they are compiled in (`contracts/assets.h`) and a same-named
    /// entry wins.
    std::map<std::string, SymphonySpec, CaseInsensitiveLess> symphonies;
    /// How deep a play nests and the whole walk's budget (27r).
    SymphonyCaps symphony_caps;

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

    /// Case-insensitive lookup of an MCP server entry. nullptr when absent.
    [[nodiscard]] const McpServerConfig* find_mcp_server(std::string_view name) const noexcept;

    /// Server names as written, in the map's (case-folded) order.
    [[nodiscard]] std::vector<std::string> mcp_server_names() const;

    /// Case-insensitive lookup of an agent entry. nullptr when absent.
    [[nodiscard]] const AgentConfig* find_agent(std::string_view name) const noexcept;

    /// Agent names as written, in the map's (case-folded) order.
    [[nodiscard]] std::vector<std::string> agent_names() const;

    /// Case-insensitive lookup of a `graphs:` entry. nullptr when absent.
    [[nodiscard]] const NamedGraphConfig* find_graph(std::string_view name) const noexcept;

    /// Graph names as written, in the file's order.
    [[nodiscard]] std::vector<std::string> graph_names() const;

    /// Case-insensitive lookup of a `suites:` entry. nullptr when absent.
    [[nodiscard]] const SuiteConfig* find_suite(std::string_view name) const noexcept;

    /// Suite names as written, in the map's (case-folded) order.
    [[nodiscard]] std::vector<std::string> suite_names() const;

    /// Case-insensitive lookup of a `symphonies:` entry. nullptr when absent.
    [[nodiscard]] const SymphonySpec* find_symphony(std::string_view name) const noexcept;
};

/// The active suite: the entry `models.default_suite` names, surrounding
/// whitespace ignored as the resolver ignores it on every pointer. nullptr
/// when it names none.
[[nodiscard]] const SuiteConfig* active_suite(const Config& config);

/// What the active suite (`models.default_suite`) pins on one backend: the
/// knobs of the member that names it (27d). Both unset when no suite is
/// active, or none of its members names the backend.
struct MemberPins {
    std::optional<std::int64_t> context_size;
    std::optional<std::vector<std::string>> toolset;

    bool operator==(const MemberPins&) const = default;
};

/// The active suite's pins on `backend`, matched as backend names are
/// (case-insensitively). The one reader of a member's knobs: the provider
/// factory, the harness's window and every surface's tool offer all ask it,
/// so a pin cannot hold on one of them and not another.
[[nodiscard]] MemberPins suite_pins(const Config& config, std::string_view backend);

/// One backend a suite puts to use, and the roles it answers for there (27e)
/// -- one model, one window, one residency, however many roles it serves.
struct SuiteBackend {
    /// As the first member naming it spells it.
    std::string backend;
    /// Role names (`suite_role_names()`), in that order.
    std::vector<std::string> roles;

    bool operator==(const SuiteBackend&) const = default;
};

/// The backends `suite`'s members name, each once (matched as backend names
/// are), in the order of their first role in `suite_role_names()`. What a
/// suite holds resident, warms, and is measured by (27e).
[[nodiscard]] std::vector<SuiteBackend> suite_backends(const SuiteConfig& suite);

/// `backends.<name>` as it runs under the active suite: the entry as written,
/// its `context_size` replaced by the suite's pin when there is one. What the
/// provider factory constructs from. A default entry when there is no such
/// backend.
[[nodiscard]] BackendConfig backend_as_run(const Config& config, std::string_view name);

/// `expand_env`, then a leading `~` or `~/` replaced by the home directory.
/// What every path-like MCP field is read through.
[[nodiscard]] std::string expand_env_and_home(std::string_view input);

/// Expands `${VAR}` references against the process environment.
///
/// An undefined variable expands to the empty string, matching the shell: a
/// config referencing ${ANTHROPIC_API_KEY} on a machine that has none must
/// still LOAD -- "local by default, cloud by choice" means a missing key is a
/// runtime message from the backend, not a parse error here. `$$` is a
/// literal `$`; a `${` with no closing brace is left untouched.
[[nodiscard]] std::string expand_env(std::string_view input);

/// Parses `content` as a config. Used directly by tests and by the edit
/// helpers' re-parse validation, which is why it takes text rather than a path.
///
/// `origin` names the source in error messages (a path, or something like
/// "<edit result>").
[[nodiscard]] Config parse_config(std::string_view content, std::string_view origin);

/// A pipeline spec from YAML text -- the same parser and the same rules a
/// `training.pipelines:` entry gets: at least one stage, every stage with a
/// name, a dataset and an eval suite, a known method, numbers in range.
/// The name defaults to `fallback_name` when the text carries none. Throws
/// ConfigError naming what is wrong.
[[nodiscard]] PipelineSpec parse_pipeline_spec(std::string_view content, std::string_view origin,
                                               std::string_view fallback_name = {});

/// A regime spec from YAML text, as a `training.regimes:` entry is read.
[[nodiscard]] RegimeSpec parse_regime_spec(std::string_view content, std::string_view origin,
                                           std::string_view fallback_name = {});

/// A symphony from a spec file's YAML text (27q) -- the same parser and the
/// same rules a `symphonies:` entry gets: at least one stage, each with a
/// unique name, a prompt and a role of `symphony_role_names()` -- a stage
/// naming a backend (a `backend:` or `model:` key, or a role that is one of
/// `backends`, the configured names) refused with the principle: the suite
/// decides placement -- a schema that is a JSON object, positive caps, and
/// no key it does not know. A stage may instead play a symphony by name
/// (27r): `play:` and optionally `input:`, never a role, a prompt, a schema
/// or caps beside it, and never the symphony's own name -- the loop the
/// parser can see (`contracts/symphony_walk.h`; a config's entries are walked
/// against each other on load). The name defaults to `fallback_name` when
/// the text carries none. What the templates and schemas mean is
/// `business/symphony/definition`'s to check. Throws ConfigError naming
/// what is wrong.
[[nodiscard]] SymphonySpec parse_symphony_spec(std::string_view content, std::string_view origin,
                                               std::string_view fallback_name = {},
                                               const std::vector<std::string>& backends = {});

/// Whether `name` can name a symphony or a stage: letters, digits, `_` and
/// `-`, not empty.
[[nodiscard]] bool is_symphony_name(std::string_view name) noexcept;

/// Reads and parses the file at `path`.
/// Throws ConfigError when it cannot be read or does not parse.
[[nodiscard]] Config load_config(const std::filesystem::path& path);

/// The starter config shipped by `apogee config init`, as bytes.
///
/// The checked-in sample at `lib/src/cli/assets/config.yaml` must byte-match
/// this exactly; a test enforces it, so the two can never drift.
[[nodiscard]] std::string_view config_template() noexcept;

/// Writes config_template() to `path`, creating parent directories.
///
/// Refuses to overwrite an existing file unless `force`, and writes atomically
/// so an interrupted init cannot leave a truncated config behind.
void save_config_template(const std::filesystem::path& path, bool force);

}  // namespace apogee::harness
