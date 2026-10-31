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

    /// The adapter sandbox's reads over 3a's registered kinds instead of its
    /// own entries: a chat, a vision and an embedding set, each an `mlx`
    /// entry, as `models list` printed them (the GUI's M4). It folds
    /// nothing, so `models list --all` prints the same bytes.
    static let kinds = PreviewRunner(
        reads: adapters.reads.filter { $0.0 != ["models", "list"] } + [(["models", "list"], TrainingDocuments.kindsRegistered)])

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

    static func training(
        said: [JobUpdate] = [], finishes: Bool = false, runner: PreviewRunner = TrainingPreview.runner, vocabulary: String? = nil
    ) -> TrainingViewModel {
        let training = TrainingViewModel(runner: runner, starter: PreviewJobs(said: said, finishes: finishes))
        training.vocabulary = { vocabulary }
        return training
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

    // MARK: - Pipelines and the cycle (GUI 4j)

    /// The completion that names the named pipelines.
    static let complete = ["__complete", "train", "pipeline", "run", "--pipeline"]

    /// The pipelines sandbox's reads (the CLI's 55h, the mock trainer):
    /// `nightly` and `skills` named, four runs -- the last stopped at its
    /// second stage -- and the first's manifest, resumed to complete.
    static let pipelines = PreviewRunner(
        reads: runner.reads + [
            (["train", "pipeline", "status"], TrainingDocuments.pipelineStatusList),
            (["train", "pipeline", "status", completeID], TrainingDocuments.pipelineStatusComplete),
        ],
        commands: [(complete, TrainingDocuments.pipelineComplete)])

    /// A store with no pipeline named and none run: the completion says
    /// nothing, the list is empty.
    static let noPipelines = PreviewRunner(
        reads: runner.reads + [(["train", "pipeline", "status"], TrainingDocuments.pipelineStatusNone)],
        commands: [(complete, TrainingDocuments.pipelineCompleteNone)])

    /// The same sandbox before anything ran: the two pipelines named, no
    /// run, no pipeline run, and the cycle with no history -- `train
    /// status`'s `cycle` null.
    static let noRuns = PreviewRunner(
        reads: reads(status: TrainingDocuments.trainStatusNone) + [
            (["train", "pipeline", "status"], TrainingDocuments.pipelineStatusNone)
        ],
        commands: [(complete, TrainingDocuments.pipelineComplete)])

    /// `skills` aborted at its second stage by the cumulative gate: the list
    /// and its manifest.
    static let aborted = PreviewRunner(
        reads: runner.reads + [
            (["train", "pipeline", "status"], TrainingDocuments.pipelineStatusAbortedList),
            (["train", "pipeline", "status", abortedID], TrainingDocuments.pipelineStatusAborted),
        ],
        commands: [(complete, TrainingDocuments.pipelineComplete)])

    /// The cycle after its passes: idle, its anchor set.
    static let cycleIdle = cycle(TrainingDocuments.trainStatusCycle, TrainingDocuments.cycleStatusPassed)
    /// Halted by its breaker after a failed pass.
    static let cycleHalted = cycle(TrainingDocuments.trainStatusCycleHalted, TrainingDocuments.cycleStatusHalted)
    /// A pass holding the lock -- read while one ran.
    static let cycleActive = cycle(TrainingDocuments.trainStatusCycleActive, TrainingDocuments.cycleStatusActive)

    static func cycle(_ status: String, _ history: String) -> PreviewRunner {
        PreviewRunner(
            reads: reads(status: status) + pipelines.reads.filter { $0.0.starts(with: ["train", "pipeline"]) } + [
                (["train", "cycle", "status"], history)
            ],
            commands: pipelines.commands)
    }

    /// The place's reads with `status` as `train status`'s answer.
    static func reads(status: String) -> [([String], String)] {
        runner.reads.filter { $0.0 != ["train", "status"] } + [(["train", "status"], status)]
    }

    /// The manifest the aborted run's page reads.
    static var abortedRun: PipelineRun {
        PipelineRun(document: ReadDocument(text: TrainingDocuments.pipelineStatusAborted)!)!
    }

    static var abortedID: String { abortedRun.id }

    static var completeID: String {
        PipelineRun(document: ReadDocument(text: TrainingDocuments.pipelineStatusComplete)!)!.id
    }

    /// The recorded `skills` run, through its second stage's second
    /// iteration: the first stage passed and fused, the second training.
    static var pipelineRunning: [JobUpdate] {
        streamed(TrainingDocuments.pipelineRun) { $0.contains(#""iteration":2,"#) && $0.contains(#""stage":2,"#) }
    }

    /// The recorded passing cycle pass, through its stage's second
    /// iteration.
    static var cycleRunning: [JobUpdate] {
        streamed(TrainingDocuments.cycleRunPassed) { $0.contains(#""iteration":2,"#) }
    }

    /// A recorded stream's lines as the jobs core reads them, through the
    /// first line `through` picks.
    static func streamed(_ stream: String, through: (String) -> Bool) -> [JobUpdate] {
        let lines = stream.split(separator: "\n").map(String.init)
        let end = lines.firstIndex(where: through) ?? lines.count - 1
        return lines[...end].map { .event(JobEvent.decode(line: $0)) }
    }

    // MARK: - What the CLI's M30 states (GUI M14)

    /// The branch binary's vocabulary: a halt taken during a pass is kept,
    /// and the pull streams.
    static let m30 = "2026-10-26"

    /// The pipelines sandbox's reads with the named pipelines listed and
    /// shown, as `train pipeline list | show` state them.
    static let specsListed = PreviewRunner(
        reads: pipelines.reads + [
            (["train", "pipeline", "list"], TrainingDocuments.pipelineList),
            (["train", "pipeline", "show", "skills"], TrainingDocuments.pipelineShowSkills),
            (["train", "pipeline", "show", "nightly"], TrainingDocuments.pipelineShowNightly),
        ],
        commands: pipelines.commands)

    /// Another's pass holding the cycle -- read while it was held on its lock.
    static let cycleHeld = cycle(TrainingDocuments.trainStatusCycleHeld, TrainingDocuments.cycleStatusHeld)
    /// The same pass, halted while it runs: `train cycle halt`'s answer then.
    static let cycleHeldHalted = PreviewRunner(
        reads: cycle(TrainingDocuments.trainStatusCycleHeldHalted, TrainingDocuments.cycleStatusHeldHalted).reads,
        commands: pipelines.commands + [(["train", "cycle", "halt"], TrainingDocuments.cycleHaltDuringPass)])
    /// The pass recorded, the halt kept.
    static let cycleHaltKept = cycle(TrainingDocuments.trainStatusCycleHaltKept, TrainingDocuments.cycleStatusHaltKept)

    /// The recorded pass halted while it ran, through the notice that says
    /// the halt is kept -- or all of it, and its end, exit 0.
    static func passHalted(whole: Bool = false) -> [JobUpdate] {
        guard whole else { return streamed(TrainingDocuments.cycleRunHalted) { $0.contains("[cycle] halted while this pass ran") } }
        return streamed(TrainingDocuments.cycleRunHalted) { _ in false } + [ended(0)]
    }

    /// The pull's stream (hand-made: a pull that lands needs the network)
    /// through its first file's 49152 bytes -- or all of it, and its end.
    static func pullStreamed(whole: Bool = false) -> [JobUpdate] {
        guard whole else { return streamed(TrainingDocuments.pullStream) { $0.contains(#""done":49152"#) } }
        return streamed(TrainingDocuments.pullStream) { _ in false } + [ended(0)]
    }

    /// A run's end as the binary left it: exit `status`.
    static func ended(_ status: Int32) -> JobUpdate {
        .ended(JobEnd(exit: ChildExit(reason: .exited, status: status), stderrTail: BoundedTail(), interrupted: false))
    }

    /// Updates folded as the place folds them.
    static func folded(_ updates: [JobUpdate]) -> JobProgress {
        var progress = JobProgress()
        updates.forEach { progress.apply($0) }
        return progress
    }
}
#endif
