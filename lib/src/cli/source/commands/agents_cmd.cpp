#include "commands/agents_cmd.h"

#include <CLI/CLI.hpp>

#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "commands/helpers.h"
#include "harness/assets.h"
#include "harness/config.h"
#include "harness/config_edit.h"
#include "harness/paths.h"
#include "platform/child_process.h"
#include "platform/platform.h"
#include "scaffold/agent.h"

namespace apogee::commands {
namespace {

[[noreturn]] void fail(const std::string& message) {
    std::cerr << "apogee agents: " << message << "\n";
    throw CLI::RuntimeError(kUserError);
}

std::filesystem::path config_path_for(const RootContext& context) {
    return harness::resolve_config_path(context.config_path);
}

harness::Config load(const std::filesystem::path& path) {
    try {
        return harness::load_config(path);
    } catch (const harness::ConfigError& e) {
        fail(e.what());
    }
}

bool terminal_in() {
    return platform::is_terminal(platform::StandardStream::In) &&
           platform::is_terminal(platform::StandardStream::Err);
}

/// A line from the terminal, with a default. The prompt goes to stderr so
/// stdout stays what the command prints.
std::string prompt_default(std::string_view label, std::string_view fallback) {
    std::cerr << label;
    if (!fallback.empty()) {
        std::cerr << " [" << fallback << "]";
    }
    std::cerr << ": " << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) {
        return std::string{fallback};
    }
    std::size_t end = line.size();
    while (end > 0 && (line[end - 1] == ' ' || line[end - 1] == '\r' || line[end - 1] == '\t')) {
        --end;
    }
    line.resize(end);
    return line.empty() ? std::string{fallback} : line;
}

std::string basename_summary(const std::vector<std::string>& paths) {
    if (paths.empty()) {
        return "-";
    }
    std::string out = std::filesystem::path{paths.front()}.filename().string();
    if (paths.size() > 1) {
        out += " +" + std::to_string(paths.size() - 1);
    }
    return out;
}

std::vector<std::string> split_words(std::string_view text) {
    std::vector<std::string> words;
    std::istringstream in{std::string{text}};
    std::string word;
    while (in >> word) {
        words.push_back(word);
    }
    return words;
}

void bind_create(CLI::App& parent, const RootContext& context) {
    struct Flags {
        std::string name;
        std::string description;
        std::string model;
        std::string tools;
        bool no_schema = false;
        bool force = false;
        std::string output_format;
        std::string collection;
        std::string save_dir;
        std::string save_name;
        std::string save_subdir;
        bool questions = false;
        std::vector<std::string> mcp;
        CLI::Option* description_option = nullptr;
        CLI::Option* model_option = nullptr;
        CLI::Option* tools_option = nullptr;
    };

    auto flags = std::make_shared<Flags>();
    CLI::App* cmd = parent.add_subcommand(
        "create", "Scaffold an agent: a starter prompt, an output schema, and a config entry");
    cmd->add_option("name", flags->name, "The agent's name (letters, digits, _ and -)")->required();
    flags->description_option =
        cmd->add_option("--description", flags->description, "Short description of the agent");
    flags->model_option =
        cmd->add_option("--model", flags->model, "Backend override (blank = models.default)")
            ->type_name(kBackendValue);
    flags->tools_option =
        cmd->add_option("--tools", flags->tools, "Tool policy: read-only (default) | all | none")
            ->type_name(words_value(harness::agent_tool_policy_names()));
    cmd->add_flag("--no-schema", flags->no_schema, "A prose-only agent (no output schema)");
    cmd->add_flag("--force", flags->force, "Replace an existing agent and its files");
    cmd->add_option("--output-format", flags->output_format,
                    "How the schema shapes the answer: auto | json | markdown")
        ->type_name(words_value(harness::agent_output_format_names()));
    cmd->add_option("--collection", flags->collection,
                    "Collection retrieved from on every run (the agent's auto_rag)")
        ->type_name(kCollectionValue);
    cmd->add_option("--save-dir", flags->save_dir, "Directory reports are saved in")
        ->type_name(kPathValue);
    cmd->add_option("--save-name", flags->save_name, "Base filename for saved reports");
    cmd->add_option("--save-subdir", flags->save_subdir,
                    "Subdirectory under the save directory (default: the agent's name)");
    cmd->add_flag("--questions", flags->questions,
                  "Let the agent ask the user questions mid-turn (ask_user) on a terminal");
    cmd->add_option("--mcp", flags->mcp, "MCP server to connect for this agent (repeatable)")
        ->type_name(kServerValue)
        ->allow_extra_args(false);
    cmd->callback([&context, flags]() {
        // Flags first; a terminal fills in what was omitted; a pipe takes
        // the defaults so a script never blocks.
        const bool tty = terminal_in();
        if (flags->description_option->count() == 0 && tty) {
            flags->description = prompt_default("Description", "");
        }
        if (flags->model_option->count() == 0 && tty) {
            flags->model = prompt_default("Model (blank = models.default)", "");
        }
        if (flags->tools_option->count() == 0 && tty) {
            std::cerr << "Tool policy:  1) read-only (never prompts)   2) all (gated like chat)"
                         "   3) none\n";
            const std::string choice = prompt_default("Select", "1");
            flags->tools = choice == "2" || choice == "all"    ? "all"
                           : choice == "3" || choice == "none" ? "none"
                                                               : "read-only";
        }
        scaffold::AgentSpec spec;
        spec.name = flags->name;
        spec.description = flags->description;
        spec.model = flags->model;
        spec.tools = flags->tools;
        spec.no_schema = flags->no_schema;
        spec.force = flags->force;
        spec.output_format = flags->output_format;
        spec.collection = flags->collection;
        spec.questions = flags->questions;
        spec.save_dir = flags->save_dir;
        spec.save_filename = flags->save_name;
        spec.save_subdir = flags->save_subdir;
        spec.mcp = flags->mcp;
        scaffold::AgentResult result;
        try {
            result = scaffold::create_agent(config_path_for(context), spec);
        } catch (const std::exception& e) {
            fail(e.what());
        }
        std::cout << "created agent '" << result.name << "'\n"
                  << "  prompt: " << result.prompt_path.string() << "\n";
        if (!result.schema_path.empty()) {
            std::cout << "  schema: " << result.schema_path.string() << "\n";
        }
        std::cout << "  config: " << result.config_path.string() << "\n\n"
                  << "Run it:   apogee analyze --agent " << result.name << "\n"
                  << "Edit it:  apogee agents edit " << result.name << "\n";
    });
}

void bind_list(CLI::App& parent, const RootContext& context) {
    CLI::App* cmd = parent.add_subcommand("list", "List the agents: name, model, tools, files");
    cmd->callback([&context]() {
        const harness::Config config = load(config_path_for(context));
        std::cout << std::left << std::setw(22) << "NAME" << std::setw(18) << "MODEL"
                  << std::setw(12) << "TOOLS" << std::setw(26) << "PROMPT" << std::setw(30)
                  << "SCHEMA"
                  << "SOURCE\n";
        for (const harness::NamedAgent& agent : harness::all_agents(config)) {
            std::string tools{harness::to_string(agent.config.tools)};
            if (agent.config.questions) {
                tools += "+questions";
            }
            std::cout << std::left << std::setw(22) << agent.name << std::setw(18)
                      << (agent.config.model.empty() ? "(default)" : agent.config.model)
                      << std::setw(12) << tools << std::setw(26)
                      << basename_summary(agent.config.prompts) << std::setw(30)
                      << basename_summary(agent.config.schemas)
                      << (agent.bundled ? "bundled" : "config") << "\n";
        }
    });
}

void bind_edit(CLI::App& parent, const RootContext& context) {
    auto name = std::make_shared<std::string>();
    CLI::App* cmd = parent.add_subcommand("edit", "Open an agent's prompt and schema in $EDITOR");
    cmd->add_option("name", *name, "The agent's name")->type_name(kAgentValue)->required();
    cmd->callback([&context, name]() {
        const std::filesystem::path config_path = config_path_for(context);
        const harness::Config config = load(config_path);
        const std::optional<harness::AgentConfig> agent = harness::resolve_agent(config, *name);
        if (!agent.has_value()) {
            fail("no agent named '" + *name + "' (see: apogee agents list)");
        }
        const std::vector<std::filesystem::path> files = scaffold::agent_files(config_path, *agent);
        if (files.empty()) {
            fail("agent '" + *name + "' has no prompt or schema files to edit");
        }
        std::error_code code;
        for (const std::filesystem::path& file : files) {
            if (!std::filesystem::exists(file, code)) {
                fail(file.string() +
                     " does not exist -- 'apogee check --fix' seeds the bundled "
                     "files; a missing custom file wants 'apogee agents create "
                     "--force'");
            }
        }
        // $EDITOR may carry flags ("code --wait"): split on spaces, like
        // every tool that honours it. The editor is the user's own foreground
        // program: it gets the terminal, the one documented exception to the
        // captured-stderr rule (see platform/child_process.h).
        const char* editor_env = std::getenv("EDITOR");
        const std::string editor = editor_env == nullptr || *editor_env == '\0' ? "vi" : editor_env;
        std::vector<std::string> words = split_words(editor);
        platform::ChildCommand command;
        command.program = words.front();
        command.arguments.assign(words.begin() + 1, words.end());
        for (const std::filesystem::path& file : files) {
            command.arguments.push_back(file.string());
        }
        std::string error;
        const std::optional<int> status = platform::run_foreground(command, error);
        if (!status.has_value()) {
            fail("could not open the editor: " + error);
        }
        if (*status != 0) {
            fail("the editor exited with status " + std::to_string(*status));
        }
    });
}

void bind_delete(CLI::App& parent, const RootContext& context) {
    auto name = std::make_shared<std::string>();
    auto purge = std::make_shared<bool>(false);
    auto keep = std::make_shared<bool>(false);
    CLI::App* cmd =
        parent.add_subcommand("delete", "Remove an agent's entry, and optionally its files");
    cmd->add_option("name", *name, "The agent's name")->type_name(kAgentValue)->required();
    cmd->add_flag("--purge", *purge, "Also delete the prompt and schema files, without asking");
    cmd->add_flag("--keep-files", *keep, "Keep the files (never asks)");
    cmd->callback([&context, name, purge, keep]() {
        if (*purge && *keep) {
            fail("--purge and --keep-files are mutually exclusive");
        }
        const std::filesystem::path config_path = config_path_for(context);
        const harness::Config config = load(config_path);
        const harness::AgentConfig* entry = config.find_agent(*name);
        if (entry == nullptr) {
            if (harness::find_bundled_agent(*name) != nullptr) {
                fail("'" + *name +
                     "' is a bundled agent with no config entry; delete its files under "
                     "prompts/ and schemas/ to reset them, or add an entry to override it");
            }
            fail("no agent named '" + *name + "' (see: apogee agents list)");
        }
        const std::vector<std::filesystem::path> files = scaffold::agent_files(config_path, *entry);
        try {
            harness::edit_config_file(config_path, [name](std::string_view content) {
                return harness::delete_agent(content, *name);
            });
        } catch (const std::exception& e) {
            fail(e.what());
        }
        std::cout << "removed agent '" << *name << "' from " << config_path.string() << "\n";
        if (files.empty() || *keep) {
            return;
        }
        bool remove = *purge;
        if (!remove) {
            if (!terminal_in()) {
                return;  // nobody to ask: the files stay
            }
            std::cout << "Associated files:\n";
            for (const std::filesystem::path& file : files) {
                std::cout << "  " << file.string() << "\n";
            }
            const std::string answer = prompt_default("Remove these files too? [y/N]", "n");
            remove = answer == "y" || answer == "Y" || answer == "yes";
        }
        if (!remove) {
            return;
        }
        for (const std::filesystem::path& file : files) {
            std::error_code code;
            if (std::filesystem::remove(file, code) || !code) {
                std::cout << "  removed " << file.string() << "\n";
            } else {
                std::cout << "  (could not remove " << file.string() << ": " << code.message()
                          << ")\n";
            }
        }
    });
}

}  // namespace

std::string_view AgentsCommand::name() const noexcept {
    return "agents";
}

std::string_view AgentsCommand::summary() const noexcept {
    return "Create and manage agent workflows (apogee analyze --agent)";
}

void AgentsCommand::bind(CLI::App& root, const RootContext& context) {
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->require_subcommand(1);
    bind_create(*cmd, context);
    bind_list(*cmd, context);
    bind_edit(*cmd, context);
    bind_delete(*cmd, context);
}

}  // namespace apogee::commands
