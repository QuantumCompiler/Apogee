#include "commands/datasets.h"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <system_error>

#include "agentloop/loop.h"
#include "agentloop/reporter.h"
#include "backends/factory.h"
#include "backends/http_client.h"
#include "commands/helpers.h"
#include "commands/train.h"
#include "harness/assets.h"
#include "harness/errors.h"
#include "harness/layout.h"
#include "harness/paths.h"
#include "logger/session.h"
#include "models/acquire.h"
#include "models/source_hf.h"
#include "platform/platform.h"
#include "training/python_env.h"
#include "training/script_runner.h"

namespace apogee::commands {
namespace {

[[noreturn]] void fail_user(const std::string& message) {
    std::cerr << "apogee datasets: " << message << "\n";
    throw CLI::RuntimeError(kUserError);
}

[[nodiscard]] harness::Config load_config_strict(const RootContext& context,
                                                 std::filesystem::path& config_path) {
    try {
        config_path = harness::resolve_config_path(context.config_path);
        return harness::load_config(config_path);
    } catch (const harness::ConfigError& e) {
        fail_user(e.what());
    }
}

[[nodiscard]] harness::Config load_config_or_default(const RootContext& context) {
    try {
        const std::filesystem::path path = harness::resolve_config_path(context.config_path);
        std::error_code code;
        if (!std::filesystem::exists(path, code)) {
            return harness::Config{};
        }
        return harness::load_config(path);
    } catch (const harness::ConfigError& e) {
        fail_user(e.what());
    }
}

std::string human_size(std::int64_t bytes) {
    if (bytes < 1024) {
        return std::to_string(bytes) + " B";
    }
    if (bytes < 1024LL * 1024) {
        return std::to_string(bytes / 1024) + " KiB";
    }
    if (bytes < 1024LL * 1024 * 1024) {
        return std::to_string(bytes / (1024LL * 1024)) + " MiB";
    }
    return std::to_string(bytes / (1024LL * 1024 * 1024)) + " GiB";
}

/// Every configured provider, built so the teacher is a real object.
struct Providers {
    harness::Harness harness;

    Providers(const harness::Config& config, const std::filesystem::path& config_path)
        : harness{config} {
        backends::BuildOptions options;
        options.config_path = config_path;
        (void)backends::build_providers(harness, options);
    }
};

bool confirmed(bool yes, std::string_view what) {
    if (yes) {
        return true;
    }
    std::cout << "\nre-run with --yes to " << what << ".\n";
    return false;
}

std::string pad(std::string value, std::size_t width) {
    if (value.size() < width) {
        value.append(width - value.size(), ' ');
    }
    return value;
}

}  // namespace

// --- the shared cores --------------------------------------------------------

struct LoadedSessions::Owned {
    std::vector<logger::Session> sessions;
};

LoadedSessions load_session_views() {
    LoadedSessions loaded;
    loaded.keep = std::make_shared<LoadedSessions::Owned>();
    loaded.keep->sessions = logger::list_sessions();
    loaded.views.reserve(loaded.keep->sessions.size());
    for (const logger::Session& session : loaded.keep->sessions) {
        loaded.views.push_back(training::SessionView{.backend = session.backend,
                                                     .started_at = session.started_at,
                                                     .messages = &session.messages});
    }
    return loaded;
}

DatasetCreateResult create_dataset(const training::DatasetStore& store,
                                   const DatasetCreateRequest& request) {
    DatasetCreateResult result;
    std::vector<std::string> lines;
    if (!request.lines.empty()) {
        lines = request.lines;
    } else {
        switch (request.source) {
            case training::CreateSource::Template:
                lines = training::template_lines();
                break;
            case training::CreateSource::Empty:
                break;
            case training::CreateSource::Sessions: {
                if (const std::string error = training::validate_session_filter(request.filter);
                    !error.empty()) {
                    throw std::runtime_error(error);
                }
                const LoadedSessions loaded = load_session_views();
                const training::MinedSessions mined =
                    training::mine_sessions(loaded.views, request.filter);
                lines = mined.lines;
                result.sessions = mined.sessions;
                result.skipped = mined.skipped;
                break;
            }
        }
    }
    if (const std::string error = store.write(request.name, lines, request.force, &result.path);
        !error.empty()) {
        throw std::runtime_error(error);
    }
    result.examples = static_cast<int>(lines.size());
    return result;
}

TeacherResolution resolve_teacher(const harness::Config& config, std::string_view name) {
    TeacherResolution resolution;
    if (name.empty()) {
        resolution.error = "a teacher is required: pass --teacher <backend>";
        return resolution;
    }
    const std::string key = configured_backend_key(config, name);
    if (key.empty()) {
        resolution.error = "teacher '" + std::string{name} + "' is not a configured backend";
        return resolution;
    }
    const harness::BackendConfig* backend = config.find_backend(key);
    if (backend == nullptr) {
        resolution.error = "teacher '" + std::string{name} + "' is not a configured backend";
        return resolution;
    }
    if (harness::is_vendor_cli(backend->type)) {
        resolution.error =
            "'" + key +
            "' is a vendor-CLI backend -- the teacher runs inside Apogee's own loop as a direct "
            "API or local call; pick an API-billing or local backend";
        return resolution;
    }
    resolution.key = key;
    resolution.parallel_safe = backend->type == harness::BackendType::Anthropic ||
                               backend->type == harness::BackendType::OpenAI ||
                               backend->type == harness::BackendType::Google;
    return resolution;
}

training::GenerateFn make_teacher(const harness::Harness& harness, std::string key) {
    return [&harness, key = std::move(key)](std::string_view system, std::string_view user,
                                            double temperature, int max_tokens,
                                            const harness::CancellationToken& cancellation) {
        training::GenerateOutcome outcome;
        std::vector<harness::ChatMessage> history{harness::ChatMessage::system(std::string{system}),
                                                  harness::ChatMessage::user(std::string{user})};
        agentloop::Options options;
        options.model = key;
        options.temperature = temperature;
        options.max_tokens = max_tokens;
        options.stream_answer = false;
        // Not a turn of anyone's conversation: a local backend runs it on its
        // own context and a session's cache is never touched.
        options.side_request = true;
        options.cancellation = cancellation;
        agentloop::NullReporter reporter;
        try {
            outcome.text = agentloop::run(harness, history, options, reporter).answer;
            outcome.ok = true;
        } catch (const harness::CancelledError& e) {
            outcome.error = e.what();
            outcome.retryable = false;
        } catch (const harness::ProviderError& e) {
            // The transport already retried a 429 with its retry-after; a
            // failure that survived that is still worth one more round of
            // longer waits before the batch is given up.
            outcome.error = e.what();
            outcome.retryable = true;
        } catch (const harness::HarnessError& e) {
            outcome.error = e.what();
            outcome.retryable = false;
        }
        return outcome;
    };
}

int effective_parallel(const TeacherResolution& teacher, int requested) noexcept {
    if (requested < 1) {
        requested = 1;
    }
    return teacher.parallel_safe ? std::min(requested, 16) : 1;
}

nlohmann::json dataset_json(const training::DatasetInfo& info) {
    return nlohmann::json{{"name", info.name},
                          {"path", info.path.string()},
                          {"lines", info.lines},
                          {"bytes", info.bytes},
                          {"shape", info.shape}};
}

nlohmann::json kit_json(const training::KitSummary& kit) {
    nlohmann::json out{{"name", kit.name},
                       {"path", kit.path.string()},
                       {"description", kit.description},
                       {"eval_items", kit.eval_items}};
    if (!kit.error.empty()) {
        out["error"] = kit.error;
    }
    return out;
}

// --- the command -----------------------------------------------------------------

std::string_view DatasetsCommand::name() const noexcept {
    return "datasets";
}

std::string_view DatasetsCommand::summary() const noexcept {
    return "Create, prepare, and distil training datasets";
}

void DatasetsCommand::bind(CLI::App& root, const RootContext& context) {
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->require_subcommand(1);

    // ---- prepare -----------------------------------------------------------
    auto prep_source = std::make_shared<std::string>();
    auto prep_format = std::make_shared<std::string>();
    auto prep_map = std::make_shared<std::string>();
    auto prep_split = std::make_shared<std::string>("train");
    auto prep_eval = std::make_shared<bool>(false);
    auto prep_flat = std::make_shared<bool>(false);
    auto prep_out = std::make_shared<std::string>();
    auto prep_force = std::make_shared<bool>(false);
    CLI::App* prepare = cmd->add_subcommand(
        "prepare", "Convert a local JSONL/JSON/CSV/Parquet file into a trainer-ready dataset");
    prepare->add_option("path", *prep_source, "A data file, or a directory of them")->required();
    prepare->add_option("--format", *prep_format,
                        "alpaca, sharegpt, chatml, oasst, or prompt-completion (auto-detected)");
    prepare->add_option("--map", *prep_map, "Column renames: dest=src[,dest2=src2]");
    prepare->add_option("--split", *prep_split, "The split to read (default train)");
    prepare->add_flag("--as-eval", *prep_eval, "Emit an eval suite {prompt, expected}");
    prepare->add_flag("--flat", *prep_flat, "Emit {prompt, completion} instead of chat messages");
    prepare->add_option("--out", *prep_out, "Output path (default training/datasets/<stem>.jsonl)");
    prepare->add_flag("-f,--force", *prep_force, "Overwrite an existing output");
    prepare->callback([&context, prep_source, prep_format, prep_map, prep_split, prep_eval,
                       prep_flat, prep_out, prep_force]() {
        const harness::Config config = load_config_or_default(context);
        const std::filesystem::path source{*prep_source};
        std::error_code code;
        if (!std::filesystem::exists(source, code)) {
            fail_user("no such file or directory: " + source.string());
        }
        const std::filesystem::path script = harness::training_scripts_dir() / "prepare_dataset.py";
        if (!std::filesystem::is_regular_file(script, code)) {
            fail_user("the shipped script is missing: " + script.string() +
                      " -- run 'apogee check --fix' to seed it");
        }
        const training::PythonEnv env = require_python_env(config, "datasets prepare");
        bool parquet = source.extension() == ".parquet";
        if (std::filesystem::is_directory(source, code)) {
            for (const auto& entry : std::filesystem::directory_iterator(source, code)) {
                if (entry.path().extension() == ".parquet") {
                    parquet = true;
                }
            }
        }
        if (parquet && !env.status().has(training::RequirementSet::Prepare)) {
            fail_user(
                "Parquet needs the 'prepare' requirement set in the Python environment -- "
                "run 'apogee train setup --with prepare'");
        }

        std::filesystem::path out;
        if (!prep_out->empty()) {
            out = *prep_out;
        } else {
            std::string stem = source.stem().string();
            if (stem.empty()) {
                stem = "dataset";
            }
            if (!training::valid_dataset_name(stem)) {
                fail_user("'" + stem + "' is not a dataset name; pass --out");
            }
            out =
                harness::training_datasets_dir() / (stem + (*prep_eval ? ".eval.jsonl" : ".jsonl"));
        }
        if (!*prep_force && std::filesystem::exists(out, code)) {
            fail_user("output already exists: " + out.string() + " (pass --force to overwrite it)");
        }
        std::filesystem::create_directories(out.parent_path(), code);

        training::ScriptRequest request;
        request.interpreter = env.interpreter();
        request.script = script;
        request.arguments = {"--source",   source.string(), "--out",
                             out.string(), "--split",       *prep_split};
        if (!prep_format->empty()) {
            request.arguments.insert(request.arguments.end(), {"--format", *prep_format});
        }
        if (!prep_map->empty()) {
            request.arguments.insert(request.arguments.end(), {"--map", *prep_map});
        }
        if (*prep_eval) {
            request.arguments.emplace_back("--as-eval");
        }
        if (*prep_flat) {
            request.arguments.emplace_back("--flat");
        }

        std::optional<nlohmann::json> record;
        const training::ScriptOutcome outcome = training::run_script(
            request,
            [&record](const training::ScriptEvent& event) {
                switch (event.kind) {
                    case training::ScriptEvent::Kind::Message:
                        std::cerr << "[prepare] " << event.text << "\n";
                        break;
                    case training::ScriptEvent::Kind::Error:
                        break;
                    case training::ScriptEvent::Kind::Record:
                        record = event.record;
                        break;
                }
            },
            harness::CancellationToken{});
        if (!outcome.ok) {
            fail_user(outcome.describe());
        }
        const int written = record.has_value() ? record->value("rows_written", 0) : 0;
        const int skipped = record.has_value() ? record->value("rows_skipped", 0) : 0;
        std::cout << "Done: " << written << " row(s) written";
        if (skipped > 0) {
            std::cout << ", " << skipped << " skipped";
        }
        std::cout << "\n" << out.string() << "\n";
        if (!*prep_eval) {
            std::cout << "\nTrain on it with:  apogee train run <snapshot> --dataset "
                      << out.stem().string() << "\n";
        }
    });

    // ---- create ------------------------------------------------------------
    auto create_name = std::make_shared<std::string>();
    auto create_from = std::make_shared<std::string>("template");
    auto create_backend = std::make_shared<std::string>();
    auto create_since = std::make_shared<std::string>();
    auto create_until = std::make_shared<std::string>();
    auto create_force = std::make_shared<bool>(false);
    CLI::App* create = cmd->add_subcommand("create", "Scaffold a dataset");
    create->add_option("name", *create_name, "Dataset name")->required();
    create->add_option("--from", *create_from,
                       "template (two example lines), sessions (your chats), or empty");
    create->add_option("--backend", *create_backend, "sessions: only chats on this backend");
    create->add_option("--since", *create_since, "sessions: on or after YYYY-MM-DD");
    create->add_option("--until", *create_until, "sessions: on or before YYYY-MM-DD");
    create->add_flag("-f,--force", *create_force, "Overwrite an existing dataset");
    create->callback([create_name, create_from, create_backend, create_since, create_until,
                      create_force]() {
        const std::optional<training::CreateSource> source =
            training::create_source_from_string(*create_from);
        if (!source.has_value()) {
            fail_user("--from must be template, sessions or empty (got '" + *create_from + "')");
        }
        DatasetCreateRequest request;
        request.name = *create_name;
        request.source = *source;
        request.filter = {*create_backend, *create_since, *create_until};
        request.force = *create_force;
        const training::DatasetStore store{harness::training_datasets_dir()};
        DatasetCreateResult result;
        try {
            result = create_dataset(store, request);
        } catch (const std::runtime_error& e) {
            fail_user(e.what());
        }
        std::cout << result.path.string() << "\n" << result.examples << " example(s)";
        if (*source == training::CreateSource::Sessions) {
            std::cout << " from " << result.sessions << " session(s)";
            if (result.skipped > 0) {
                std::cout << ", " << result.skipped << " exchange(s) skipped";
            }
        }
        std::cout << "\n";
    });

    // ---- synth -------------------------------------------------------------
    auto synth_name = std::make_shared<std::string>();
    auto synth_teacher = std::make_shared<std::string>();
    auto synth_kit = std::make_shared<std::string>();
    auto synth_count = std::make_shared<int>(0);
    auto synth_topic = std::make_shared<std::string>();
    auto synth_temperature = std::make_shared<double>(0.0);
    auto synth_max_tokens = std::make_shared<int>(4096);
    auto synth_parallel = std::make_shared<int>(4);
    auto synth_force = std::make_shared<bool>(false);
    CLI::App* synth = cmd->add_subcommand(
        "synth", "Distil a dataset from a teacher model for a training kit's skill");
    synth->add_option("name", *synth_name, "Dataset name")->required();
    synth->add_option("--teacher", *synth_teacher, "The backend that generates the examples")
        ->required();
    synth->add_option("--kit", *synth_kit, "A training kit name or path ('datasets kits')")
        ->required();
    synth->add_option("--count", *synth_count, "Examples to generate (default: the kit's)");
    synth->add_option("--topic", *synth_topic, "Extra focus appended to every batch");
    synth->add_option("--temperature", *synth_temperature,
                      "Teacher sampling temperature (default: the kit's)");
    synth->add_option("--max-tokens", *synth_max_tokens, "Teacher per-call token budget");
    synth->add_option("--parallel", *synth_parallel,
                      "Batches in flight for an API teacher (default 4; a local teacher runs one)");
    synth->add_flag("-f,--force", *synth_force, "Overwrite an existing dataset");
    synth->callback([&context, synth_name, synth_teacher, synth_kit, synth_count, synth_topic,
                     synth_temperature, synth_max_tokens, synth_parallel, synth_force]() {
        std::filesystem::path config_path;
        const harness::Config config = load_config_strict(context, config_path);
        const training::DatasetStore store{harness::training_datasets_dir()};
        if (!*synth_force && store.exists(*synth_name)) {
            fail_user("dataset already exists: " + store.path_for(*synth_name).string() +
                      " (pass --force to overwrite it)");
        }
        const std::optional<std::filesystem::path> kit_path =
            training::find_kit(harness::training_kits_dir(), *synth_kit);
        if (!kit_path.has_value()) {
            fail_user("no kit named '" + *synth_kit + "' -- 'apogee datasets kits' lists them");
        }
        training::Kit kit;
        try {
            kit = training::load_kit(*kit_path);
        } catch (const std::exception& e) {
            fail_user(e.what());
        }
        if (const std::string error = training::validate_kit(kit); !error.empty()) {
            fail_user("kit '" + kit.name + "': " + error);
        }
        const TeacherResolution teacher = resolve_teacher(config, *synth_teacher);
        if (teacher.key.empty()) {
            fail_user(teacher.error);
        }
        const Providers providers{config, config_path};
        const std::vector<std::string> built = providers.harness.provider_names();
        if (std::find(built.begin(), built.end(), teacher.key) == built.end()) {
            fail_user("teacher '" + teacher.key +
                      "' could not be built -- 'apogee check' says why");
        }

        training::SynthOptions options;
        options.count = *synth_count;
        options.topic = *synth_topic;
        options.temperature = *synth_temperature;
        options.max_tokens = *synth_max_tokens;
        options.parallel = effective_parallel(teacher, *synth_parallel);
        const bool tty = platform::is_terminal(platform::StandardStream::Err);
        options.on_progress = [tty](int produced, int target) {
            if (tty) {
                std::cerr << "\r[synth] " << produced << "/" << target << " examples" << std::flush;
            }
        };
        std::cerr << "[synth] teacher " << teacher.key << ", kit " << kit.name << ", "
                  << options.parallel << " batch(es) in flight\n";
        const training::SynthResult result =
            training::synthesize(kit, make_teacher(providers.harness, teacher.key), options);
        if (tty) {
            std::cerr << "\n";
        }
        if (result.cancelled) {
            throw CLI::RuntimeError(kCancelled);
        }
        if (!result.error.empty()) {
            fail_user(result.error);
        }
        std::filesystem::path path;
        if (const std::string error = store.write(
                *synth_name, training::example_lines(result.examples), *synth_force, &path);
            !error.empty()) {
            fail_user(error);
        }
        std::cout << path.string() << "\n"
                  << result.examples.size() << " example(s) in " << result.calls
                  << " teacher call(s)";
        if (result.failed_batches > 0) {
            std::cout << ", " << result.failed_batches << " batch(es) skipped";
        }
        if (result.retries > 0) {
            std::cout << ", " << result.retries << " retried";
        }
        std::cout << "\n\nTrain on it with:  apogee train run <snapshot> --dataset " << *synth_name
                  << "\n";
    });

    // ---- kits --------------------------------------------------------------
    CLI::App* kits = cmd->add_subcommand("kits", "List the installed training kits");
    kits->callback([]() {
        const std::vector<training::KitSummary> installed =
            training::list_kits(harness::training_kits_dir());
        if (installed.empty()) {
            std::cout << "no kits under " << harness::training_kits_dir().string()
                      << " -- run 'apogee check --fix' to seed the bundled ones\n";
            return;
        }
        std::size_t width = 4;
        for (const training::KitSummary& kit : installed) {
            width = std::max(width, kit.name.size());
        }
        std::cout << pad("NAME", width) << "  EVAL  DESCRIPTION\n";
        for (const training::KitSummary& kit : installed) {
            std::cout << pad(kit.name, width) << "  " << pad(std::to_string(kit.eval_items), 4)
                      << "  " << (kit.error.empty() ? kit.description : "ERROR: " + kit.error)
                      << "\n";
        }
    });

    // ---- list / info / delete ---------------------------------------------
    CLI::App* list = cmd->add_subcommand("list", "List the datasets");
    list->callback([]() {
        const training::DatasetStore store{harness::training_datasets_dir()};
        const std::vector<training::DatasetInfo> datasets = store.list();
        if (datasets.empty()) {
            std::cout << "no datasets under " << store.dir().string() << "\n";
            return;
        }
        std::size_t width = 4;
        for (const training::DatasetInfo& info : datasets) {
            width = std::max(width, info.name.size());
        }
        std::cout << pad("NAME", width) << "  LINES  SHAPE  SIZE\n";
        for (const training::DatasetInfo& info : datasets) {
            std::cout << pad(info.name, width) << "  " << pad(std::to_string(info.lines), 5) << "  "
                      << pad(info.shape, 5) << "  " << human_size(info.bytes) << "\n";
        }
    });

    auto info_name = std::make_shared<std::string>();
    CLI::App* info = cmd->add_subcommand("info", "Show one dataset");
    info->add_option("name", *info_name, "Dataset name")->required();
    info->callback([info_name]() {
        const training::DatasetStore store{harness::training_datasets_dir()};
        const std::optional<training::DatasetInfo> found = store.info(*info_name);
        if (!found.has_value()) {
            fail_user("no dataset named '" + *info_name + "' in " + store.dir().string());
        }
        std::cout << "name:   " << found->name << "\n"
                  << "path:   " << found->path.string() << "\n"
                  << "lines:  " << found->lines << "\n"
                  << "shape:  " << found->shape << "\n"
                  << "size:   " << human_size(found->bytes) << "\n";
    });

    auto del_name = std::make_shared<std::string>();
    auto del_yes = std::make_shared<bool>(false);
    CLI::App* remove = cmd->add_subcommand("delete", "Remove a dataset");
    remove->add_option("name", *del_name, "Dataset name")->required();
    remove->add_flag("-y,--yes", *del_yes, "Do not ask for confirmation");
    remove->callback([del_name, del_yes]() {
        const training::DatasetStore store{harness::training_datasets_dir()};
        if (!store.exists(*del_name)) {
            fail_user("no dataset named '" + *del_name + "' in " + store.dir().string());
        }
        std::cout << "will remove:\n  " << store.path_for(*del_name).string() << "\n";
        if (!confirmed(*del_yes, "delete")) {
            return;
        }
        if (const std::string error = store.remove(*del_name); !error.empty()) {
            fail_user(error);
        }
        std::cout << "\nremoved.\n";
    });

    // ---- pull --------------------------------------------------------------
    auto pull_ref = std::make_shared<std::string>();
    CLI::App* pull = cmd->add_subcommand(
        "pull", "Download a Hugging Face dataset's files for 'datasets prepare'");
    pull->add_option("ref", *pull_ref, "owner/name[@revision][:file]")->required();
    pull->callback([pull_ref]() {
        const std::optional<models::HfRef> parsed = models::parse_hf_ref(*pull_ref);
        if (!parsed.has_value()) {
            fail_user("'" + *pull_ref +
                      "' is not a Hugging Face dataset ref (expected owner/name)");
        }
        backends::HttpClient client{std::make_unique<backends::CurlTransport>()};
        const std::string token = models::hf_token({});
        const harness::CancellationToken cancellation;
        const models::HfTree tree = models::list_repo_tree(
            client, *parsed, models::HfRepoKind::Dataset, token, cancellation);
        if (!tree.ok) {
            fail_user(tree.error);
        }
        std::vector<models::HfFile> wanted;
        std::int64_t total = 0;
        for (const models::HfFile& file : tree.files) {
            if (!parsed->file.empty() ? file.path != parsed->file
                                      : !models::dataset_file_wanted(file.path)) {
                continue;
            }
            total += file.size;
            wanted.push_back(file);
        }
        if (wanted.empty()) {
            fail_user(parsed->file.empty()
                          ? "'" + parsed->repo_id() +
                                "' holds no data files (Parquet, JSONL, JSON, CSV, Arrow, text)"
                          : "no file named '" + parsed->file + "' in '" + parsed->repo_id() + "'");
        }
        const std::filesystem::path destination =
            harness::training_raw_datasets_dir() / models::repo_directory_name(*parsed);
        std::cout << "downloading " << wanted.size() << " file(s), " << human_size(total)
                  << ", into " << destination.string() << "\n";
        std::vector<models::TreeItem> items;
        std::vector<models::SourcePromise> promises;
        promises.reserve(wanted.size());
        for (const models::HfFile& file : wanted) {
            promises.emplace_back();
            models::TreeItem item;
            item.relative = file.path;
            item.source = models::http_source(client, *parsed, models::HfRepoKind::Dataset, file,
                                              token, cancellation, promises.back());
            item.promise = promises.back();
            items.push_back(std::move(item));
        }
        std::size_t last_index = 0;
        const models::AcquireTreeResult result = models::acquire_tree(
            destination, items,
            [&last_index](std::size_t index, std::size_t count, std::string_view relative,
                          std::int64_t, std::int64_t) {
                if (index != last_index) {
                    last_index = index;
                    std::cout << "  [" << index << "/" << count << "] " << relative << "\n";
                }
            });
        if (!result.ok) {
            fail_user(result.error);
        }
        std::cout << "\n"
                  << result.path.string() << "\n"
                  << result.files << " file(s), " << human_size(result.bytes) << "\n"
                  << "\nConvert it with:  apogee datasets prepare " << result.path.string() << "\n";
    });
}

}  // namespace apogee::commands
