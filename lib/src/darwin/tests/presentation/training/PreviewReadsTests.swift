import Foundation
import Testing

@testable import PresentationTraining

/// The previews' recorded reads answer the lines the view-model builds (GUI
/// M23): a preview's document is keyed by a prefix of the line, so a line
/// that moves -- a run's id or a spec's name after `--` -- would otherwise
/// leave the preview drawing an empty state, or a shorter prefix's document,
/// and fail nothing. Each preview home is driven as its previews drive it.
@Suite("Training preview reads", .timeLimit(.minutes(1)))
@MainActor
struct PreviewReadsTests {
    @Test("every read the Training previews ask is answered, and every prefix holding a name after -- is one a line reaches")
    func previewReads() async {
        let log = PreviewReadLog()
        await PreviewReadLog.$current.withValue(log) {
            for (runner, runs) in [
                (TrainingPreview.pipelines, [TrainingPreview.completeID]),
                (TrainingPreview.aborted, [TrainingPreview.abortedID]),
                (TrainingPreview.resumes, [TrainingPreview.namedID, TrainingPreview.specFileID, TrainingPreview.keptFailedID, TrainingPreview.cyclePassID]),
                (TrainingPreview.resumesHeld, [TrainingPreview.namedID]),
            ] {
                let training = TrainingPreview.training(runner: runner)
                await training.connect(binary: previewBinary)
                for id in runs { await training.openPipelineRun(id) }
            }
            let specs = TrainingPreview.training(runner: TrainingPreview.specsListed)
            await specs.connect(binary: previewBinary)
            for name in ["skills", "nightly"] { await specs.openSpec(name) }
        }
        #expect(log.unanswered.isEmpty, "reads no recorded document answers: \(log.unanswered)")
        #expect(log.unreached.isEmpty, "prefixes no line reaches: \(log.unreached)")
    }
}
