import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationTraining

/// A recorded stream from `tests/business/jobs/Fixtures/`, a line an element.
func jobStream(_ name: String) -> [String] {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
        .appendingPathComponent("../../business/jobs/Fixtures/\(name).jsonl")
    return ((try? String(contentsOf: url, encoding: .utf8)) ?? "").split(separator: "\n").map(String.init)
}

/// GUI 4j: the Training place sees the named pipelines and the unattended
/// cycle -- over the documents and streams the CLI's 55h gives, recorded
/// from the branch's binary with the mock trainer (`train-pipeline-*`,
/// `train-cycle-*`, `train-status-*`). Nothing here trains.
@Suite("Pipelines and the cycle", .timeLimit(.minutes(1)))
@MainActor
struct PipelineCycleTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let complete = ["__complete", "train", "pipeline", "run", "--pipeline", ""]
    static let abortedID = PipelineRun(document: ReadDocument(text: fixture("train-pipeline-status-aborted"))!)!.id

    /// The place over the sandbox's reads: `train status` as `status` says,
    /// the named pipelines as `named` completes them, the runs as `runs`
    /// lists them, and the cycle's history when `cycle` names one.
    func training(
        status: String = "train-status-none", named: String = "train-pipeline-complete", runs: String = "train-pipeline-status-none",
        cycle: String? = nil, jobs: [ScriptedJob] = []
    ) async -> (TrainingViewModel, ScriptedRunner, ScriptedJobs) {
        let runner = ScriptedRunner()
        runner.reading(["train", "status"], fixture(status))
        runner.reading(["train", "versions"], #"{"object":"list","data":[]}"#)
        runner.reading(["train", "regime", "list"], fixture("train-regimes"))
        runner.reading(["datasets", "kits"], fixture("datasets-kits"))
        runner.reading(["datasets", "list"], fixture("datasets-list"))
        runner.reading(["models", "list"], fixture("models-stored-list-all"))
        runner.reading(["train", "pipeline", "status"], fixture(runs))
        runner.reading(["train", "pipeline", "status", Self.abortedID], fixture("train-pipeline-status-aborted"))
        if let cycle { runner.reading(["train", "cycle", "status"], fixture(cycle)) }
        runner.answering(Self.complete, .done(stdout: fixture(named, "txt")))
        let starter = ScriptedJobs(jobs)
        let model = TrainingViewModel(runner: runner, starter: starter)
        await model.connect(binary: Self.binary)
        return (model, runner, starter)
    }

    /// Says `lines` on `job` as its stream, one by one.
    func say(_ lines: some Sequence<String>, on job: ScriptedJob) {
        for line in lines { job.say(.event(JobEvent.decode(line: line))) }
    }

    func words(_ model: TrainingViewModel) -> [String] {
        model.pipelineStages.map(TrainingViewModel.words)
    }

    // MARK: - Pipelines

    @Test("the named pipelines are the binary's completion of --pipeline, the runs train pipeline status's, newest first")
    func pipelinesListed() async {
        let (model, runner, _) = await training(runs: "train-pipeline-status-list")
        #expect(model.pipelineSpecs == ["nightly", "skills"])
        #expect(runner.calls.contains("run " + Self.complete.joined(separator: "|")))
        #expect(runner.calls.contains("read train|pipeline|status"))
        #expect(model.pipelineRuns.map(\.status) == ["aborted", "complete", "complete", "complete"])
        #expect(TrainingViewModel.words(model.pipelineRuns[0]) == "skills · aborted · 1 of 2 stages passed")
        #expect(model.specsText == nil && model.pipelinesText == nil)
    }

    @Test("with none named and none run, the place says what a named pipeline is; the list is empty, not failed")
    func noPipelines() async {
        let (model, _, _) = await training(named: "train-pipeline-complete-none")
        #expect(model.pipelineSpecs.isEmpty && model.pipelineRuns.isEmpty)
        #expect(model.specsText?.contains("training.pipelines entry in your config") == true)
        #expect(model.pipelinesText == nil)
    }

    @Test("a run opened reads its manifest: each stage's word, its run and its gate's score; Resume from the first that has not passed")
    func runOpened() async throws {
        let (model, runner, _) = await training(runs: "train-pipeline-status-aborted-list")
        #expect(model.openPipeline == nil)
        await model.openPipelineRun(Self.abortedID)
        #expect(runner.calls.contains("read train|pipeline|status|\(Self.abortedID)"))
        let run = try #require(model.openPipeline)
        #expect(run.status == "aborted")
        #expect(words(model) == ["passed · cumulative 100%", "failed · cumulative 50%"])
        #expect(model.pipelineStages.map(\.runID) == ["\(Self.abortedID)-s0", "\(Self.abortedID)-s1"])
        #expect(TrainingViewModel.resumeWords(run) == "Resume from stage 2, refusals")
        #expect(model.canResume(run))
        let done = try #require(PipelineRun(document: ReadDocument(text: fixture("train-pipeline-status-complete"))!))
        #expect(TrainingViewModel.resumeWords(done) == nil && !model.canResume(done), "a complete run offers no resume")
    }

    @Test("Run asks first in the TUI's words, then starts train pipeline run --pipeline <name> with its stream, supervised")
    func runAsksFirst() async {
        let job = ScriptedJob()
        let (model, _, starter) = await training(jobs: [job])
        model.pending = .runPipeline("skills")
        #expect(model.pendingTitle == "Run the pipeline skills?")
        #expect(model.pendingConsequence?.hasPrefix("Each stage is a training run, held to the cumulative gate") == true)
        #expect(model.pendingAction == "Start" && model.pending?.isDestructive == false)
        #expect(model.pending?.arguments == ["train", "pipeline", "run", "--pipeline", "skills"])
        #expect(starter.starts.isEmpty, "nothing trains before the person's word")
        await model.confirm()
        #expect(starter.starts == [["train", "pipeline", "run", "--pipeline", "skills", "--output-format", "stream-json"]])
        #expect(starter.streamed == [true])
        #expect(model.isRunning && model.running == "Running skills" && model.live == .pipeline && model.showsLivePipeline)
        job.end()
        #expect(await eventually { !model.isRunning && model.live == nil })
    }

    @Test("a run is watched stage by stage from its stream, then read back from its manifest; its gate's refusal is said in the CLI's words")
    func watchedStageByStage() async throws {
        let job = ScriptedJob()
        let (model, runner, _) = await training(jobs: [job])
        model.pending = .runPipeline("skills")
        await model.confirm()
        let lines = jobStream("train-pipeline-run")
        func through(_ marker: (String) -> Bool) -> ArraySlice<String> {
            let end = lines.firstIndex(where: marker)!
            return lines[...end]
        }
        say(through { $0.contains(#""index":2"#) && $0.contains(#""state":"pending""#) }, on: job)
        #expect(await eventually { words(model) == ["pending", "pending"] })
        #expect(model.livePipelineID == Self.abortedID)
        say(lines.dropFirst(4).prefix { !$0.contains(#""iteration":3"#) }, on: job)
        #expect(await eventually { words(model) == ["training · iter 2/4 · loss 0.6200 · 10.0 it/s", "pending"] })
        say(lines.drop { !$0.contains(#""iteration":3"#) }.prefix { !$0.contains(#""state":"fusing""#) }, on: job)
        #expect(await eventually { words(model).first == "evaluating" }, "past its iterations, the stage evaluates")
        say(lines.drop { !$0.contains(#""state":"fusing""#) }.prefix { !$0.contains(#""state":"failed""#) }, on: job)
        #expect(await eventually { words(model) == ["passed · cumulative 100%", "evaluating"] })
        // The read after: the list and the manifest as the binary left them.
        runner.reading(["train", "pipeline", "status"], fixture("train-pipeline-status-aborted-list"))
        runner.reading(["train", "pipeline", "status", Self.abortedID], fixture("train-pipeline-status-aborted"))
        say(lines.drop { !$0.contains(#""state":"failed""#) }, on: job)
        #expect(await eventually { words(model) == ["passed · cumulative 100%", "failed · cumulative 50%"] })
        job.end(status: 2)
        // The run's end is said once its read-back is in.
        #expect(await eventually { !model.isRunning && model.failure != nil })
        #expect(model.selectedPipelineRun == Self.abortedID, "the page stays on the run, now its manifest")
        #expect(!model.showsLivePipeline && model.openPipeline?.status == "aborted")
        #expect(words(model) == ["passed · cumulative 100%", "failed · cumulative 50%"])
        #expect(model.failure?.hasPrefix("pipeline aborted -- stage 'refusals' failed the cumulative eval gate") == true)
        #expect(model.pipelineRuns.first?.isResumable == true)
    }

    @Test("Resume asks first, naming its stage, then starts train pipeline resume <id> with its stream; a resumed stage's old score is not shown as its gate's")
    func resumed() async throws {
        let job = ScriptedJob()
        let (model, runner, starter) = await training(runs: "train-pipeline-status-aborted-list", jobs: [job])
        var said: [String] = []
        model.say = { said.append($0) }
        await model.openPipelineRun(Self.abortedID)
        model.pending = .resumePipeline(Self.abortedID)
        #expect(model.pendingTitle == "Resume \(Self.abortedID) from stage 2, refusals?")
        #expect(model.pendingAction == "Resume")
        #expect(starter.starts.isEmpty)
        await model.confirm()
        #expect(starter.starts == [["train", "pipeline", "resume", Self.abortedID, "--output-format", "stream-json"]])
        #expect(starter.streamed == [true] && model.showsLivePipeline)
        let lines = jobStream("train-pipeline-resume")
        say(lines.prefix(4), on: job)
        #expect(await eventually { words(model) == ["passed · cumulative 100%", "failed · cumulative 50%"] }, "its first write: what is there")
        say(lines.dropFirst(4).prefix(1), on: job)
        #expect(await eventually { words(model) == ["passed · cumulative 100%", "training"] }, "the stage trains again: its last score is not this attempt's")
        runner.reading(["train", "pipeline", "status", Self.abortedID], fixture("train-pipeline-status-complete"))
        say(lines.dropFirst(5), on: job)
        job.end()
        #expect(await eventually { !model.isRunning && !said.isEmpty }, "said once its read-back is in")
        #expect(model.openPipeline?.status == "complete")
        #expect(said == ["Pipeline \(Self.abortedID): complete"])
        #expect(words(model) == ["passed · cumulative 100%", "passed · cumulative 100%"])
    }

    @Test("Stop reaches a running pipeline; what passed stays, and Resume is said")
    func stopped() async {
        let job = ScriptedJob()
        let (model, _, _) = await training(jobs: [job])
        var said: [String] = []
        model.say = { said.append($0) }
        model.pending = .runPipeline("skills")
        await model.confirm()
        say(jobStream("train-pipeline-run-stopped").prefix(4), on: job)
        await model.stop()
        #expect(await eventually { !model.isRunning && !said.isEmpty })
        #expect(said == ["Stopped: what passed stays -- Resume continues it"])
    }

    @Test("one act at a time: while a pipeline runs, nothing else starts and the cycle's pass isn't offered")
    func oneAtATime() async {
        let job = ScriptedJob()
        let (model, _, starter) = await training(jobs: [job])
        model.pending = .runPipeline("nightly")
        await model.confirm()
        #expect(!model.canRunCycle)
        model.pending = .runCycle
        await model.confirm()
        #expect(starter.starts.count == 1)
        job.end()
        #expect(await eventually { !model.isRunning && model.canRunCycle })
    }

    // MARK: - The cycle

    @Test("with no history, train status's cycle is null: the empty state, the read that exits 1 never asked")
    func cycleNoHistory() async {
        let (model, runner, _) = await training()
        #expect(model.cycle == nil && model.cycleText == nil)
        #expect(model.noHistoryText?.hasPrefix("No pass has run yet, so the cycle has no history") == true)
        #expect(!runner.calls.contains("read train|cycle|status"))
        #expect(model.canRunCycle && model.canHaltCycle && !model.canResumeCycle)
    }

    @Test("the schedule stays the person's: the place says launchd or cron runs it, and offers one pass, a halt and a resume -- nothing else")
    func scheduleStaysYours() {
        #expect(TrainingViewModel.scheduleWords.contains("launchd or cron"))
        #expect(TrainingViewModel.scheduleWords.contains("Nothing here schedules it"))
        let verbs: [TrainingViewModel.Pending] = [.runCycle, .haltCycle, .resumeCycle]
        #expect(verbs.map(\.arguments) == [["train", "cycle", "run"], ["train", "cycle", "halt"], ["train", "cycle", "resume"]])
    }

    @Test("the cycle's state: its backend, anchor, breaker and passes, read once train status says it has a history")
    func cycleRead() async throws {
        let (model, runner, _) = await training(status: "train-status-cycle", cycle: "train-cycle-status-passed")
        #expect(runner.calls.contains("read train|cycle|status"))
        let cycle = try #require(model.cycle)
        #expect(TrainingViewModel.anchorWords(cycle) == "v1 (100%)")
        #expect(cycle.runs.map(TrainingViewModel.words) == ["skipped", "pass · scored 100% · promoted v1 · 2 examples from directory"],
            "a 0 is a score never reached: unsaid, as the CLI's table prints it -")
        let halted = try #require(CycleHistory(document: ReadDocument(text: fixture("train-cycle-status-halted"))!))
        #expect(TrainingViewModel.words(halted.runs[2]) == "fail · 1 example from directory", "aborted before the dual gate: no score")
        let later = try #require(CycleHistory(document: ReadDocument(text: fixture("train-cycle-status-active"))!))
        #expect(TrainingViewModel.anchorWords(later) == "v1 (100%)")
        let fresh = try #require(CycleHistory(document: ReadDocument(text: fixture("train-cycle-status-halted-no-history"))!))
        #expect(TrainingViewModel.anchorWords(fresh) == "not set -- the first passing pass sets it")
        #expect(!model.cycleHeld && !model.cycleHalted && model.cycleHoldText == nil && model.noHistoryText == nil)
        #expect(model.canRunCycle && model.canHaltCycle && !model.canResumeCycle)
    }

    @Test("Run one pass asks first in the TUI's words, then starts train cycle run with its stream; the pass is watched and the history read back")
    func runOnceConfirmed() async {
        let job = ScriptedJob()
        let (model, runner, starter) = await training(status: "train-status-cycle", cycle: "train-cycle-status-passed", jobs: [job])
        var said: [String] = []
        model.say = { said.append($0) }
        model.pending = .runCycle
        #expect(model.pendingTitle == "Run the training cycle once?")
        #expect(model.pendingConsequence == "It trains, gates and may promote -- this machine is held until it ends.")
        #expect(model.pendingAction == "Run one pass" && model.pending?.isDestructive == false)
        #expect(starter.starts.isEmpty, "nothing runs before the person's word")
        await model.confirm()
        #expect(starter.starts == [["train", "cycle", "run", "--output-format", "stream-json"]] && starter.streamed == [true])
        #expect(model.live == .cycle && model.cycleHeld && !model.canHaltCycle, "no halt while a pass holds the cycle")
        let lines = jobStream("train-cycle-run-passed")
        say(lines.prefix { !$0.contains(#""iteration":3"#) }, on: job)
        #expect(await eventually { model.run?.pass?.state == "training" && model.liveStages.first?.iteration?.iteration == 2 })
        #expect(model.liveStages.map(TrainingViewModel.words) == ["training · iter 2/4 · loss 0.6200 · 10.0 it/s"])
        let reads = runner.calls.filter { $0 == "read train|cycle|status" }.count
        say(lines.drop { !$0.contains(#""iteration":3"#) }, on: job)
        job.end()
        #expect(await eventually { !model.isRunning && !said.isEmpty }, "said once its read-back is in")
        #expect(model.live == nil)
        #expect(runner.calls.filter { $0 == "read train|cycle|status" }.count == reads + 1, "the history read back")
        #expect(said.count == 1 && said[0].hasPrefix("[cycle] complete -- nightly-model promoted to v1"))
    }

    @Test("a failing pass is said in the CLI's words; the halt it left is read back, Resume offered and Run held")
    func failedPass() async {
        let job = ScriptedJob()
        let (model, runner, _) = await training(status: "train-status-cycle", cycle: "train-cycle-status-passed", jobs: [job])
        model.pending = .runCycle
        await model.confirm()
        runner.reading(["train", "status"], fixture("train-status-cycle-halted"))
        runner.reading(["train", "cycle", "status"], fixture("train-cycle-status-halted"))
        say(jobStream("train-cycle-run-failed"), on: job)
        job.end(status: 2)
        #expect(await eventually { !model.isRunning && model.failure != nil }, "said once its read-back is in")
        #expect(model.cycleHalted)
        #expect(model.failure?.hasPrefix("cycle FAILED -- pipeline aborted") == true)
        #expect(!model.canRunCycle && !model.canHaltCycle && model.canResumeCycle)
        #expect(model.cycleHoldText?.contains("circuit breaker: 1 consecutive failed cycle(s) reached circuit_breaker_k 1") == true)
    }

    @Test("Halt and Resume ask first, run as their verbs and are read back; their answers said in the CLI's words")
    func haltAndResume() async {
        let (model, runner, _) = await training(status: "train-status-cycle", cycle: "train-cycle-status-passed")
        var said: [String] = []
        model.say = { said.append($0) }
        runner.answering(["train", "cycle", "halt"], .done(stdout: fixture("train-cycle-halt", "txt")))
        runner.answering(["train", "cycle", "resume"], .done(stdout: fixture("train-cycle-resume", "txt")))
        model.pending = .haltCycle
        #expect(model.pendingTitle == "Halt the training cycle?" && model.pendingAction == "Halt")
        #expect(model.pendingConsequence == "Every 'cycle run' refuses until 'cycle resume' -- your scheduler's passes too.")
        #expect(!runner.calls.contains("run train|cycle|halt"))
        runner.reading(["train", "status"], fixture("train-status-cycle-halted"))
        runner.reading(["train", "cycle", "status"], fixture("train-cycle-status-halted-by-hand"))
        await model.confirm()
        #expect(runner.calls.contains("run train|cycle|halt"))
        #expect(said == ["cycle halted -- 'apogee train cycle run' refuses until 'apogee train cycle resume'"])
        #expect(model.cycleHalted && model.canResumeCycle && !model.canRunCycle && !model.canHaltCycle, "read back")
        #expect(model.cycleHoldText?.contains("halted by 'apogee train cycle halt'") == true)

        model.pending = .resumeCycle
        #expect(model.pendingTitle == "Resume the training cycle?" && model.pendingAction == "Resume")
        runner.reading(["train", "status"], fixture("train-status-cycle"))
        runner.reading(["train", "cycle", "status"], fixture("train-cycle-status-passed"))
        await model.confirm()
        #expect(runner.calls.contains("run train|cycle|resume"))
        #expect(said.last?.hasPrefix("cycle resumed (was halted: ") == true)
        #expect(!model.cycleHalted && model.canRunCycle && model.canHaltCycle)
    }

    @Test("a pass another holds -- the person's scheduler's -- holds Run and Halt back, and says why")
    func heldElsewhere() async {
        let (model, _, _) = await training(status: "train-status-cycle-active", cycle: "train-cycle-status-active")
        #expect(model.cycle?.active == true && model.cycleHeld)
        #expect(!model.canRunCycle && !model.canHaltCycle && !model.canResumeCycle)
        #expect(model.cycleHoldText == TrainingViewModel.heldWords)
    }

    @Test("a failed train status is said where the cycle is, and no apogee is the place's empty state")
    func cycleFailedAndMissing() async {
        let (model, runner, _) = await training(status: "train-status-cycle", cycle: "train-cycle-status-passed")
        runner.failing(["train", "status"], .timedOut(after: .seconds(60), stderrTail: BoundedTail()))
        await model.appeared()
        #expect(model.cycleText?.hasPrefix("The cycle could not be read: it did not finish within") == true, "train status's failure, where the cycle is")
        let none = TrainingViewModel(runner: ScriptedRunner(), starter: ScriptedJobs([]))
        await none.connect(binary: nil)
        #expect(none.cycleText == TrainingViewModel.missingWords && none.pipelinesText == TrainingViewModel.missingWords)
        #expect(!none.canRunCycle && !none.canHaltCycle && none.specsText == nil)
    }
}
