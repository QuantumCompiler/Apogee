#include "training/regime.h"

#include <algorithm>
#include <stdexcept>
#include <system_error>

#include "training/datasets.h"

namespace apogee::training {

harness::RegimeSpec apply_regime_flags(harness::RegimeSpec base, const RegimeFlags& flags,
                                       const std::vector<std::string>& installed) {
    if (!flags.teacher.empty()) {
        base.teacher = flags.teacher;
    }
    if (!flags.student.empty()) {
        base.student = flags.student;
    }
    if (!flags.kits.empty()) {
        base.kits = flags.kits;
    } else if (flags.all_kits) {
        base.kits = installed;
        std::ranges::sort(base.kits);
    }
    if (flags.count > 0) {
        base.count = flags.count;
    }
    if (!flags.promote_as.empty()) {
        base.promote_as = flags.promote_as;
    }
    if (flags.iters > 0) {
        base.iters = flags.iters;
    }
    if (flags.temperature > 0.0) {
        base.temperature = flags.temperature;
    }
    if (base.name.empty()) {
        base.name = "regime";
    }
    return base;
}

std::string validate_regime_kits(const harness::RegimeSpec& spec,
                                 const std::filesystem::path& kits_dir) {
    if (spec.kits.empty()) {
        return "no kits -- pass --kit <name> (repeatable), --all-kits, or set regime.kits; "
               "'apogee datasets kits' lists them";
    }
    for (const std::string& name : spec.kits) {
        const std::optional<std::filesystem::path> path = find_kit(kits_dir, name);
        if (!path.has_value()) {
            return "kit '" + name + "': not installed under " + kits_dir.string() +
                   " and not a file";
        }
        try {
            const Kit kit = load_kit(*path);
            if (const std::string problem = validate_kit(kit); !problem.empty()) {
                return "kit '" + name + "': " + problem;
            }
        } catch (const std::exception& e) {
            return "kit '" + name + "': " + e.what();
        }
    }
    return {};
}

harness::PipelineStageSpec regime_stage(const harness::RegimeSpec& spec, const Kit& kit,
                                        const std::filesystem::path& dataset,
                                        const std::filesystem::path& suite) {
    harness::PipelineStageSpec stage;
    stage.name = kit.name;
    stage.dataset = dataset.string();
    stage.eval_suite = suite.string();
    stage.iters = spec.iters > 0 ? spec.iters : kit.train.iters;
    stage.batch_size = kit.train.batch_size;
    stage.num_layers = kit.train.num_layers;
    return stage;
}

RegimeOutcome run_regime(const RegimeRequest& request) {
    RegimeOutcome outcome;
    if (request.trainer == nullptr || !request.generate) {
        outcome.error = request.trainer == nullptr ? "no trainer" : "no teacher";
        return outcome;
    }
    if (const std::string problem = validate_regime_kits(request.spec, request.kits_dir);
        !problem.empty()) {
        outcome.error = problem;
        return outcome;
    }
    std::error_code code;
    std::filesystem::create_directories(request.work_dir, code);
    if (code) {
        outcome.error = "could not create " + request.work_dir.string() + ": " + code.message();
        return outcome;
    }
    auto say = [&request](const std::string& text) {
        if (request.on_message) {
            request.on_message(text);
        }
    };

    // --- phase 1: a dataset and a suite per kit ---------------------------
    harness::PipelineSpec pipeline;
    pipeline.name = request.spec.name;
    pipeline.student = request.spec.student;
    std::vector<ResolvedStage> resolved;
    const DatasetStore store{request.work_dir};
    for (const std::string& name : request.spec.kits) {
        if (request.cancellation.stop_requested()) {
            outcome.cancelled = true;
            outcome.error = "cancelled";
            return outcome;
        }
        const std::optional<std::filesystem::path> path = find_kit(request.kits_dir, name);
        const Kit kit = load_kit(*path);
        const int target = request.spec.count > 0 ? request.spec.count : kit.synth.count;
        say("[synth] kit '" + kit.name + "': " + std::to_string(target) +
            " example(s) from the teacher");
        SynthOptions options;
        options.count = request.spec.count;
        options.temperature = request.spec.temperature;
        options.max_tokens = request.max_tokens;
        options.parallel = request.parallel;
        options.cancellation = request.cancellation;
        options.on_progress = [&](int produced, int total) {
            say("[synth] " + kit.name + ": " + std::to_string(produced) + "/" +
                std::to_string(total));
        };
        const SynthResult synth = synthesize(kit, request.generate, options);
        if (synth.cancelled) {
            outcome.cancelled = true;
            outcome.error = "cancelled";
            return outcome;
        }
        if (synth.examples.empty()) {
            outcome.error = "kit '" + kit.name + "': the teacher produced nothing usable" +
                            (synth.error.empty() ? std::string{} : ": " + synth.error);
            return outcome;
        }
        std::filesystem::path dataset;
        if (const std::string failure =
                store.write(kit.name, example_lines(synth.examples), true, &dataset);
            !failure.empty()) {
            outcome.error = "kit '" + kit.name + "': " + failure;
            return outcome;
        }
        const std::filesystem::path suite = request.work_dir / (kit.name + ".eval.jsonl");
        try {
            write_eval_suite(kit, suite);
        } catch (const std::exception& e) {
            outcome.error = "kit '" + kit.name + "': writing its eval suite: " + e.what();
            return outcome;
        }
        say("[synth] kit '" + kit.name + "': " + std::to_string(synth.examples.size()) +
            " example(s) in " + std::to_string(synth.calls) + " call(s) -> " + dataset.string());
        outcome.kits.push_back(RegimeKitResult{.kit = kit.name,
                                               .dataset = dataset,
                                               .suite = suite,
                                               .examples = static_cast<int>(synth.examples.size()),
                                               .calls = synth.calls});
        pipeline.stages.push_back(regime_stage(request.spec, kit, dataset, suite));
        resolved.push_back(
            ResolvedStage{.dataset = dataset, .suite = kit.eval, .suite_label = "kit:" + kit.name});
    }

    // --- phase 2: the eval-gated pipeline ---------------------------------
    PipelineRequest run;
    run.spec = &pipeline;
    run.stages = std::move(resolved);
    run.pipeline_run_id = request.regime_run_id + std::string{kRegimePipelineSuffix};
    run.base_model = request.base_model;
    run.training_dir = request.training_dir;
    run.trainer = request.trainer;
    run.judge_backend = request.judge_backend;
    run.judge = request.judge;
    run.gate_mode = request.gate_mode;
    run.on_progress = request.on_progress;
    run.cancellation = request.cancellation;
    say("[regime] pipeline " + run.pipeline_run_id + ": " + std::to_string(pipeline.stages.size()) +
        " stage(s)");
    outcome.pipeline = run_pipeline(run);
    outcome.cancelled = outcome.pipeline->cancelled;
    outcome.promote_run_id = outcome.pipeline->manifest.promote_run_id();
    if (!outcome.pipeline->ok) {
        outcome.error = outcome.pipeline->error;
        return outcome;
    }
    outcome.ok = true;
    return outcome;
}

}  // namespace apogee::training
