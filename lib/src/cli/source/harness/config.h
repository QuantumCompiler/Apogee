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

/// Whether `type` drives a vendor's official CLI on a personal subscription.
///
/// One predicate, in the harness, because two surfaces refuse these by type
/// for the same reason and must not disagree: `serve` never dispatches to
/// one, and `analyze` never runs an agent on one -- the CLI runs its own
/// tools outside Apogee's gate, so an agent's tool policy cannot hold there.
[[nodiscard]] bool is_vendor_cli(BackendType type) noexcept;

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
/// expanded at load -- Ommi expanded only the former while its documentation
/// showed the latter, which is the trap this closes.
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
/// Three words, from Ommi's `fs_permissions`: `ask` prompts the user every
/// time and is the default when a tool is not listed; `allow` never prompts;
/// `deny` never runs. Kept as an enum in the harness rather than a string so
/// an unrecognised value fails at load -- a typo that read as "not allow" and
/// silently meant ask would be the wrong kind of forgiving.
enum class PermissionLevel : std::uint8_t { Ask, Allow, Deny };

[[nodiscard]] std::string_view to_string(PermissionLevel level) noexcept;
[[nodiscard]] std::optional<PermissionLevel> permission_level_from_string(
    std::string_view name) noexcept;

/// The `permissions:` section: one level per **tool name**.
///
/// Keyed by tool name rather than by a fixed pair of fields (Ommi gated
/// exactly `write_file` and `delete_file`) so the shell tool, the notes tools,
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

    PermissionsConfig permissions;
    ToolsConfig tools;
    KnowledgeConfig knowledge;
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
};

/// `expand_env`, then a leading `~` or `~/` replaced by the home directory.
/// What every path-like MCP field is read through.
[[nodiscard]] std::string expand_env_and_home(std::string_view input);

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
