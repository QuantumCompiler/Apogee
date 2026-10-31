#include "cli/train_stream.h"

#include <cstddef>
#include <optional>

#include "training/trainer.h"

namespace apogee::commands {

void PipelineRelay::manifest(const training::PipelineRunManifest& manifest) {
    const std::size_t count = manifest.stages.size();
    // A stage's status is never empty, so the first write says every one.
    said_.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const training::PipelineStageRecord& stage = manifest.stages[i];
        if (said_[i] == stage.status) {
            continue;
        }
        said_[i] = stage.status;
        events_.emit_stage(
            i + 1, count, stage.name, stage.status, manifest.pipeline_run_id, stage.run_id,
            stage.eval.has_value() ? std::optional<double>{stage.cumulative_score} : std::nullopt);
    }
}

void PipelineRelay::progress(const training::PipelineProgress& progress) {
    const std::size_t stage =
        progress.stage_index >= 0 ? static_cast<std::size_t>(progress.stage_index) + 1 : 0;
    if (progress.training != nullptr) {
        const training::ProgressEvent& event = *progress.training;
        switch (event.kind) {
            case training::ProgressEvent::Kind::Iteration:
                events_.emit_iteration(event.iteration, event.total_iters, event.loss, event.lr,
                                       event.throughput, stage);
                return;
            case training::ProgressEvent::Kind::Message:
                events_.on_notice("[train] " + event.text);
                return;
            case training::ProgressEvent::Kind::Error:
                events_.on_notice("[train] error: " + event.text);
                return;
        }
        return;
    }
    if (!progress.message.empty()) {
        events_.on_notice(progress.message);
    }
}

}  // namespace apogee::commands
