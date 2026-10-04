#include "training/script_trainer.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace apogee::training {
namespace {

class ScriptCandidateRunner final : public CandidateRunner {
public:
    ScriptCandidateRunner(ScriptTrainerSpec spec, std::filesystem::path base,
                          std::filesystem::path adapter)
        : spec_{std::move(spec)}, base_{std::move(base)}, adapter_{std::move(adapter)} {}

    [[nodiscard]] CandidateReply run(std::string_view prompt,
                                     const harness::CancellationToken& cancellation) override {
        CandidateReply reply;
        bool have_text = false;
        const ScriptOutcome outcome = run_script(
            script_request(spec_, infer_arguments(base_, adapter_, prompt, kCandidateMaxTokens)),
            [&reply, &have_text](const ScriptEvent& event) {
                if (event.kind == ScriptEvent::Kind::Record && event.record.contains("text") &&
                    event.record["text"].is_string()) {
                    reply.text = event.record["text"].get<std::string>();
                    have_text = true;
                }
            },
            cancellation, spec_.spawn);
        if (!outcome.ok) {
            reply.error = outcome.cancelled ? "cancelled" : outcome.describe();
            return reply;
        }
        if (!have_text) {
            reply.error =
                spec_.script.filename().string() + " --mode infer produced no {\"text\"} record";
            return reply;
        }
        reply.ok = true;
        return reply;
    }

private:
    ScriptTrainerSpec spec_;
    std::filesystem::path base_;
    std::filesystem::path adapter_;
};

}  // namespace

ScriptRequest script_request(const ScriptTrainerSpec& spec, std::vector<std::string> arguments) {
    ScriptRequest request;
    request.interpreter = spec.interpreter;
    request.script = spec.script;
    request.arguments = std::move(arguments);
    // The seeded tree must stay byte-identical to the shipped copy after a
    // run, or the doctor's drift row reads a `__pycache__` as an edit.
    request.environment.emplace_back("PYTHONDONTWRITEBYTECODE", "1");
    return request;
}

std::vector<std::string> train_arguments(const TrainRequest& request) {
    std::vector<std::string> args{"--mode",       "train",
                                  "--model",      request.model_dir.string(),
                                  "--dataset",    request.dataset.string(),
                                  "--method",     request.method,
                                  "--output-dir", (request.output_dir / kAdapterDirName).string(),
                                  "--data-dir",   (request.output_dir / kDataDirName).string()};
    if (request.iters > 0) {
        args.insert(args.end(), {"--iters", std::to_string(request.iters)});
    }
    if (request.batch_size > 0) {
        args.insert(args.end(), {"--batch-size", std::to_string(request.batch_size)});
    }
    if (request.num_layers > 0) {
        args.insert(args.end(), {"--num-layers", std::to_string(request.num_layers)});
    }
    if (request.grad_checkpoint) {
        args.emplace_back("--grad-checkpoint");
    }
    if (request.mask_prompt) {
        args.emplace_back("--mask-prompt");
    }
    return args;
}

std::vector<std::string> fuse_arguments(const std::filesystem::path& base,
                                        const std::filesystem::path& adapter,
                                        const std::filesystem::path& out) {
    return {"--mode",         "fuse",           "--model",      base.string(),
            "--adapter-path", adapter.string(), "--output-dir", out.string()};
}

std::vector<std::string> infer_arguments(const std::filesystem::path& base,
                                         const std::filesystem::path& adapter,
                                         std::string_view prompt, int max_tokens) {
    return {"--mode",         "infer",
            "--model",        base.string(),
            "--adapter-path", adapter.string(),
            "--prompt",       std::string{prompt},
            "--max-tokens",   std::to_string(max_tokens)};
}

ScriptTrainer::ScriptTrainer(ScriptTrainerSpec spec) : spec_{std::move(spec)} {}

std::string_view ScriptTrainer::name() const noexcept {
    return spec_.name;
}

TrainerCapabilities ScriptTrainer::capabilities() const {
    return spec_.capabilities;
}

TrainOutcome ScriptTrainer::train(const TrainRequest& request, const ProgressSink& on_progress,
                                  const harness::CancellationToken& cancellation) {
    TrainOutcome outcome;
    outcome.adapter_dir = request.output_dir / kAdapterDirName;
    const ScriptOutcome run = run_script(
        script_request(spec_, train_arguments(request)),
        [&outcome, &on_progress](const ScriptEvent& event) {
            // A message or error arrives already classified; a record's raw
            // line is where the iteration's numbers are read from.
            ProgressEvent delivered;
            if (event.kind == ScriptEvent::Kind::Record) {
                delivered = parse_progress_line(event.text);
            } else {
                delivered.kind = event.kind == ScriptEvent::Kind::Error
                                     ? ProgressEvent::Kind::Error
                                     : ProgressEvent::Kind::Message;
                delivered.text = event.text;
            }
            if (delivered.kind == ProgressEvent::Kind::Iteration) {
                outcome.final_loss = delivered.loss;
                outcome.iterations = delivered.iteration;
            }
            if (on_progress) {
                on_progress(delivered);
            }
        },
        cancellation, spec_.spawn);
    outcome.ok = run.ok;
    outcome.cancelled = run.cancelled;
    if (!run.ok) {
        outcome.error = run.cancelled ? "cancelled" : run.describe();
    }
    return outcome;
}

std::string ScriptTrainer::fuse(const std::filesystem::path& base,
                                const std::filesystem::path& adapter,
                                const std::filesystem::path& out, const MessageSink& on_message,
                                const harness::CancellationToken& cancellation) {
    const ScriptOutcome run = run_script(
        script_request(spec_, fuse_arguments(base, adapter, out)),
        [&on_message](const ScriptEvent& event) {
            if (on_message && event.kind == ScriptEvent::Kind::Message) {
                on_message(event.text);
            }
        },
        cancellation, spec_.spawn);
    if (run.ok) {
        return {};
    }
    return run.cancelled ? std::string{"cancelled"} : run.describe();
}

std::unique_ptr<CandidateRunner> ScriptTrainer::candidate_runner(
    const std::filesystem::path& base, const std::filesystem::path& adapter) {
    return std::make_unique<ScriptCandidateRunner>(spec_, base, adapter);
}

}  // namespace apogee::training
