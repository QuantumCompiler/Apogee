#include "commands/complete_sources.h"

#include <algorithm>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "commands/config_cmd.h"
#include "commands/embed.h"
#include "commands/knowledge_core.h"
#include "harness/assets.h"
#include "harness/cancellation.h"
#include "harness/layout.h"
#include "harness/paths.h"
#include "knowledge/store.h"
#include "logger/session.h"
#include "models/source_ollama.h"
#include "models/store.h"
#include "platform/child_process.h"
#include "tools/toolsets.h"
#include "training/datasets.h"
#include "training/kit.h"
#include "training/script_runner.h"
#include "training/store.h"

namespace apogee::commands {
namespace {

[[nodiscard]] bool has_space(std::string_view text) {
    return text.find_first_of(" \t") != std::string_view::npos;
}

/// The stems of `dir`'s files ending in `suffix`, the suffix removed.
[[nodiscard]] std::vector<std::string> stems(const std::filesystem::path& dir,
                                             std::string_view suffix) {
    std::vector<std::string> out;
    std::error_code code;
    for (const auto& entry : std::filesystem::directory_iterator(dir, code)) {
        const std::string name = entry.path().filename().string();
        if (entry.is_regular_file(code) && name.size() > suffix.size() && name.ends_with(suffix)) {
            out.push_back(name.substr(0, name.size() - suffix.size()));
        }
    }
    return out;
}

/// The store the `models` verbs use: GGUFs under the data directory, and
/// SafeTensors sets under `paths.hf_dir` when the config sets it.
[[nodiscard]] models::StoreRoots store_roots(const harness::Config& config) {
    const std::filesystem::path models = harness::models_dir();
    return models::StoreRoots{
        .models = models,
        .safetensors =
            config.paths.hf_dir.empty()
                ? models
                : std::filesystem::path{harness::expand_env_and_home(config.paths.hf_dir)}};
}

/// Models in the store, and each set of their weights by handle
/// (`<model>/<format>/<id>`), for one format or both.
[[nodiscard]] std::vector<std::string> stored_models(const models::StoreRoots& roots, bool ggufs,
                                                     bool snapshots) {
    std::vector<std::string> out;
    if (ggufs) {
        for (const models::StoredGguf& stored : models::list_store_ggufs(roots)) {
            out.push_back(stored.model);
            out.push_back(stored.model + "/" + std::string{models::kGgufFormat} + "/" + stored.id);
        }
    }
    if (snapshots) {
        for (const models::StoredSnapshot& stored : models::list_store_snapshots(roots)) {
            out.push_back(stored.model);
            out.push_back(stored.model + "/" + std::string{models::kSafetensorsFormat} + "/" +
                          stored.id);
        }
    }
    return out;
}

/// The ids of the model the command's first positional names.
[[nodiscard]] NameList weight_ids(const CompletionContext& context, bool ggufs) {
    NameList list;
    if (context.positionals.empty() || context.config == nullptr) {
        list.none = "name the model first";
        return list;
    }
    const models::StoreRoots roots = store_roots(*context.config);
    const models::StoreTarget target =
        models::resolve_store_target(roots, context.positionals.front());
    if (!target.error.empty() || target.outside) {
        return list;
    }
    if (ggufs) {
        for (const models::StoredGguf& stored : models::list_store_ggufs(roots, target.model)) {
            list.names.push_back(stored.id);
        }
    } else {
        for (const models::StoredSnapshot& stored :
             models::list_store_snapshots(roots, target.model)) {
            list.names.push_back(stored.id);
        }
    }
    list.none =
        std::string{"no "} + (ggufs ? "GGUFs" : "SafeTensors sets") + " stored for " + target.model;
    return list;
}

/// The lines `git <arguments>` prints in the working directory, or nothing
/// outside a repository.
[[nodiscard]] std::vector<std::string> git_lines(std::vector<std::string> arguments) {
    platform::ChildCommand command;
    command.program = "git";
    command.arguments = std::move(arguments);
    const training::CommandResult result =
        training::run_command(command, harness::CancellationToken{});
    std::vector<std::string> out;
    if (!result.ok()) {
        return out;
    }
    std::istringstream lines{result.out};
    for (std::string line; std::getline(lines, line);) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            out.push_back(line);
        }
    }
    return out;
}

[[nodiscard]] std::vector<std::string> kit_names() {
    std::vector<std::string> out;
    for (const harness::BundledKit& kit : harness::bundled_kits()) {
        out.emplace_back(kit.name);
    }
    for (const training::KitSummary& kit : training::list_kits(harness::training_kits_dir())) {
        out.push_back(kit.name);
    }
    return out;
}

}  // namespace

NameList list_names(std::string_view kind, const CompletionContext& context) {
    const harness::Config empty;
    const harness::Config& config = context.config != nullptr ? *context.config : empty;
    NameList list;

    if (kind == kCollectionValue || kind == kCollectionListValue) {
        list.names = collection_names();
        list.none = "none yet -- 'apogee embed ingest <name> <path>' makes one";
    } else if (kind == kGraphValue) {
        list.names = config.graph_names();
        for (std::string& name : collection_names()) {
            list.names.push_back(std::move(name));
        }
        list.none = "no named graphs or collections yet";
    } else if (kind == kNamedGraphValue) {
        list.names = config.graph_names();
        list.none = "no named graphs -- 'apogee config add-graph' makes one";
    } else if (kind == kAgentValue) {
        for (const harness::NamedAgent& agent : harness::all_agents(config)) {
            list.names.push_back(agent.name);
        }
    } else if (kind == kChatValue) {
        for (const logger::Session& session : logger::list_sessions()) {
            list.names.push_back(session.chat_id);
            // A name with a space cannot be one word on a command line.
            if (!session.custom_name.empty() && !has_space(session.custom_name)) {
                list.names.push_back(session.custom_name);
            }
        }
        list.none = "no saved conversations yet";
    } else if (kind == kServerValue) {
        list.names = config.mcp_server_names();
        list.none = "no MCP servers configured -- 'apogee mcp create'";
    } else if (kind == kDatasetValue) {
        for (const training::DatasetInfo& dataset :
             training::DatasetStore{harness::training_datasets_dir()}.list()) {
            list.names.push_back(dataset.name);
        }
        list.paths = true;
    } else if (kind == kKitValue) {
        list.names = kit_names();
        list.paths = true;
    } else if (kind == kSuiteValue) {
        list.names = stems(harness::training_suites_dir(), ".jsonl");
        for (std::string& name : stems(harness::training_datasets_dir(), ".eval.jsonl")) {
            list.names.push_back(std::move(name));
        }
        for (std::string& name : kit_names()) {
            list.names.push_back(std::move(name));
        }
        list.paths = true;
    } else if (kind == kRunValue) {
        for (const training::RunSummary& run :
             training::TrainingStore{harness::training_dir()}.list_runs()) {
            list.names.push_back(run.id);
        }
        list.none = "no training runs yet -- 'apogee train run'";
    } else if (kind == kPipelineRunValue) {
        for (const training::PipelineSummary& run :
             training::TrainingStore{harness::training_dir()}.list_pipelines()) {
            list.names.push_back(run.id);
        }
        list.none = "no pipeline runs yet -- 'apogee train pipeline run'";
    } else if (kind == kPipelineValue) {
        for (const auto& [name, spec] : config.training.pipelines) {
            list.names.push_back(name);
        }
        list.paths = true;
    } else if (kind == kRegimeValue) {
        for (const auto& [name, spec] : config.training.regimes) {
            list.names.push_back(name);
        }
        list.paths = true;
    } else if (kind == kModelValue) {
        list.names = stored_models(store_roots(config), true, true);
        list.none = "the model store is empty -- 'apogee models pull'";
    } else if (kind == kSnapshotValue) {
        list.names = stored_models(store_roots(config), false, true);
        list.paths = true;
    } else if (kind == kGgufValue) {
        list.names = stored_models(store_roots(config), true, false);
        list.paths = true;
    } else if (kind == kSnapshotIdValue) {
        list = weight_ids(context, false);
    } else if (kind == kGgufIdValue) {
        list = weight_ids(context, true);
    } else if (kind == kPullRefValue) {
        for (const models::OllamaEntry& entry : models::list_store(models::ollama_store_root())) {
            list.names.push_back(entry.ref);
        }
    } else if (kind == kRecordValue) {
        const auto db = context.flags.find("--db");
        const std::string name =
            knowledge_collection(config, db == context.flags.end() ? "" : db->second);
        const std::filesystem::path path = collection_path(name);
        std::error_code code;
        if (std::filesystem::is_regular_file(path, code)) {  // opening would create it
            for (const knowledge::Record& record : knowledge::Store{path, {}}.list()) {
                list.names.push_back(record.id);
            }
        }
        list.none = "no records in '" + name + "' yet -- 'apogee knowledge capture'";
    } else if (kind == kGitRefValue) {
        list.names = git_lines({"for-each-ref", "--format=%(refname:short)", "refs/heads",
                                "refs/remotes", "refs/tags"});
        list.none = "not in a git repository";
    } else if (kind == kGitRemoteValue) {
        list.names = git_lines({"remote"});
        list.none = "no remotes, or not in a git repository";
    } else if (kind == kConfigKeyValue) {
        list.names = config_keys(config);
    } else if (kind == kToolValue) {
        for (const std::string_view tool : tools::destructive_tool_names()) {
            list.names.emplace_back(tool);
        }
    } else {
        // A kind declared in `kNameValues` with no branch here: a test calls
        // every kind, so this is a failure there, never a silent nothing.
        throw std::invalid_argument("no source for the name kind '" + std::string{kind} + "'");
    }
    return list;
}

CompletionSources default_completion_sources() {
    return [](std::string_view kind, const CompletionContext& context) {
        return list_names(kind, context);
    };
}

}  // namespace apogee::commands
