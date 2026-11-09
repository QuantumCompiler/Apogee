import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationTraining

/// GUI 6i: the Training screens follow the CLI's resume and status truth --
/// a cycle pass's pipeline never resumed here, `running` read as the ids the
/// CLI writes, a dead pass's lock named with its remedy, a resume passing the
/// spec its document names and naming the CLI's stage, an unreadable history
/// shown as unreadable. The documents were recorded from the build tree's
/// binary (with the CLI's 57g and 57h) over a scratch home on the mock
/// trainer, the home written `/Users/someone`: `nightly` named `Nightly SFT`
/// and a spec-file run each aborted at stage 2, `three` failed with its first
/// stage kept failed, a cycle pass aborted, and the cycle's lock written with
/// a dead process's id, then a live one's. Nothing here trains.
@Suite("The Training screens follow the CLI (GUI 6i)", .timeLimit(.minutes(1)))
@MainActor
struct TrainingFollowsCLITests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let complete = ["__complete", "train", "pipeline", "run", "--pipeline", ""]
    /// The branch binary's vocabulary: a halt taken during a pass is kept.
    static let m30 = "2026-10-26"
    static let namedID = "pipe-20261109-040503"
    static let specFileID = "pipe-20261109-041444"
    static let keptFailedID = "pipe-20261109-041458"
    static let cyclePassID = "cycle-20261109-041521"
    static let lock = "/Users/someone/.apogee/training/cycle/cycle.lock"

    /// The place over the sandbox's reads: `train status` as `status` says,
    /// the named pipelines `train pipeline list`'s -- or, where `listed` is
    /// false, refused as an older binary refuses it, the names then the
    /// completion's `named` -- each recorded run's document by its id, as
    /// `runs` overrides them, and the cycle's history when `cycle` names one.
    func training(
        vocabulary: String? = Self.m30, status: String = "train-status-fresh", cycle: String? = nil, listed: Bool = true,
        named: String = "train-pipeline-complete", runs: [String: String] = [:], jobs: [ScriptedJob] = []
    ) async -> (TrainingViewModel, ScriptedRunner, ScriptedJobs) {
        let runner = ScriptedRunner()
        runner.reading(["train", "status"], fixture(status))
        runner.reading(["train", "versions"], #"{"object":"list","data":[]}"#)
        runner.reading(["train", "regime", "list"], fixture("train-regimes"))
        runner.reading(["datasets", "kits"], fixture("datasets-kits"))
        runner.reading(["datasets", "list"], fixture("datasets-list"))
        runner.reading(["models", "list"], fixture("models-stored-list-all"))
        runner.reading(["train", "pipeline", "status"], fixture("train-pipeline-status-resumes-list"))
        let documents = [
            Self.namedID: fixture("train-pipeline-status-named"),
            Self.specFileID: fixture("train-pipeline-status-spec-file"),
            Self.keptFailedID: fixture("train-pipeline-status-kept-failed"),
            Self.cyclePassID: fixture("train-pipeline-status-cycle-pass"),
        ].merging(runs) { $1 }
        for (id, text) in documents { runner.reading(["train", "pipeline", "status", id], text) }
        if listed {
            runner.reading(["train", "pipeline", "list"], fixture("train-pipeline-list-sweep"))
        } else {
            runner.failing(
                ["train", "pipeline", "list"],
                .failed(exit: ChildExit(reason: .exited, status: 106), stderrTail: tail(fixture("train-pipeline-list-older", "err"))))
        }
        if let cycle { runner.reading(["train", "cycle", "status"], fixture(cycle)) }
        runner.answering(Self.complete, .done(stdout: fixture(named, "txt")))
        let starter = ScriptedJobs(jobs)
        let model = TrainingViewModel(runner: runner, starter: starter)
        model.vocabulary = { vocabulary }
        await model.connect(binary: Self.binary)
        return (model, runner, starter)
    }

    /// A recorded document changed as `change` says -- an older binary's
    /// shape of it, or a twin under another id -- written back as JSON.
    func derived(_ name: String, _ change: (inout [String: Any]) -> Void) -> String {
        changed(fixture(name), change)
    }

    /// A document's text changed as `change` says, written back as JSON.
    func changed(_ text: String, _ change: (inout [String: Any]) -> Void) -> String {
        var object = (try? JSONSerialization.jsonObject(with: Data(text.utf8))) as? [String: Any] ?? [:]
        change(&object)
        return String(decoding: (try? JSONSerialization.data(withJSONObject: object)) ?? Data(), as: UTF8.self)
    }

    /// The document as a binary before the CLI's 57g and 57h wrote it: its
    /// manifest alone, no `holder`, `spec_ref` or `resume`.
    func older(_ name: String) -> String {
        olderShape(fixture(name))
    }

    /// `older`, of a document's text.
    func olderShape(_ text: String) -> String {
        changed(text) { $0 = ["kind": "pipeline", "pipeline": $0["pipeline"] ?? [:]] }
    }

    func run(_ text: String) throws -> PipelineRun {
        let document = try #require(ReadDocument(text: text))
        return try #require(PipelineRun(document: document))
    }

    // MARK: - 1. A cycle pass's pipeline is never resumed here

    @Test("a cycle pass's aborted pipeline is never offered Resume -- with its document's resume or without -- and its page points at Run one pass")
    func cyclePassNotResumed() async throws {
        let (model, _, starter) = await training()
        await model.openPipelineRun(Self.cyclePassID)
        let pass = try #require(model.openPipeline)
        #expect(pass.isCyclePass && pass.status == "aborted")
        #expect(TrainingViewModel.resumePoint(pass) == nil && TrainingViewModel.resumeWords(pass) == nil, "no Resume line")
        #expect(!model.canResume(pass) && model.resumeHold(pass) == nil)
        #expect(TrainingViewModel.pageLine(pass) == TrainingViewModel.cyclePassWords)
        #expect(TrainingViewModel.pageLine(pass).contains("Run one pass on the Cycle tab runs the next pass"))
        #expect(!TrainingViewModel.pageLine(pass).contains("promote it from Runs"))
        model.requestResume(pass)
        #expect(model.pending == nil && starter.starts.isEmpty, "nothing is asked, nothing starts")

        // An older binary's document of the same pass: aborted, so its status
        // alone would offer a resume -- on the spec's own data, outside the
        // cycle.
        let older = try run(older("train-pipeline-status-cycle-pass"))
        #expect(!older.hasResumeField && older.isResumable)
        #expect(TrainingViewModel.resumeWords(older) == nil && !model.canResume(older))
        #expect(TrainingViewModel.pageLine(older) == TrainingViewModel.cyclePassWords)

        // A complete pass: the cycle judged it, never Runs.
        let done = try run(derived("train-pipeline-status-cycle-pass") { document in
            var manifest = document["pipeline"] as? [String: Any] ?? [:]
            manifest["status"] = "complete"
            document["pipeline"] = manifest
        })
        #expect(TrainingViewModel.pageLine(done) == TrainingViewModel.cyclePassCompleteWords)
    }

    /// The recorded pass as it stood mid-pass: its manifest `running`, its
    /// one stage training and not yet gated, and `holder` as `holder` says
    /// (`nil` leaves the recording's null) -- the document the CLI writes
    /// while the scheduler's pass, or one this place started, runs.
    func runningPass(holder: Int?) -> String {
        derived("train-pipeline-status-cycle-pass") { document in
            var manifest = document["pipeline"] as? [String: Any] ?? [:]
            manifest["status"] = "running"
            var stages = manifest["stages"] as? [[String: Any]] ?? []
            stages[0]["status"] = "training"
            stages[0]["eval_results"] = nil
            manifest["stages"] = stages
            document["pipeline"] = manifest
            if let holder { document["holder"] = ["pid": holder] }
        }
    }

    @Test("a cycle pass running now -- a live holder, or an older binary's running -- says it runs and the cycle judges it, never discarded; a dead pass's running keeps 57h's line")
    func cyclePassRunning() async throws {
        let (model, _, starter) = await training(runs: [Self.cyclePassID: runningPass(holder: 67959)])
        await model.openPipelineRun(Self.cyclePassID)
        let live = try #require(model.openPipeline)
        #expect(live.isCyclePass && live.status == "running" && live.holder == 67959 && live.hasResumeField)
        #expect(TrainingViewModel.resumeWords(live) == nil && model.resumeHold(live) == nil && !model.canResume(live))
        #expect(TrainingViewModel.pageLine(live) == TrainingViewModel.cyclePassRunningWords(67959))
        #expect(TrainingViewModel.pageLine(live)
            == "A cycle pass, running now (process 67959): its candidate is the cycle's, judged by its dual gate when the pass ends -- never from Runs.")
        #expect(!TrainingViewModel.pageLine(live).contains("discarded"), "the CLI never says it while the pass runs")
        model.requestResume(live)
        #expect(model.pending == nil && starter.starts.isEmpty)

        // A holder whose lock names no process the CLI can read.
        let unnamed = try run(runningPass(holder: 0))
        #expect(TrainingViewModel.pageLine(unnamed).hasPrefix("A cycle pass, running now (its lock names no process it can read):"))
        #expect(!TrainingViewModel.pageLine(unnamed).contains("discarded"))

        // An older binary's document of the running pass: no holder, no
        // resume -- live or dead cannot be told, so it runs as its manifest
        // says.
        let older = try run(olderShape(runningPass(holder: 67959)))
        #expect(!older.hasResumeField && older.holder == nil)
        #expect(TrainingViewModel.pageLine(older) == TrainingViewModel.cyclePassRunningWords(nil))
        #expect(TrainingViewModel.pageLine(older).hasPrefix("A cycle pass, running as its manifest says:"))
        #expect(!TrainingViewModel.pageLine(older).contains("discarded"))

        // A binary from 57g on with no live holder: the pass died while it
        // ran, and its candidate is discarded -- 57h's line, as the CLI says.
        let dead = try run(runningPass(holder: nil))
        #expect(dead.status == "running" && dead.holder == nil && dead.hasResumeField)
        #expect(TrainingViewModel.pageLine(dead) == TrainingViewModel.cyclePassWords)
    }

    @Test("a pipe- run aborted at the same stage is still offered Resume, with its document's resume and without")
    func pipeTwinResumes() async throws {
        let twin = derived("train-pipeline-status-cycle-pass") { document in
            var manifest = document["pipeline"] as? [String: Any] ?? [:]
            manifest["pipeline_run_id"] = "pipe-20261109-041521"
            document["pipeline"] = manifest
            document["resume"] = ["stage": 1, "index": 0, "name": "base", "pipeline": "cyclic", "note": NSNull()]
            document["spec_ref"] = "cyclic"
        }
        let (model, _, _) = await training()
        let withField = try run(twin)
        #expect(!withField.isCyclePass && model.canResume(withField))
        #expect(TrainingViewModel.resumeWords(withField) == "Resume from stage 1, base")
        #expect(TrainingViewModel.pageLine(withField) == TrainingViewModel.ordinaryRunWords)
        let without = try run(older("train-pipeline-status-cycle-pass").replacingOccurrences(of: Self.cyclePassID, with: "pipe-20261109-041521"))
        #expect(!without.hasResumeField && model.canResume(without), "cyclic is a named pipeline")
        #expect(TrainingViewModel.resumeWords(without) == "Resume from stage 1, base")
    }

    // MARK: - 2. running, the CLI's ids

    @Test("train-status-pipeline-running: the running id read, listed once and first, as the run recent_runs carries")
    func runningListedOnce() async {
        let (model, _, _) = await training(status: "train-status-pipeline-running")
        #expect(model.status?.runningIDs == ["pipe-20261026-074116-s1"])
        #expect(model.runs.count == 5 && model.runs.filter { $0.id == "pipe-20261026-074116-s1" }.count == 1)
        #expect(model.runs.first?.id == "pipe-20261026-074116-s1" && model.runs.first?.listedByID == false, "its facts, from recent_runs")
    }

    @Test("a run still training older than the five newest is listed first as a row of its id, status running, and opens")
    func runningOlderListed() async throws {
        let (model, runner, _) = await training(status: "train-status-running-older")
        let older = "pipe-20261109-040503-s0"
        #expect(model.runs.map(\.id) == [older] + (model.status?.recent.map(\.id) ?? []))
        let row = try #require(model.runs.first)
        #expect(row.listedByID && row.status == "running" && row.trainer.isEmpty && row.evalPassed == nil)
        #expect(TrainingViewModel.summary(row) == TrainingViewModel.listedByIDWords)
        #expect(TrainingViewModel.summary(model.runs[1]) == "lora · mock · loss 0.5200 · not gated yet", "a recent run's line as before")
        model.selectedRun = older
        #expect(model.currentRun?.id == older && model.currentRun?.listedByID == true, "it can be opened")
        #expect(runner.calls.filter { $0.contains(older) }.isEmpty, "no extra read for it")
    }

    // MARK: - 3. A dead pass's lock

    @Test("a dead pass's lock (57g's stale_lock): Run held with its path and remedy, the State stopped, never a pass running; Halt still offered")
    func staleLock() async {
        let (model, _, _) = await training(status: "train-status-cycle-stale-lock", cycle: "train-cycle-status-stale-lock")
        #expect(model.staleLock == Self.lock && !model.cycleHeld && !model.cycleHalted)
        #expect(!model.canRunCycle && model.canHaltCycle && !model.canResumeCycle)
        #expect(model.cycleStateWords == "stopped, its lock left behind")
        let hold = model.cycleHoldText ?? ""
        #expect(hold.contains("cycle.lock is left by a process that is not running; remove \(Self.lock) to run again"))
        #expect(hold.contains("every pass refuses"))
        #expect(!hold.contains("A pass holds the cycle") && !hold.contains("a pass is running"))
        #expect(!hold.contains(TrainingViewModel.lockRemedyWords), "a binary that tells them apart needs no guess")
        #expect(model.noHistoryText == nil && model.cycleErrorText == nil)
    }

    @Test("a first pass killed before it wrote a history: the empty state, Run held by its lock, the read that exits 1 never asked")
    func staleLockNoHistory() async {
        let (model, runner, _) = await training(status: "train-status-stale-lock-no-history")
        #expect(model.cycle == nil && model.noHistoryText != nil)
        #expect(!runner.calls.contains("read train|cycle|status"))
        #expect(model.staleLock == Self.lock && !model.canRunCycle && model.canHaltCycle)
        #expect(model.cycleHoldText?.contains("remove \(Self.lock) to run again") == true)
    }

    @Test("a live pass's lock, as a binary from 57g states it, keeps today's words: a pass running, Run held, no remedy")
    func liveLock() async {
        let (model, _, _) = await training(status: "train-status-cycle-live-lock", cycle: "train-cycle-status-live-lock")
        #expect(model.cycleHeld && model.staleLock == nil)
        #expect(!model.canRunCycle && model.canHaltCycle && !model.canResumeCycle)
        #expect(model.cycleStateWords == "a pass is running")
        #expect(model.cycleHoldText == nil, "M30's binary keeps a halt taken now: nothing held back but Run, as before")
        let (older, _, _) = await training(vocabulary: "2026-10-25", status: "train-status-cycle-live-lock", cycle: "train-cycle-status-live-lock")
        #expect(older.cycleHoldText == TrainingViewModel.heldWords)
    }

    @Test("an older binary's held cycle -- no stale_lock, not this place's pass -- adds the CLI's own remedy")
    func olderHeldRemedy() async {
        let (model, _, _) = await training(status: "train-status-cycle-held", cycle: "train-cycle-status-held")
        #expect(model.cycleHeld && model.staleLock == nil && !model.canRunCycle)
        #expect(model.cycleHoldText == TrainingViewModel.lockRemedyWords)
        #expect(TrainingViewModel.lockRemedyWords
            == "If no pass is running, one that died left its lock: remove training/cycle/cycle.lock under your Apogee home.")
        #expect(model.cycleStateWords == "a pass is running", "an older binary cannot tell, so the State keeps its word")
    }

    @Test("a pass this place runs is no other process's: no remedy beside it, whatever the binary")
    func ownPassNoRemedy() async {
        let job = ScriptedJob()
        let (model, _, _) = await training(vocabulary: "2026-10-25", status: "train-status-cycle", cycle: "train-cycle-status-passed", jobs: [job])
        model.pending = .runCycle
        await model.confirm()
        #expect(model.live == .cycle && model.cycleHeld)
        #expect(model.cycleHoldText == TrainingViewModel.heldWords)
        job.end()
        #expect(await eventually { !model.isRunning })
    }

    // MARK: - 4. Resume passes the spec and names the CLI's stage

    @Test("Resume on Nightly SFT, whose name is not its key, asks at the CLI's stage, then starts train pipeline resume <id> --pipeline nightly with its stream")
    func namedResume() async throws {
        let job = ScriptedJob()
        let (model, _, starter) = await training(jobs: [job])
        await model.openPipelineRun(Self.namedID)
        let named = try #require(model.openPipeline)
        #expect(named.specName == "Nightly SFT" && !model.pipelineSpecs.contains(named.specName), "its name is no key")
        #expect(model.canResume(named) && model.resumeHold(named) == nil)
        #expect(TrainingViewModel.resumeWords(named) == "Resume from stage 2, b")
        #expect(TrainingViewModel.resumeHelp(named) == "apogee train pipeline resume \(Self.namedID) --pipeline nightly: after the last stage that passed")
        #expect(TrainingViewModel.pageLine(named) == TrainingViewModel.ordinaryRunWords)
        model.requestResume(named)
        #expect(model.pending == .resumePipeline(Self.namedID, pipeline: "nightly"))
        #expect(model.pendingTitle == "Resume \(Self.namedID) from stage 2, b?")
        #expect(model.pendingConsequence?.hasPrefix("What passed stays") == true)
        #expect(starter.starts.isEmpty, "nothing trains before the person's word")
        await model.confirm()
        #expect(starter.starts == [["train", "pipeline", "resume", Self.namedID, "--pipeline", "nightly", "--output-format", "stream-json"]])
        #expect(starter.streamed == [true] && model.showsLivePipeline)
        job.end()
        #expect(await eventually { !model.isRunning })
    }

    @Test("the pE run: the confirm names stage 3, c -- where the CLI resumes -- and says stage 1, a, stays failed")
    func keptFailedResume() async throws {
        let job = ScriptedJob()
        let (model, _, starter) = await training(jobs: [job])
        await model.openPipelineRun(Self.keptFailedID)
        let kept = try #require(model.openPipeline)
        #expect(kept.resumesAt?.index == 1, "the older rule would have said stage 1")
        #expect(TrainingViewModel.resumeWords(kept) == "Resume from stage 3, c; stage 1, a, failed its gate and stays as it is")
        model.requestResume(kept)
        #expect(model.pendingTitle == "Resume \(Self.keptFailedID) from stage 3, c?")
        #expect(model.pendingConsequence?.hasPrefix("Stage 1, a, failed its gate and stays as it is. What passed stays") == true)
        await model.confirm()
        #expect(starter.starts == [["train", "pipeline", "resume", Self.keptFailedID, "--pipeline", "three", "--output-format", "stream-json"]])
        job.end()
        #expect(await eventually { !model.isRunning })
    }

    @Test("a spec-file run resumes naming its file; with the file gone, Resume is held with the console line")
    func specFileRun() async throws {
        let (model, _, _) = await training()
        await model.openPipelineRun(Self.specFileID)
        let file = try #require(model.openPipeline)
        #expect(model.canResume(file))
        #expect(TrainingViewModel.resume(file).arguments == ["train", "pipeline", "resume", Self.specFileID, "--pipeline", "/Users/someone/two.yaml"])

        let (gone, _, starter) = await training(runs: [Self.specFileID: fixture("train-pipeline-status-spec-file-gone")])
        await gone.openPipelineRun(Self.specFileID)
        let lost = try #require(gone.openPipeline)
        #expect(TrainingViewModel.resumeWords(lost) == "Resume from stage 2, b", "the CLI still says where")
        #expect(!gone.canResume(lost))
        #expect(gone.resumeHold(lost)
            == "Resume needs its spec: run 'apogee train pipeline resume \(Self.specFileID) --pipeline <spec>' in the console.")
        #expect(fixture("train-pipeline-resume-no-spec", "err").contains("-- pass --pipeline <spec> to resume \(Self.specFileID)"),
            "what the bare resume says then")
        gone.requestResume(lost)
        #expect(gone.pending == nil && starter.starts.isEmpty)
    }

    @Test("a run a live process holds (57g's holder) keeps its Resume line but holds the button, in the CLI's words")
    func heldByLiveProcess() async throws {
        let (model, _, _) = await training(runs: [Self.namedID: fixture("train-pipeline-status-named-held")])
        await model.openPipelineRun(Self.namedID)
        let held = try #require(model.openPipeline)
        #expect(TrainingViewModel.resumeWords(held) == "Resume from stage 2, b")
        #expect(!model.canResume(held))
        #expect(model.resumeHold(held) == "This pipeline is running now (process 67959); a resume is refused while it runs.")
    }

    @Test("an older binary: a run whose spec_name is no named pipeline holds Resume with the console line; a spec-keyed run resumes bare, as before")
    func olderBinaryResume() async throws {
        let (model, _, _) = await training(runs: [Self.namedID: older("train-pipeline-status-named")])
        await model.openPipelineRun(Self.namedID)
        let named = try #require(model.openPipeline)
        #expect(!named.hasResumeField && TrainingViewModel.resumeWords(named) == "Resume from stage 2, b")
        #expect(!model.canResume(named))
        #expect(model.resumeHold(named) == "Resume needs its spec: run 'apogee train pipeline resume \(Self.namedID) --pipeline <spec>' in the console.")
        #expect(TrainingViewModel.resumeHelp(named).hasSuffix(": from the first stage that has not passed"))

        // 4j's sandbox, its names the completion's: `skills` is one.
        let abortedID = "pipe-20261026-074112"
        let (spec, _, _) = await training(
            vocabulary: "2026-10-25", listed: false, runs: [abortedID: fixture("train-pipeline-status-aborted")])
        #expect(spec.pipelineSpecs == ["nightly", "skills"])
        await spec.openPipelineRun(abortedID)
        let skills = try #require(spec.openPipeline)
        #expect(spec.canResume(skills) && spec.resumeHold(skills) == nil)
        #expect(TrainingViewModel.resume(skills).arguments == ["train", "pipeline", "resume", abortedID], "bare, as before")
        let (none, _, _) = await training(
            vocabulary: "2026-10-25", listed: false, named: "train-pipeline-complete-none", runs: [abortedID: fixture("train-pipeline-status-aborted")])
        await none.openPipelineRun(abortedID)
        let unnamed = try #require(none.openPipeline)
        #expect(!none.canResume(unnamed) && none.resumeHold(unnamed)?.hasPrefix("Resume needs its spec") == true)
    }

    // MARK: - 5. An unreadable history

    @Test("an unreadable history (57h's cycle_error): train cycle status never read, the CLI's reason where the history is, never the no-history text; Run, Halt and Resume held")
    func unreadableHistory() async {
        let (model, runner, _) = await training(status: "train-status-cycle-unreadable")
        #expect(!runner.calls.contains("read train|cycle|status"))
        #expect(model.cycleError == "/Users/someone/.apogee/training/cycle/history.json: cycle history: runs must be an array")
        #expect(model.cycleErrorText?.hasPrefix(
            "The cycle history could not be read: /Users/someone/.apogee/training/cycle/history.json: cycle history: runs must be an array") == true)
        #expect(model.cycleErrorText?.contains(TrainingViewModel.unreadableWords) == true)
        #expect(model.noHistoryText == nil && model.cycleText == nil)
        #expect(!model.canRunCycle && !model.canHaltCycle && !model.canResumeCycle)
    }

    @Test("a fresh home from a binary with 57h -- cycle and cycle_error both null -- keeps the empty state and its controls")
    func freshHome() async {
        let (model, runner, _) = await training(status: "train-status-fresh")
        #expect(model.noHistoryText?.hasPrefix("No pass has run yet, so the cycle has no history") == true)
        #expect(model.cycleErrorText == nil && model.staleLock == nil && model.cycleHoldText == nil)
        #expect(!runner.calls.contains("read train|cycle|status"))
        #expect(model.canRunCycle && model.canHaltCycle && !model.canResumeCycle)
    }
}
