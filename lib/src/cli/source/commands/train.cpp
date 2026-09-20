#include "commands/train.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

#include "agentloop/loop.h"
#include "agentloop/reporter.h"
#include "backends/factory.h"
#include "commands/datasets.h"
#include "commands/helpers.h"
#include "commands/models_pull.h"
#include "commands/status_line.h"
#include "commands/terminal.h"
#include "harness/config_edit.h"
#include "harness/errors.h"
#include "harness/layout.h"
#include "harness/paths.h"
#include "models/gguf_inspect.h"
#include "models/quantize.h"
#include "platform/child_process.h"
#include "platform/platform.h"
#include "training/cycle.h"
#include "training/kit.h"
#include "training/manifest.h"
#include "training/mlx_trainer.h"
#include "training/mock_trainer.h"
#include "training/peft_trainer.h"
#include "training/pipeline.h"
#include "training/regime.h"
#include "training/script_runner.h"
#include "training/store.h"

namespace apogee::commands {
namespace {

[[noreturn]] void fail_user(const std::string& message) {
    std::cerr << "apogee train: " << message << "\n";
    throw CLI::RuntimeError(kUserError);
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

[[nodiscard]] harness::Config load_config_strict(const RootContext& context,
                                                 std::filesystem::path& config_path) {
    try {
        config_path = harness::resolve_config_path(context.config_path);
        return harness::load_config(config_path);
    } catch (const harness::ConfigError& e) {
        fail_user(e.what());
    }
}

std::string describe_status(const training::PythonEnvStatus& status) {
    if (!status.exists) {
        return "not created";
    }
    std::string sets;
    for (const std::string& set : status.sets) {
        sets += sets.empty() ? set : ", " + set;
    }
    return "at " + status.interpreter.parent_path().parent_path().string() +
           (sets.empty() ? std::string{"; no requirement sets installed"} : "; sets: " + sets);
}

// --- Ctrl-C ---------------------------------------------------------------------
//
// A run is a child process that can hold a GPU for an hour; Ctrl-C must
// terminate it cleanly and record the run as cancelled, never leave a
// driver running behind a dead parent. The handler only flips the token --
// the read loop notices between chunks and terminates the child.
harness::CancellationToken g_interrupt_token;
std::atomic<int> g_interrupt_count{0};

void on_interrupt(int /*signal*/) {
    g_interrupt_token.cancel();
    if (g_interrupt_count.fetch_add(1) >= 2) {
        // A third Ctrl-C while the child is still winding down: the user
        // means it.
        std::_Exit(kCancelled);
    }
}

class InterruptScope {
public:
    InterruptScope() : previous_{install()} {}

    ~InterruptScope() {
        std::signal(SIGINT, previous_);
    }

    InterruptScope(const InterruptScope&) = delete;
    InterruptScope& operator=(const InterruptScope&) = delete;
    InterruptScope(InterruptScope&&) = delete;
    InterruptScope& operator=(InterruptScope&&) = delete;

    [[nodiscard]] static const harness::CancellationToken& token() noexcept {
        return g_interrupt_token;
    }

private:
    /// Arms the token and the counter, then installs the handler.
    static void (*install())(int) {
        g_interrupt_token = harness::CancellationToken::create();
        g_interrupt_count.store(0);
        return std::signal(SIGINT, on_interrupt);
    }

    void (*previous_)(int) = nullptr;
};

// --- the composition ----------------------------------------------------------------

/// Every configured provider, built so the judge and the teacher are real
/// objects; the build's report kept, so a refusal can say why one is not.
struct Providers {
    harness::Harness harness;
    backends::BuildResult built;

    Providers(const harness::Config& config, const std::filesystem::path& config_path)
        : harness{config} {
        backends::BuildOptions options;
        options.config_path = config_path;
        built = backends::build_providers(harness, options);
    }
};

/// One judge call through the shared loop: a side request, the fixed
/// prompt, the verdict budget, temperature zero.
[[nodiscard]] training::JudgeFn make_judge(const harness::Harness& harness, std::string key) {
    return [&harness, key = std::move(key)](std::string_view prompt, std::string_view candidate,
                                            std::string_view baseline,
                                            const harness::CancellationToken& cancellation) {
        training::JudgeReply reply;
        std::vector<harness::ChatMessage> history{
            harness::ChatMessage::user(training::judge_prompt(prompt, candidate, baseline))};
        agentloop::Options options;
        options.model = key;
        options.temperature = 0.0;
        options.max_tokens = training::kJudgeMaxTokens;
        options.stream_answer = false;
        options.side_request = true;
        options.cancellation = cancellation;
        agentloop::NullReporter reporter;
        try {
            reply.text = agentloop::run(harness, history, options, reporter).answer;
            reply.ok = true;
        } catch (const harness::HarnessError& e) {
            reply.error = e.what();
        }
        return reply;
    };
}

/// The trainer `name` builds: the mock in-process; `mlx` and `peft` need
/// the environment, their requirement set and the seeded driver.
[[nodiscard]] std::unique_ptr<training::Trainer> make_trainer(const harness::Config& config,
                                                              std::string_view name,
                                                              std::string_view purpose) {
    if (name == "mock") {
        return std::make_unique<training::MockTrainer>();
    }
    const training::PythonEnv env = require_python_env(config, purpose);
    const training::RequirementSet set =
        name == "mlx" ? training::RequirementSet::Mlx : training::RequirementSet::Peft;
    if (!env.status().has(set)) {
        fail_user("the '" + std::string{name} +
                  "' requirement set is not installed in the Python environment -- run "
                  "'apogee train setup --trainer " +
                  std::string{name} + "'");
    }
    const std::filesystem::path scripts = harness::training_scripts_dir();
    const std::filesystem::path script =
        scripts / (name == "mlx" ? training::kMlxScriptName : training::kPeftScriptName);
    std::error_code code;
    if (!std::filesystem::is_regular_file(script, code)) {
        fail_user("the shipped driver is missing: " + script.string() +
                  " -- run 'apogee check --fix' to seed it");
    }
    if (name == "mlx") {
        return training::make_mlx_trainer(env.interpreter(), scripts);
    }
    return training::make_peft_trainer(env.interpreter(), scripts);
}

/// The trainer to use: the flag, else the manifest's (for eval and
/// promote, which must match the run), else the config's, else auto.
[[nodiscard]] std::string choose_trainer(const harness::Config& config, const std::string& flag,
                                         const std::string& recorded) {
    std::string requested = flag;
    if (requested.empty()) {
        requested = recorded;
    }
    if (requested.empty()) {
        requested = config.training.trainer;
    }
    const training::TrainerChoice choice =
        training::select_trainer(requested, training::detect_host());
    if (choice.name.empty()) {
        fail_user(choice.error);
    }
    return choice.name;
}

/// The converter the promote step runs: the mock's for a mock run, else the
/// vendored `convert_hf_to_gguf.py` under the environment's interpreter
/// with the `convert` set installed.
[[nodiscard]] training::Converter make_converter(const harness::Config& config,
                                                 std::string_view trainer_name) {
    if (trainer_name == "mock") {
        return [](const std::filesystem::path& fused, const std::filesystem::path& gguf,
                  const training::MessageSink& on_message, const harness::CancellationToken&) {
            if (on_message) {
                on_message("mock converter: writing a minimal GGUF");
            }
            return training::write_mock_gguf(fused, gguf);
        };
    }
    const training::PythonEnv env = require_python_env(config, "train promote");
    if (!env.status().has(training::RequirementSet::Convert)) {
        fail_user(
            "the GGUF converter needs the 'convert' requirement set in the Python environment "
            "-- run 'apogee train setup --with convert'");
    }
    const std::filesystem::path script =
        harness::training_scripts_dir() / "convert" / "convert_hf_to_gguf.py";
    std::error_code code;
    if (!std::filesystem::is_regular_file(script, code)) {
        fail_user("the vendored converter is missing: " + script.string() +
                  " -- run 'apogee check --fix' to seed it");
    }
    return [interpreter = env.interpreter(), script](
               const std::filesystem::path& fused, const std::filesystem::path& gguf,
               const training::MessageSink& on_message,
               const harness::CancellationToken& cancellation) {
        training::ScriptRequest request;
        request.interpreter = interpreter;
        request.script = script;
        request.arguments = {"--outtype", "f16", "--outfile", gguf.string(), fused.string()};
        request.environment.emplace_back("PYTHONDONTWRITEBYTECODE", "1");
        const training::ScriptOutcome outcome = training::run_script(
            request,
            [&on_message](const training::ScriptEvent& event) {
                if (on_message && event.kind != training::ScriptEvent::Kind::Error) {
                    on_message(event.text);
                }
            },
            cancellation);
        if (outcome.ok) {
            return std::string{};
        }
        return outcome.cancelled ? std::string{"cancelled"} : outcome.describe();
    };
}

[[nodiscard]] training::Verifier header_verifier() {
    return [](const std::filesystem::path& gguf) {
        const models::GgufInfo info = models::inspect_gguf(gguf);
        return info.parsed ? std::string{} : info.parse_error;
    };
}

[[nodiscard]] training::Quantizer in_process_quantizer() {
    return [](const std::filesystem::path& input, const std::filesystem::path& output,
              std::string_view type) {
        const models::QuantizeResult result = models::quantize(input, output, type);
        return result.ok ? std::string{} : result.error;
    };
}

[[nodiscard]] training::RunManifest require_manifest(const training::TrainingStore& store,
                                                     const std::string& run_id) {
    if (!training::valid_run_id(run_id)) {
        fail_user("'" + run_id + "' is not a run id");
    }
    std::string error;
    const std::optional<training::RunManifest> manifest =
        training::read_manifest(store.run_dir(run_id), error);
    if (!manifest.has_value()) {
        fail_user("no run '" + run_id + "': " + error + " ('apogee train status' lists the runs)");
    }
    return *manifest;
}

std::string percent(double score) {
    return std::to_string(std::lround(score * 100.0)) + "%";
}

std::string pad(std::string value, std::size_t width) {
    if (value.size() < width) {
        value.append(width - value.size(), ' ');
    }
    return value;
}

std::string eval_glyph(const std::optional<bool>& passed, const std::optional<double>& score) {
    if (!passed.has_value()) {
        return "-";
    }
    return std::string{*passed ? "pass " : "FAIL "} + percent(score.value_or(0.0));
}

/// The judge `name` (the flag, else `training.judge_backend`) resolves to;
/// an empty key means none. A refusal fails the command.
[[nodiscard]] TeacherResolution resolve_judge(const harness::Config& config,
                                              const std::string& flag) {
    const std::string name = flag.empty() ? config.training.judge_backend : flag;
    TeacherResolution judge;
    if (name.empty()) {
        return judge;
    }
    judge = resolve_teacher(config, name);
    if (judge.key.empty()) {
        fail_user("judge: " + judge.error);
    }
    return judge;
}

// --- promotion, shared by promote, the regime and the cycle -------------------------
//
// One body: the gate, the name, the quantize type, the build, the config
// edit only after the GGUF parses, the ledger only after the config. The
// promote command calls it; the regime and the cycle hand it to the cores as
// the promote closure, so "a failing candidate never reaches inference" is
// one rule in one place.

struct PromoteRequest {
    std::string backend;
    bool force = false;
    std::string quantize;
    bool keep_fused = false;
};

struct PromoteChecks {
    std::string error;
    std::string warning;
    /// The configured entry's key when `backend` names one.
    std::string existing_key;
};

/// Every refusal before the expensive part.
[[nodiscard]] PromoteChecks precheck_promotion(const harness::Config& config,
                                               const training::RunManifest& manifest,
                                               const PromoteRequest& request) {
    PromoteChecks checks;
    const training::GateVerdict gate = training::eval_gate(
        manifest, training::gate_mode_from_string(config.training.effective_gate_mode()),
        request.force);
    if (!gate.ok) {
        checks.error = gate.error;
        return checks;
    }
    checks.warning = gate.warning;
    if (request.backend.empty() || request.backend.find_first_of("/\\ \t") != std::string::npos) {
        checks.error = "--as must be a plain backend name (got '" + request.backend + "')";
        return checks;
    }
    checks.existing_key = configured_backend_key(config, request.backend);
    const harness::BackendConfig* existing =
        checks.existing_key.empty() ? nullptr : config.find_backend(checks.existing_key);
    if (existing != nullptr && existing->type != harness::BackendType::LlamaCpp) {
        checks.error = "'" + checks.existing_key + "' is a " +
                       std::string{harness::to_string(existing->type)} +
                       " backend -- promote into an existing llamacpp entry, or a new name";
        return checks;
    }
    if (!request.quantize.empty()) {
        bool known = false;
        std::string names;
        for (const models::QuantType& type : models::quant_types()) {
            known = known || type.name == request.quantize;
            names += (names.empty() ? "" : ", ") + type.name;
        }
        if (!known) {
            checks.error =
                "unknown quantization type '" + request.quantize + "' (want one of: " + names + ")";
            return checks;
        }
        if (!models::quantize_supported()) {
            checks.error =
                "--quantize needs a build with llama.cpp compiled in (-DAPOGEE_ENABLE_LLAMA=ON). "
                "Promote without it and run 'apogee models quantize' on the F16 GGUF";
            return checks;
        }
    }
    return checks;
}

struct PromoteOutcome {
    bool ok = false;
    bool cancelled = false;
    /// A refusal: nothing was attempted.
    bool refused = false;
    std::string error;
    int version = 0;
    std::string gguf_path;
    std::string backend_key;
    /// An existing entry repointed rather than a new one appended.
    bool updated = false;
    training::PruneResult pruned;
};

[[nodiscard]] PromoteOutcome promote_run(
    const harness::Config& config, const std::filesystem::path& config_path,
    const training::TrainingStore& store, const training::RunManifest& manifest,
    training::Trainer& trainer, const training::Converter& convert, const PromoteRequest& request,
    const training::MessageSink& on_message, const harness::CancellationToken& cancellation) {
    PromoteOutcome outcome;
    const PromoteChecks checks = precheck_promotion(config, manifest, request);
    if (!checks.error.empty()) {
        outcome.refused = true;
        outcome.error = checks.error;
        return outcome;
    }
    std::string ledger_error;
    training::VersionLedger ledger =
        training::load_ledger(store.versions_dir(), request.backend, ledger_error)
            .value_or(training::VersionLedger{.backend = request.backend});
    if (!ledger_error.empty()) {
        outcome.refused = true;
        outcome.error = "the version ledger is unreadable: " + ledger_error;
        return outcome;
    }
    ledger.backend = checks.existing_key.empty() ? request.backend : checks.existing_key;
    outcome.backend_key = ledger.backend;
    outcome.updated = !checks.existing_key.empty();
    const training::PromotePlan plan =
        training::plan_promotion(ledger, store.run_dir(manifest.run_id), store.versions_dir(),
                                 ledger.backend, request.quantize, request.keep_fused);
    outcome.version = plan.version;
    outcome.gguf_path = plan.gguf_path.string();
    if (on_message) {
        on_message("run " + manifest.run_id + " -> " + ledger.backend + " v" +
                   std::to_string(plan.version));
    }
    const training::ArtifactsResult built =
        training::build_promotion_artifacts(manifest, plan, trainer, convert, header_verifier(),
                                            in_process_quantizer(), on_message, cancellation);
    if (built.cancelled) {
        outcome.cancelled = true;
        outcome.error = "cancelled";
        return outcome;
    }
    if (!built.ok) {
        outcome.error = built.error + " -- the config and the version ledger are unchanged";
        return outcome;
    }

    // The GGUF exists and parses: now, and only now, the config.
    try {
        if (outcome.updated) {
            harness::edit_config_file(config_path, [&](std::string_view content) {
                return harness::set_backend_model_path(content, ledger.backend,
                                                       plan.gguf_path.string());
            });
        } else {
            harness::BackendConfig entry;
            entry.type = harness::BackendType::LlamaCpp;
            entry.model_path = plan.gguf_path.string();
            harness::edit_config_file(config_path, [&](std::string_view content) {
                return harness::append_backend(content, ledger.backend, entry, false);
            });
        }
    } catch (const harness::ConfigEditError& e) {
        std::error_code code;
        std::filesystem::remove(plan.gguf_path, code);
        outcome.error = std::string{"registering the backend: "} + e.what() +
                        " -- the GGUF was removed and the ledger is unchanged";
        return outcome;
    } catch (const harness::ConfigError& e) {
        std::error_code code;
        std::filesystem::remove(plan.gguf_path, code);
        outcome.error = std::string{"registering the backend: "} + e.what() +
                        " -- the GGUF was removed and the ledger is unchanged";
        return outcome;
    }

    outcome.pruned = training::record_promotion(
        ledger, training::promotion_entry(manifest, plan, training::rfc3339_now()),
        config.training.retain_versions, training::rfc3339_now());
    if (const std::string failure = training::save_ledger(store.versions_dir(), ledger);
        !failure.empty()) {
        outcome.error = "the backend is registered, but the ledger could not be saved: " + failure;
        return outcome;
    }
    outcome.ok = true;
    return outcome;
}

// --- the pipeline console ---------------------------------------------------------

/// Renders a pipeline's progress: a header per stage, the training ticks as
/// the status line (a line every tenth on a pipe), every message on its
/// own line.
class PipelineConsole {
public:
    explicit PipelineConsole(std::size_t total_stages)
        : total_{total_stages},
          tty_{platform::is_terminal(platform::StandardStream::Err)},
          status_{writer_, options_for(tty_)} {}

    void operator()(const training::PipelineProgress& progress) {
        if (progress.stage_index != current_) {
            status_.clear();
            current_ = progress.stage_index;
            last_percent_ = -1;
            status_.print_line("-- stage " + std::to_string(progress.stage_index + 1) + "/" +
                               std::to_string(total_) + ": " + progress.stage_name);
        }
        if (progress.training != nullptr) {
            const training::ProgressEvent& event = *progress.training;
            switch (event.kind) {
                case training::ProgressEvent::Kind::Iteration: {
                    const std::string line = render_iteration(event);
                    if (tty_) {
                        status_.set(line);
                        break;
                    }
                    const int pct =
                        event.total_iters > 0 ? event.iteration * 100 / event.total_iters : 0;
                    if (pct / 10 != last_percent_ / 10 || event.iteration == event.total_iters) {
                        std::cerr << "[train] " << line << "\n";
                    }
                    last_percent_ = pct;
                    break;
                }
                case training::ProgressEvent::Kind::Message:
                    status_.print_line("[train] " + event.text);
                    break;
                case training::ProgressEvent::Kind::Error:
                    status_.print_line("[train] error: " + event.text);
                    break;
            }
            return;
        }
        if (!progress.message.empty()) {
            status_.print_line(progress.message);
        }
    }

    void finish() {
        status_.clear();
    }

private:
    [[nodiscard]] static StatusLine::Options options_for(bool tty) {
        StatusLine::Options options;
        options.active = tty;
        return options;
    }

    std::size_t total_;
    bool tty_;
    TerminalWriter writer_{std::cerr};
    StatusLine status_;
    int current_ = -1;
    int last_percent_ = -1;
};

void print_pipeline_summary(const training::PipelineRunManifest& manifest) {
    std::cout << (manifest.complete() ? "pipeline complete -- "
                                      : "pipeline " + manifest.status + " -- ")
              << manifest.pipeline_run_id << "\n";
    for (const training::PipelineStageRecord& stage : manifest.stages) {
        std::string score = "-";
        if (stage.eval.has_value()) {
            score = percent(stage.cumulative_score) + " (" +
                    std::to_string(stage.eval->num_passed) + "/" +
                    std::to_string(stage.eval->total) + ")";
        }
        std::cout << "  " << pad(std::to_string(stage.index + 1) + ".", 4) << pad(stage.name, 22)
                  << pad(stage.status, 12) << score << "\n";
    }
    const std::string promotable = manifest.promote_run_id();
    if (!promotable.empty() && manifest.complete()) {
        std::cout << "\nPromote the last passing stage:\n  apogee train promote " << promotable
                  << " --as <backend>\n";
    }
}

void print_pipeline_table(const training::PipelineRunManifest& manifest) {
    std::cout << "pipeline " << manifest.pipeline_run_id << "\n"
              << "  status:  " << manifest.status << "\n";
    if (!manifest.spec_name.empty()) {
        std::cout << "  spec:    " << manifest.spec_name << "\n";
    }
    std::cout << "  student: " << manifest.student << "\n"
              << "  base:    " << manifest.base_model << "\n"
              << "  started: " << manifest.started_at << "\n";
    if (!manifest.completed_at.empty()) {
        std::cout << "  completed: " << manifest.completed_at << "\n";
    }
    std::cout << "\n  " << pad("#", 4) << pad("STAGE", 22) << pad("STATUS", 12)
              << pad("CUMULATIVE", 16) << "RUN\n";
    for (const training::PipelineStageRecord& stage : manifest.stages) {
        std::string score = "-";
        if (stage.eval.has_value()) {
            score = percent(stage.cumulative_score) + " (" +
                    std::to_string(stage.eval->num_passed) + "/" +
                    std::to_string(stage.eval->total) + ")";
        }
        std::cout << "  " << pad(std::to_string(stage.index + 1), 4) << pad(stage.name, 22)
                  << pad(stage.status, 12) << pad(score, 16) << stage.run_id << "\n";
    }
    const std::string promotable = manifest.promote_run_id();
    if (!promotable.empty()) {
        std::cout << "\nPromote the last passing stage:\n  apogee train promote " << promotable
                  << " --as <backend>\n";
    }
    if (!manifest.complete() && manifest.first_unpassed() < manifest.stages.size()) {
        std::cout << "Resume from stage " << (manifest.first_unpassed() + 1)
                  << ":\n  apogee train pipeline resume " << manifest.pipeline_run_id << "\n";
    }
}

// --- the spec resolvers -------------------------------------------------------------

/// A pipeline by file path, else a `training.pipelines:` entry.
[[nodiscard]] harness::PipelineSpec resolve_pipeline_spec(const harness::Config& config,
                                                          const std::string& value) {
    std::error_code code;
    const std::filesystem::path path{value};
    if (std::filesystem::is_regular_file(path, code)) {
        std::ifstream in{path, std::ios::binary};
        std::ostringstream text;
        text << in.rdbuf();
        try {
            return harness::parse_pipeline_spec(text.str(), path.string(), path.stem().string());
        } catch (const harness::ConfigError& e) {
            fail_user(e.what());
        }
    }
    if (const auto named = config.training.pipelines.find(value);
        named != config.training.pipelines.end()) {
        return named->second;
    }
    if (value.find('/') != std::string::npos || value.ends_with(".yaml") ||
        value.ends_with(".yml")) {
        fail_user("no pipeline spec file at " + value);
    }
    fail_user("no pipeline '" + value +
              "': not a spec file, and not an entry under training.pipelines");
}

/// A pipeline's inputs: the student snapshot and, per stage, the dataset
/// (unless the caller overrides them) and the suite.
struct PipelineInputs {
    std::filesystem::path base_model;
    std::vector<training::ResolvedStage> stages;
};

[[nodiscard]] PipelineInputs resolve_pipeline_inputs(const harness::Config& config,
                                                     const RootContext& context,
                                                     const harness::PipelineSpec& spec,
                                                     bool datasets_needed) {
    PipelineInputs inputs;
    if (spec.student.empty()) {
        fail_user("pipeline '" + spec.name + "' names no student snapshot");
    }
    const StudentResolution student =
        resolve_student(config, harness::models_dir(),
                        snapshot_root(harness::models_dir(), context.config_path), spec.student);
    if (!student.error.empty()) {
        fail_user("pipeline '" + spec.name + "': " + student.error);
    }
    inputs.base_model = student.path;
    for (const harness::PipelineStageSpec& stage : spec.stages) {
        training::ResolvedStage resolved;
        if (datasets_needed) {
            std::string error;
            resolved.dataset =
                resolve_dataset(harness::training_datasets_dir(), stage.dataset, error);
            if (resolved.dataset.empty()) {
                fail_user("stage '" + stage.name + "': " + error);
            }
        }
        const SuiteResolution suite = resolve_suite(harness::training_dir(), stage.eval_suite);
        if (!suite.error.empty()) {
            fail_user("stage '" + stage.name + "': " + suite.error);
        }
        resolved.suite = suite.items;
        resolved.suite_label = suite.label;
        inputs.stages.push_back(std::move(resolved));
    }
    return inputs;
}

/// A regime from `--regime <file>`, a named entry, a bare positional that
/// is a file, or nothing (ad hoc from flags).
[[nodiscard]] harness::RegimeSpec resolve_regime_spec(const harness::Config& config,
                                                      const std::string& file,
                                                      const std::string& named) {
    auto from_file = [](const std::filesystem::path& path) {
        std::ifstream in{path, std::ios::binary};
        std::ostringstream text;
        text << in.rdbuf();
        try {
            return harness::parse_regime_spec(text.str(), path.string(), path.stem().string());
        } catch (const harness::ConfigError& e) {
            fail_user(e.what());
        }
    };
    std::error_code code;
    if (!file.empty()) {
        if (!std::filesystem::is_regular_file(file, code)) {
            fail_user("no regime spec file at " + file);
        }
        return from_file(file);
    }
    if (!named.empty()) {
        if (const auto entry = config.training.regimes.find(named);
            entry != config.training.regimes.end()) {
            return entry->second;
        }
        if (std::filesystem::is_regular_file(named, code)) {
            return from_file(named);
        }
        fail_user("no regime '" + named +
                  "': not an entry under training.regimes, and not a spec file");
    }
    return harness::RegimeSpec{};
}

void print_cycle_history(const training::CycleHistory& history, bool active) {
    std::cout << "cycle for backend " << (history.backend.empty() ? "-" : history.backend) << "\n"
              << "  state:             "
              << (history.halted ? "HALTED -- " + history.halt_reason
                  : active       ? std::string{"running now"}
                                 : std::string{"idle"})
              << "\n"
              << "  runs:              " << history.total_runs << " (" << history.consecutive_fails
              << " consecutive failure(s))\n"
              << "  anchor:            "
              << (history.anchor_version > 0
                      ? "v" + std::to_string(history.anchor_version) + " (" +
                            percent(history.anchor_score) + ")"
                      : std::string{"not set -- the first passing cycle sets it"})
              << "\n";
    if (!history.sessions_until.empty()) {
        std::cout << "  sessions consumed: through " << history.sessions_until << "\n";
    }
    if (history.runs.empty()) {
        std::cout << "  no cycle runs recorded yet\n";
        return;
    }
    std::cout << "\n  " << pad("RUN AT", 21) << pad("SOURCE", 20) << pad("GATE", 9)
              << pad("SCORE", 7) << pad("PREV", 7) << pad("ANCHOR", 8) << "PROMOTED\n";
    for (const training::CycleRunRecord& run : history.runs) {
        auto score = [](double value) { return value == 0.0 ? std::string{"-"} : percent(value); };
        std::string at = run.run_at;
        if (at.size() > 19) {
            at = at.substr(0, 19);
        }
        std::cout << "  " << pad(at, 21) << pad(run.source.empty() ? "-" : run.source, 20)
                  << pad(run.gate, 9) << pad(score(run.cycle_score), 7)
                  << pad(score(run.prev_score), 7) << pad(score(run.anchor_score), 8)
                  << (run.promoted_version > 0 ? "v" + std::to_string(run.promoted_version) : "-")
                  << "\n";
        if (!run.gate_reason.empty()) {
            std::cout << "      " << run.gate_reason << "\n";
        }
        if (!run.note.empty()) {
            std::cout << "      " << run.note << "\n";
        }
    }
}

}  // namespace

// --- the resolvers ------------------------------------------------------------------

std::string detect_trainer(std::string& reason) {
    reason.clear();
    const training::TrainerChoice choice =
        training::select_trainer("auto", training::detect_host());
    if (choice.name.empty()) {
        reason =
            "not Apple Silicon, and nvidia-smi is not on PATH -- no trainer stack fits this host";
    }
    return choice.name;
}

StudentResolution resolve_student(const harness::Config& config,
                                  const std::filesystem::path& models_dir,
                                  const std::filesystem::path& snapshot_root,
                                  std::string_view name) {
    StudentResolution resolution;
    std::error_code code;
    const std::filesystem::path given{std::string{name}};
    if (std::filesystem::is_directory(given, code)) {
        resolution.error = training::validate_student(given);
        if (resolution.error.empty()) {
            resolution.path = std::filesystem::absolute(given, code);
        }
        return resolution;
    }
    if (const std::string key = configured_backend_key(config, name); !key.empty()) {
        const harness::BackendConfig* backend = config.find_backend(key);
        resolution.error =
            "'" + std::string{name} + "' is a backend entry (" +
            std::string{backend != nullptr ? harness::to_string(backend->type) : "?"} +
            "), not a snapshot: a backend runs a GGUF, and only full-precision SafeTensors "
            "snapshots are trainable. Pull one with 'apogee models pull <owner>/<repo> "
            "--safetensors' and name its directory";
        return resolution;
    }
    if (name.find('/') == std::string_view::npos && name.find('\\') == std::string_view::npos &&
        name.find("..") == std::string_view::npos) {
        for (const std::filesystem::path& root : {snapshot_root, models_dir}) {
            const std::filesystem::path candidate = root / std::string{name};
            if (std::filesystem::is_directory(candidate, code)) {
                resolution.error = training::validate_student(candidate);
                if (resolution.error.empty()) {
                    resolution.path = candidate;
                }
                return resolution;
            }
        }
    }
    resolution.error = training::validate_student(given);
    if (resolution.error.empty()) {
        resolution.path = std::filesystem::absolute(given, code);
    }
    return resolution;
}

std::filesystem::path resolve_dataset(const std::filesystem::path& datasets_dir,
                                      std::string_view name_or_path, std::string& error) {
    error.clear();
    std::error_code code;
    const std::filesystem::path given{std::string{name_or_path}};
    if (std::filesystem::is_regular_file(given, code)) {
        return std::filesystem::absolute(given, code);
    }
    if (training::valid_dataset_name(name_or_path)) {
        const training::DatasetStore store{datasets_dir};
        if (store.exists(name_or_path)) {
            return store.path_for(name_or_path);
        }
    }
    error = "no dataset named '" + std::string{name_or_path} +
            "' and no such file -- 'apogee datasets list' shows the datasets";
    return {};
}

SuiteResolution resolve_suite(const std::filesystem::path& training_dir,
                              std::string_view name_or_path) {
    SuiteResolution resolution;
    std::error_code code;
    auto load = [&resolution](const std::filesystem::path& path) {
        resolution.items = training::load_eval_suite(path, resolution.error);
        resolution.label = path.string();
    };
    const std::filesystem::path given{std::string{name_or_path}};
    if (std::filesystem::is_regular_file(given, code)) {
        load(std::filesystem::absolute(given, code));
        return resolution;
    }
    const std::string name{name_or_path};
    if (training::valid_dataset_name(name)) {
        for (const std::filesystem::path& candidate :
             {training_dir / "suites" / (name + ".jsonl"),
              training_dir / "datasets" / (name + ".eval.jsonl")}) {
            if (std::filesystem::is_regular_file(candidate, code)) {
                load(candidate);
                return resolution;
            }
        }
        if (const std::optional<std::filesystem::path> kit_path =
                training::find_kit(training_dir / "kits", name);
            kit_path.has_value()) {
            try {
                const training::Kit kit = training::load_kit(*kit_path);
                if (const std::string problem = training::validate_kit(kit); !problem.empty()) {
                    resolution.error = problem;
                    return resolution;
                }
                resolution.items = kit.eval;
                resolution.label = "kit:" + kit.name;
            } catch (const std::exception& e) {
                resolution.error = e.what();
            }
            return resolution;
        }
    }
    resolution.error = "no eval suite '" + name +
                       "': not a file, not under training/suites/, not a prepared "
                       "<name>.eval.jsonl, not a kit";
    return resolution;
}

std::string render_iteration(const training::ProgressEvent& event) {
    std::ostringstream out;
    out << "iter " << event.iteration;
    if (event.total_iters > 0) {
        out << "/" << event.total_iters;
    }
    out << " · loss " << std::fixed << std::setprecision(4) << event.loss;
    out << " · lr " << std::scientific << std::setprecision(2) << event.lr;
    out << " · " << std::fixed << std::setprecision(1) << event.throughput << " it/s";
    return out.str();
}

// --- setup (the datasets item) ------------------------------------------------------

training::PythonEnv require_python_env(const harness::Config& config, std::string_view purpose) {
    training::PythonEnv env{harness::training_venv_dir()};
    if (env.exists()) {
        return env;
    }
    const bool interactive =
        platform::is_terminal(platform::StandardStream::In) && !stdin_is_piped();
    if (!interactive) {
        std::cerr << "apogee " << purpose
                  << ": the Python environment has not been created yet. Run 'apogee train "
                     "setup' first (it creates "
                  << env.dir().string() << ", never touching the system Python)\n";
        throw CLI::RuntimeError(kUserError);
    }
    std::cerr << purpose << " needs a Python environment, which Apogee keeps under\n  "
              << env.dir().string() << "\n(never the system Python). Create it now? [y/N] "
              << std::flush;
    std::string answer;
    std::getline(std::cin, answer);
    if (answer != "y" && answer != "Y" && answer != "yes") {
        fail_user("not created. Run 'apogee train setup' when ready");
    }
    std::string error;
    const std::filesystem::path base = training::locate_base_python(config.training.python, error);
    if (base.empty()) {
        fail_user(error);
    }
    std::cerr << "creating the Python environment from " << base.string() << " ...\n";
    if (const std::string failure = env.create(base); !failure.empty()) {
        fail_user(failure);
    }
    return env;
}

void run_train_setup(const harness::Config& config, const SetupRequest& request) {
    training::PythonEnv env{harness::training_venv_dir()};
    std::vector<training::RequirementSet> sets = request.with;
    if (!request.trainer.empty()) {
        std::string trainer = request.trainer;
        if (trainer == "auto") {
            std::string reason;
            trainer = detect_trainer(reason);
            if (trainer.empty()) {
                std::cout << "no trainer stack selected: " << reason << "\n";
            } else {
                std::cout << "trainer stack for this host: " << trainer << "\n";
            }
        }
        if (trainer == "mlx") {
            sets.push_back(training::RequirementSet::Mlx);
        } else if (trainer == "peft") {
            sets.push_back(training::RequirementSet::Peft);
        } else if (!trainer.empty()) {
            fail_user("--trainer must be auto, mlx or peft (got '" + trainer + "')");
        }
    }

    if (!env.exists()) {
        std::string error;
        const std::filesystem::path base =
            training::locate_base_python(config.training.python, error);
        if (base.empty()) {
            fail_user(error);
        }
        std::cout << "creating " << env.dir().string() << " from " << base.string() << " ...\n";
        if (const std::string failure = env.create(base); !failure.empty()) {
            fail_user(failure);
        }
        std::cout << "created\n";
    } else {
        std::cout << "environment already exists at " << env.dir().string() << "\n";
    }

    for (const training::RequirementSet set : sets) {
        std::cout << "installing the '" << training::to_string(set) << "' set:";
        for (const std::string_view package : training::packages_for(set)) {
            std::cout << " " << package;
        }
        std::cout << " ...\n" << std::flush;
        if (const std::string failure = env.install(set); !failure.empty()) {
            fail_user(failure);
        }
        std::cout << "installed\n";
    }

    std::cout << "\npython env: " << describe_status(env.status()) << "\n";
    if (sets.empty()) {
        std::cout << "\nNothing installed yet. Add a requirement set when a command needs it:\n"
                  << "  apogee train setup --with prepare     # Parquet in 'datasets prepare'\n"
                  << "  apogee train setup --trainer auto     # the trainer stack for this host\n"
                  << "  apogee train setup --with convert     # the GGUF converter, for promote\n";
    }
}

// --- the command -----------------------------------------------------------------

std::string_view TrainCommand::name() const noexcept {
    return "train";
}

std::string_view TrainCommand::summary() const noexcept {
    return "Fine-tune local models: setup, run, eval, promote, rollback, versions, status, "
           "pipeline, regime, cycle";
}

void TrainCommand::bind(CLI::App& root, const RootContext& context) {
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->require_subcommand(1);

    // ---- setup -------------------------------------------------------------
    auto setup_trainer = std::make_shared<std::string>();
    auto setup_with = std::make_shared<std::vector<std::string>>();
    CLI::App* setup = cmd->add_subcommand(
        "setup", "Create the Python environment under training/venv and install requirement sets");
    setup->add_option("--trainer", *setup_trainer,
                      "Install a trainer's stack: auto (detect this host), mlx, or peft");
    setup->add_option("--with", *setup_with,
                      "Install a requirement set: prepare (Parquet), mlx, peft, convert");
    setup->callback([&context, setup_trainer, setup_with]() {
        const harness::Config config = load_config_or_default(context);
        SetupRequest request;
        request.trainer = *setup_trainer;
        for (const std::string& name : *setup_with) {
            const std::optional<training::RequirementSet> set =
                training::requirement_set_from_string(name);
            if (!set.has_value()) {
                std::string names;
                for (const std::string_view known : training::requirement_set_names()) {
                    if (!names.empty()) {
                        names += ", ";
                    }
                    names += known;
                }
                fail_user("unknown requirement set '" + name + "' (want one of: " + names + ")");
            }
            request.with.push_back(*set);
        }
        run_train_setup(config, request);
    });

    // ---- run ---------------------------------------------------------------
    auto run_student = std::make_shared<std::string>();
    auto run_dataset = std::make_shared<std::string>();
    auto run_method = std::make_shared<std::string>("lora");
    auto run_iters = std::make_shared<int>(0);
    auto run_batch = std::make_shared<int>(0);
    auto run_layers = std::make_shared<int>(0);
    auto run_grad = std::make_shared<bool>(false);
    auto run_mask = std::make_shared<bool>(false);
    auto run_trainer = std::make_shared<std::string>();
    CLI::App* run = cmd->add_subcommand(
        "run", "Fine-tune a SafeTensors snapshot on a dataset, recording a run");
    run->add_option("student", *run_student,
                    "A snapshot: a directory, or a name under paths.hf_dir or models/")
        ->required();
    run->add_option("--dataset", *run_dataset, "A dataset name ('datasets list') or a .jsonl path")
        ->required();
    run->add_option("--method", *run_method, "lora (default) or qlora");
    run->add_option("--iters", *run_iters, "Training iterations (default: the driver's)");
    run->add_option("--batch-size", *run_batch, "Batch size per step");
    run->add_option("--num-layers", *run_layers, "Transformer layers LoRA is applied to");
    run->add_flag("--grad-checkpoint", *run_grad, "Gradient checkpointing: slower, less memory");
    run->add_flag("--mask-prompt", *run_mask, "Completion-only loss: the prompt is not trained on");
    run->add_option("--trainer", *run_trainer, "auto (default), mlx, peft, or mock");
    run->callback([&context, run_student, run_dataset, run_method, run_iters, run_batch, run_layers,
                   run_grad, run_mask, run_trainer]() {
        std::filesystem::path config_path;
        const harness::Config config = load_config_strict(context, config_path);
        if (*run_method != "lora" && *run_method != "qlora") {
            fail_user("--method must be lora or qlora (got '" + *run_method + "')");
        }
        const StudentResolution student = resolve_student(
            config, harness::models_dir(),
            snapshot_root(harness::models_dir(), context.config_path), *run_student);
        if (!student.error.empty()) {
            fail_user(student.error);
        }
        std::string error;
        const std::filesystem::path dataset =
            resolve_dataset(harness::training_datasets_dir(), *run_dataset, error);
        if (dataset.empty()) {
            fail_user(error);
        }
        const std::string trainer_name = choose_trainer(config, *run_trainer, {});
        const std::unique_ptr<training::Trainer> trainer =
            make_trainer(config, trainer_name, "train run");
        const training::TrainerCapabilities capabilities = trainer->capabilities();
        if (std::ranges::find(capabilities.methods, *run_method) == capabilities.methods.end()) {
            fail_user("the " + trainer_name + " trainer does not support --method " + *run_method);
        }

        const std::filesystem::path runs_dir = harness::training_runs_dir();
        training::TrainRequest request;
        request.model_dir = student.path;
        request.dataset = dataset;
        request.method = *run_method;
        request.iters = *run_iters;
        request.batch_size = *run_batch;
        request.num_layers = *run_layers;
        request.grad_checkpoint = *run_grad;
        request.mask_prompt = *run_mask;
        request.run_id = training::new_run_id(runs_dir);
        request.output_dir = runs_dir / request.run_id;

        training::RunManifest manifest;
        manifest.run_id = request.run_id;
        manifest.trainer = trainer_name;
        manifest.base_model = student.path.string();
        manifest.dataset = dataset.string();
        manifest.dataset_hash = training::dataset_digest(dataset);
        manifest.method = request.method;
        manifest.iters = request.iters;
        manifest.batch_size = request.batch_size;
        manifest.num_layers = request.num_layers;
        manifest.grad_checkpoint = request.grad_checkpoint;
        manifest.mask_prompt = request.mask_prompt;
        manifest.adapter_dir = (request.output_dir / training::kAdapterDirName).string();
        manifest.status = std::string{training::kStatusRunning};
        manifest.started_at = training::rfc3339_now();
        if (const std::string failure = training::write_manifest(request.output_dir, manifest);
            !failure.empty()) {
            fail_user(failure);
        }

        const bool tty = platform::is_terminal(platform::StandardStream::Err);
        TerminalWriter writer{std::cerr};
        StatusLine::Options status_options;
        status_options.active = tty;
        StatusLine status{writer, status_options};
        status.print_line("[train] run " + request.run_id + ": " + trainer_name + " " +
                          request.method + " on " + student.path.filename().string() +
                          ", dataset " + dataset.filename().string());
        int last_percent = -1;
        std::string last_line;
        const InterruptScope interrupt;
        const training::TrainOutcome outcome = trainer->train(
            request,
            [&](const training::ProgressEvent& event) {
                switch (event.kind) {
                    case training::ProgressEvent::Kind::Iteration: {
                        last_line = render_iteration(event);
                        if (tty) {
                            status.set(last_line);
                            break;
                        }
                        // On a pipe: a line at every tenth, and the last.
                        const int percent =
                            event.total_iters > 0 ? event.iteration * 100 / event.total_iters : 0;
                        if (percent / 10 != last_percent / 10 ||
                            event.iteration == event.total_iters) {
                            std::cerr << "[train] " << last_line << "\n";
                        }
                        last_percent = percent;
                        break;
                    }
                    case training::ProgressEvent::Kind::Message:
                        status.print_line("[train] " + event.text);
                        break;
                    case training::ProgressEvent::Kind::Error:
                        status.print_line("[train] error: " + event.text);
                        break;
                }
            },
            InterruptScope::token());
        status.clear();

        manifest.final_loss = outcome.final_loss;
        manifest.iterations = outcome.iterations;
        manifest.finished_at = training::rfc3339_now();
        if (outcome.ok) {
            manifest.status = std::string{training::kStatusComplete};
        } else if (outcome.cancelled) {
            manifest.status = std::string{training::kStatusCancelled};
        } else {
            manifest.status = std::string{training::kStatusFailed};
        }
        manifest.error = outcome.ok ? std::string{} : outcome.error;
        if (const std::string failure = training::write_manifest(request.output_dir, manifest);
            !failure.empty()) {
            std::cerr << "apogee train: warning: " << failure << "\n";
        }
        if (outcome.cancelled) {
            std::cerr << "[train] cancelled at iteration " << outcome.iterations
                      << "; recorded as cancelled in " << request.output_dir.string() << "\n";
            throw CLI::RuntimeError(kCancelled);
        }
        if (!outcome.ok) {
            std::cerr << "apogee train: run " << request.run_id << " failed: " << outcome.error
                      << "\n  recorded as failed in "
                      << training::manifest_path(request.output_dir).string() << "\n";
            throw CLI::RuntimeError(kBackendError);
        }
        std::cout << "run " << request.run_id << " complete\n"
                  << "  " << (last_line.empty() ? std::string{"no iterations reported"} : last_line)
                  << "\n"
                  << "  adapter:  " << manifest.adapter_dir << "\n"
                  << "  manifest: " << training::manifest_path(request.output_dir).string()
                  << "\n\nGate it, then promote it:\n"
                  << "  apogee train eval " << request.run_id << " --suite <path|name>\n"
                  << "  apogee train promote " << request.run_id << " --as <backend>\n";
    });

    // ---- eval --------------------------------------------------------------
    auto eval_run = std::make_shared<std::string>();
    auto eval_suite = std::make_shared<std::string>();
    auto eval_judge = std::make_shared<std::string>();
    auto eval_force = std::make_shared<bool>(false);
    auto eval_trainer = std::make_shared<std::string>();
    CLI::App* eval = cmd->add_subcommand("eval", "Gate a run: substring and pairwise-judge items");
    eval->add_option("run", *eval_run, "The run id")->required();
    eval->add_option("--suite", *eval_suite,
                     "A .jsonl path, a suite under training/suites, a prepared <name>.eval, or "
                     "a kit (default: training.eval_suite_path)");
    eval->add_option("--judge", *eval_judge,
                     "The backend that judges items without `expected` (default: "
                     "training.judge_backend; none skips them)");
    eval->add_flag("-f,--force", *eval_force, "Re-run when results already exist");
    eval->add_option("--trainer", *eval_trainer, "Override the run's recorded trainer");
    eval->callback([&context, eval_run, eval_suite, eval_judge, eval_force, eval_trainer]() {
        std::filesystem::path config_path;
        const harness::Config config = load_config_strict(context, config_path);
        const training::TrainingStore store{harness::training_dir()};
        training::RunManifest manifest = require_manifest(store, *eval_run);
        if (!manifest.complete()) {
            fail_user("run " + manifest.run_id + " is " + manifest.status +
                      " -- only a complete run can be evaluated");
        }
        if (manifest.eval.has_value() && !*eval_force) {
            std::cout << "eval already ran for " << manifest.run_id << " on "
                      << manifest.eval->run_at << ": "
                      << (manifest.eval->passed ? "passed" : "FAILED") << " ("
                      << percent(manifest.eval->score) << ", " << manifest.eval->num_passed << "/"
                      << manifest.eval->total << ")\n  pass --force to re-run\n";
            return;
        }
        const std::string suite_name =
            eval_suite->empty() ? config.training.eval_suite_path : *eval_suite;
        if (suite_name.empty()) {
            fail_user("no eval suite: pass --suite <path|name> or set training.eval_suite_path");
        }
        const SuiteResolution suite = resolve_suite(harness::training_dir(), suite_name);
        if (!suite.error.empty()) {
            fail_user(suite.error);
        }
        const std::string judge_name =
            eval_judge->empty() ? config.training.judge_backend : *eval_judge;
        TeacherResolution judge;
        if (!judge_name.empty()) {
            judge = resolve_teacher(config, judge_name);
            if (judge.key.empty()) {
                fail_user("judge: " + judge.error);
            }
        }
        const std::string trainer_name = choose_trainer(config, *eval_trainer, manifest.trainer);
        const std::unique_ptr<training::Trainer> trainer =
            make_trainer(config, trainer_name, "train eval");
        std::optional<Providers> providers;
        if (!judge.key.empty()) {
            providers.emplace(config, config_path);
        }

        const std::unique_ptr<training::CandidateRunner> candidate =
            trainer->candidate_runner(manifest.base_model, manifest.adapter_dir);
        std::unique_ptr<training::CandidateRunner> baseline;
        training::EvalOptions options;
        if (!judge.key.empty()) {
            baseline = trainer->candidate_runner(manifest.base_model, {});
            options.baseline = baseline.get();
            options.judge = make_judge(providers->harness, judge.key);
            options.judge_backend = judge.key;
        }
        const bool tty = platform::is_terminal(platform::StandardStream::Err);
        options.on_progress = [tty](int done, int total) {
            if (tty) {
                std::cerr << "\r[eval] " << done << "/" << total << " item(s)" << std::flush;
            }
        };
        const InterruptScope interrupt;
        options.cancellation = InterruptScope::token();
        std::cerr << "[eval] run " << manifest.run_id << ", suite " << suite.label << " ("
                  << suite.items.size() << " item(s))"
                  << (judge.key.empty() ? std::string{"; no judge: unmatched items skip"}
                                        : ", judge " + judge.key)
                  << "\n";
        training::EvalRun result = training::run_eval(suite.items, *candidate, options);
        if (tty) {
            std::cerr << "\n";
        }
        if (result.cancelled) {
            throw CLI::RuntimeError(kCancelled);
        }
        if (!result.ok) {
            std::cerr << "apogee train: eval failed: " << result.error << "\n";
            throw CLI::RuntimeError(kBackendError);
        }
        result.results.suite = suite.label;
        int index = 0;
        for (const training::EvalItemResult& item : result.results.items) {
            ++index;
            std::cout << "  [" << (item.passed ? "pass" : "FAIL") << "] " << index << " ("
                      << item.check_type;
            if (item.check_type == "judge") {
                std::cout << ": " << item.judge_winner;
            }
            std::cout << ")";
            if (!item.expected.empty()) {
                std::cout << " expected \"" << item.expected << "\"";
            }
            std::cout << "\n";
            if (!item.passed) {
                std::string got = item.got;
                if (got.size() > 80) {
                    got = got.substr(0, 80) + "...";
                }
                std::cout << "        got: \"" << got << "\"\n";
            }
        }
        std::cout << "\n"
                  << (result.results.passed ? "eval PASSED" : "eval FAILED") << " -- "
                  << percent(result.results.score) << " (" << result.results.num_passed << "/"
                  << result.results.total << ")";
        if (result.results.num_skipped > 0) {
            std::cout << "; " << result.results.num_skipped
                      << " item(s) with no `expected` skipped and auto-passed -- name a --judge "
                         "to score them";
        }
        std::cout << "\n";
        manifest.eval = result.results;
        if (const std::string failure =
                training::write_manifest(store.run_dir(manifest.run_id), manifest);
            !failure.empty()) {
            fail_user(failure);
        }
        std::cout << "  recorded in "
                  << training::manifest_path(store.run_dir(manifest.run_id)).string() << "\n";
        if (result.results.passed) {
            std::cout << "\nReady to promote:\n  apogee train promote " << manifest.run_id
                      << " --as <backend>\n";
        } else {
            std::cout << "\nRe-train or adjust the suite, then re-run:\n  apogee train eval "
                      << manifest.run_id << " --suite " << suite_name << " --force\n";
        }
    });

    // ---- promote -----------------------------------------------------------
    auto promote_id = std::make_shared<std::string>();
    auto promote_as = std::make_shared<std::string>();
    auto promote_force = std::make_shared<bool>(false);
    auto promote_quantize = std::make_shared<std::string>();
    auto promote_keep = std::make_shared<bool>(false);
    auto promote_trainer = std::make_shared<std::string>();
    CLI::App* promote = cmd->add_subcommand(
        "promote", "Fuse, convert to GGUF, verify, and register the run as a llamacpp backend");
    promote->add_option("run", *promote_id, "The run id")->required();
    promote->add_option("--as", *promote_as, "The backend name: new, or an existing llamacpp entry")
        ->required();
    promote->add_flag("-f,--force", *promote_force, "Skip the eval gate");
    promote->add_option("--quantize", *promote_quantize,
                        "Quantize the F16 GGUF to this type (needs a build with llama.cpp)");
    promote->add_flag("--keep-fused", *promote_keep,
                      "Keep the fused SafeTensors checkpoint under the run directory");
    promote->add_option("--trainer", *promote_trainer, "Override the run's recorded trainer");
    promote->callback([&context, promote_id, promote_as, promote_force, promote_quantize,
                       promote_keep, promote_trainer]() {
        std::filesystem::path config_path;
        const harness::Config config = load_config_strict(context, config_path);
        const training::TrainingStore store{harness::training_dir()};
        const training::RunManifest manifest = require_manifest(store, *promote_id);
        PromoteRequest request;
        request.backend = *promote_as;
        request.force = *promote_force;
        request.quantize = *promote_quantize;
        request.keep_fused = *promote_keep;

        // Everything that can refuse does so BEFORE the expensive part.
        const PromoteChecks checks = precheck_promotion(config, manifest, request);
        if (!checks.error.empty()) {
            fail_user(checks.error);
        }
        if (!checks.warning.empty()) {
            std::cerr << "apogee train: warning: " << checks.warning << "\n";
        }
        const std::string trainer_name = choose_trainer(config, *promote_trainer, manifest.trainer);
        const std::unique_ptr<training::Trainer> trainer =
            make_trainer(config, trainer_name, "train promote");
        const training::Converter convert = make_converter(config, trainer_name);

        const InterruptScope interrupt;
        const PromoteOutcome result = promote_run(
            config, config_path, store, manifest, *trainer, convert, request,
            [](std::string_view text) { std::cerr << "[promote] " << text << "\n"; },
            InterruptScope::token());
        if (result.cancelled) {
            throw CLI::RuntimeError(kCancelled);
        }
        if (result.refused) {
            fail_user(result.error);
        }
        if (!result.ok) {
            std::cerr << "apogee train: promote failed: " << result.error << "\n";
            throw CLI::RuntimeError(kBackendError);
        }
        std::cout << (result.updated ? "updated backend " : "promoted to new backend ")
                  << result.backend_key << " -> v" << result.version << "\n"
                  << "  gguf:   " << result.gguf_path << "\n"
                  << "  config: " << config_path.string() << "\n";
        if (manifest.eval.has_value()) {
            std::cout << "  eval:   " << percent(manifest.eval->score) << " ("
                      << manifest.eval->num_passed << "/" << manifest.eval->total << ")\n";
        }
        for (const std::string& removed : result.pruned.removed) {
            std::cout << "  pruned: " << removed << " (retain_versions "
                      << config.training.retain_versions << ")\n";
        }
        for (const std::string& failed : result.pruned.failed) {
            std::cerr << "apogee train: warning: could not prune " << failed << "\n";
        }
        std::cout << "\nChat with it:\n  apogee chat -m " << result.backend_key << "\n";
        const std::optional<training::VersionLedger> ledger =
            store.list_versions(result.backend_key);
        if (ledger.has_value() && ledger->kept() > 1) {
            std::cout << "Roll back if needed:\n  apogee train rollback " << result.backend_key
                      << "\n";
        }
    });

    // ---- rollback ----------------------------------------------------------
    auto rollback_backend = std::make_shared<std::string>();
    CLI::App* rollback =
        cmd->add_subcommand("rollback", "Repoint a backend at its previous promoted version");
    rollback->add_option("backend", *rollback_backend, "The backend name")->required();
    rollback->callback([&context, rollback_backend]() {
        std::filesystem::path config_path;
        const harness::Config config = load_config_strict(context, config_path);
        const training::TrainingStore store{harness::training_dir()};
        std::string error;
        std::optional<training::VersionLedger> ledger =
            training::load_ledger(store.versions_dir(), *rollback_backend, error);
        if (!error.empty()) {
            fail_user("the version ledger is unreadable: " + error);
        }
        if (!ledger.has_value()) {
            fail_user("no version history for backend '" + *rollback_backend +
                      "' -- 'apogee train promote <run> --as " + *rollback_backend +
                      "' creates the first version");
        }
        const training::RollbackTarget target = training::rollback_target(*ledger);
        if (target.entry == nullptr) {
            fail_user(target.error);
        }
        const std::string key = configured_backend_key(config, *rollback_backend);
        if (key.empty()) {
            fail_user("backend '" + *rollback_backend +
                      "' is not in the config, so there is nothing to repoint");
        }
        const int from = ledger->active_version;
        const int to = target.entry->version;
        const std::string gguf = target.entry->gguf_path;
        try {
            harness::edit_config_file(config_path, [&](std::string_view content) {
                return harness::set_backend_model_path(content, key, gguf);
            });
        } catch (const harness::ConfigEditError& e) {
            fail_user(e.what());
        } catch (const harness::ConfigError& e) {
            fail_user(e.what());
        }
        ledger->active_version = to;
        if (const std::string failure = training::save_ledger(store.versions_dir(), *ledger);
            !failure.empty()) {
            fail_user("the config is repointed, but the ledger could not be saved: " + failure);
        }
        std::cout << "rolled back " << key << ": v" << from << " -> v" << to << "\n"
                  << "  gguf: " << gguf << "\n  nothing was deleted; v" << from
                  << " stays on disk\n";
    });

    // ---- versions ----------------------------------------------------------
    auto versions_backend = std::make_shared<std::string>();
    CLI::App* versions = cmd->add_subcommand("versions", "List a backend's promoted versions");
    versions->add_option("backend", *versions_backend, "The backend name (default: every ledger)");
    versions->callback([versions_backend]() {
        const training::TrainingStore store{harness::training_dir()};
        std::vector<training::VersionLedger> ledgers;
        if (versions_backend->empty()) {
            ledgers = store.all_versions();
            if (ledgers.empty()) {
                std::cout << "no promoted versions yet\n";
                return;
            }
        } else {
            std::string error;
            const std::optional<training::VersionLedger> ledger =
                training::load_ledger(store.versions_dir(), *versions_backend, error);
            if (!error.empty()) {
                fail_user("the version ledger is unreadable: " + error);
            }
            if (!ledger.has_value()) {
                fail_user("no version history for backend '" + *versions_backend + "'");
            }
            ledgers.push_back(*ledger);
        }
        for (const training::VersionLedger& ledger : ledgers) {
            std::cout << "versions of " << ledger.backend << " (active v" << ledger.active_version
                      << ")\n"
                      << "  " << pad("VERSION", 9) << pad("PROMOTED AT", 22) << pad("RUN", 20)
                      << pad("EVAL", 11) << "GGUF\n";
            for (const training::VersionEntry& entry : ledger.versions) {
                std::string promoted = entry.promoted_at;
                if (promoted.size() > 19) {
                    promoted = promoted.substr(0, 19);
                }
                std::cout << "  " << pad("v" + std::to_string(entry.version), 9)
                          << pad(promoted, 22) << pad(entry.run_id, 20)
                          << pad(eval_glyph(entry.eval_passed, entry.eval_score), 11)
                          << std::filesystem::path{entry.gguf_path}.filename().string();
                if (entry.pruned()) {
                    std::cout << "  (pruned)";
                }
                if (entry.version == ledger.active_version) {
                    std::cout << "  <- active";
                }
                std::cout << "\n";
            }
        }
    });

    // ---- status ------------------------------------------------------------
    CLI::App* status = cmd->add_subcommand(
        "status", "The cycle, the active pipeline, the runs and the active versions");
    status->callback([&context]() {
        const harness::Config config = load_config_or_default(context);
        const training::TrainingStore store{harness::training_dir()};
        const std::filesystem::path cycle_dir = harness::training_cycle_dir();
        if (training::history_exists(cycle_dir)) {
            std::string error;
            const training::CycleHistory history =
                training::load_history(cycle_dir, config.training.cycle.backend, error);
            if (!error.empty()) {
                std::cout << "Cycle: history unreadable -- " << error << "\n";
            } else {
                std::cout << "Cycle: "
                          << (history.halted ? "HALTED -- " + history.halt_reason
                              : training::cycle_lock_held(cycle_dir) ? std::string{"running now"}
                                                                     : std::string{"idle"})
                          << "; backend " << history.backend << "; " << history.total_runs
                          << " run(s), " << history.consecutive_fails << " consecutive failure(s)"
                          << (history.anchor_version > 0
                                  ? "; anchor v" + std::to_string(history.anchor_version) + " (" +
                                        percent(history.anchor_score) + ")"
                                  : std::string{})
                          << "\n";
            }
        } else if (config.training.cycle.configured()) {
            std::cout << "Cycle: configured (pipeline '" << config.training.cycle.pipeline
                      << "' -> " << config.training.cycle.backend
                      << "), no runs yet ('apogee train cycle run')\n";
        } else {
            std::cout << "Cycle: not configured (training.cycle)\n";
        }
        const std::vector<training::PipelineSummary> pipelines = store.list_pipelines();
        const std::optional<training::PipelineSummary> active = store.active_pipeline();
        if (active.has_value()) {
            std::cout << "Pipeline: " << active->id << " RUNNING (" << active->passed << "/"
                      << active->stages << " stage(s) passed)\n";
        } else if (pipelines.empty()) {
            std::cout << "Pipeline: none yet ('apogee train pipeline run --pipeline <spec>')\n";
        } else {
            std::cout << "Pipeline: none in progress; " << pipelines.size() << " run(s), latest "
                      << pipelines.front().id << " " << pipelines.front().status << " ("
                      << pipelines.front().passed << "/" << pipelines.front().stages
                      << " passed)\n";
        }
        const std::vector<training::RunSummary> runs = store.list_runs();
        int running = 0;
        for (const training::RunSummary& run : runs) {
            if (run.status == training::kStatusRunning) {
                ++running;
            }
        }
        if (runs.empty()) {
            std::cout << "Runs: none yet ('apogee train run <snapshot> --dataset <name>')\n";
        } else {
            const std::size_t shown = std::min<std::size_t>(runs.size(), 5);
            std::cout << "Runs: " << runs.size() << " (" << running << " running); recent " << shown
                      << ":\n";
            for (std::size_t i = 0; i < shown; ++i) {
                const training::RunSummary& run = runs[i];
                std::ostringstream loss;
                loss << std::fixed << std::setprecision(4) << run.final_loss;
                std::cout << "  " << pad(run.id, 20) << pad(run.trainer, 6) << pad(run.status, 11)
                          << "loss " << pad(loss.str(), 9) << "eval "
                          << eval_glyph(run.eval_passed, run.eval_score) << "\n";
            }
        }
        const std::vector<training::VersionLedger> ledgers = store.all_versions();
        if (ledgers.empty()) {
            std::cout << "Versions: none promoted yet\n";
        } else {
            std::cout << "Versions:\n";
            for (const training::VersionLedger& ledger : ledgers) {
                std::cout << "  " << pad(ledger.backend, 20) << "active v" << ledger.active_version
                          << "  (" << ledger.kept() << " kept of " << ledger.versions.size()
                          << ")\n";
            }
        }
    });

    // ---- pipeline ----------------------------------------------------------
    CLI::App* pipeline =
        cmd->add_subcommand("pipeline", "Multi-stage runs: fused chaining under a cumulative gate");
    pipeline->require_subcommand(1);

    auto pipe_spec = std::make_shared<std::string>();
    auto pipe_continue = std::make_shared<bool>(false);
    auto pipe_judge = std::make_shared<std::string>();
    auto pipe_trainer = std::make_shared<std::string>();
    CLI::App* pipe_run = pipeline->add_subcommand(
        "run",
        "Run a pipeline spec: each stage a fresh LoRA on the previous stage's fused "
        "weights, gated cumulatively at 100%");
    pipe_run
        ->add_option("--pipeline", *pipe_spec,
                     "A spec file (YAML), or a name under training.pipelines")
        ->required();
    pipe_run->add_flag("--continue-on-fail", *pipe_continue,
                       "Go on to the next stage when the cumulative gate fails");
    pipe_run->add_option("--judge", *pipe_judge,
                         "The backend that judges items without `expected` (default: "
                         "training.judge_backend)");
    pipe_run->add_option("--trainer", *pipe_trainer, "auto (default), mlx, peft, or mock");

    auto resume_id = std::make_shared<std::string>();
    auto resume_spec = std::make_shared<std::string>();
    auto resume_continue = std::make_shared<bool>(false);
    auto resume_judge = std::make_shared<std::string>();
    auto resume_trainer = std::make_shared<std::string>();
    CLI::App* pipe_resume = pipeline->add_subcommand(
        "resume", "Continue a pipeline from the first stage that has not passed");
    pipe_resume->add_option("id", *resume_id, "The pipeline run id")->required();
    pipe_resume->add_option("--pipeline", *resume_spec,
                            "The spec, when the run's named pipeline is no longer in the config");
    pipe_resume->add_flag("--continue-on-fail", *resume_continue,
                          "Go on to the next stage when the cumulative gate fails");
    pipe_resume->add_option("--judge", *resume_judge, "The judge backend");
    pipe_resume->add_option("--trainer", *resume_trainer, "auto (default), mlx, peft, or mock");

    auto pipe_status_id = std::make_shared<std::string>();
    CLI::App* pipe_status =
        pipeline->add_subcommand("status", "A pipeline run's stages, or every pipeline run");
    pipe_status->add_option("id", *pipe_status_id, "The pipeline run id (default: list them)");

    // One body for run and resume: resolve, drive, report.
    auto drive_pipeline = [&context](const harness::Config& config,
                                     const harness::PipelineSpec& spec,
                                     std::optional<training::PipelineRunManifest> existing,
                                     bool continue_on_fail, const std::string& judge_flag,
                                     const std::string& trainer_flag) {
        const PipelineInputs inputs = resolve_pipeline_inputs(config, context, spec, true);
        const TeacherResolution judge = resolve_judge(config, judge_flag);
        const std::string trainer_name = choose_trainer(config, trainer_flag, {});
        const std::unique_ptr<training::Trainer> trainer =
            make_trainer(config, trainer_name, "train pipeline run");
        std::optional<Providers> providers;
        if (!judge.key.empty()) {
            providers.emplace(config, std::filesystem::path{});
        }
        training::PipelineRequest request;
        request.spec = &spec;
        request.stages = inputs.stages;
        request.base_model =
            existing.has_value() ? std::filesystem::path{existing->base_model} : inputs.base_model;
        request.training_dir = harness::training_dir();
        request.trainer = trainer.get();
        if (!judge.key.empty()) {
            request.judge_backend = judge.key;
            request.judge = make_judge(providers->harness, judge.key);
        }
        request.gate_mode = training::gate_mode_from_string(config.training.effective_gate_mode());
        request.continue_on_fail = continue_on_fail;
        request.pipeline_run_id = existing.has_value()
                                      ? existing->pipeline_run_id
                                      : training::new_run_id(harness::training_pipelines_dir(),
                                                             std::chrono::system_clock::now(),
                                                             training::kPipelineIdPrefix);
        PipelineConsole console{spec.stages.size()};
        request.on_progress = [&console](const training::PipelineProgress& progress) {
            console(progress);
        };
        const InterruptScope interrupt;
        request.cancellation = InterruptScope::token();
        std::cerr << "[pipeline] " << (existing.has_value() ? "resuming " : "") << spec.name
                  << " as " << request.pipeline_run_id << ": " << spec.stages.size()
                  << " stage(s), " << trainer_name << " on "
                  << request.base_model.filename().string()
                  << (judge.key.empty() ? std::string{"; no judge: unmatched items skip"}
                                        : ", judge " + judge.key)
                  << "\n";
        const training::PipelineOutcome outcome =
            existing.has_value() ? training::resume_pipeline(request, std::move(*existing))
                                 : training::run_pipeline(request);
        console.finish();
        print_pipeline_summary(outcome.manifest);
        if (outcome.cancelled) {
            std::cerr << "[pipeline] cancelled; recorded as " << outcome.manifest.status
                      << " -- 'apogee train pipeline resume " << request.pipeline_run_id
                      << "' continues it\n";
            throw CLI::RuntimeError(kCancelled);
        }
        if (!outcome.ok) {
            std::cerr << "apogee train: " << outcome.error << "\n";
            throw CLI::RuntimeError(kBackendError);
        }
    };

    pipe_run->callback(
        [&context, drive_pipeline, pipe_spec, pipe_continue, pipe_judge, pipe_trainer]() {
            std::filesystem::path config_path;
            const harness::Config config = load_config_strict(context, config_path);
            const harness::PipelineSpec spec = resolve_pipeline_spec(config, *pipe_spec);
            drive_pipeline(config, spec, std::nullopt, *pipe_continue, *pipe_judge, *pipe_trainer);
        });

    pipe_resume->callback([&context, drive_pipeline, resume_id, resume_spec, resume_continue,
                           resume_judge, resume_trainer]() {
        std::filesystem::path config_path;
        const harness::Config config = load_config_strict(context, config_path);
        const training::TrainingStore store{harness::training_dir()};
        if (!training::valid_run_id(*resume_id)) {
            fail_user("'" + *resume_id + "' is not a pipeline run id");
        }
        std::optional<training::PipelineRunManifest> existing = store.get_pipeline(*resume_id);
        if (!existing.has_value()) {
            fail_user("no pipeline run '" + *resume_id +
                      "' ('apogee train pipeline status' lists them)");
        }
        harness::PipelineSpec spec;
        if (!resume_spec->empty()) {
            spec = resolve_pipeline_spec(config, *resume_spec);
        } else if (const auto named = config.training.pipelines.find(existing->spec_name);
                   named != config.training.pipelines.end()) {
            spec = named->second;
        } else {
            fail_user("pipeline '" + existing->spec_name +
                      "' is not under training.pipelines any more -- pass --pipeline <spec> to "
                      "resume " +
                      *resume_id);
        }
        if (const std::string refusal = training::resume_check(*existing, spec); !refusal.empty()) {
            fail_user(refusal);
        }
        std::error_code code;
        if (!std::filesystem::is_directory(existing->base_model, code)) {
            fail_user("the run's snapshot is gone: " + existing->base_model);
        }
        drive_pipeline(config, spec, std::move(existing), *resume_continue, *resume_judge,
                       *resume_trainer);
    });

    pipe_status->callback([pipe_status_id]() {
        const training::TrainingStore store{harness::training_dir()};
        if (pipe_status_id->empty()) {
            const std::vector<training::PipelineSummary> pipelines = store.list_pipelines();
            if (pipelines.empty()) {
                std::cout << "no pipeline runs yet ('apogee train pipeline run --pipeline "
                             "<spec>')\n";
                return;
            }
            std::cout << "  " << pad("PIPELINE", 30) << pad("SPEC", 16) << pad("STATUS", 10)
                      << pad("STAGES", 8) << "STARTED\n";
            for (const training::PipelineSummary& pipeline : pipelines) {
                std::cout << "  " << pad(pipeline.id, 30) << pad(pipeline.spec_name, 16)
                          << pad(pipeline.status, 10)
                          << pad(std::to_string(pipeline.passed) + "/" +
                                     std::to_string(pipeline.stages),
                                 8)
                          << pipeline.started_at << "\n";
            }
            return;
        }
        if (!training::valid_run_id(*pipe_status_id)) {
            fail_user("'" + *pipe_status_id + "' is not a pipeline run id");
        }
        const std::optional<training::PipelineRunManifest> manifest =
            store.get_pipeline(*pipe_status_id);
        if (!manifest.has_value()) {
            fail_user("no pipeline run '" + *pipe_status_id + "'");
        }
        print_pipeline_table(*manifest);
    });

    // ---- regime ------------------------------------------------------------
    CLI::App* regime = cmd->add_subcommand(
        "regime", "Distil a teacher into the student across kits: synth, pipeline, promote");
    regime->require_subcommand(1);
    auto regime_named = std::make_shared<std::string>();
    auto regime_file = std::make_shared<std::string>();
    auto regime_teacher = std::make_shared<std::string>();
    auto regime_student = std::make_shared<std::string>();
    auto regime_kits = std::make_shared<std::vector<std::string>>();
    auto regime_all = std::make_shared<bool>(false);
    auto regime_as = std::make_shared<std::string>();
    auto regime_count = std::make_shared<int>(0);
    auto regime_iters = std::make_shared<int>(0);
    auto regime_temperature = std::make_shared<double>(0.0);
    auto regime_max_tokens = std::make_shared<int>(4096);
    auto regime_judge = std::make_shared<std::string>();
    auto regime_no_promote = std::make_shared<bool>(false);
    auto regime_trainer = std::make_shared<std::string>();
    CLI::App* regime_run = regime->add_subcommand(
        "run",
        "Run a regime: a dataset per kit from the teacher, one eval-gated pipeline, "
        "the last passing stage promoted");
    regime_run->add_option("name", *regime_named,
                           "A regime under training.regimes (or a spec file path)");
    regime_run->add_option("--regime", *regime_file, "A regime spec file (YAML)");
    regime_run->add_option("--teacher", *regime_teacher, "The backend that synthesises the data");
    regime_run->add_option("--student", *regime_student,
                           "The snapshot to fine-tune: a directory, or a name under paths.hf_dir "
                           "or models/");
    regime_run->add_option("--kit", *regime_kits, "A kit per stage, in order (repeatable)");
    regime_run->add_flag("--all-kits", *regime_all,
                         "Every installed kit, alphabetically (an explicit --kit list wins)");
    regime_run->add_option("--as", *regime_as, "Promote the last passing stage into this backend");
    regime_run->add_option("--count", *regime_count,
                           "Examples per kit (default: each kit's synth.count)");
    regime_run->add_option("--iters", *regime_iters,
                           "Iterations per stage (default: each kit's train.iters)");
    regime_run->add_option("--temperature", *regime_temperature,
                           "The teacher's sampling temperature (default: each kit's)");
    regime_run->add_option("--max-tokens", *regime_max_tokens,
                           "The teacher's per-call token budget (default 4096)");
    regime_run->add_option("--judge", *regime_judge,
                           "The backend that judges items without `expected` (default: "
                           "training.judge_backend)");
    regime_run->add_flag("--no-promote", *regime_no_promote,
                         "Stop after the gated pipeline; promote by hand");
    regime_run->add_option("--trainer", *regime_trainer, "auto (default), mlx, peft, or mock");
    regime_run->callback([&context, regime_named, regime_file, regime_teacher, regime_student,
                          regime_kits, regime_all, regime_as, regime_count, regime_iters,
                          regime_temperature, regime_max_tokens, regime_judge, regime_no_promote,
                          regime_trainer]() {
        std::filesystem::path config_path;
        const harness::Config config = load_config_strict(context, config_path);
        if (*regime_max_tokens <= 0) {
            fail_user("--max-tokens must be positive");
        }
        training::RegimeFlags flags;
        flags.teacher = *regime_teacher;
        flags.student = *regime_student;
        flags.kits = *regime_kits;
        flags.all_kits = *regime_all;
        flags.count = *regime_count;
        flags.promote_as = *regime_as;
        flags.iters = *regime_iters;
        flags.temperature = *regime_temperature;
        std::vector<std::string> installed;
        for (const training::KitSummary& kit : training::list_kits(harness::training_kits_dir())) {
            if (kit.error.empty()) {
                installed.push_back(kit.name);
            }
        }
        const harness::RegimeSpec spec = training::apply_regime_flags(
            resolve_regime_spec(config, *regime_file, *regime_named), flags, installed);

        // Every refusal before a teacher call: the teacher, the student, the
        // kits, the judge, the trainer, and for a promotion its converter.
        if (spec.teacher.empty()) {
            fail_user("no teacher -- pass --teacher <backend>, or set regime.teacher");
        }
        const TeacherResolution teacher = resolve_teacher(config, spec.teacher);
        if (teacher.key.empty()) {
            fail_user("teacher: " + teacher.error);
        }
        if (spec.student.empty()) {
            fail_user("no student -- pass --student <snapshot>, or set regime.student");
        }
        const StudentResolution student = resolve_student(
            config, harness::models_dir(),
            snapshot_root(harness::models_dir(), context.config_path), spec.student);
        if (!student.error.empty()) {
            fail_user("student: " + student.error);
        }
        if (const std::string problem =
                training::validate_regime_kits(spec, harness::training_kits_dir());
            !problem.empty()) {
            fail_user(problem);
        }
        const TeacherResolution judge = resolve_judge(config, *regime_judge);
        const std::string trainer_name = choose_trainer(config, *regime_trainer, {});
        const std::unique_ptr<training::Trainer> trainer =
            make_trainer(config, trainer_name, "train regime run");
        const bool promoting = !*regime_no_promote && !spec.promote_as.empty();
        std::optional<training::Converter> convert;
        if (promoting) {
            if (spec.promote_as.find_first_of("/\\ \t") != std::string::npos) {
                fail_user("--as must be a plain backend name (got '" + spec.promote_as + "')");
            }
            const std::string key = configured_backend_key(config, spec.promote_as);
            const harness::BackendConfig* existing =
                key.empty() ? nullptr : config.find_backend(key);
            if (existing != nullptr && existing->type != harness::BackendType::LlamaCpp) {
                fail_user("'" + key + "' is a " + std::string{harness::to_string(existing->type)} +
                          " backend -- promote into an existing llamacpp entry, or a new name");
            }
            convert = make_converter(config, trainer_name);
        }
        const Providers providers{config, config_path};
        const std::vector<std::string> built = providers.harness.provider_names();
        if (std::find(built.begin(), built.end(), teacher.key) == built.end()) {
            fail_user("teacher '" + teacher.key +
                      "' could not be built: " + providers.built.skipped_summary());
        }

        training::RegimeRequest request;
        request.spec = spec;
        request.kits_dir = harness::training_kits_dir();
        request.training_dir = harness::training_dir();
        request.regime_run_id =
            training::new_run_id(harness::training_regime_dir(), std::chrono::system_clock::now(),
                                 training::kRegimeIdPrefix);
        request.work_dir = harness::training_regime_dir() / request.regime_run_id;
        request.base_model = student.path;
        request.generate = make_teacher(providers.harness, teacher.key);
        request.max_tokens = *regime_max_tokens;
        request.parallel = effective_parallel(teacher, 4);
        request.trainer = trainer.get();
        if (!judge.key.empty()) {
            request.judge_backend = judge.key;
            request.judge = make_judge(providers.harness, judge.key);
        }
        request.gate_mode = training::gate_mode_from_string(config.training.effective_gate_mode());
        PipelineConsole console{spec.kits.size()};
        request.on_message = [&console](std::string_view text) {
            console(training::PipelineProgress{.stage_index = -1, .message = std::string{text}});
        };
        request.on_progress = [&console](const training::PipelineProgress& progress) {
            console(progress);
        };
        const InterruptScope interrupt;
        request.cancellation = InterruptScope::token();
        std::string kits;
        for (const std::string& kit : spec.kits) {
            kits += (kits.empty() ? "" : " -> ") + kit;
        }
        std::cerr << "[regime] " << spec.name << " as " << request.regime_run_id << ": teacher "
                  << teacher.key << " -> student " << student.path.filename().string() << " ("
                  << trainer_name << "); kits " << kits
                  << (promoting ? "; promote as " + spec.promote_as : std::string{}) << "\n";
        const training::RegimeOutcome outcome = training::run_regime(request);
        console.finish();
        if (outcome.cancelled) {
            std::cerr << "[regime] cancelled; the work directory is kept at "
                      << request.work_dir.string() << "\n";
            throw CLI::RuntimeError(kCancelled);
        }
        for (const training::RegimeKitResult& kit : outcome.kits) {
            std::cout << "kit " << kit.kit << ": " << kit.examples << " example(s) in " << kit.calls
                      << " call(s) -> " << kit.dataset.string() << "\n";
        }
        if (outcome.pipeline.has_value()) {
            print_pipeline_summary(outcome.pipeline->manifest);
        }
        if (!outcome.ok) {
            std::cerr << "apogee train: regime " << spec.name << ": " << outcome.error
                      << "\n  the synthesised data and suites are kept at "
                      << request.work_dir.string() << "\n";
            throw CLI::RuntimeError(kBackendError);
        }
        if (!promoting) {
            if (!outcome.promote_run_id.empty()) {
                std::cout << "\nRegime complete. Promote when ready:\n  apogee train promote "
                          << outcome.promote_run_id << " --as <backend>\n";
            }
            return;
        }
        if (outcome.promote_run_id.empty()) {
            fail_user("the pipeline completed but no stage passed -- nothing to promote");
        }
        const training::TrainingStore store{harness::training_dir()};
        const training::RunManifest final_stage = require_manifest(store, outcome.promote_run_id);
        PromoteRequest promote;
        promote.backend = spec.promote_as;
        std::cerr << "[regime] promoting " << outcome.promote_run_id << " as " << spec.promote_as
                  << "\n";
        const PromoteOutcome result = promote_run(
            config, config_path, store, final_stage, *trainer, *convert, promote,
            [](std::string_view text) { std::cerr << "[promote] " << text << "\n"; },
            InterruptScope::token());
        if (result.cancelled) {
            throw CLI::RuntimeError(kCancelled);
        }
        if (!result.ok) {
            std::cerr << "apogee train: regime " << spec.name << ": promote: " << result.error
                      << "\n  promote by hand: apogee train promote " << outcome.promote_run_id
                      << " --as " << spec.promote_as << "\n";
            throw CLI::RuntimeError(result.refused ? kUserError : kBackendError);
        }
        std::cout << "\nregime " << spec.name << " complete -- " << result.backend_key
                  << " is now v" << result.version << "\n  gguf: " << result.gguf_path
                  << "\n\nChat with it:\n  apogee chat -m " << result.backend_key << "\n";
    });

    // ---- cycle -------------------------------------------------------------
    CLI::App* cycle = cmd->add_subcommand(
        "cycle", "The unattended, scheduler-invoked loop: one gated pass per run");
    cycle->require_subcommand(1);
    auto cycle_source = std::make_shared<std::string>();
    CLI::App* cycle_run = cycle->add_subcommand(
        "run",
        "One gated pass: collect, merge, run the cycle's pipeline, gate against the "
        "last pass and the anchor, promote or discard");
    cycle_run->add_option("--source", *cycle_source,
                          "A queue directory for this run, replacing the first directory source");
    cycle_run->callback([&context, cycle_source]() {
        std::filesystem::path config_path;
        const harness::Config config = load_config_strict(context, config_path);
        const harness::CycleConfig& settings = config.training.cycle;
        if (!settings.configured()) {
            fail_user(
                "training.cycle is not configured: it needs 'pipeline' (a training.pipelines "
                "entry or a spec file), 'backend' (where a passing cycle promotes) and at least "
                "one entry under 'sources' -- see the template block in config.yaml");
        }
        const harness::PipelineSpec spec = resolve_pipeline_spec(config, settings.pipeline);
        // The datasets are the merged sources; only the suites are resolved.
        const PipelineInputs inputs = resolve_pipeline_inputs(config, context, spec, false);
        const TeacherResolution judge = resolve_judge(config, settings.judge_backend);
        const std::string trainer_name = choose_trainer(config, {}, {});
        const std::unique_ptr<training::Trainer> trainer =
            make_trainer(config, trainer_name, "train cycle run");
        const training::Converter convert = make_converter(config, trainer_name);
        {
            PromoteRequest probe;
            probe.backend = settings.backend;
            if (probe.backend.find_first_of("/\\ \t") != std::string::npos) {
                fail_user("training.cycle.backend must be a plain backend name");
            }
            const std::string key = configured_backend_key(config, settings.backend);
            const harness::BackendConfig* existing =
                key.empty() ? nullptr : config.find_backend(key);
            if (existing != nullptr && existing->type != harness::BackendType::LlamaCpp) {
                fail_user("training.cycle.backend '" + key + "' is a " +
                          std::string{harness::to_string(existing->type)} +
                          " backend, not a llamacpp one");
            }
        }
        std::optional<Providers> providers;
        if (!judge.key.empty()) {
            providers.emplace(config, config_path);
        }
        std::optional<LoadedSessions> sessions;
        for (const harness::CycleSourceConfig& source : settings.sources) {
            if (source.type == "sessions" && !sessions.has_value()) {
                sessions = load_session_views();
            }
        }

        training::CycleRequest request;
        request.config = settings;
        request.cycle_dir = harness::training_cycle_dir();
        request.training_dir = harness::training_dir();
        request.spec = &spec;
        request.stages = inputs.stages;
        request.base_model = inputs.base_model;
        request.source_override = *cycle_source;
        if (sessions.has_value()) {
            request.sessions = sessions->views;
        }
        request.trainer = trainer.get();
        if (!judge.key.empty()) {
            request.judge_backend = judge.key;
            request.judge = make_judge(providers->harness, judge.key);
        }
        request.gate_mode = training::gate_mode_from_string(config.training.effective_gate_mode());
        const training::TrainingStore store{harness::training_dir()};
        const InterruptScope interrupt;
        request.cancellation = InterruptScope::token();
        request.promote = [&](std::string_view run_id) {
            training::CyclePromotion promotion;
            std::string error;
            const std::optional<training::RunManifest> manifest =
                training::read_manifest(store.run_dir(run_id), error);
            if (!manifest.has_value()) {
                promotion.error = error;
                return promotion;
            }
            PromoteRequest promote;
            promote.backend = settings.backend;
            const PromoteOutcome result = promote_run(
                config, config_path, store, *manifest, *trainer, convert, promote,
                [](std::string_view text) { std::cerr << "[promote] " << text << "\n"; },
                InterruptScope::token());
            promotion.ok = result.ok;
            promotion.error = result.error;
            promotion.version = result.version;
            promotion.gguf_path = result.gguf_path;
            return promotion;
        };
        PipelineConsole console{spec.stages.size()};
        request.on_message = [&console](std::string_view text) {
            console(training::PipelineProgress{.stage_index = -1, .message = std::string{text}});
        };
        request.on_progress = [&console](const training::PipelineProgress& progress) {
            console(progress);
        };
        const training::CycleOutcome outcome = training::run_cycle(request);
        console.finish();
        if (outcome.cancelled) {
            std::cerr << "[cycle] cancelled before an outcome was recorded\n";
            throw CLI::RuntimeError(kCancelled);
        }
        if (!outcome.ok) {
            fail_user(outcome.error);
        }
        const training::CycleRunRecord& record = *outcome.record;
        if (record.gate == training::kGateSkipped) {
            std::cout << "cycle skipped -- " << record.gate_reason << "\n";
            return;
        }
        if (record.gate == training::kGateFail) {
            std::cout << "cycle FAILED -- " << record.gate_reason
                      << "\n  the candidate was discarded; nothing reached inference\n";
            if (outcome.halted_now) {
                std::cout << "  " << outcome.history.halt_reason
                          << "\n  the loop is halted: fix the data or the suite, then 'apogee "
                             "train cycle resume'\n";
            }
            throw CLI::RuntimeError(kBackendError);
        }
        std::cout << "cycle PASSED -- " << settings.backend << " promoted to v"
                  << record.promoted_version << " (" << percent(record.cycle_score) << ")\n"
                  << "  history: " << training::history_path(request.cycle_dir).string() << "\n";
    });

    CLI::App* cycle_status = cycle->add_subcommand("status", "The cycle's history and anchor");
    cycle_status->callback([&context]() {
        const harness::Config config = load_config_or_default(context);
        const std::filesystem::path cycle_dir = harness::training_cycle_dir();
        std::string error;
        const training::CycleHistory history =
            training::load_history(cycle_dir, config.training.cycle.backend, error);
        if (!error.empty()) {
            fail_user("the cycle history is unreadable: " + error);
        }
        if (!training::history_exists(cycle_dir)) {
            std::cout << "no cycle runs yet"
                      << (config.training.cycle.configured()
                              ? " -- 'apogee train cycle run' performs one gated pass"
                              : " -- configure training.cycle first")
                      << "\n";
            return;
        }
        print_cycle_history(history, training::cycle_lock_held(cycle_dir));
    });

    CLI::App* cycle_halt = cycle->add_subcommand(
        "halt", "Halt the loop: every 'cycle run' refuses until 'cycle resume'");
    cycle_halt->callback([&context]() {
        const harness::Config config = load_config_or_default(context);
        const std::filesystem::path cycle_dir = harness::training_cycle_dir();
        std::string error;
        training::CycleHistory history =
            training::load_history(cycle_dir, config.training.cycle.backend, error);
        if (!error.empty()) {
            fail_user("the cycle history is unreadable: " + error);
        }
        if (history.halted) {
            std::cout << "already halted: " << history.halt_reason
                      << "\n  'apogee train cycle resume' clears it\n";
            return;
        }
        training::halt_cycle(history, "halted by 'apogee train cycle halt'");
        if (const std::string failure = training::save_history(cycle_dir, history);
            !failure.empty()) {
            fail_user(failure);
        }
        std::cout << "cycle halted -- 'apogee train cycle run' refuses until 'apogee train cycle "
                     "resume'\n";
    });

    CLI::App* cycle_resume = cycle->add_subcommand(
        "resume", "Clear a halt (manual or the circuit breaker) and the failure count");
    cycle_resume->callback([&context]() {
        const harness::Config config = load_config_or_default(context);
        const std::filesystem::path cycle_dir = harness::training_cycle_dir();
        std::string error;
        training::CycleHistory history =
            training::load_history(cycle_dir, config.training.cycle.backend, error);
        if (!error.empty()) {
            fail_user("the cycle history is unreadable: " + error);
        }
        if (!training::history_exists(cycle_dir)) {
            std::cout << "no cycle history yet -- nothing to resume\n";
            return;
        }
        const std::string reason = history.halt_reason;
        const bool was_halted = training::resume_cycle(history);
        if (const std::string failure = training::save_history(cycle_dir, history);
            !failure.empty()) {
            fail_user(failure);
        }
        if (was_halted) {
            std::cout << "cycle resumed (was halted: " << reason
                      << "); the failure count is reset\n";
        } else {
            std::cout << "the cycle was not halted; the failure count is reset\n";
        }
    });
}

}  // namespace apogee::commands
