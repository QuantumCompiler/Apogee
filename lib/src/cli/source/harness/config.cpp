#include "harness/config.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <utility>

#include "platform/platform.h"

namespace apogee::harness {
namespace {

/// ASCII-only case folding.
///
/// Deliberately not locale-aware: config keys are identifiers, and a
/// locale-sensitive fold would make the same file load differently on two
/// machines -- the Turkish dotless-i problem, which is a real class of bug in
/// config loaders that reach for tolower() with the default locale.
char fold(char c) noexcept {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

/// The one place a backend type name is spelled.
///
/// Widening the enum is a row here plus a case in the switch below -- the
/// loader dispatches through this table, so no other file learns the new name.
constexpr std::array<std::pair<std::string_view, BackendType>, 9> kBackendTypeNames{{
    {"anthropic", BackendType::Anthropic},
    {"openai", BackendType::OpenAI},
    {"google", BackendType::Google},
    {"llamacpp", BackendType::LlamaCpp},
    {"claude-cli", BackendType::ClaudeCli},
    {"codex-cli", BackendType::CodexCli},
    {"gemini-cli", BackendType::GeminiCli},
    {"ollama-cli", BackendType::OllamaCli},
    {"mock", BackendType::Mock},
}};

constexpr std::array<std::pair<std::string_view, StatusMode>, 3> kStatusModeNames{{
    {"line", StatusMode::Line},
    {"verbose", StatusMode::Verbose},
    {"quiet", StatusMode::Quiet},
}};

constexpr std::array<std::pair<std::string_view, AgentToolPolicy>, 3> kAgentToolPolicyNames{{
    {"read-only", AgentToolPolicy::ReadOnly},
    {"all", AgentToolPolicy::All},
    {"none", AgentToolPolicy::None},
}};

constexpr std::array<std::pair<std::string_view, AgentOutputFormat>, 3> kAgentOutputFormatNames{{
    {"auto", AgentOutputFormat::Auto},
    {"json", AgentOutputFormat::Json},
    {"markdown", AgentOutputFormat::Markdown},
}};

std::string accepted_backend_types() {
    std::string out;
    for (const auto& [name, unused] : kBackendTypeNames) {
        if (!out.empty()) {
            out += ", ";
        }
        out += name;
    }
    return out;
}

[[noreturn]] void fail(std::string_view origin, std::string_view message) {
    std::ostringstream out;
    out << origin << ": " << message;
    throw ConfigError(out.str());
}

/// Reads a scalar as a string, expanding ${ENV} references.
/// Returns "" for a null/missing node so an empty key is not an error.
std::string scalar(const YAML::Node& node, std::string_view origin, std::string_view key) {
    if (!node.IsDefined() || node.IsNull()) {
        return {};
    }
    if (!node.IsScalar()) {
        fail(origin, std::string{key} + ": expected a single value");
    }
    return expand_env(node.Scalar());
}

std::optional<std::int64_t> integer(const YAML::Node& node, std::string_view origin,
                                    std::string_view key) {
    if (!node.IsDefined() || node.IsNull()) {
        return std::nullopt;
    }
    try {
        return node.as<std::int64_t>();
    } catch (const YAML::Exception&) {
        fail(origin, std::string{key} + ": expected a whole number, got '" + node.Scalar() + "'");
    }
}

std::optional<double> number(const YAML::Node& node, std::string_view origin,
                             std::string_view key) {
    if (!node.IsDefined() || node.IsNull()) {
        return std::nullopt;
    }
    try {
        return node.as<double>();
    } catch (const YAML::Exception&) {
        fail(origin, std::string{key} + ": expected a number, got '" + node.Scalar() + "'");
    }
}

BackendConfig parse_backend(const YAML::Node& node, std::string_view origin,
                            const std::string& name) {
    const std::string where = "backends." + name;
    if (!node.IsMap()) {
        fail(origin, where + ": expected a block of settings");
    }

    BackendConfig backend;

    const std::string type_name = scalar(node["type"], origin, where + ".type");
    if (type_name.empty()) {
        fail(origin,
             where + ": missing required 'type' (one of: " + accepted_backend_types() + ")");
    }
    const std::optional<BackendType> type = backend_type_from_string(type_name);
    if (!type.has_value()) {
        fail(origin, where + ": unknown type '" + type_name +
                         "' (accepted types: " + accepted_backend_types() + ")");
    }
    backend.type = *type;

    backend.api_key = scalar(node["api_key"], origin, where + ".api_key");
    backend.model = scalar(node["model"], origin, where + ".model");
    backend.model_path = scalar(node["model_path"], origin, where + ".model_path");
    backend.embedding_model = scalar(node["embedding_model"], origin, where + ".embedding_model");

    backend.mmproj_path = scalar(node["mmproj_path"], origin, where + ".mmproj_path");
    backend.system_prompt = scalar(node["system_prompt"], origin, where + ".system_prompt");
    backend.context_size = integer(node["context_size"], origin, where + ".context_size");
    backend.max_tokens = integer(node["max_tokens"], origin, where + ".max_tokens");
    backend.temperature = number(node["temperature"], origin, where + ".temperature");
    backend.binary = scalar(node["binary"], origin, where + ".binary");
    backend.mode = scalar(node["mode"], origin, where + ".mode");
    backend.host = scalar(node["host"], origin, where + ".host");
    backend.idle_unload_seconds =
        integer(node["idle_unload_seconds"], origin, where + ".idle_unload_seconds");

    return backend;
}

/// A `true`/`false` scalar, or a load failure naming the key.
bool boolean(const YAML::Node& node, std::string_view origin, std::string_view key, bool fallback) {
    if (!node.IsDefined() || node.IsNull()) {
        return fallback;
    }
    const std::string value = scalar(node, origin, key);
    if (value == "true") {
        return true;
    }
    if (value == "false") {
        return false;
    }
    fail(origin, std::string{key} + ": '" + value + "' is not true or false");
}

/// A list of strings at `node`, each read through `expand_env_and_home`
/// when `paths`, else through `expand_env`.
std::vector<std::string> string_list(const YAML::Node& node, std::string_view origin,
                                     std::string_view key, bool paths) {
    std::vector<std::string> out;
    if (!node.IsDefined() || node.IsNull()) {
        return out;
    }
    if (!node.IsSequence()) {
        fail(origin, std::string{key} + ": expected a list of strings");
    }
    for (const YAML::Node& item : node) {
        const std::string value = scalar(item, origin, std::string{key} + "[]");
        out.push_back(paths ? expand_env_and_home(value) : value);
    }
    return out;
}

/// A whole number at `node` that must be zero or positive; `fallback` when
/// absent.
int non_negative(const YAML::Node& node, std::string_view origin, const std::string& key,
                 int fallback) {
    const std::optional<std::int64_t> value = integer(node, origin, key);
    if (!value.has_value()) {
        return fallback;
    }
    if (*value < 0) {
        fail(origin, key + ": must be 0 or positive");
    }
    return static_cast<int>(*value);
}

/// A number in `[low, high]` at `node`; `fallback` when absent.
double bounded(const YAML::Node& node, std::string_view origin, const std::string& key, double low,
               double high, double fallback) {
    const std::optional<double> value = number(node, origin, key);
    if (!value.has_value()) {
        return fallback;
    }
    if (*value < low || *value > high) {
        fail(origin,
             key + ": must be between " + std::to_string(low) + " and " + std::to_string(high));
    }
    return *value;
}

PipelineStageSpec parse_pipeline_stage(const YAML::Node& node, std::string_view origin,
                                       const std::string& where) {
    if (!node.IsMap()) {
        fail(origin, where + ": expected a block of settings");
    }
    PipelineStageSpec stage;
    stage.name = scalar(node["name"], origin, where + ".name");
    if (stage.name.empty()) {
        fail(origin, where + ": a stage needs a name");
    }
    stage.dataset = expand_env_and_home(scalar(node["dataset"], origin, where + ".dataset"));
    if (stage.dataset.empty()) {
        fail(origin, where + " ('" + stage.name + "'): a stage needs a dataset");
    }
    stage.eval_suite =
        expand_env_and_home(scalar(node["eval_suite"], origin, where + ".eval_suite"));
    if (stage.eval_suite.empty()) {
        fail(origin, where + " ('" + stage.name +
                         "'): a stage needs an eval_suite -- the cumulative gate is the "
                         "pipeline's contract");
    }
    stage.method = scalar(node["method"], origin, where + ".method");
    if (!stage.method.empty() &&
        std::ranges::find(lora_methods(), std::string_view{stage.method}) == lora_methods().end()) {
        fail(origin,
             where + ".method: unknown value '" + stage.method + "' (accepted: lora, qlora)");
    }
    stage.iters = non_negative(node["iters"], origin, where + ".iters", 0);
    stage.batch_size = non_negative(node["batch_size"], origin, where + ".batch_size", 0);
    stage.num_layers = non_negative(node["num_layers"], origin, where + ".num_layers", 0);
    stage.grad_checkpoint =
        boolean(node["grad_checkpoint"], origin, where + ".grad_checkpoint", false);
    stage.mask_prompt = boolean(node["mask_prompt"], origin, where + ".mask_prompt", false);
    stage.rehearsal_fraction =
        bounded(node["rehearsal_fraction"], origin, where + ".rehearsal_fraction", 0.0, 1.0, 0.0);
    return stage;
}

PipelineSpec parse_pipeline_node(const YAML::Node& node, std::string_view origin,
                                 const std::string& where, std::string_view fallback_name) {
    if (!node.IsDefined() || node.IsNull() || !node.IsMap()) {
        fail(origin, where + ": expected a block of settings");
    }
    PipelineSpec spec;
    spec.name = scalar(node["name"], origin, where + ".name");
    if (spec.name.empty()) {
        spec.name = std::string{fallback_name};
    }
    spec.student = expand_env_and_home(scalar(node["student"], origin, where + ".student"));
    const YAML::Node stages = node["stages"];
    if (!stages.IsDefined() || stages.IsNull() || !stages.IsSequence() || stages.size() == 0) {
        fail(origin, where + ": a pipeline needs at least one stage under 'stages'");
    }
    int index = 0;
    for (const YAML::Node& stage : stages) {
        spec.stages.push_back(
            parse_pipeline_stage(stage, origin, where + ".stages[" + std::to_string(index) + "]"));
        ++index;
    }
    return spec;
}

RegimeSpec parse_regime_node(const YAML::Node& node, std::string_view origin,
                             const std::string& where, std::string_view fallback_name) {
    if (!node.IsDefined() || node.IsNull() || !node.IsMap()) {
        fail(origin, where + ": expected a block of settings");
    }
    RegimeSpec spec;
    spec.name = scalar(node["name"], origin, where + ".name");
    if (spec.name.empty()) {
        spec.name = std::string{fallback_name};
    }
    spec.teacher = scalar(node["teacher"], origin, where + ".teacher");
    spec.student = expand_env_and_home(scalar(node["student"], origin, where + ".student"));
    spec.kits = string_list(node["kits"], origin, where + ".kits", false);
    for (const std::string& kit : spec.kits) {
        if (kit.empty()) {
            fail(origin, where + ".kits: an empty kit name");
        }
    }
    spec.count = non_negative(node["count"], origin, where + ".count", 0);
    spec.promote_as = scalar(node["promote_as"], origin, where + ".promote_as");
    spec.iters = non_negative(node["iters"], origin, where + ".iters", 0);
    spec.temperature = bounded(node["temperature"], origin, where + ".temperature", 0.0, 2.0, 0.0);
    return spec;
}

CycleSourceConfig parse_cycle_source(const YAML::Node& node, std::string_view origin,
                                     const std::string& where) {
    if (!node.IsMap()) {
        fail(origin, where + ": expected a block with a 'type'");
    }
    CycleSourceConfig source;
    source.type = scalar(node["type"], origin, where + ".type");
    if (source.type != "directory" && source.type != "sessions") {
        fail(origin,
             where + ".type: unknown value '" + source.type + "' (accepted: directory, sessions)");
    }
    source.dir = expand_env_and_home(scalar(node["dir"], origin, where + ".dir"));
    source.log_consent = boolean(node["log_consent"], origin, where + ".log_consent", false);
    source.backend = scalar(node["backend"], origin, where + ".backend");
    source.since = scalar(node["since"], origin, where + ".since");
    if (source.type == "sessions" && !source.log_consent) {
        // The refusal names the key and the two risks, at load rather than
        // at 3 a.m. under a scheduler.
        fail(origin, where +
                         ": a 'sessions' source needs 'log_consent: true' -- your chat "
                         "sessions are your own data (privacy), and training a model on its "
                         "own answers reinforces its mistakes (self-reinforcement); set the "
                         "key only once you have read both");
    }
    if (!source.since.empty()) {
        const bool shaped = source.since.size() == 10 && source.since[4] == '-' &&
                            source.since[7] == '-' &&
                            std::all_of(source.since.begin(), source.since.end(),
                                        [](char c) { return c == '-' || (c >= '0' && c <= '9'); });
        if (!shaped) {
            fail(origin, where + ".since: '" + source.since + "' is not a YYYY-MM-DD date");
        }
    }
    return source;
}

CycleConfig parse_cycle(const YAML::Node& node, std::string_view origin) {
    const std::string where = "training.cycle";
    if (!node.IsMap()) {
        fail(origin, where + ": expected a block of settings");
    }
    CycleConfig cycle;
    cycle.pipeline = expand_env_and_home(scalar(node["pipeline"], origin, where + ".pipeline"));
    cycle.backend = scalar(node["backend"], origin, where + ".backend");
    cycle.anchor_version =
        non_negative(node["anchor_version"], origin, where + ".anchor_version", 0);
    cycle.regression_threshold = bounded(node["regression_threshold"], origin,
                                         where + ".regression_threshold", 0.0, 1.0, 0.0);
    cycle.circuit_breaker_k =
        non_negative(node["circuit_breaker_k"], origin, where + ".circuit_breaker_k",
                     CycleConfig::kDefaultCircuitBreakerK);
    cycle.judge_backend = scalar(node["judge_backend"], origin, where + ".judge_backend");
    if (const YAML::Node sources = node["sources"]; sources.IsDefined() && !sources.IsNull()) {
        if (!sources.IsSequence()) {
            fail(origin, where + ".sources: expected a list of sources");
        }
        int index = 0;
        for (const YAML::Node& source : sources) {
            cycle.sources.push_back(parse_cycle_source(
                source, origin, where + ".sources[" + std::to_string(index) + "]"));
            ++index;
        }
    }
    return cycle;
}

AgentConfig parse_agent(const YAML::Node& node, std::string_view origin, const std::string& name) {
    const std::string where = "agents." + name;
    AgentConfig agent;
    if (!node.IsDefined() || node.IsNull()) {
        return agent;
    }
    if (!node.IsMap()) {
        fail(origin, where + ": expected a mapping of settings");
    }
    agent.description = scalar(node["description"], origin, where + ".description");
    agent.model = scalar(node["model"], origin, where + ".model");
    agent.prompts = string_list(node["prompts"], origin, where + ".prompts", true);
    agent.schemas = string_list(node["schemas"], origin, where + ".schemas", true);
    agent.mcp = string_list(node["mcp"], origin, where + ".mcp", false);
    agent.questions = boolean(node["questions"], origin, where + ".questions", false);
    agent.collection = scalar(node["collection"], origin, where + ".collection");
    agent.save_dir = expand_env_and_home(scalar(node["save_dir"], origin, where + ".save_dir"));
    agent.save_filename = scalar(node["save_filename"], origin, where + ".save_filename");
    agent.save_subdir = scalar(node["save_subdir"], origin, where + ".save_subdir");

    // Every enum is validated at load: a typo that silently meant `all` would
    // be the wrong kind of forgiving for the field that IS the permission
    // model.
    if (const std::string value = scalar(node["tools"], origin, where + ".tools"); !value.empty()) {
        const std::optional<AgentToolPolicy> policy = agent_tool_policy_from_string(value);
        if (!policy.has_value()) {
            fail(origin, where + ".tools: '" + value +
                             "' is not a tool policy (accepted: read-only, all, none)");
        }
        agent.tools = *policy;
    }
    if (const std::string value = scalar(node["output_format"], origin, where + ".output_format");
        !value.empty()) {
        const std::optional<AgentOutputFormat> format = agent_output_format_from_string(value);
        if (!format.has_value()) {
            fail(origin, where + ".output_format: '" + value +
                             "' is not an output format (accepted: auto, json, markdown)");
        }
        agent.output_format = *format;
    }
    return agent;
}

}  // namespace

bool CaseInsensitiveLess::operator()(std::string_view lhs, std::string_view rhs) const noexcept {
    return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
                                        [](char a, char b) noexcept { return fold(a) < fold(b); });
}

std::string_view to_string(BackendType value) noexcept {
    for (const auto& [name, type] : kBackendTypeNames) {
        if (type == value) {
            return name;
        }
    }
    return "unknown";
}

std::optional<BackendType> backend_type_from_string(std::string_view name) noexcept {
    for (const auto& [candidate, type] : kBackendTypeNames) {
        if (candidate == name) {
            return type;
        }
    }
    return std::nullopt;
}

bool is_vendor_cli(BackendType type) noexcept {
    switch (type) {
        case BackendType::ClaudeCli:
        case BackendType::CodexCli:
        case BackendType::GeminiCli:
        case BackendType::OllamaCli:
            return true;
        case BackendType::Anthropic:
        case BackendType::OpenAI:
        case BackendType::Google:
        case BackendType::LlamaCpp:
        case BackendType::Mock:
            return false;
    }
    return false;
}

std::string_view to_string(AgentToolPolicy policy) noexcept {
    for (const auto& [name, candidate] : kAgentToolPolicyNames) {
        if (candidate == policy) {
            return name;
        }
    }
    return "read-only";
}

std::optional<AgentToolPolicy> agent_tool_policy_from_string(std::string_view name) noexcept {
    for (const auto& [candidate, policy] : kAgentToolPolicyNames) {
        if (candidate == name) {
            return policy;
        }
    }
    return std::nullopt;
}

std::string_view to_string(AgentOutputFormat format) noexcept {
    for (const auto& [name, candidate] : kAgentOutputFormatNames) {
        if (candidate == format) {
            return name;
        }
    }
    return "auto";
}

std::optional<AgentOutputFormat> agent_output_format_from_string(std::string_view name) noexcept {
    for (const auto& [candidate, format] : kAgentOutputFormatNames) {
        if (candidate == name) {
            return format;
        }
    }
    return std::nullopt;
}

std::vector<std::string_view> agent_tool_policy_names() {
    std::vector<std::string_view> out;
    for (const auto& [name, policy] : kAgentToolPolicyNames) {
        out.push_back(name);
    }
    return out;
}

std::vector<std::string_view> agent_output_format_names() {
    std::vector<std::string_view> out;
    for (const auto& [name, format] : kAgentOutputFormatNames) {
        out.push_back(name);
    }
    return out;
}

std::span<const std::string_view> lora_methods() noexcept {
    static constexpr std::array<std::string_view, 2> kMethods{"lora", "qlora"};
    return kMethods;
}

const NamedGraphConfig* Config::find_graph(std::string_view name) const noexcept {
    const CaseInsensitiveLess less;
    for (const auto& [key, graph] : graphs) {
        if (!less(key, name) && !less(name, key)) {
            return &graph;
        }
    }
    return nullptr;
}

std::vector<std::string> Config::graph_names() const {
    std::vector<std::string> names;
    names.reserve(graphs.size());
    for (const auto& [name, unused] : graphs) {
        names.push_back(name);
    }
    return names;
}

const AgentConfig* Config::find_agent(std::string_view name) const noexcept {
    const auto it = agents.find(name);
    return it == agents.end() ? nullptr : &it->second;
}

std::vector<std::string> Config::agent_names() const {
    std::vector<std::string> names;
    names.reserve(agents.size());
    for (const auto& [name, unused] : agents) {
        names.push_back(name);
    }
    return names;
}

std::span<const std::string_view> backend_type_names() noexcept {
    static const std::array<std::string_view, kBackendTypeNames.size()> names = [] {
        std::array<std::string_view, kBackendTypeNames.size()> out{};
        for (std::size_t i = 0; i < kBackendTypeNames.size(); ++i) {
            out[i] = kBackendTypeNames[i].first;
        }
        return out;
    }();
    return names;
}

const McpServerConfig* Config::find_mcp_server(std::string_view name) const noexcept {
    const auto it = mcp_servers.find(name);
    return it == mcp_servers.end() ? nullptr : &it->second;
}

std::vector<std::string> Config::mcp_server_names() const {
    std::vector<std::string> names;
    names.reserve(mcp_servers.size());
    for (const auto& [name, unused] : mcp_servers) {
        names.push_back(name);
    }
    return names;
}

std::string expand_env_and_home(std::string_view input) {
    std::string expanded = expand_env(input);
    if (expanded == "~" || expanded.starts_with("~/")) {
        if (const std::optional<std::string> home = platform::home_directory(); home.has_value()) {
            return *home + expanded.substr(1);
        }
    }
    return expanded;
}

std::string_view to_string(PermissionLevel level) noexcept {
    switch (level) {
        case PermissionLevel::Ask:
            return "ask";
        case PermissionLevel::Allow:
            return "allow";
        case PermissionLevel::Deny:
            return "deny";
    }
    return "ask";
}

std::optional<PermissionLevel> permission_level_from_string(std::string_view name) noexcept {
    if (name == "ask") {
        return PermissionLevel::Ask;
    }
    if (name == "allow") {
        return PermissionLevel::Allow;
    }
    if (name == "deny") {
        return PermissionLevel::Deny;
    }
    return std::nullopt;
}

PermissionLevel PermissionsConfig::level(std::string_view tool) const noexcept {
    const auto it = levels.find(tool);
    return it == levels.end() ? PermissionLevel::Ask : it->second;
}

std::string KnowledgeConfig::collection() const {
    return db.empty() ? std::string{kDefaultCollection} : db;
}

bool ToolsConfig::is_disabled(std::string_view toolset) const noexcept {
    for (const std::string& name : disabled) {
        if (name == toolset) {
            return true;
        }
    }
    return false;
}

std::string_view to_string(StatusMode value) noexcept {
    for (const auto& [name, mode] : kStatusModeNames) {
        if (mode == value) {
            return name;
        }
    }
    return "line";
}

std::optional<StatusMode> status_mode_from_string(std::string_view name) noexcept {
    for (const auto& [candidate, mode] : kStatusModeNames) {
        if (candidate == name) {
            return mode;
        }
    }
    return std::nullopt;
}

const BackendConfig* Config::find_backend(std::string_view name) const noexcept {
    const auto it = backends.find(name);
    return it == backends.end() ? nullptr : &it->second;
}

std::vector<std::string> Config::backend_names() const {
    std::vector<std::string> names;
    names.reserve(backends.size());
    for (const auto& [name, unused] : backends) {
        names.push_back(name);
    }
    return names;
}

const EmbeddingConfig* Config::find_embedding(std::string_view name) const noexcept {
    const auto it = embeddings.find(name);
    return it == embeddings.end() ? nullptr : &it->second;
}

std::vector<std::string> Config::embedding_names() const {
    std::vector<std::string> names;
    names.reserve(embeddings.size());
    for (const auto& [name, unused] : embeddings) {
        names.push_back(name);
    }
    return names;
}

std::string expand_env(std::string_view input) {
    std::string out;
    out.reserve(input.size());

    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] != '$') {
            out.push_back(input[i]);
            continue;
        }
        // "$$" is a literal '$' -- the escape hatch for a value that really
        // does contain one.
        if (i + 1 < input.size() && input[i + 1] == '$') {
            out.push_back('$');
            ++i;
            continue;
        }
        if (i + 1 >= input.size() || input[i + 1] != '{') {
            out.push_back('$');
            continue;
        }
        const std::size_t close = input.find('}', i + 2);
        if (close == std::string_view::npos) {
            // Unterminated: pass through untouched rather than guessing where
            // the name ends.
            out.push_back('$');
            continue;
        }
        const std::string name{input.substr(i + 2, close - (i + 2))};
        // An undefined variable expands to empty, not an error: a config
        // naming ${ANTHROPIC_API_KEY} must still load on a machine with no
        // key. "Local by default, cloud by choice."
        if (const char* value = std::getenv(name.c_str()); value != nullptr) {
            out += value;
        }
        i = close;
    }

    return out;
}

Config parse_config(std::string_view content, std::string_view origin) {
    YAML::Node root;
    try {
        root = YAML::Load(std::string{content});
    } catch (const YAML::Exception& e) {
        fail(origin, std::string{"not valid YAML: "} + e.what());
    }

    Config config;

    // An empty file is a valid config: zero backends, zero models. A fresh
    // install with no keys and no models must load and pass `apogee check`.
    if (!root.IsDefined() || root.IsNull()) {
        return config;
    }
    if (!root.IsMap()) {
        fail(origin, "expected a mapping at the top level");
    }

    if (const YAML::Node backends = root["backends"]; backends.IsDefined() && !backends.IsNull()) {
        if (!backends.IsMap()) {
            fail(origin, "backends: expected a mapping of name -> settings");
        }
        for (const auto& entry : backends) {
            const std::string name = entry.first.Scalar();
            if (name.empty()) {
                fail(origin, "backends: an entry has an empty name");
            }
            BackendConfig backend = parse_backend(entry.second, origin, name);
            const auto [it, inserted] = config.backends.emplace(name, std::move(backend));
            if (!inserted) {
                // Reached only when two keys fold to the same name. Ommi
                // merged them (Viper lowercased keys); Apogee names both.
                fail(origin, "backends: '" + name + "' collides with '" + it->first +
                                 "' -- backend names are compared case-insensitively, so these "
                                 "would be the same backend; rename one");
            }
        }
    }

    if (const YAML::Node models = root["models"]; models.IsDefined() && !models.IsNull()) {
        if (!models.IsMap()) {
            fail(origin, "models: expected a mapping");
        }
        config.models.default_backend = scalar(models["default"], origin, "models.default");
        config.models.default_embedding =
            scalar(models["default_embedding"], origin, "models.default_embedding");
        config.models.default_extraction =
            scalar(models["default_extraction"], origin, "models.default_extraction");
    }

    if (const YAML::Node paths = root["paths"]; paths.IsDefined() && !paths.IsNull()) {
        if (!paths.IsMap()) {
            fail(origin, "paths: expected a mapping");
        }
        config.paths.gguf_dir = scalar(paths["gguf_dir"], origin, "paths.gguf_dir");
        config.paths.hf_dir = scalar(paths["hf_dir"], origin, "paths.hf_dir");
        config.paths.mcp_dir = scalar(paths["mcp_dir"], origin, "paths.mcp_dir");
        config.paths.embeddings_dir =
            scalar(paths["embeddings_dir"], origin, "paths.embeddings_dir");
    }

    if (const YAML::Node embeddings = root["embeddings"];
        embeddings.IsDefined() && !embeddings.IsNull()) {
        if (!embeddings.IsMap()) {
            fail(origin, "embeddings: expected a mapping of collection name -> settings");
        }
        for (const auto& entry : embeddings) {
            const std::string name = entry.first.Scalar();
            if (name.empty()) {
                fail(origin, "embeddings: an entry has an empty name");
            }
            const std::string where = "embeddings." + name;
            const YAML::Node node = entry.second;
            EmbeddingConfig collection;
            if (node.IsDefined() && !node.IsNull()) {
                if (!node.IsMap()) {
                    fail(origin, where + ": expected a mapping of settings");
                }
                collection.chunk_size = integer(node["chunk_size"], origin, where + ".chunk_size");
                collection.chunk_overlap =
                    integer(node["chunk_overlap"], origin, where + ".chunk_overlap");
                collection.description =
                    scalar(node["description"], origin, where + ".description");
                collection.backend = scalar(node["backend"], origin, where + ".backend");
                collection.retriever = scalar(node["retriever"], origin, where + ".retriever");
                collection.rerank = scalar(node["rerank"], origin, where + ".rerank");
                if (const YAML::Node graph = node["graph"]; graph.IsDefined() && !graph.IsNull()) {
                    if (!graph.IsMap()) {
                        fail(origin, where + ".graph: expected a mapping");
                    }
                    collection.graph.enabled =
                        boolean(graph["enabled"], origin, where + ".graph.enabled", false);
                    collection.graph.extract_backend =
                        scalar(graph["extract_backend"], origin, where + ".graph.extract_backend");
                    // Validated here rather than at the point of use: an
                    // out-of-range depth would otherwise be clamped silently
                    // on every turn, and a config that says 3 means it.
                    if (const std::optional<std::int64_t> hops =
                            integer(graph["hops"], origin, where + ".graph.hops");
                        hops.has_value()) {
                        if (*hops < 1 || *hops > 2) {
                            fail(origin, where + ".graph.hops: " + std::to_string(*hops) +
                                             " is out of range (1 or 2)");
                        }
                        collection.graph.hops = static_cast<int>(*hops);
                    }
                    if (const std::optional<std::int64_t> max_entities =
                            integer(graph["max_entities"], origin, where + ".graph.max_entities");
                        max_entities.has_value()) {
                        if (*max_entities < 1) {
                            fail(origin, where + ".graph.max_entities: must be at least 1");
                        }
                        collection.graph.max_entities = static_cast<int>(*max_entities);
                    }
                }
            }
            const auto [it, inserted] = config.embeddings.emplace(name, std::move(collection));
            if (!inserted) {
                // The same rule as backends, for the same reason: a collection
                // is addressed by name, and two names that fold together would
                // be the same file on a case-insensitive filesystem.
                fail(origin, "embeddings: '" + name + "' collides with '" + it->first +
                                 "' -- collection names are compared case-insensitively, so "
                                 "these would be the same collection; rename one");
            }
        }
    }

    if (const YAML::Node auto_rag = root["auto_rag"]; auto_rag.IsDefined() && !auto_rag.IsNull()) {
        config.auto_rag = scalar(auto_rag, origin, "auto_rag");
    }

    if (const YAML::Node permissions = root["permissions"];
        permissions.IsDefined() && !permissions.IsNull()) {
        if (!permissions.IsMap()) {
            fail(origin, "permissions: expected a mapping of tool name -> ask | allow | deny");
        }
        for (const auto& entry : permissions) {
            const std::string tool = entry.first.Scalar();
            if (tool.empty()) {
                fail(origin, "permissions: an entry has an empty tool name");
            }
            const std::string value = scalar(entry.second, origin, "permissions." + tool);
            const std::optional<PermissionLevel> level = permission_level_from_string(value);
            if (!level.has_value()) {
                fail(origin, "permissions." + tool + ": '" + value +
                                 "' is not a permission level (accepted: ask, allow, deny)");
            }
            config.permissions.levels[tool] = *level;
        }
    }

    if (const YAML::Node servers = root["mcp_servers"]; servers.IsDefined() && !servers.IsNull()) {
        if (!servers.IsMap()) {
            fail(origin, "mcp_servers: expected a mapping of server name -> settings");
        }
        for (const auto& entry : servers) {
            const std::string name = entry.first.Scalar();
            if (name.empty()) {
                fail(origin, "mcp_servers: an entry has an empty name");
            }
            const std::string where = "mcp_servers." + name;
            const YAML::Node node = entry.second;
            McpServerConfig server;
            if (node.IsDefined() && !node.IsNull()) {
                if (!node.IsMap()) {
                    fail(origin, where + ": expected a mapping of settings");
                }
                server.command =
                    expand_env_and_home(scalar(node["command"], origin, where + ".command"));
                for (const char* list : {"args", "env"}) {
                    const YAML::Node items = node[list];
                    if (!items.IsDefined() || items.IsNull()) {
                        continue;
                    }
                    if (!items.IsSequence()) {
                        fail(origin, where + "." + list + ": expected a list of strings");
                    }
                    std::vector<std::string>& target =
                        std::string{list} == "args" ? server.args : server.env;
                    for (const YAML::Node& item : items) {
                        target.push_back(
                            expand_env_and_home(scalar(item, origin, where + "." + list + "[]")));
                    }
                }
                if (const YAML::Node enabled = node["enabled"];
                    enabled.IsDefined() && !enabled.IsNull()) {
                    const std::string value = scalar(enabled, origin, where + ".enabled");
                    if (value == "true") {
                        server.enabled = true;
                    } else if (value == "false") {
                        server.enabled = false;
                    } else {
                        fail(origin, where + ".enabled: '" + value + "' is not true or false");
                    }
                }
            }
            const auto [it, inserted] = config.mcp_servers.emplace(name, std::move(server));
            if (!inserted) {
                fail(origin, "mcp_servers: '" + name + "' collides with '" + it->first +
                                 "' -- server names are compared case-insensitively, so these "
                                 "would be the same server; rename one");
            }
        }
    }

    if (const YAML::Node agents = root["agents"]; agents.IsDefined() && !agents.IsNull()) {
        if (!agents.IsMap()) {
            fail(origin, "agents: expected a mapping of agent name -> settings");
        }
        for (const auto& entry : agents) {
            const std::string name = entry.first.Scalar();
            if (name.empty()) {
                fail(origin, "agents: an entry has an empty name");
            }
            AgentConfig agent = parse_agent(entry.second, origin, name);
            const auto [it, inserted] = config.agents.emplace(name, std::move(agent));
            if (!inserted) {
                fail(origin, "agents: '" + name + "' collides with '" + it->first +
                                 "' -- agent names are compared case-insensitively, so these "
                                 "would be the same agent; rename one");
            }
        }
    }

    if (const YAML::Node graphs = root["graphs"]; graphs.IsDefined() && !graphs.IsNull()) {
        if (!graphs.IsMap()) {
            fail(origin, "graphs: expected a mapping of graph name -> settings");
        }
        for (const auto& entry : graphs) {
            const std::string name = entry.first.Scalar();
            if (name.empty()) {
                fail(origin, "graphs: an entry has an empty name");
            }
            const std::string where = "graphs." + name;
            const YAML::Node node = entry.second;
            NamedGraphConfig graph;
            if (node.IsDefined() && !node.IsNull()) {
                if (!node.IsMap()) {
                    fail(origin, where + ": expected a mapping of settings");
                }
                graph.collections =
                    string_list(node["collections"], origin, where + ".collections", false);
                graph.extract_backend =
                    scalar(node["extract_backend"], origin, where + ".extract_backend");
                if (const std::optional<std::int64_t> hops =
                        integer(node["hops"], origin, where + ".hops");
                    hops.has_value()) {
                    if (*hops < 1 || *hops > 2) {
                        fail(origin, where + ".hops: " + std::to_string(*hops) +
                                         " is out of range (1 or 2)");
                    }
                    graph.hops = static_cast<int>(*hops);
                }
                if (const std::optional<std::int64_t> max_entities =
                        integer(node["max_entities"], origin, where + ".max_entities");
                    max_entities.has_value()) {
                    if (*max_entities < 1) {
                        fail(origin, where + ".max_entities: must be at least 1");
                    }
                    graph.max_entities = static_cast<int>(*max_entities);
                }
            }
            if (config.find_graph(name) != nullptr) {
                fail(origin, "graphs: '" + name +
                                 "' collides with an earlier entry -- graph names are compared "
                                 "case-insensitively, so these would be the same graph; rename "
                                 "one");
            }
            config.graphs.emplace_back(name, std::move(graph));
        }
    }

    if (const YAML::Node tools = root["tools"]; tools.IsDefined() && !tools.IsNull()) {
        if (!tools.IsMap()) {
            fail(origin, "tools: expected a mapping");
        }
        config.tools.fs_root = scalar(tools["fs_root"], origin, "tools.fs_root");
        if (const YAML::Node disabled = tools["disabled"];
            disabled.IsDefined() && !disabled.IsNull()) {
            if (!disabled.IsSequence()) {
                fail(origin, "tools.disabled: expected a list of toolset names");
            }
            for (const YAML::Node& item : disabled) {
                config.tools.disabled.push_back(scalar(item, origin, "tools.disabled[]"));
            }
        }
    }

    if (const YAML::Node knowledge = root["knowledge"];
        knowledge.IsDefined() && !knowledge.IsNull()) {
        if (!knowledge.IsMap()) {
            fail(origin, "knowledge: expected a mapping");
        }
        config.knowledge.auto_capture =
            boolean(knowledge["auto_capture"], origin, "knowledge.auto_capture", false);
        config.knowledge.db = scalar(knowledge["db"], origin, "knowledge.db");
        if (config.knowledge.db.find("..") != std::string::npos ||
            config.knowledge.db.find('/') != std::string::npos ||
            config.knowledge.db.find('\\') != std::string::npos) {
            // A collection is NAMED, not pathed -- the rule `embed` applies,
            // caught here rather than at the first capture.
            fail(origin,
                 "knowledge.db: '" + config.knowledge.db + "' is not a plain collection name");
        }
    }

    if (const YAML::Node training = root["training"]; training.IsDefined() && !training.IsNull()) {
        if (!training.IsMap()) {
            fail(origin, "training: expected a mapping");
        }
        config.training.python = scalar(training["python"], origin, "training.python");
        config.training.trainer = scalar(training["trainer"], origin, "training.trainer");
        if (!config.training.trainer.empty() && config.training.trainer != "auto" &&
            config.training.trainer != "mlx" && config.training.trainer != "peft" &&
            config.training.trainer != "mock") {
            fail(origin, "training.trainer: unknown value '" + config.training.trainer +
                             "' (accepted: auto, mlx, peft, mock)");
        }
        config.training.judge_backend =
            scalar(training["judge_backend"], origin, "training.judge_backend");
        config.training.eval_suite_path =
            scalar(training["eval_suite_path"], origin, "training.eval_suite_path");
        if (const std::optional<std::int64_t> retain =
                integer(training["retain_versions"], origin, "training.retain_versions");
            retain.has_value()) {
            if (*retain < 0) {
                fail(origin, "training.retain_versions: must be 0 (keep all) or positive");
            }
            config.training.retain_versions = static_cast<int>(*retain);
        }
        config.training.gate_mode = scalar(training["gate_mode"], origin, "training.gate_mode");
        if (!config.training.gate_mode.empty() &&
            config.training.gate_mode != TrainingConfig::kGateHard &&
            config.training.gate_mode != TrainingConfig::kGateSoft) {
            fail(origin, "training.gate_mode: unknown value '" + config.training.gate_mode +
                             "' (accepted: hard, soft)");
        }
        if (const YAML::Node pipelines = training["pipelines"];
            pipelines.IsDefined() && !pipelines.IsNull()) {
            if (!pipelines.IsMap()) {
                fail(origin, "training.pipelines: expected a mapping of name -> pipeline");
            }
            for (const auto& entry : pipelines) {
                const std::string name = entry.first.Scalar();
                if (name.empty()) {
                    fail(origin, "training.pipelines: an entry has an empty name");
                }
                config.training.pipelines.emplace(
                    name,
                    parse_pipeline_node(entry.second, origin, "training.pipelines." + name, name));
            }
        }
        if (const YAML::Node regimes = training["regimes"];
            regimes.IsDefined() && !regimes.IsNull()) {
            if (!regimes.IsMap()) {
                fail(origin, "training.regimes: expected a mapping of name -> regime");
            }
            for (const auto& entry : regimes) {
                const std::string name = entry.first.Scalar();
                if (name.empty()) {
                    fail(origin, "training.regimes: an entry has an empty name");
                }
                config.training.regimes.emplace(
                    name,
                    parse_regime_node(entry.second, origin, "training.regimes." + name, name));
            }
        }
        if (const YAML::Node cycle = training["cycle"]; cycle.IsDefined() && !cycle.IsNull()) {
            config.training.cycle = parse_cycle(cycle, origin);
        }
    }

    if (const YAML::Node mode = root["status_mode"]; mode.IsDefined() && !mode.IsNull()) {
        const std::string name = scalar(mode, origin, "status_mode");
        const std::optional<StatusMode> parsed = status_mode_from_string(name);
        if (!parsed.has_value()) {
            fail(origin,
                 "status_mode: unknown value '" + name + "' (accepted: line, verbose, quiet)");
        }
        config.status_mode = *parsed;
    }

    if (const YAML::Node color = root["color"]; color.IsDefined() && !color.IsNull()) {
        try {
            config.color = color.as<bool>();
        } catch (const YAML::Exception&) {
            fail(origin, "color: expected true or false, got '" + color.Scalar() + "'");
        }
    }

    return config;
}

PipelineSpec parse_pipeline_spec(std::string_view content, std::string_view origin,
                                 std::string_view fallback_name) {
    YAML::Node root;
    try {
        root = YAML::Load(std::string{content});
    } catch (const YAML::Exception& e) {
        fail(origin, std::string{"not valid YAML: "} + e.what());
    }
    return parse_pipeline_node(root, origin, "pipeline", fallback_name);
}

RegimeSpec parse_regime_spec(std::string_view content, std::string_view origin,
                             std::string_view fallback_name) {
    YAML::Node root;
    try {
        root = YAML::Load(std::string{content});
    } catch (const YAML::Exception& e) {
        fail(origin, std::string{"not valid YAML: "} + e.what());
    }
    return parse_regime_node(root, origin, "regime", fallback_name);
}

Config load_config(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw ConfigError(path.string() +
                          ": cannot open config file (run 'apogee config init' to create one)");
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) {
        throw ConfigError(path.string() + ": error reading config file");
    }
    return parse_config(buffer.str(), path.string());
}

}  // namespace apogee::harness

namespace apogee::harness {

bool operator==(const ModelsConfig& lhs, const ModelsConfig& rhs) noexcept {
    return lhs.default_backend == rhs.default_backend &&
           lhs.default_embedding == rhs.default_embedding &&
           lhs.default_extraction == rhs.default_extraction;
}

}  // namespace apogee::harness
