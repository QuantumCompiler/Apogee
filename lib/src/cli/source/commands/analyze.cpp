#include "commands/analyze.h"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "agent/tool.h"
#include "agentloop/loop.h"
#include "agentloop/rag.h"
#include "agentloop/reporter.h"
#include "agentloop/retriever.h"
#include "agentloop/review_context.h"
#include "agentloop/structured.h"
#include "ansi/ansi.h"
#include "backends/factory.h"
#include "commands/ask_prompt.h"
#include "commands/cli_reporter.h"
#include "commands/helpers.h"
#include "commands/json_reporter.h"
#include "commands/line_reader.h"
#include "commands/permissions.h"
#include "commands/terminal.h"
#include "harness/assets.h"
#include "harness/config.h"
#include "harness/config_edit.h"
#include "harness/errors.h"
#include "harness/harness.h"
#include "harness/paths.h"
#include "harness/roles.h"
#include "mcp/registry.h"
#include "platform/platform.h"
#include "render/json_report.h"
#include "tools/git.h"

namespace apogee::commands {
namespace {

/// What the model is asked when the user gave no input at all: the agent's
/// prompt may carry every instruction it needs (the bundled three do).
constexpr std::string_view kDefaultInput = "Proceed with the task described in your instructions.";

struct AnalyzeFlags {
    std::string agent;
    std::string positional;
    std::string input;
    std::vector<std::string> prompts;
    std::vector<std::string> schemas;
    bool interactive = false;
    bool list = false;
    bool json = false;
    bool markdown = false;
    bool text = false;
    bool show = false;
    std::string save_dir;
    std::string save_name;
    std::string model;
    std::string rag;
    int rag_limit = 4;
    std::string retriever;
    std::string rerank;
    std::string branch;
    std::string base;
    std::string remote = "origin";
    bool fetch = false;
    bool no_fetch = false;
    bool no_questions = false;
    bool quiet = false;
    bool verbose = false;
    bool no_color = false;
    double temperature = 0.0;
    std::int64_t max_tokens = 0;
    OutputFormat output_format = OutputFormat::Text;

    CLI::Option* rag_option = nullptr;
    CLI::Option* temperature_option = nullptr;
    CLI::Option* max_tokens_option = nullptr;
};

[[noreturn]] void fail_user(const std::string& message) {
    std::cerr << "apogee analyze: " << message << "\n";
    throw CLI::RuntimeError(kUserError);
}

[[noreturn]] void fail_backend(const std::string& message) {
    std::cerr << "apogee analyze: " << message << "\n";
    throw CLI::RuntimeError(kBackendError);
}

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\n' ||
                                   text[begin] == '\r' || text[begin] == '\t')) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\n' || text[end - 1] == '\r' ||
                           text[end - 1] == '\t')) {
        --end;
    }
    return std::string{text.substr(begin, end - begin)};
}

std::optional<std::string> read_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

/// The agent as it will run: its entry, its files' contents, its schema.
struct LoadedAgent {
    /// Empty for an ad-hoc `--prompt` run.
    std::string name;
    harness::AgentConfig config;
    const harness::BundledAgent* bundled = nullptr;
    std::vector<std::string> prompt_texts;
    std::vector<std::string> schema_texts;
    /// The first schema, parsed, when JSON is asked of the model.
    std::optional<nlohmann::json> schema;

    [[nodiscard]] bool structured() const noexcept {
        return schema.has_value();
    }
};

std::string known_agent_names(const harness::Config& config) {
    std::string names;
    for (const harness::NamedAgent& agent : harness::all_agents(config)) {
        names += names.empty() ? "" : ", ";
        names += agent.name;
    }
    return names;
}

LoadedAgent load_agent(const harness::Config& config, const std::filesystem::path& config_path,
                       const AnalyzeFlags& flags) {
    LoadedAgent loaded;
    if (!flags.agent.empty()) {
        const std::optional<harness::AgentConfig> resolved =
            harness::resolve_agent(config, flags.agent, &loaded.bundled);
        if (!resolved.has_value()) {
            fail_user("no agent named '" + flags.agent + "' (available: " +
                      known_agent_names(config) + "; create one with 'apogee agents create')");
        }
        loaded.name = flags.agent;
        loaded.config = *resolved;
    } else {
        // Ad hoc: prompt files on the command line, read-only by default --
        // the safe policy for something that was never configured.
        loaded.config.tools = harness::AgentToolPolicy::ReadOnly;
    }
    if (!flags.prompts.empty()) {
        loaded.config.prompts = flags.prompts;
    }
    if (!flags.schemas.empty()) {
        loaded.config.schemas = flags.schemas;
    }

    const std::filesystem::path home = harness::home_for_config(config_path);
    for (const std::string& prompt : loaded.config.prompts) {
        const std::filesystem::path path = harness::resolve_agent_path(home, prompt);
        if (const std::optional<std::string> text = read_text(path); text.has_value()) {
            loaded.prompt_texts.push_back(*text);
        } else if (loaded.bundled != nullptr &&
                   prompt == harness::bundled_prompt_relative_path(loaded.bundled->name)) {
            // Not seeded yet: the compiled-in text, so a fresh install runs.
            loaded.prompt_texts.emplace_back(loaded.bundled->prompt);
        } else {
            fail_user("reading prompt file " + path.string() + ": cannot open" +
                      (loaded.name.empty() ? "" : " (agent '" + loaded.name + "')"));
        }
    }
    for (const std::string& schema : loaded.config.schemas) {
        const std::filesystem::path path = harness::resolve_agent_path(home, schema);
        if (const std::optional<std::string> text = read_text(path); text.has_value()) {
            loaded.schema_texts.push_back(*text);
        } else if (loaded.bundled != nullptr &&
                   schema == harness::bundled_schema_relative_path(loaded.bundled->name)) {
            loaded.schema_texts.emplace_back(loaded.bundled->schema);
        } else {
            fail_user("reading schema file " + path.string() + ": cannot open" +
                      (loaded.name.empty() ? "" : " (agent '" + loaded.name + "')"));
        }
    }
    if (loaded.prompt_texts.empty() && loaded.name.empty()) {
        fail_user("no prompt: pass --prompt <file>");
    }

    if (!loaded.schema_texts.empty() &&
        loaded.config.output_format != harness::AgentOutputFormat::Markdown) {
        // Validated against the FIRST schema; a broken schema is the user's
        // problem to hear about now, not the model's to guess at.
        nlohmann::json parsed = nlohmann::json::parse(loaded.schema_texts.front(), nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object()) {
            fail_user("schema " + loaded.config.schemas.front() + " is not a JSON object");
        }
        const agentloop::ValidationResult valid = agentloop::validate_schema(parsed);
        if (!valid.ok) {
            std::string why;
            for (const std::string& error : valid.errors) {
                why += "\n  " + error;
            }
            fail_user("schema " + loaded.config.schemas.front() +
                      " is not a valid draft-07 JSON Schema:" + why);
        }
        loaded.schema = std::move(parsed);
    }
    return loaded;
}

/// The persona: the agent's prompts, then the OUTPUT FORMAT block, then the
/// review note. Apogee's chat persona and the backend's system_prompt are
/// NOT added -- an agent writing release notes should not also be carrying
/// instructions saying it is a coding assistant.
std::string system_prompt_for(const LoadedAgent& loaded, const agentloop::ReviewContext& review) {
    std::string prompt;
    for (const std::string& text : loaded.prompt_texts) {
        const std::string trimmed = trim(text);
        if (trimmed.empty()) {
            continue;
        }
        prompt += prompt.empty() ? "" : "\n\n";
        prompt += trimmed;
    }
    if (!loaded.schema_texts.empty()) {
        const bool markdown = loaded.config.output_format == harness::AgentOutputFormat::Markdown;
        const std::string block = agentloop::schema_instruction(loaded.schema_texts, markdown);
        prompt += prompt.empty() ? block : "\n\n" + block;
    }
    return agentloop::compose_system_prompt(prompt, agentloop::review_note(review));
}

std::string resolve_input(const AnalyzeFlags& flags) {
    if (!flags.positional.empty()) {
        return flags.positional;
    }
    if (!flags.input.empty()) {
        return flags.input;
    }
    if (!flags.interactive && stdin_is_piped()) {
        return trim(read_stdin());
    }
    return {};
}

void print_agent_list(const harness::Config& config) {
    std::cout << "Configured agents:\n";
    // What an agent with no model of its own would run on: the one resolver,
    // never the raw pointer -- a listing that read `models.default` directly
    // would be a second chain, and would disagree the day a rung is added.
    std::string fallback = "default";
    try {
        if (const std::string key = harness::resolve_chat_backend(config, ""); !key.empty()) {
            fallback = "default (" + key + ")";
        }
    } catch (const std::exception&) {
        // No default configured: "default" says enough for a listing.
    }
    for (const harness::NamedAgent& agent : harness::all_agents(config)) {
        std::string model = agent.config.model.empty() ? fallback : agent.config.model;
        std::string detail = agent.config.description;
        if (detail.empty() && !agent.config.prompts.empty()) {
            detail =
                "[" + std::filesystem::path{agent.config.prompts.front()}.filename().string() + "]";
        }
        const std::string origin = agent.bundled             ? "(bundled) "
                                   : agent.overrides_bundled ? "(overrides bundled) "
                                                             : "";
        std::cout << "  " << std::left << std::setw(24) << agent.name << "  " << std::setw(28)
                  << model << "  " << origin << detail << "\n";
    }
}

/// One turn's outcome, before rendering.
struct TurnOutput {
    std::string answer;
    std::optional<nlohmann::json> json;
    bool structured = false;
    bool conforms = true;
    std::vector<std::string> errors;
    bool hit_limit = false;
};

std::string render_output(const TurnOutput& turn, Rendering rendering) {
    switch (rendering) {
        case Rendering::Json:
            if (turn.structured && !turn.conforms) {
                // Never a silent pass: a reader of the JSON sees the flag.
                return nlohmann::json{
                    {"conforms", false}, {"errors", turn.errors}, {"raw", turn.answer}}
                    .dump(2);
            }
            return turn.json.has_value() ? turn.json->dump(2) : turn.answer;
        case Rendering::Markdown:
            return turn.json.has_value()
                       ? render::render_markdown(*turn.json)
                       : render::render_report(turn.answer, render::ReportFormat::Markdown);
        case Rendering::Text:
            return turn.json.has_value()
                       ? render::render_text(*turn.json)
                       : render::render_report(turn.answer, render::ReportFormat::Text);
        case Rendering::Raw:
            break;
    }
    return turn.answer;
}

/// Where a run's report is saved, or empty for no save.
std::filesystem::path save_directory(const AnalyzeFlags& flags, const LoadedAgent& loaded,
                                     const std::filesystem::path& home) {
    std::filesystem::path dir;
    if (!flags.save_dir.empty()) {
        dir = harness::expand_env_and_home(flags.save_dir);
    } else if (!loaded.config.save_dir.empty()) {
        dir = loaded.config.save_dir;
    } else if (!loaded.name.empty()) {
        // A named agent saves by default, under the layout's row -- derived
        // from the config path so a temp tree stays hermetic.
        dir = home / "analyses";
    }
    if (!dir.empty() && !loaded.config.save_subdir.empty()) {
        dir /= loaded.config.save_subdir;
    }
    return dir;
}

std::string save_base_name(const AnalyzeFlags& flags, const LoadedAgent& loaded) {
    if (!flags.save_name.empty()) {
        return flags.save_name;
    }
    if (!loaded.config.save_filename.empty()) {
        return loaded.config.save_filename;
    }
    return loaded.name.empty() ? "apogee-analyze" : loaded.name;
}

/// Writes `content` under `dir`; the path written, or empty with `error`.
std::filesystem::path save_report(const std::filesystem::path& dir, std::string_view base,
                                  Rendering rendering, const std::string& content,
                                  std::string& error) {
    std::error_code code;
    std::filesystem::create_directories(dir, code);
    if (code) {
        error = "could not create " + dir.string() + ": " + code.message();
        return {};
    }
    const std::filesystem::path path =
        dir / report_filename(base, extension_for(rendering), std::chrono::system_clock::now());
    try {
        harness::write_file_atomically(path, content + "\n");
    } catch (const std::exception& e) {
        error = e.what();
        return {};
    }
    return path;
}

std::string fetch_mode_for(const AnalyzeFlags& flags) {
    if (flags.fetch && flags.no_fetch) {
        fail_user("--fetch and --no-fetch are mutually exclusive");
    }
    return flags.fetch ? "always" : flags.no_fetch ? "never" : "auto";
}

}  // namespace

bool should_print_report(bool show, bool json, bool stdout_is_tty, bool will_save) {
    return show || json || !stdout_is_tty || !will_save;
}

std::string report_filename(std::string_view base, std::string_view extension,
                            std::chrono::system_clock::time_point when) {
    const std::time_t seconds = std::chrono::system_clock::to_time_t(when);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    std::ostringstream out;
    out << (base.empty() ? "apogee-analyze" : base) << "-" << std::put_time(&local, "%Y%m%d-%H%M%S")
        << extension;
    return out.str();
}

Rendering resolve_rendering(bool json_flag, bool text_flag, bool markdown_flag, bool has_schema,
                            std::string_view answer) {
    if (json_flag) {
        return Rendering::Json;
    }
    if (text_flag) {
        return Rendering::Text;
    }
    if (markdown_flag) {
        return Rendering::Markdown;
    }
    if (has_schema) {
        const std::string trimmed = render::strip_code_fence(answer);
        if (trimmed.starts_with("{") || trimmed.starts_with("[")) {
            return Rendering::Markdown;
        }
    }
    return Rendering::Raw;
}

std::string_view extension_for(Rendering rendering) noexcept {
    switch (rendering) {
        case Rendering::Json:
            return ".json";
        case Rendering::Text:
            return ".txt";
        case Rendering::Markdown:
        case Rendering::Raw:
            break;
    }
    return ".md";
}

std::string_view AnalyzeCommand::name() const noexcept {
    return "analyze";
}

std::string_view AnalyzeCommand::summary() const noexcept {
    return "Run a named agent workflow against an input";
}

void AnalyzeCommand::bind(CLI::App& root, const RootContext& context) {
    auto flags = std::make_shared<AnalyzeFlags>();

    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    // Upper-case so it cannot collide with the `--input`/`--text` long names:
    // CLI11 matches a positional against every long name.
    cmd->add_option("INPUT", flags->positional, "The input text (or --input, or piped stdin)");
    cmd->add_option("-a,--agent", flags->agent, "The agent to run (see --list)")
        ->type_name(kAgentValue);
    cmd->add_flag("--list", flags->list, "List the agents and exit; needs no backend");
    cmd->add_option("--prompt", flags->prompts,
                    "Prompt file; overrides the agent's prompts (repeatable)")
        ->type_name(kPathValue)
        ->allow_extra_args(false);
    cmd->add_option("--schema", flags->schemas,
                    "Schema file; overrides the agent's schemas (repeatable)")
        ->type_name(kPathValue)
        ->allow_extra_args(false);
    cmd->add_option("--input", flags->input, "Input text (alternative to the argument or stdin)");
    cmd->add_flag("--interactive", flags->interactive, "A multi-turn session under the agent");
    cmd->add_flag("--json", flags->json, "Print the raw JSON, unrendered");
    cmd->add_flag("--markdown", flags->markdown, "Render the report as Markdown (the default)");
    cmd->add_flag("--text", flags->text, "Render the report as plain text");
    cmd->add_flag("--show", flags->show, "Print the report even when it was saved");
    cmd->add_option("--save", flags->save_dir, "Directory to save the report in")
        ->type_name(kPathValue);
    cmd->add_option("--save-name", flags->save_name, "Base filename for the saved report");
    cmd->add_option("-m,--model", flags->model,
                    "Backend override (default: the agent's, then models.default)")
        ->type_name(kBackendValue);
    flags->rag_option =
        cmd->add_option("--rag", flags->rag,
                        "Retrieve context from this collection; \"\" switches the agent's off")
            ->type_name(kCollectionValue);
    cmd->add_option("--rag-limit", flags->rag_limit, "How many chunks to inject (default 4)");
    cmd->add_option("--retriever", flags->retriever,
                    "How to search the collection: lexical, vector, hybrid, or auto")
        ->type_name(words_value(agentloop::retriever_names()))
        ->check([](const std::string& value) {
            return agentloop::valid_retriever(value)
                       ? std::string{}
                       : agentloop::retriever_values_message("", value);
        });
    cmd->add_option("--rerank", flags->rerank,
                    "Backend that reorders retrieved chunks with one generation call, or off")
        ->type_name(kBackendValue);
    cmd->add_option("--branch", flags->branch,
                    "Branch under review (the head); reviewed without checking it out")
        ->type_name(kGitRefValue);
    cmd->add_option("--base", flags->base, "Ref to compare against (default: the default branch)")
        ->type_name(kGitRefValue);
    cmd->add_option("--remote", flags->remote, "Remote to resolve refs against (default origin)")
        ->type_name(kGitRemoteValue);
    cmd->add_flag("--fetch", flags->fetch, "Always fetch the refs before diffing");
    cmd->add_flag("--no-fetch", flags->no_fetch, "Never fetch; refuse a ref that is absent");
    cmd->add_flag("--no-questions", flags->no_questions,
                  "Never advertise ask_user, even for an agent that opted in");
    flags->temperature_option =
        cmd->add_option("-t,--temperature", flags->temperature, "Sampling temperature");
    flags->max_tokens_option =
        cmd->add_option("-n,--max-tokens", flags->max_tokens, "Maximum tokens to generate");
    cmd->add_flag("-q,--quiet", flags->quiet, "Suppress status output");
    cmd->add_flag("-v,--verbose", flags->verbose, "Print progress notes to stderr");
    cmd->add_flag("--no-color", flags->no_color, "Disable ANSI colour output");
    cmd->add_option_function<std::string>(
           "--output-format",
           [flags](const std::string& value) {
               const std::optional<OutputFormat> parsed = output_format_from_string(value);
               if (!parsed.has_value()) {
                   throw CLI::ValidationError("--output-format",
                                              "expected 'text' or 'stream-json'");
               }
               flags->output_format = *parsed;
           },
           "Output format: text (default) or stream-json for a machine driver")
        ->type_name(words_value(format_names()));

    cmd->callback([&context, flags]() {
        harness::Config config;
        const std::filesystem::path config_path = harness::resolve_config_path(context.config_path);
        try {
            config = harness::load_config(config_path);
        } catch (const harness::ConfigError& e) {
            fail_user(e.what());
        }

        if (flags->list) {
            print_agent_list(config);
            return;
        }
        if (flags->agent.empty() && flags->prompts.empty()) {
            fail_user("provide --agent <name> (see --list) or --prompt <file>");
        }
        const LoadedAgent loaded = load_agent(config, config_path, *flags);
        const std::filesystem::path home = harness::home_for_config(config_path);
        std::string input = resolve_input(*flags);

        // --- the model, and the refusal by type ------------------------------
        const std::string requested = flags->model.empty() ? loaded.config.model : flags->model;
        if (!requested.empty() && !names_a_configured_backend(config, requested)) {
            std::string known;
            for (const std::string& name : config.backend_names()) {
                known += known.empty() ? "" : ", ";
                known += name;
            }
            fail_user("no backend named '" + requested + "'" +
                      (known.empty() ? "" : " (configured: " + known + ")"));
        }
        const std::string model = harness::resolve_chat_backend(config, requested);
        if (const harness::BackendConfig* entry =
                config.find_backend(configured_backend_key(config, model));
            entry != nullptr && harness::is_vendor_cli(entry->type)) {
            // Refused BEFORE anything is built, so no vendor CLI is spawned:
            // the CLI runs its own tools outside Apogee's gate, so an agent's
            // policy cannot hold there and the loop would see no tool call.
            fail_user("backend '" + model + "' is a vendor-CLI backend (type " +
                      std::string{harness::to_string(entry->type)} +
                      "); analyze runs agents on API-billing and local backends only -- an "
                      "agent's tool policy cannot be enforced inside a vendor CLI's own loop");
        }

        harness::Harness harness{config};
        backends::BuildOptions build_options;
        build_options.config_path = config_path;
        const backends::BuildResult built = backends::build_providers(harness, build_options);
        if (built.constructed_count() == 0) {
            std::string message = "no usable backend is configured";
            if (!built.skipped_summary().empty()) {
                message += " -- " + built.skipped_summary();
            } else {
                message += " (add one with 'apogee config add-backend')";
            }
            fail_user(message);
        }
        for (const backends::BackendStatus& status : built.statuses) {
            if (!status.constructed && status.name == model) {
                fail_user("backend '" + model + "' is configured but unavailable -- " +
                          status.reason);
            }
        }

        // --- the review context: flags, never free text ---------------------
        agentloop::ReviewContext review;
        review.head = flags->branch;
        review.base = flags->base;
        review.remote = flags->remote.empty() ? "origin" : flags->remote;
        review.fetch = fetch_mode_for(*flags);
        tools::ReviewDefaults review_defaults;
        review_defaults.head = review.head;
        review_defaults.base = review.base;
        review_defaults.remote = review.remote;
        review_defaults.fetch = review.fetch;

        const std::string system_prompt = system_prompt_for(loaded, review);
        const std::optional<double> temperature =
            flags->temperature_option->count() > 0
                ? std::optional<double>{flags->temperature}
                : resolve_temperature(std::nullopt, config, model);
        const std::optional<std::int64_t> max_tokens =
            flags->max_tokens_option->count() > 0 ? std::optional<std::int64_t>{flags->max_tokens}
                                                  : resolve_max_tokens(std::nullopt, config, model);
        const harness::AgentToolPolicy policy = loaded.config.tools;
        const bool stdout_tty = platform::is_terminal(platform::StandardStream::Out);
        const std::filesystem::path save_dir = save_directory(*flags, loaded, home);
        const std::string save_base = save_base_name(*flags, loaded);
        const bool will_save = !save_dir.empty();

        // The registry IS the policy: what is not registered is not
        // advertised, whatever the model asks for. Read-only agents connect
        // only the servers they name, and keep only their read-only tools.
        const auto mcp_registry = std::make_shared<mcp::Registry>();
        const auto build_tools = [&](const std::function<void(std::string_view)>& status) {
            // Designators in declaration order: C++20 requires it, and GCC
            // enforces what Clang only warns about.
            return make_built_in_tools(BuiltInToolOptions{
                .config = &config,
                .harness = &harness,
                .review = review_defaults,
                .policy = policy,
                .mcp_servers = std::optional<std::vector<std::string>>{loaded.config.mcp},
                .mcp = mcp_registry,
                .mcp_status = status,
                .mcp_server_log = flags->verbose
                                      ? mcp::StderrTail::Sink{[](std::string_view bytes) {
                                            std::cerr << bytes << std::flush;
                                        }}
                                      : mcp::StderrTail::Sink{}});
        };

        std::vector<harness::ChatMessage> history;
        if (!system_prompt.empty()) {
            history.push_back(harness::ChatMessage::system(system_prompt));
        }

        // The session's retrieval choice: the flag, else the agent's own
        // collection -- the `auto_rag` mechanism read from the agent instead
        // of the top-level key. `/rag` in an interactive session re-points it.
        bool rag_flag_given = flags->rag_option->count() > 0;
        std::string rag_flag = flags->rag;

        // One turn, for every mode: retrieval into the transient prefix, the
        // structured or plain run, and an honest outcome.
        const auto run_turn = [&](const std::string& question, agentloop::Options options,
                                  agentloop::Reporter& reporter,
                                  const std::function<void(std::string_view, bool)>& note) {
            const RagChoice rag_choice =
                choose_rag_collection(rag_flag_given, rag_flag, loaded.config.collection);
            if (rag_choice.active()) {
                const agentloop::RagResult rag =
                    retrieve_for_collection(harness, config, rag_choice.collection, question,
                                            flags->rag_limit, flags->retriever, flags->rerank, {});
                if (!rag.error.empty() && !flags->retriever.empty()) {
                    fail_user(rag.error);
                }
                if (rag.error.empty() && !rag.prefix.empty()) {
                    options.transient_prefix = rag.prefix;
                }
                note(describe_retrieval(rag_choice, rag), !rag.error.empty());
            }
            TurnOutput turn;
            if (loaded.structured()) {
                const agentloop::StructuredResult result =
                    agentloop::run_structured(harness, history, options, reporter, *loaded.schema);
                turn.answer = result.answer;
                turn.json = result.json;
                turn.structured = true;
                turn.conforms = result.conforms;
                turn.errors = result.errors;
                turn.hit_limit = result.run.hit_iteration_limit;
            } else {
                const agentloop::RunResult result =
                    agentloop::run(harness, history, options, reporter);
                turn.answer = result.answer;
                turn.hit_limit = result.hit_iteration_limit;
                if (flags->json || flags->markdown || flags->text) {
                    turn.json = agentloop::extract_json(turn.answer);
                }
            }
            if (turn.hit_limit) {
                note("tool-call limit reached; answered without tools", true);
            }
            if (turn.structured && !turn.conforms) {
                std::string why;
                for (const std::string& error : turn.errors) {
                    why += why.empty() ? "" : "; ";
                    why += error;
                }
                note("the answer does not conform to the schema (conforms: false): " + why, true);
            }
            return turn;
        };

        // --- machine mode: the same loop, a different Reporter ---------------
        if (flags->output_format == OutputFormat::StreamJson) {
            if (flags->interactive) {
                fail_user("--interactive has no machine-mode form; drive 'apogee chat' instead");
            }
            JsonReporter reporter{std::cout};
            reporter.begin_session(model);
            agent::ToolRegistry registry =
                build_tools([](std::string_view line) { std::cerr << line << "\n"; });
            agentloop::Options options;
            options.model = model;
            options.temperature = temperature;
            options.max_tokens = max_tokens;
            options.stream_answer = true;
            options.tools = registry.empty() ? nullptr : &registry;
            if (policy == harness::AgentToolPolicy::All) {
                options.permission = make_permission_checker(config, nullptr);
            }
            history.push_back(
                harness::ChatMessage::user(input.empty() ? std::string{kDefaultInput} : input));
            try {
                const TurnOutput turn = run_turn(
                    input, options, reporter,
                    [](std::string_view line, bool) { std::cerr << "[apogee] " << line << "\n"; });
                harness::ChatResponse response;
                response.message = harness::ChatMessage::assistant(turn.answer);
                response.model = model;
                reporter.emit_result(response);
                if (will_save) {
                    const Rendering rendering =
                        resolve_rendering(flags->json, flags->text, flags->markdown,
                                          loaded.structured(), turn.answer);
                    std::string error;
                    const std::filesystem::path saved = save_report(
                        save_dir, save_base, rendering, render_output(turn, rendering), error);
                    if (saved.empty()) {
                        std::cerr << "[apogee] warning: could not save the report -- " << error
                                  << "\n";
                    } else {
                        std::cerr << "Saved: " << saved.string() << "\n";
                    }
                }
            } catch (const harness::CancelledError&) {
                reporter.emit_error("cancelled");
                throw CLI::RuntimeError(kCancelled);
            } catch (const harness::HarnessError& e) {
                reporter.emit_error(e.what());
                fail_backend(e.what());
            }
            return;
        }

        // --- the terminal ----------------------------------------------------
        TerminalWriter status_writer{std::cerr};
        CliReporter::Options reporter_options;
        reporter_options.answer_stream = &std::cout;
        reporter_options.decorate = stdout_tty && !flags->quiet;
        reporter_options.verbosity = flags->verbose ? ansi::Verbosity::Verbose
                                     : flags->quiet ? ansi::Verbosity::Quiet
                                                    : ansi::Verbosity::Line;
        reporter_options.style =
            ansi::Style::detect(flags->no_color ? ansi::ColorMode::Never : ansi::ColorMode::Auto);
        reporter_options.width = static_cast<std::size_t>(platform::terminal_width().value_or(80));
        CliReporter reporter{status_writer, reporter_options};
        const ansi::Style& style = reporter_options.style;
        const auto note = [&](std::string_view line, bool warning) {
            reporter.status().print_line(
                style.tag(warning ? ansi::Role::Warning : ansi::Role::Apogee) + " " +
                std::string{line});
        };

        if (flags->verbose) {
            reporter.status().print_line(
                style.tag(ansi::Role::Apogee) + " " + model +
                (loaded.name.empty() ? "" : "  ·  agent " + loaded.name) + "  ·  tools " +
                std::string{harness::to_string(policy)} +
                (review.active() ? "  ·  review " + agentloop::review_summary(review) : ""));
        }

        agent::ToolRegistry registry = build_tools(mcp_status_line(reporter.status()));
        agentloop::Options options;
        options.model = model;
        options.temperature = temperature;
        options.max_tokens = max_tokens;
        options.tools = registry.empty() ? nullptr : &registry;
        // The gate only for an `all` agent: a read-only registry has nothing
        // to prompt for, which is the whole point of the policy.
        if (policy == harness::AgentToolPolicy::All) {
            const auto approvals = std::make_shared<SessionApprovals>();
            options.permission = make_permission_checker(config, approvals);
            options.confirm = terminal_confirm_fn(reporter.status(), style, config_path, approvals);
        }
        // ask_user: per-agent opt-in, on a terminal, unless switched off --
        // and a null AskFn means the tool is never advertised at all.
        if (loaded.config.questions && !flags->no_questions) {
            options.ask = terminal_ask_fn(reporter.status(), style);
        }

        const auto deliver = [&](const TurnOutput& turn, bool print, bool streamed) {
            const Rendering rendering = resolve_rendering(flags->json, flags->text, flags->markdown,
                                                          loaded.structured(), turn.answer);
            const std::string output = render_output(turn, rendering);
            if (print && !streamed) {
                std::cout << output << "\n" << std::flush;
            }
            if (will_save) {
                std::string error;
                const std::filesystem::path saved =
                    save_report(save_dir, save_base, rendering, output, error);
                if (saved.empty()) {
                    note("could not save the report -- " + error, true);
                } else {
                    // stderr, so a redirected stdout holds only the report.
                    std::cerr << "Saved: " << saved.string() << "\n";
                }
            }
        };

        if (!flags->interactive) {
            const bool print = should_print_report(flags->show, flags->json, stdout_tty, will_save);
            // Live streaming only when the raw answer is what will be shown;
            // a report rendered afterwards must not appear twice.
            const bool stream =
                print && !loaded.structured() && !flags->json && !flags->markdown && !flags->text;
            options.stream_answer = stream;
            history.push_back(
                harness::ChatMessage::user(input.empty() ? std::string{kDefaultInput} : input));
            try {
                const TurnOutput turn = run_turn(input, options, reporter, note);
                deliver(turn, print, stream);
            } catch (const harness::CancelledError&) {
                throw CLI::RuntimeError(kCancelled);
            } catch (const harness::NoAvailableBackendError& e) {
                fail_user(e.what());
            } catch (const harness::HarnessError& e) {
                fail_backend(e.what());
            }
            return;
        }

        // --- interactive: a session under the persona ------------------------
        options.stream_answer = false;
        EditingLineReader::Options reader_options;
        reader_options.history_path = default_history_path();
        reader_options.suggest = word_suggester({"/help", "/rag", "/rags", "/exit", "/quit"});
        const std::unique_ptr<LineReader> reader =
            make_line_reader(std::move(reader_options), std::cin);
        if (reporter_options.decorate) {
            reporter.status().print_line(style.tag(ansi::Role::Apogee) + " " + model +
                                         (loaded.name.empty() ? "" : "  ·  agent " + loaded.name) +
                                         "  ·  tools " + std::string{harness::to_string(policy)} +
                                         "  ·  /help for commands");
        }
        bool first = true;
        for (;;) {
            std::string question;
            if (first && !input.empty()) {
                question = input;
            } else {
                std::optional<std::string> line;
                if (reader->interactive()) {
                    line = reader->read(style.colorize("You: ", ansi::Color::Green));
                } else {
                    if (reporter_options.decorate) {
                        std::cerr << style.colorize("You: ", ansi::Color::Green) << std::flush;
                    }
                    line = reader->read({});
                }
                if (!line.has_value()) {
                    break;
                }
                question = trim(*line);
                if (question.empty()) {
                    continue;
                }
                reader->remember(question);
                if (question == "exit" || question == "quit" || question == "/exit" ||
                    question == "/quit") {
                    break;
                }
                if (question == "/help") {
                    reporter.status().print_line("/help  /rag [name|off]  /rags  /exit");
                    continue;
                }
                if (question == "/rags") {
                    for (const std::string& collection : config.embedding_names()) {
                        reporter.status().print_line("  " + collection);
                    }
                    continue;
                }
                if (question == "/rag" || question.starts_with("/rag ")) {
                    const std::string argument = trim(question.substr(4));
                    if (argument.empty()) {
                        const RagChoice choice = choose_rag_collection(rag_flag_given, rag_flag,
                                                                       loaded.config.collection);
                        reporter.status().print_line(
                            style.tag(ansi::Role::Rag) + " " +
                            (choice.active() ? "retrieving from '" + choice.collection + "'"
                                             : "retrieval off -- /rag <name> to enable"));
                    } else {
                        // An explicit choice for the rest of the session; `off`
                        // is the empty flag, exactly as `--rag ""` is.
                        rag_flag_given = true;
                        rag_flag = argument == "off" ? std::string{} : argument;
                        reporter.status().print_line(style.tag(ansi::Role::Rag) + " " +
                                                     (argument == "off"
                                                          ? "retrieval off"
                                                          : "retrieving from '" + argument + "'"));
                    }
                    continue;
                }
            }
            first = false;
            history.push_back(harness::ChatMessage::user(question));
            try {
                const TurnOutput turn = run_turn(question, options, reporter, note);
                std::cout << "\n";
                deliver(turn, true, false);
            } catch (const harness::CancelledError&) {
                note("cancelled", true);
                history.pop_back();
            } catch (const harness::HarnessError& e) {
                note(e.what(), true);
                history.pop_back();
            }
        }
    });
}

}  // namespace apogee::commands
