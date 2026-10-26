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

    /// The same reads over a config hand-edited into invalid JSON (a missing
    /// comma): `train regime list` and `models list --all` refuse it in their
    /// recorded words; the rest read no config.
    static let badConfig = PreviewRunner(
        reads: runner.reads,
        refusals: [
            (["train", "regime", "list"], TrainingDocuments.regimesBadConfig),
            (["models", "list"], TrainingDocuments.modelsBadConfig),
        ])

    /// `train status` and `datasets list` past the reader's limit -- nothing
    /// the probes did made them refuse, so this is the reader's own outcome.
    static let unfinished = PreviewRunner(reads: runner.reads, unfinished: [["train", "status"], ["datasets", "list"]])

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

    /// The adapter sandbox's reads (GUI 4i): a run that passed its gate on
    /// the mock trainer, `based` holding an adapter version, `q4` one gated on
    /// its quantized base, `tuned` a merged one; the datasets and regimes as
    /// above.
    static let adapters = PreviewRunner(reads: [
        (["train", "status"], TrainingDocuments.trainStatusPassed),
        (["train", "versions"], TrainingDocuments.trainVersionsAdapters),
        (["train", "regime", "list"], TrainingDocuments.trainRegimes),
        (["datasets", "kits"], TrainingDocuments.datasetsKits),
        (["datasets", "list"], TrainingDocuments.datasetsList),
        (["models", "list"], TrainingDocuments.modelsListAdapters),
    ])

    /// The run that passed its gate, as that `train status` lists it.
    static var passed: TrainingRun {
        TrainingStatus(document: ReadDocument(text: TrainingDocuments.trainStatusPassed)!).recent.first!
    }

    /// A job's end as the CLI left it: exit 1, `stderr` its words.
    static func refusal(_ stderr: String) -> [JobUpdate] {
        var tail = BoundedTail()
        tail.append(Data((stderr + "\n").utf8))
        return [.ended(JobEnd(exit: ChildExit(reason: .exited, status: 1), stderrTail: tail, interrupted: false))]
    }

    /// The recorded pull's lines on its stdout -- the first `count`, or all
    /// of them and its end.
    static func pulling(_ count: Int? = nil) -> [JobUpdate] {
        let lines = TrainingDocuments.pullDone.split(separator: "\n", omittingEmptySubsequences: false).map(String.init)
        let said = lines.prefix(count ?? lines.count).map { JobUpdate.said($0) }
        guard count == nil else { return said }
        return said + [.ended(JobEnd(exit: ChildExit(reason: .exited, status: 0), stderrTail: BoundedTail(), interrupted: false))]
    }

    /// The adapter promote sheet for the passed run, onto `based`.
    static func promotingAdapter(_ training: TrainingViewModel) {
        training.newPromotion(of: passed)
        training.promoteForm.adapter = true
        training.promoteForm.backend = "based"
    }

    /// Waits for what a finished preview job leaves behind.
    static func settled(_ condition: @MainActor () -> Bool) async {
        for _ in 0..<100 where !condition() {
            try? await Task.sleep(for: .milliseconds(10))
        }
    }

    static func training(said: [JobUpdate] = [], finishes: Bool = false, runner: PreviewRunner = TrainingPreview.runner) -> TrainingViewModel {
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
