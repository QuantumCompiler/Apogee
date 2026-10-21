#if DEBUG
import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess

/// The Training place's view-model for its previews, over the recorded
/// documents (ADR 0004): nothing starts the binary, and nothing trains.
@MainActor
enum TrainingPreview {
    /// `train versions` before any promotion, as
    /// tests/presentation/training/TrainingViewModelTests.swift holds it.
    static let versions = #"{"object":"list","data":[]}"#

    static let runner = PreviewRunner(reads: [
        (["train", "status"], TrainingDocuments.trainStatus),
        (["train", "versions"], versions),
        (["train", "regime", "list"], TrainingDocuments.trainRegimes),
        (["datasets", "kits"], TrainingDocuments.datasetsKits),
        (["datasets", "list"], TrainingDocuments.datasetsList),
        (["models", "list"], TrainingDocuments.modelsListAll),
    ])

    /// The recorded run, which failed its gate.
    static let runID = "20261021-051826"

    /// The recorded run, as `train status` lists it.
    static var run: TrainingRun {
        TrainingStatus(document: ReadDocument(text: TrainingDocuments.trainStatus)!).recent.first!
    }

    /// A run in progress: the trainer's lines through its second iteration,
    /// each the busy line in its turn, as the jobs core samples stderr.
    static var running: [JobUpdate] {
        TrainingDocuments.trainRun.split(separator: "\n").prefix(4).map { .busy(String($0)) }
    }

    /// A promotion the gate refused: exit 1, the CLI's last word on stderr,
    /// as TrainingViewModelTests ends it.
    static var refused: [JobUpdate] {
        var tail = BoundedTail()
        tail.append(Data("apogee train: run failed its gate\n".utf8))
        return [.ended(JobEnd(exit: ChildExit(reason: .exited, status: 1), stderrTail: tail, interrupted: false))]
    }

    static func training(said: [JobUpdate] = [], finishes: Bool = false) -> TrainingViewModel {
        TrainingViewModel(runner: runner, starter: PreviewJobs(said: said, finishes: finishes))
    }

    /// Every document read, and the recorded run chosen.
    static func read(_ training: TrainingViewModel) async {
        await training.connect(binary: previewBinary)
        training.selectedRun = runID
    }

    /// The promotion sheet for the recorded run, as the tests fill it.
    static func promoting(_ training: TrainingViewModel) {
        training.newPromotion(of: run)
        training.promoteForm.backend = "helper"
    }
}
#endif
