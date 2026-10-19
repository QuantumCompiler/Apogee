#include "cli/complete_sources.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "cli/config_cmd.h"
#include "cli/embed.h"
#include "cli/graph.h"
#include "cli/task_cmd.h"
#include "contracts/assets.h"
#include "contracts/cancellation.h"
#include "contracts/layout.h"
#include "contracts/paths.h"
#include "contracts/review_skill.h"
#include "knowledge/store.h"
#include "logger/session.h"
#include "models/source_ollama.h"
#include "modelstore/kv_cache.h"
#include "modelstore/store.h"
#include "operations/knowledge_core.h"
#include "operations/renames.h"
#include "platform/child_process.h"
#include "symphony/definition.h"
#include "tasks/ledger.h"
#include "tasks/recipe.h"
#include "tasks/task.h"
#include "tools/toolsets.h"
#include "training/convert.h"
#include "training/datasets.h"
#include "training/kit.h"
#include "training/mlx_convert.h"
#include "training/regime_specs.h"
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

/// One format's models and their handles (`<model>/<format>/<id>`), each
/// model before its handles, the models in the one order (45a).
void push_format(std::vector<std::string>& out,
                 std::vector<std::pair<std::string, std::string>> found) {
    std::vector<std::string> models;
    for (const auto& [model, handle] : found) {
        models.push_back(model);
    }
    order_names(models);
    for (const std::string& model : models) {
        out.push_back(model);
        std::vector<std::string> handles;
        for (const auto& [owner, handle] : found) {
            if (owner == model) {
                handles.push_back(handle);
            }
        }
        order_names(handles);
        out.insert(out.end(), handles.begin(), handles.end());
    }
}

/// What a menu shows beside the store's names (45b): each under its format,
/// a GGUF handle by its file -- the family and quantization its name carries
/// -- and its size.
void note_store(const models::StoreRoots& roots, NameList& list) {
    const auto size_of = [](const std::filesystem::path& file) {
        std::error_code code;
        const std::uintmax_t bytes = std::filesystem::file_size(file, code);
        return code ? std::string{} : " · " + models::mib(static_cast<std::int64_t>(bytes));
    };
    for (const models::StoredSnapshot& stored : models::list_store_snapshots(roots)) {
        list.notes.try_emplace(stored.model, CandidateNote{.group = "SafeTensors"});
        list.notes[models::weights_handle(stored.model, models::kSafetensorsFormat, stored.id)] = {
            .group = "SafeTensors", .description = "a SafeTensors set"};
    }
    for (const models::StoredGguf& stored : models::list_store_ggufs(roots)) {
        list.notes.try_emplace(stored.model, CandidateNote{.group = "GGUF"});
        list.notes[models::weights_handle(stored.model, models::kGgufFormat, stored.id)] = {
            .group = "GGUF", .description = stored.file.filename().string() + size_of(stored.file)};
    }
    for (const models::StoredMlx& stored : models::list_store_mlx(roots)) {
        list.notes.try_emplace(stored.model, CandidateNote{.group = "MLX"});
        list.notes[models::weights_handle(stored.model, models::kMlxFormat, stored.id)] = {
            .group = "MLX", .description = "an MLX directory"};
    }
    for (const models::StoredPipeline& stored : models::list_store_pipelines(roots)) {
        list.notes.try_emplace(stored.model, CandidateNote{.group = "Pipelines"});
        list.notes[models::weights_handle(stored.model, models::kPipelineFormat, stored.id)] = {
            .group = "Pipelines", .description = "a diffusion pipeline"};
    }
}

/// Models in the store, and each set of their weights by handle
/// (`<model>/<format>/<id>`), for the formats asked -- by format, then name
/// (45a), the formats in `models list`'s order: SafeTensors, GGUF, MLX,
/// pipelines (49a).
[[nodiscard]] std::vector<std::string> stored_models(const models::StoreRoots& roots, bool ggufs,
                                                     bool snapshots, bool mlx = false,
                                                     bool pipelines = false) {
    std::vector<std::string> out;
    if (snapshots) {
        std::vector<std::pair<std::string, std::string>> found;
        for (const models::StoredSnapshot& stored : models::list_store_snapshots(roots)) {
            found.emplace_back(
                stored.model,
                stored.model + "/" + std::string{models::kSafetensorsFormat} + "/" + stored.id);
        }
        push_format(out, std::move(found));
    }
    if (ggufs) {
        std::vector<std::pair<std::string, std::string>> found;
        for (const models::StoredGguf& stored : models::list_store_ggufs(roots)) {
            found.emplace_back(stored.model, stored.model + "/" + std::string{models::kGgufFormat} +
                                                 "/" + stored.id);
        }
        push_format(out, std::move(found));
    }
    if (mlx) {
        std::vector<std::pair<std::string, std::string>> found;
        for (const models::StoredMlx& stored : models::list_store_mlx(roots)) {
            found.emplace_back(stored.model,
                               models::weights_handle(stored.model, models::kMlxFormat, stored.id));
        }
        push_format(out, std::move(found));
    }
    if (pipelines) {
        std::vector<std::pair<std::string, std::string>> found;
        for (const models::StoredPipeline& stored : models::list_store_pipelines(roots)) {
            found.emplace_back(stored.model, models::weights_handle(
                                                 stored.model, models::kPipelineFormat, stored.id));
        }
        push_format(out, std::move(found));
    }
    return out;
}

/// The stored GGUFs no backend points at, by the name `models list` notes
/// under each -- its file's stem; one a backend already has is no new name.
[[nodiscard]] std::vector<std::string> unregistered_gguf_names(const harness::Config& config) {
    std::vector<models::StoredGguf> pointed_at;
    const std::vector<models::StoredGguf> ggufs = models::list_store_ggufs(store_roots(config));
    for (const auto& [name, backend] : config.backends) {
        if (!backend.model_path.empty()) {
            if (const std::optional<models::StoredGguf> stored = models::stored_gguf_at(
                    ggufs, harness::expand_env_and_home(backend.model_path))) {
                pointed_at.push_back(*stored);
            }
        }
    }
    const std::vector<std::string> taken = config.backend_names();
    std::vector<std::string> out;
    for (const models::StoredGguf& stored : ggufs) {
        const std::string stem = stored.file.stem().string();
        const bool registered = std::ranges::any_of(
            pointed_at,
            [&stored](const models::StoredGguf& other) { return other.dir == stored.dir; });
        if (!registered && std::ranges::find(taken, stem) == taken.end()) {
            out.push_back(stem);
        }
    }
    // The stored MLX models the same way, by the name add-backend fills (27b).
    const std::vector<models::StoredMlx> mlx = models::list_store_mlx(store_roots(config));
    for (const models::StoredMlx& stored : mlx) {
        const bool registered = std::ranges::any_of(config.backends, [&stored](const auto& entry) {
            return !entry.second.model_path.empty() &&
                   models::stored_mlx_at({stored},
                                         harness::expand_env_and_home(entry.second.model_path))
                       .has_value();
        });
        const std::string name = models::stored_mlx_name(stored);
        if (!registered && std::ranges::find(taken, name) == taken.end()) {
            out.push_back(name);
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
    } else if (kind == kModelSuiteValue) {
        list.names = config.suite_names();
        list.none = "no suites -- 'apogee config add-suite' makes one";
    } else if (kind == kModelSuiteOrOffValue) {
        list.names = config.suite_names();
        list.names.emplace_back(harness::kSuiteOff);
    } else if (kind == kNamedGraphValue) {
        list.names = config.graph_names();
        list.none = "no named graphs -- 'apogee config add-graph' makes one";
    } else if (kind == kSourcedGraphValue) {
        for (const auto& [name, graph] : config.graphs) {
            if (graph_updatable(graph)) {
                list.names.push_back(name);
            }
        }
        list.none =
            "no named graph has source trees -- 'apogee graph build --source <dir> --graph "
            "<name>' adds one";
    } else if (kind == kAgentValue) {
        for (const harness::NamedAgent& agent : harness::all_agents(config)) {
            list.names.push_back(agent.name);
        }
    } else if (kind == kChatValue) {
        for (const logger::Session& session : logger::list_sessions()) {
            // What `chats list` shows of it, for a menu (45b).
            const std::string said = session.updated_at.substr(0, 10) + " · " +
                                     std::to_string(session.turns) +
                                     (session.turns == 1 ? " turn" : " turns");
            list.names.push_back(session.chat_id);
            list.notes[session.chat_id] = {.group = {},
                                           .description = session.display_name() + " · " + said};
            // A name with a space cannot be one word on a command line.
            if (!session.custom_name.empty() && !has_space(session.custom_name)) {
                list.names.push_back(session.custom_name);
                list.notes[session.custom_name] = {.group = {},
                                                   .description = session.chat_id + " · " + said};
            }
        }
        list.none = "no saved conversations yet";
        list.as_listed = true;  // newest first (45a), never alphabetical
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
    } else if (kind == kTaskValue) {
        for (const tasks::Task& task : tasks::list_tasks(harness::tasks_dir())) {
            list.names.push_back(task.id);
        }
        list.none = "no tasks yet -- 'apogee task run'";
    } else if (kind == kHaltableTaskValue || kind == kCancellableTaskValue) {
        // The ledgers `task list` reads -- the read the TASK kind makes --
        // each held to the verb's own test.
        const tasks::Request request =
            kind == kHaltableTaskValue ? tasks::Request::Halt : tasks::Request::Cancel;
        for (const tasks::Task& task : tasks::list_tasks(harness::tasks_dir())) {
            if (task_stoppable(task, request)) {
                list.names.push_back(task.id);
            }
        }
        list.none = "no task 'task " + std::string{tasks::to_string(request)} +
                    "' takes -- 'apogee task list' shows where each stands";
    } else if (kind == kResumableTaskValue) {
        // The same ledgers, held to resume's own test from the folder TAB
        // runs in -- the one `task resume` compares against.
        std::error_code code;
        const std::filesystem::path here = std::filesystem::current_path(code);
        if (!code) {
            for (const tasks::Task& task : tasks::list_tasks(harness::tasks_dir())) {
                if (task_resume_refusal(task, here).empty()) {
                    list.names.push_back(task.id);
                }
            }
        }
        list.none =
            "no task resumes here -- one that is not finished, in the folder it was started in";
    } else if (kind == kSymphonyValue || kind == kSymphonyNameValue) {
        for (const symphony::Definition& definition :
             symphony::catalog(config, harness::symphonies_dir()).definitions) {
            list.names.push_back(definition.spec.name);
        }
        // `edit` takes a name alone: a spec file is edited in place.
        list.paths = kind == kSymphonyValue;
    } else if (kind == kRecipeValue) {
        for (const tasks::RecipeEntry& entry : tasks::recipe_book(harness::recipes_dir()).recipes) {
            list.names.push_back(entry.recipe.name);
        }
        list.none = "no recipes -- 'apogee check --fix' seeds the shipped ones";
    } else if (kind == kReviewSkillValue) {
        for (const harness::ReviewSkillEntry& entry :
             harness::review_skill_book(harness::review_skills_dir()).skills) {
            list.names.push_back(entry.skill.name);
        }
        list.none = "no review skills -- 'apogee check --fix' seeds the shipped ones";
    } else if (kind == kReviewTargetValue) {
        for (const auto& [name, target] : config.review_targets) {
            list.names.push_back(name);
        }
        list.none = "no review targets -- 'apogee review targets add' adds one";
    } else if (kind == kSymphonyEntryValue) {
        for (const auto& [name, spec] : config.symphonies) {
            list.names.push_back(name);
        }
        list.none =
            "no symphonies in the config -- a starter or a spec file has no entry to delete";
    } else if (kind == kPipelineValue) {
        for (const auto& [name, spec] : config.training.pipelines) {
            list.names.push_back(name);
        }
        list.paths = true;
    } else if (kind == kRegimeValue) {
        // Every regime a run can name (42a): the config's entries, the specs
        // under training/regimes/, the shipped ones.
        for (const training::RegimeEntry& entry :
             training::regime_book(config, harness::training_regimes_dir()).regimes) {
            list.names.push_back(entry.spec.name);
        }
        list.paths = true;
    } else if (kind == kModelValue) {
        list.names = stored_models(store_roots(config), true, true, true, true);
        list.none = "the model store is empty -- 'apogee models pull'";
        list.as_listed = true;  // by format, then name (45a)
        note_store(store_roots(config), list);
    } else if (kind == kModelNameValue) {
        // A model by the name its backends share (the user's request), as the
        // BACKEND column shows them without their level.
        list.names = model_names(config, store_roots_of(config));
        list.none = "no models -- 'apogee models pull' stores one";
    } else if (kind == kSnapshotValue) {
        list.names = stored_models(store_roots(config), false, true);
        list.paths = true;
        list.as_listed = true;
        note_store(store_roots(config), list);
    } else if (kind == kGgufValue) {
        list.names = stored_models(store_roots(config), true, false);
        list.paths = true;
        list.as_listed = true;
        note_store(store_roots(config), list);
    } else if (kind == kBackendOrWeightsValue) {
        // The configured first, then the store's weights by format (45a).
        list.names = config.backend_names();
        order_names(list.names);
        for (const std::string& name : stored_models(store_roots(config), true, true, true, true)) {
            if (name.find('/') != std::string::npos) {
                list.names.push_back(name);  // a handle; a bare model name is no weights
            }
        }
        list.none = "no backends configured and the model store is empty";
        list.as_listed = true;
    } else if (kind == kModelOrBackendValue) {
        // The configured backends that point into the store first, then the
        // store by format (45a).
        const models::StoreRoots roots = store_roots(config);
        const std::vector<models::StoredGguf> ggufs = models::list_store_ggufs(roots);
        const std::vector<models::StoredMlx> mlx = models::list_store_mlx(roots);
        for (const auto& [name, backend] : config.backends) {
            if (backend.model_path.empty()) {
                continue;
            }
            const std::string path = harness::expand_env_and_home(backend.model_path);
            if (models::stored_gguf_at(ggufs, path).has_value() ||
                models::stored_mlx_at(mlx, path).has_value()) {
                list.names.push_back(name);
            }
        }
        order_names(list.names);
        for (std::string& name : stored_models(roots, true, true, true, true)) {
            list.names.push_back(std::move(name));
        }
        list.none = "the model store is empty -- 'apogee models pull'";
        list.as_listed = true;
    } else if (kind == kVideoPipelineValue) {
        // The store's pipelines, by model then handle (49c).
        list.names = stored_models(store_roots(config), false, false, false, true);
        list.none = "no pipeline in the store -- 'apogee models pull <owner>/<repo>' stores one";
        list.as_listed = true;
        note_store(store_roots(config), list);
    } else if (kind == kNewBackendValue) {
        list.names = unregistered_gguf_names(config);
    } else if (kind == kSnapshotIdValue) {
        list = weight_ids(context, false);
    } else if (kind == kGgufIdValue) {
        list = weight_ids(context, true);
    } else if (kind == kConvertPrecisionValue) {
        // `--mlx` after `--type` cannot be seen: the line is read up to the
        // cursor.
        if (context.flags.contains("--mlx")) {
            list.names = training::mlx_precision_names();
        } else {
            list.names = training::converter_out_types();
        }
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
    } else if (kind == kAllowedHostValue) {
        list.names = config.tools.allowed_hosts;
        list.none = "no allowed hosts yet -- 'apogee config add-allowed-host'";
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
