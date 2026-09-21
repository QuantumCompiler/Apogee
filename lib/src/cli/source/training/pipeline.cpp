#include "training/pipeline.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <random>
#include <stdexcept>
#include <system_error>

#include "harness/config_edit.h"

namespace apogee::training {
namespace {

/// Non-blank lines of a JSONL file; nullopt when it cannot be opened.
std::optional<std::vector<std::string>> read_lines(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return std::nullopt;
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.find_first_not_of(" \t") != std::string::npos) {
            lines.push_back(line);
        }
    }
    return lines;
}

std::string percent_of(double score) {
    return std::to_string(static_cast<int>(std::lround(score * 100.0))) + "%";
}

/// The whole orchestration, shared by `run_pipeline` and `resume_pipeline`:
/// every stage from the first unpassed one.
class StageRunner {
public:
    StageRunner(const PipelineRequest& request, PipelineRunManifest manifest)
        : request_{request},
          manifest_{std::move(manifest)},
          pipeline_dir_{request.training_dir / kPipelinesDirName / manifest_.pipeline_run_id},
          runs_dir_{request.training_dir / "runs"} {}

    [[nodiscard]] PipelineOutcome run() {
        const harness::PipelineSpec& spec = *request_.spec;
        // The resume point: after the last passed stage, from its fused
        // checkpoint when it left one.
        std::size_t resume_from = 0;
        std::filesystem::path current_base{manifest_.base_model};
        for (std::size_t i = 0; i < manifest_.stages.size(); ++i) {
            if (manifest_.stages[i].status == kStagePassed) {
                resume_from = i + 1;
                if (!manifest_.stages[i].fused_dir.empty()) {
                    current_base = manifest_.stages[i].fused_dir;
                }
            }
        }
        manifest_.status = std::string{kPipelineRunning};
        if (const std::string failure = save(); !failure.empty()) {
            return finish(kPipelineFailed, failure);
        }

        for (std::size_t i = resume_from; i < spec.stages.size(); ++i) {
            if (request_.cancellation.stop_requested()) {
                return finish(kPipelineAborted, "cancelled", true);
            }
            const harness::PipelineStageSpec& stage = spec.stages[i];
            PipelineStageRecord& record = manifest_.stages[i];
            const int index = static_cast<int>(i);
            record.index = index;
            record.name = stage.name;
            record.base_model = current_base.string();
            record.run_id = stage_run_id(manifest_.pipeline_run_id, index);
            const std::filesystem::path stage_dir = runs_dir_ / record.run_id;

            // --- training ------------------------------------------------
            std::filesystem::path dataset = request_.stages[i].dataset;
            if (stage.rehearsal_fraction > 0.0 && i > 0) {
                std::vector<std::filesystem::path> priors;
                for (std::size_t j = 0; j < i; ++j) {
                    priors.emplace_back(request_.stages[j].dataset);
                }
                const std::filesystem::path mixed = stage_dir / "rehearsal.jsonl";
                if (const std::string failure =
                        mix_datasets(dataset, priors, stage.rehearsal_fraction, mixed);
                    failure.empty()) {
                    dataset = mixed;
                } else {
                    say(index, stage.name, kStageTraining,
                        "[warn] rehearsal mixing failed: " + failure +
                            " -- training on the stage's own dataset");
                }
            }
            record.dataset = dataset.string();
            record.status = std::string{kStageTraining};
            if (const std::string failure = save(); !failure.empty()) {
                return finish(kPipelineFailed, failure);
            }

            TrainRequest train;
            train.model_dir = current_base;
            train.dataset = dataset;
            train.method = stage.method.empty() ? std::string{"lora"} : stage.method;
            train.iters = stage.iters;
            train.batch_size = stage.batch_size;
            train.num_layers = stage.num_layers;
            train.grad_checkpoint = stage.grad_checkpoint;
            train.mask_prompt = stage.mask_prompt;
            train.run_id = record.run_id;
            train.output_dir = stage_dir;

            RunManifest run;
            run.run_id = record.run_id;
            run.trainer = std::string{request_.trainer->name()};
            run.base_model = current_base.string();
            run.dataset = dataset.string();
            run.dataset_hash = dataset_digest(dataset);
            run.method = train.method;
            run.iters = train.iters;
            run.batch_size = train.batch_size;
            run.num_layers = train.num_layers;
            run.grad_checkpoint = train.grad_checkpoint;
            run.mask_prompt = train.mask_prompt;
            run.adapter_dir = (stage_dir / kAdapterDirName).string();
            run.status = std::string{kStatusRunning};
            run.started_at = rfc3339_now();
            run.pipeline_run_id = manifest_.pipeline_run_id;
            if (i > 0) {
                run.parent_run = stage_run_id(manifest_.pipeline_run_id, index - 1);
            }
            if (const std::string failure = write_manifest(stage_dir, run); !failure.empty()) {
                record.status = std::string{kStageFailed};
                return finish(kPipelineFailed, "stage '" + stage.name + "': " + failure);
            }

            say(index, stage.name, kStageTraining,
                "[train] stage " + std::to_string(index + 1) + "/" +
                    std::to_string(spec.stages.size()) + " '" + stage.name + "': " + run.trainer +
                    " " + run.method + " on " + current_base.filename().string() + ", dataset " +
                    dataset.filename().string());
            const TrainOutcome trained = request_.trainer->train(
                train,
                [&](const ProgressEvent& event) {
                    if (request_.on_progress) {
                        request_.on_progress(PipelineProgress{.stage_index = index,
                                                              .stage_name = stage.name,
                                                              .phase = std::string{kStageTraining},
                                                              .training = &event});
                    }
                },
                request_.cancellation);
            run.final_loss = trained.final_loss;
            run.iterations = trained.iterations;
            run.finished_at = rfc3339_now();
            if (trained.cancelled) {
                run.status = std::string{kStatusCancelled};
                run.error = trained.error;
                (void)write_manifest(stage_dir, run);
                return finish(kPipelineAborted, "cancelled", true);
            }
            if (!trained.ok) {
                run.status = std::string{kStatusFailed};
                run.error = trained.error;
                (void)write_manifest(stage_dir, run);
                record.status = std::string{kStageFailed};
                return finish(kPipelineFailed,
                              "stage '" + stage.name + "' training failed: " + trained.error);
            }
            run.status = std::string{kStatusComplete};
            record.adapter_dir = trained.adapter_dir.string();
            run.adapter_dir = record.adapter_dir;
            if (const std::string failure = write_manifest(stage_dir, run); !failure.empty()) {
                record.status = std::string{kStageFailed};
                return finish(kPipelineFailed, "stage '" + stage.name + "': " + failure);
            }

            // --- the cumulative gate --------------------------------------
            record.status = std::string{kStageEvaluating};
            if (const std::string failure = save(); !failure.empty()) {
                return finish(kPipelineFailed, failure);
            }
            const std::vector<EvalItem> items = merge_suites(request_.stages, i);
            say(index, stage.name, kStageEvaluating,
                "[eval] cumulative suite: " + std::to_string(i + 1) + " suite(s), " +
                    std::to_string(items.size()) + " item(s)" +
                    (request_.judge_backend.empty()
                         ? std::string{"; no judge: unmatched items skip"}
                         : ", judge " + request_.judge_backend));
            const std::unique_ptr<CandidateRunner> candidate =
                request_.trainer->candidate_runner(current_base, trained.adapter_dir);
            std::unique_ptr<CandidateRunner> baseline;
            EvalOptions options;
            options.cancellation = request_.cancellation;
            if (request_.judge) {
                baseline = request_.trainer->candidate_runner(current_base, {});
                options.baseline = baseline.get();
                options.judge = request_.judge;
                options.judge_backend = request_.judge_backend;
            }
            EvalRun eval = run_eval(items, *candidate, options);
            if (eval.cancelled) {
                return finish(kPipelineAborted, "cancelled", true);
            }
            if (!eval.ok) {
                record.status = std::string{kStageFailed};
                return finish(kPipelineFailed,
                              "stage '" + stage.name + "' eval failed: " + eval.error);
            }
            std::string label;
            for (std::size_t j = 0; j <= i; ++j) {
                label += (label.empty() ? "" : " + ") + request_.stages[j].suite_label;
            }
            eval.results.suite = label;
            run.eval = eval.results;
            (void)write_manifest(stage_dir, run);
            record.eval = eval.results;
            record.cumulative_score = eval.results.score;
            record.cumulative_passed = eval.results.passed;

            const bool gate_passed = eval.results.passed;
            const std::string tally = percent_of(eval.results.score) + " (" +
                                      std::to_string(eval.results.num_passed) + "/" +
                                      std::to_string(eval.results.total) + ")";
            if (gate_passed) {
                say(index, stage.name, kStageEvaluating,
                    "[gate] stage '" + stage.name + "' PASSED the cumulative eval -- " + tally);
                record.status = std::string{kStagePassed};
            } else {
                say(index, stage.name, kStageEvaluating,
                    "[gate] stage '" + stage.name + "' FAILED the cumulative eval -- " + tally);
                record.status = std::string{kStageFailed};
                if (request_.gate_mode == GateMode::Hard && !request_.continue_on_fail) {
                    return finish(kPipelineAborted,
                                  "pipeline aborted -- stage '" + stage.name +
                                      "' failed the cumulative eval gate (" + tally +
                                      "). Fix the data or the suite and 'apogee train pipeline "
                                      "resume " +
                                      manifest_.pipeline_run_id +
                                      "', or pass --continue-on-fail to go on regardless");
                }
                say(index, stage.name, kStageEvaluating,
                    request_.continue_on_fail
                        ? "[warn] the gate failed -- continuing (--continue-on-fail)"
                        : "[warn] the gate failed -- continuing (gate_mode is soft)");
            }

            // --- fuse for the next stage (never the last) ----------------
            if (i + 1 < spec.stages.size()) {
                record.status = std::string{kStageFusing};
                if (const std::string failure = save(); !failure.empty()) {
                    return finish(kPipelineFailed, failure);
                }
                const std::filesystem::path fused = stage_dir / kFusedDirName;
                say(index, stage.name, kStageFusing,
                    "[fuse] merging stage " + std::to_string(index + 1) +
                        "'s adapter into its base for stage " + std::to_string(index + 2));
                const std::string failure = request_.trainer->fuse(
                    current_base, trained.adapter_dir, fused,
                    [&](std::string_view text) {
                        say(index, stage.name, kStageFusing, "[fuse] " + std::string{text});
                    },
                    request_.cancellation);
                if (request_.cancellation.stop_requested()) {
                    return finish(kPipelineAborted, "cancelled", true);
                }
                if (!failure.empty()) {
                    record.status = std::string{kStageFailed};
                    return finish(kPipelineFailed,
                                  "stage '" + stage.name + "' fuse failed: " + failure);
                }
                record.fused_dir = fused.string();
                current_base = fused;
                record.status = std::string{gate_passed ? kStagePassed : kStageFailed};
            }
            if (const std::string failure = save(); !failure.empty()) {
                return finish(kPipelineFailed, failure);
            }
        }
        return finish(kPipelineComplete, {});
    }

private:
    [[nodiscard]] std::string save() {
        return write_pipeline_manifest(pipeline_dir_, manifest_);
    }

    void say(int index, const std::string& name, std::string_view phase, std::string message) {
        if (request_.on_progress) {
            request_.on_progress(PipelineProgress{.stage_index = index,
                                                  .stage_name = name,
                                                  .phase = std::string{phase},
                                                  .message = std::move(message)});
        }
    }

    [[nodiscard]] PipelineOutcome finish(std::string_view status, std::string error,
                                         bool cancelled = false) {
        manifest_.status = std::string{status};
        if (status == kPipelineComplete) {
            manifest_.completed_at = rfc3339_now();
        }
        PipelineOutcome outcome;
        if (const std::string failure = save(); !failure.empty() && error.empty()) {
            error = failure;
        }
        outcome.ok = status == kPipelineComplete && error.empty();
        outcome.cancelled = cancelled;
        outcome.error = std::move(error);
        outcome.manifest = manifest_;
        return outcome;
    }

    const PipelineRequest& request_;
    PipelineRunManifest manifest_;
    std::filesystem::path pipeline_dir_;
    std::filesystem::path runs_dir_;
};

}  // namespace

int PipelineRunManifest::last_passed() const noexcept {
    int last = -1;
    for (const PipelineStageRecord& stage : stages) {
        if (stage.status == kStagePassed) {
            last = stage.index;
        }
    }
    return last;
}

std::size_t PipelineRunManifest::first_unpassed() const noexcept {
    for (std::size_t i = 0; i < stages.size(); ++i) {
        if (stages[i].status != kStagePassed) {
            return i;
        }
    }
    return stages.size();
}

std::string PipelineRunManifest::promote_run_id() const {
    const int last = last_passed();
    return last < 0 ? std::string{} : stage_run_id(pipeline_run_id, last);
}

nlohmann::json pipeline_manifest_to_json(const PipelineRunManifest& manifest) {
    nlohmann::json stages = nlohmann::json::array();
    for (const PipelineStageRecord& stage : manifest.stages) {
        nlohmann::json row{{"index", stage.index},
                           {"name", stage.name},
                           {"run_id", stage.run_id},
                           {"base_model", stage.base_model},
                           {"dataset", stage.dataset},
                           {"adapter_dir", stage.adapter_dir},
                           {"fused_dir", stage.fused_dir},
                           {"status", stage.status},
                           {"cumulative_score", stage.cumulative_score},
                           {"cumulative_passed", stage.cumulative_passed}};
        if (stage.eval.has_value()) {
            row["eval_results"] = eval_results_to_json(*stage.eval);
        }
        stages.push_back(std::move(row));
    }
    nlohmann::json out{{"pipeline_run_id", manifest.pipeline_run_id},
                       {"spec_name", manifest.spec_name},
                       {"student", manifest.student},
                       {"base_model", manifest.base_model},
                       {"stages", std::move(stages)},
                       {"status", manifest.status},
                       {"started_at", manifest.started_at}};
    if (!manifest.completed_at.empty()) {
        out["completed_at"] = manifest.completed_at;
    }
    return out;
}

PipelineRunManifest pipeline_manifest_from_json(const nlohmann::json& json) {
    if (!json.is_object()) {
        throw std::runtime_error("pipeline manifest: expected an object");
    }
    PipelineRunManifest manifest;
    manifest.pipeline_run_id = json.value("pipeline_run_id", std::string{});
    if (manifest.pipeline_run_id.empty()) {
        throw std::runtime_error("pipeline manifest: no pipeline_run_id");
    }
    manifest.spec_name = json.value("spec_name", std::string{});
    manifest.student = json.value("student", std::string{});
    manifest.base_model = json.value("base_model", std::string{});
    manifest.status = json.value("status", std::string{kPipelineRunning});
    manifest.started_at = json.value("started_at", std::string{});
    manifest.completed_at = json.value("completed_at", std::string{});
    if (const auto stages = json.find("stages"); stages != json.end()) {
        if (!stages->is_array()) {
            throw std::runtime_error("pipeline manifest: stages must be an array");
        }
        for (const nlohmann::json& row : *stages) {
            if (!row.is_object()) {
                throw std::runtime_error("pipeline manifest: a stage must be an object");
            }
            PipelineStageRecord stage;
            stage.index = row.value("index", 0);
            stage.name = row.value("name", std::string{});
            stage.run_id = row.value("run_id", std::string{});
            stage.base_model = row.value("base_model", std::string{});
            stage.dataset = row.value("dataset", std::string{});
            stage.adapter_dir = row.value("adapter_dir", std::string{});
            stage.fused_dir = row.value("fused_dir", std::string{});
            stage.status = row.value("status", std::string{kStagePending});
            stage.cumulative_score = row.value("cumulative_score", 0.0);
            stage.cumulative_passed = row.value("cumulative_passed", false);
            if (const auto eval = row.find("eval_results"); eval != row.end() && !eval->is_null()) {
                stage.eval = eval_results_from_json(*eval);
            }
            manifest.stages.push_back(std::move(stage));
        }
    }
    return manifest;
}

std::filesystem::path pipeline_manifest_path(const std::filesystem::path& dir) {
    return dir / kManifestFileName;
}

std::string write_pipeline_manifest(const std::filesystem::path& dir,
                                    const PipelineRunManifest& manifest) {
    std::error_code code;
    std::filesystem::create_directories(dir, code);
    if (code) {
        return "could not create " + dir.string() + ": " + code.message();
    }
    try {
        harness::write_file_atomically(pipeline_manifest_path(dir),
                                       pipeline_manifest_to_json(manifest).dump(2) + "\n");
    } catch (const std::exception& e) {
        return e.what();
    }
    return {};
}

std::optional<PipelineRunManifest> read_pipeline_manifest(const std::filesystem::path& dir,
                                                          std::string& error) {
    error.clear();
    const std::filesystem::path path = pipeline_manifest_path(dir);
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        error = "no pipeline manifest at " + path.string();
        return std::nullopt;
    }
    const nlohmann::json json = nlohmann::json::parse(in, nullptr, false);
    if (json.is_discarded()) {
        error = path.string() + " is not JSON";
        return std::nullopt;
    }
    try {
        return pipeline_manifest_from_json(json);
    } catch (const std::runtime_error& e) {
        error = path.string() + ": " + e.what();
        return std::nullopt;
    }
}

std::string stage_run_id(std::string_view pipeline_run_id, int index) {
    return std::string{pipeline_run_id} + "-s" + std::to_string(index);
}

PipelineOutcome run_pipeline(const PipelineRequest& request) {
    PipelineOutcome outcome;
    if (request.spec == nullptr || request.spec->stages.empty()) {
        outcome.error = "the pipeline spec has no stages";
        return outcome;
    }
    if (request.trainer == nullptr) {
        outcome.error = "no trainer";
        return outcome;
    }
    if (request.stages.size() != request.spec->stages.size()) {
        outcome.error = "the spec has " + std::to_string(request.spec->stages.size()) +
                        " stage(s) but " + std::to_string(request.stages.size()) + " were resolved";
        return outcome;
    }
    if (request.pipeline_run_id.empty() || !valid_run_id(request.pipeline_run_id)) {
        outcome.error = "'" + request.pipeline_run_id + "' is not a pipeline run id";
        return outcome;
    }
    PipelineRunManifest manifest;
    manifest.pipeline_run_id = request.pipeline_run_id;
    manifest.spec_name = request.spec->name;
    manifest.student = request.spec->student;
    manifest.base_model = request.base_model.string();
    manifest.started_at = rfc3339_now();
    int index = 0;
    for (const harness::PipelineStageSpec& stage : request.spec->stages) {
        PipelineStageRecord record;
        record.index = index++;
        record.name = stage.name;
        record.dataset = stage.dataset;
        manifest.stages.push_back(std::move(record));
    }
    return StageRunner{request, std::move(manifest)}.run();
}

std::string resume_check(const PipelineRunManifest& existing, const harness::PipelineSpec& spec) {
    if (existing.complete()) {
        return "pipeline " + existing.pipeline_run_id + " is already complete";
    }
    if (spec.stages.size() != existing.stages.size()) {
        return "the spec has " + std::to_string(spec.stages.size()) + " stage(s) but pipeline " +
               existing.pipeline_run_id + " ran with " + std::to_string(existing.stages.size()) +
               " -- the stages that passed did so under a different plan, so this cannot "
               "resume safely; start a new run";
    }
    return {};
}

PipelineOutcome resume_pipeline(const PipelineRequest& request, PipelineRunManifest existing) {
    PipelineOutcome outcome;
    if (request.spec == nullptr || request.trainer == nullptr) {
        outcome.error = request.spec == nullptr ? "the pipeline spec has no stages" : "no trainer";
        return outcome;
    }
    if (const std::string refusal = resume_check(existing, *request.spec); !refusal.empty()) {
        outcome.error = refusal;
        outcome.manifest = std::move(existing);
        return outcome;
    }
    if (request.stages.size() != request.spec->stages.size()) {
        outcome.error = "the spec has " + std::to_string(request.spec->stages.size()) +
                        " stage(s) but " + std::to_string(request.stages.size()) + " were resolved";
        return outcome;
    }
    return StageRunner{request, std::move(existing)}.run();
}

std::vector<EvalItem> merge_suites(const std::vector<ResolvedStage>& stages, std::size_t through) {
    std::vector<EvalItem> items;
    for (std::size_t i = 0; i <= through && i < stages.size(); ++i) {
        items.insert(items.end(), stages[i].suite.begin(), stages[i].suite.end());
    }
    return items;
}

std::string mix_datasets(const std::filesystem::path& primary,
                         const std::vector<std::filesystem::path>& priors, double fraction,
                         const std::filesystem::path& out) {
    const std::optional<std::vector<std::string>> primary_lines = read_lines(primary);
    if (!primary_lines.has_value()) {
        return "cannot read " + primary.string();
    }
    std::vector<std::string> mixed = *primary_lines;
    for (const std::filesystem::path& prior : priors) {
        const std::optional<std::vector<std::string>> lines = read_lines(prior);
        if (!lines.has_value() || lines->empty()) {
            continue;
        }
        const auto n = static_cast<std::size_t>(static_cast<double>(lines->size()) * fraction);
        if (n == 0) {
            continue;
        }
        // A sample without replacement, seeded from the sizes alone so the
        // same inputs mix the same way on every run (the reference's
        // `len*1000+n` seed).
        std::vector<std::size_t> order(lines->size());
        for (std::size_t i = 0; i < order.size(); ++i) {
            order[i] = i;
        }
        std::mt19937_64 engine{static_cast<std::uint64_t>(lines->size() * 1000 + n)};
        std::shuffle(order.begin(), order.end(), engine);
        for (std::size_t i = 0; i < n && i < order.size(); ++i) {
            mixed.push_back((*lines)[order[i]]);
        }
    }
    std::mt19937_64 engine{static_cast<std::uint64_t>(mixed.size() * 7919)};
    std::shuffle(mixed.begin(), mixed.end(), engine);

    std::error_code code;
    std::filesystem::create_directories(out.parent_path(), code);
    if (code) {
        return "could not create " + out.parent_path().string() + ": " + code.message();
    }
    std::string text;
    for (const std::string& line : mixed) {
        text += line;
        text += '\n';
    }
    try {
        harness::write_file_atomically(out, text);
    } catch (const std::exception& e) {
        return e.what();
    }
    return {};
}

}  // namespace apogee::training
