import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationTraining

/// GUI M14: the Training place takes what the CLI's M30 states -- the named
/// pipelines from `train pipeline list | show`, a halt taken while a pass runs
/// kept and said by the pass's notice, and `datasets pull` as a job's stream
/// -- over recordings from the branch binary (schema 2026-10-26) in a sandbox
/// on the mock trainer, the home written `/Users/someone`. The pass was held
/// on its lock by a FIFO nobody read while the halt was taken. The pull that
/// lands is hand-made (`datasets-pull-handmade.jsonl`): a real one needs the
/// network. An older binary -- one that refuses `train pipeline list`, or
/// whose vocabulary is dated before 2026-10-26 or not said -- keeps 4i's and
/// 4j's behaviour.
@Suite("Pipelines, the halt and the pull as the CLI states them (GUI M14)", .timeLimit(.minutes(1)))
@MainActor
struct PipelinesHaltPullTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let complete = ["__complete", "train", "pipeline", "run", "--pipeline", ""]
    /// The branch binary's vocabulary, and the installed 0.2.0's before it.
    static let m30 = "2026-10-26"
    static let older = "2026-10-25"
    static let haltKept =
        "[cycle] halted while this pass ran (halted by 'apogee train cycle halt') -- the halt is kept: 'apogee train cycle run' refuses until 'apogee train cycle resume'"
    static let landed = "/Users/someone/.apogee/training/datasets/raw/someone--heat-notes"

    /// The place over the sandbox's reads, `train status` and the cycle's
    /// history as named; `lists` says whether the binary answers `train
    /// pipeline list` (M30's) or refuses it as the installed 0.2.0 does.
    func training(
        vocabulary: String?, status: String = "train-status-none", cycle: String? = nil, lists: Bool = true, jobs: [ScriptedJob] = []
    ) async -> (TrainingViewModel, ScriptedRunner, ScriptedJobs) {
        let runner = ScriptedRunner()
        runner.reading(["train", "status"], fixture(status))
        runner.reading(["train", "versions"], #"{"object":"list","data":[]}"#)
        runner.reading(["train", "regime", "list"], fixture("train-regimes"))
        runner.reading(["datasets", "kits"], fixture("datasets-kits"))
        runner.reading(["datasets", "list"], fixture("datasets-list"))
        runner.reading(["models", "list"], fixture("models-stored-list-all"))
        runner.reading(["train", "pipeline", "status"], fixture("train-pipeline-status-none"))
        if lists {
            runner.reading(["train", "pipeline", "list"], fixture("train-pipeline-list"))
            runner.reading(["train", "pipeline", "show", "skills"], fixture("train-pipeline-show-skills"))
            runner.reading(["train", "pipeline", "show", "nightly"], fixture("train-pipeline-show-nightly"))
        } else {
            runner.failing(["train", "pipeline", "list"], .failed(exit: ChildExit(reason: .exited, status: 106), stderrTail: words("train-pipeline-list-older")))
        }
        if let cycle { runner.reading(["train", "cycle", "status"], fixture(cycle)) }
        runner.answering(Self.complete, .done(stdout: fixture("train-pipeline-complete", "txt")))
        let starter = ScriptedJobs(jobs)
        let model = TrainingViewModel(runner: runner, starter: starter)
        model.vocabulary = { vocabulary }
        await model.connect(binary: Self.binary)
        return (model, runner, starter)
    }

    /// A recorded stderr, as the tail a refusal leaves.
    func words(_ name: String) -> BoundedTail {
        tail(fixture(name, "err").trimmingCharacters(in: .newlines))
    }

    func say(_ lines: some Sequence<String>, on job: ScriptedJob) {
        for line in lines { job.say(.event(JobEvent.decode(line: line))) }
    }

    // MARK: - The named pipelines: train pipeline list | show

    @Test("the named pipelines are train pipeline list's, never completion's: each row its stage count and student")
    func specsListed() async {
        let (model, runner, _) = await training(vocabulary: Self.m30)
        #expect(runner.calls.contains("read train|pipeline|list"))
        #expect(!runner.calls.contains("run " + Self.complete.joined(separator: "|")), "the list replaces completion")
        #expect(model.pipelineSpecs == ["nightly", "skills"])
        #expect(model.specs.map(TrainingViewModel.words) == ["1 stage · student tiny", "2 stages · student tiny"])
        #expect(model.specsText == nil)
    }

    @Test("none named: the list is empty, the place says what a named pipeline is, and completion is never asked")
    func noneListed() async {
        let (model, runner, _) = await training(vocabulary: Self.m30)
        runner.reading(["train", "pipeline", "list"], fixture("train-pipeline-list-none"))
        await model.refresh()
        #expect(model.specs.isEmpty && model.pipelineSpecs.isEmpty)
        #expect(model.specsText?.contains("training.pipelines entry in your config") == true)
        #expect(!runner.calls.contains("run " + Self.complete.joined(separator: "|")))
    }

    @Test("a spec's page is train pipeline show's: its student and each stage's dataset, gate and tuning; Run… from it hands the page to the run")
    func specPage() async throws {
        let job = ScriptedJob()
        let (model, runner, starter) = await training(vocabulary: Self.m30, jobs: [job])
        await model.openSpec("skills")
        #expect(runner.calls.contains("read train|pipeline|show|skills"))
        let spec = try #require(model.openSpecPlan)
        #expect(spec.name == "skills" && spec.student == "tiny" && spec.path == nil)
        #expect(TrainingViewModel.source(spec) == "a training.pipelines entry")
        #expect(spec.stages.map(TrainingViewModel.planWords) == ["dataset starter · gate hello", "dataset regress · gate hello + nope"])
        #expect(spec.stages.map(TrainingViewModel.tuningWords) == ["lora · 4 iterations", "lora · 4 iterations"])
        #expect(!model.showsLivePipeline && model.openPipeline == nil)
        model.pending = .runPipeline(spec.name)
        await model.confirm()
        #expect(starter.starts == [["train", "pipeline", "run", "--pipeline", "skills", "--output-format", "stream-json"]])
        #expect(model.openedSpec == nil && model.showsLivePipeline, "the run going now takes the page")
        job.end()
        #expect(await eventually { !model.isRunning })
        await model.openSpec("nightly")
        #expect(model.openSpecPlan?.stages.map(\.name) == ["base"])
        await model.openPipelineRun(nil)
        #expect(model.openedSpec == nil && model.openSpecPlan == nil, "a run's page and a spec's take turns")
    }

    @Test("a spec the binary has no answer for is said in its words")
    func specRefused() async {
        let (model, runner, _) = await training(vocabulary: Self.m30)
        runner.failing(["train", "pipeline", "show", "nosuch"], .failed(exit: ChildExit(reason: .exited, status: 1), stderrTail: words("train-pipeline-show-unknown")))
        await model.openSpec("nosuch")
        #expect(model.openSpecPlan == nil)
        #expect(model.specFailure?.hasPrefix("apogee train: no pipeline 'nosuch': not a spec file, and not an entry under training.pipelines") == true)
    }

    @Test("an older binary refuses train pipeline list: the names are completion's, as 4j read them, with no plan")
    func olderCompletes() async {
        let (model, runner, _) = await training(vocabulary: Self.older, lists: false)
        #expect(runner.calls.contains("read train|pipeline|list"))
        #expect(runner.calls.contains("run " + Self.complete.joined(separator: "|")))
        #expect(model.pipelineSpecs == ["nightly", "skills"])
        #expect(model.specs.isEmpty && model.specsText == nil)
    }

    // MARK: - A halt taken while a pass runs

    @Test("on a binary that keeps it, Halt is offered while another's pass holds the cycle, asked in the CLI's words, run and read back")
    func haltOfferedWhileHeld() async {
        let (model, runner, _) = await training(vocabulary: Self.m30, status: "train-status-cycle-held", cycle: "train-cycle-status-held")
        var said: [String] = []
        model.say = { said.append($0) }
        #expect(model.cycleHeld && !model.cycleHalted)
        #expect(model.canHaltCycle && !model.canRunCycle && !model.canResumeCycle)
        #expect(model.cycleHoldText == nil, "nothing is held back, so nothing is said")
        model.pending = .haltCycle
        #expect(model.pendingConsequence
            == "Every 'cycle run' refuses until 'cycle resume' -- your scheduler's passes too. The pass running now finishes, and keeps the halt.")
        runner.answering(["train", "cycle", "halt"], .done(stdout: fixture("train-cycle-halt-during-pass", "txt")))
        runner.reading(["train", "status"], fixture("train-status-cycle-held-halted"))
        runner.reading(["train", "cycle", "status"], fixture("train-cycle-status-held-halted"))
        await model.confirm()
        #expect(runner.calls.contains("run train|cycle|halt"))
        #expect(said == ["the pass running now finishes, and keeps the halt"], "the CLI's answer, its last line")
        #expect(model.cycleHeld && model.cycleHalted && model.canResumeCycle && !model.canHaltCycle && !model.canRunCycle)
        #expect(model.cycleHoldText
            == "Halted: halted by 'apogee train cycle halt'. The pass running now finishes, and keeps the halt: every pass after it refuses -- your scheduler's too -- until Resume clears it.")
    }

    @Test("an older binary, or one that hasn't said, keeps 4j's: no Halt while a pass holds the cycle, and today's warning")
    func olderHoldsHaltBack() async {
        for vocabulary in [Self.older, nil] {
            let (model, _, _) = await training(vocabulary: vocabulary, status: "train-status-cycle-active", cycle: "train-cycle-status-active")
            #expect(model.cycleHeld && !model.canHaltCycle && !model.canRunCycle)
            #expect(model.cycleHoldText == TrainingViewModel.heldWords)
            model.pending = .haltCycle
            #expect(model.pendingConsequence == "Every 'cycle run' refuses until 'cycle resume' -- your scheduler's passes too.")
        }
    }

    @Test("a pass run here and halted while it runs finishes; its halt notice is drawn live, then kept on the tab and said; the next pass waits for Resume")
    func passHaltNotice() async {
        let job = ScriptedJob()
        let (model, runner, starter) = await training(vocabulary: Self.m30, status: "train-status-cycle", cycle: "train-cycle-status-passed", jobs: [job])
        var said: [String] = []
        model.say = { said.append($0) }
        model.pending = .runCycle
        await model.confirm()
        #expect(starter.starts == [["train", "cycle", "run", "--output-format", "stream-json"]])
        #expect(model.live == .cycle && model.cycleHeld && model.canHaltCycle, "Halt is offered while the pass runs here")
        let lines = jobStream("train-cycle-run-halted")
        say(lines.prefix { !$0.contains(#""iteration":3,"#) }, on: job)
        #expect(await eventually { model.liveStages.first?.iteration?.iteration == 2 })
        runner.answering(["train", "cycle", "halt"], .done(stdout: fixture("train-cycle-halt-during-pass", "txt")))
        runner.reading(["train", "status"], fixture("train-status-cycle-held-halted"))
        runner.reading(["train", "cycle", "status"], fixture("train-cycle-status-held-halted"))
        model.pending = .haltCycle
        await model.confirm()
        #expect(model.isRunning && model.cycleHalted && model.canResumeCycle)
        #expect(model.liveHaltNotice == nil, "said only once the pass has finished")
        say(lines.drop { !$0.contains(#""iteration":3,"#) }.prefix { !$0.contains(#""type":"document""#) }, on: job)
        #expect(await eventually { model.liveHaltNotice == Self.haltKept }, "drawn as it comes")
        runner.reading(["train", "status"], fixture("train-status-cycle-halt-kept"))
        runner.reading(["train", "cycle", "status"], fixture("train-cycle-status-halt-kept"))
        say(lines.drop { !$0.contains(#""type":"document""#) }, on: job)
        job.end()
        #expect(await eventually { !model.isRunning && said.count == 2 }, "said once its read-back is in")
        #expect(said == ["the pass running now finishes, and keeps the halt", Self.haltKept])
        #expect(model.keptHalt == Self.haltKept, "kept on the tab after the pass")
        #expect(model.cycleHalted && !model.cycleHeld && !model.canRunCycle && model.canResumeCycle)
        #expect(model.cycle?.runs.last?.promotedVersion == 2, "the pass finished as it would have")
        runner.answering(["train", "cycle", "resume"], .done(stdout: fixture("train-cycle-resume-after-kept", "txt")))
        runner.reading(["train", "status"], fixture("train-status-cycle-resumed"))
        runner.reading(["train", "cycle", "status"], fixture("train-cycle-status-resumed"))
        model.pending = .resumeCycle
        await model.confirm()
        #expect(said.last == "cycle resumed (was halted: halted by 'apogee train cycle halt'); the failure count is reset")
        #expect(model.keptHalt == nil && !model.cycleHalted && model.canRunCycle, "Resume clears the halt and its notice")
    }

    // MARK: - datasets pull as a job

    @Test("on a binary that streams it, the pull is a job: its bytes as they move, then Prepare it on the folder its document names")
    func pullStreamed() async throws {
        let pull = ScriptedJob()
        let prepare = ScriptedJob()
        let (model, _, starter) = await training(vocabulary: Self.m30, jobs: [pull, prepare])
        var said: [String] = []
        model.say = { said.append($0) }
        model.newPull()
        model.pullRef = "someone/heat-notes"
        #expect(model.pullArguments == ["datasets", "pull", "someone/heat-notes"], "the sheet shows the command as typed")
        model.pull()
        #expect(starter.starts == [["datasets", "pull", "someone/heat-notes", "--output-format", "stream-json"]])
        #expect(starter.streamed == [true] && model.running == "Pulling")
        let lines = jobStream("datasets-pull-handmade")
        say(lines.prefix { !$0.contains(#""done":65536"#) }, on: pull)
        #expect(await eventually { model.run?.bytes?.done == 49152 })
        #expect(model.pullBytes.map(TrainingViewModel.bytesWords) == "49 KB of 100 KB · data/train.jsonl (1 of 2)")
        say(lines.drop { !$0.contains(#""done":65536"#) }, on: pull)
        #expect(await eventually { model.run?.documents.count == 1 })
        #expect(model.pullBytes.map(TrainingViewModel.bytesWords) == "40 KB of 40 KB · data/test.jsonl (2 of 2)")
        pull.end()
        #expect(await eventually { model.pulled != nil })
        let pulled = try #require(model.pulled)
        #expect(pulled.path == Self.landed && pulled.summary == "2 file(s), 136 KiB")
        #expect(said == ["Pulled: 2 file(s), 136 KiB"])
        model.prepare(pulled.path)
        #expect(starter.starts.last == ["datasets", "prepare", Self.landed])
        prepare.end()
        #expect(await eventually { !model.isRunning })
    }

    @Test("Prepare takes the folder the pull's document names, whatever its lines said")
    func folderFromTheDocument() throws {
        let document = try #require(jobStream("datasets-pull-handmade").compactMap { line -> JSONValue? in
            if case .document(let value) = JobEvent.decode(line: line) { value } else { nil }
        }.last)
        let alone = try #require(TrainingViewModel.PulledDataset(document: document, said: []))
        #expect(alone.path == Self.landed && alone.summary == "2 files, 140 KB", "no line said: the document's count")
        #expect(TrainingViewModel.PulledDataset(document: .object(["object": .string("video.project")]), said: []) == nil,
            "another verb's document names no dataset")
    }

    @Test("Stop reaches a streamed pull: it ends on the CLI's cancel, said in its words, nothing pulled")
    func pullStopped() async {
        let job = ScriptedJob()
        let recorded = jobStream("datasets-pull-stopped")
        job.stopping = recorded.dropFirst().map { .event(JobEvent.decode(line: $0)) }
        let (model, _, _) = await training(vocabulary: Self.m30, jobs: [job])
        var said: [String] = []
        model.say = { said.append($0) }
        model.newPull()
        model.pullRef = "someone/heat-notes"
        model.pull()
        say(recorded.prefix(1), on: job)
        await model.stop()
        #expect(await eventually { !model.isRunning && !said.isEmpty })
        #expect(said == ["cancelled -- nothing was written"])
        #expect(model.pulled == nil && model.failure == nil)
    }

    @Test("a streamed pull the CLI refuses ends on its error, said in its words, with nothing to prepare")
    func pullRefusedStreamed() async {
        let job = ScriptedJob()
        let (model, _, _) = await training(vocabulary: Self.m30, jobs: [job])
        model.newPull()
        model.pullRef = "nonsense"
        model.pull()
        say(jobStream("datasets-pull-bad-ref"), on: job)
        job.end(status: 1, stderr: fixture("datasets-pull-bad-ref", "err").trimmingCharacters(in: .newlines))
        #expect(await eventually { model.failure != nil })
        #expect(model.failure == "'nonsense' is not a Hugging Face dataset ref (expected owner/name)")
        #expect(model.pulled == nil)
    }

    @Test("an older binary, or one that hasn't said, pulls as text -- it refuses the stream's flag -- its folder from its words, as 4i did")
    func olderPullsAsText() async throws {
        #expect(fixture("datasets-pull-stream-older", "err").hasPrefix("The following arguments were not expected: stream-json --output-format"))
        for vocabulary in [Self.older, nil] {
            let job = ScriptedJob()
            let (model, _, starter) = await training(vocabulary: vocabulary, jobs: [job])
            model.newPull()
            model.pullRef = "someone/heat-notes"
            model.pull()
            #expect(starter.starts == [["datasets", "pull", "someone/heat-notes"]] && starter.streamed == [false])
            for line in fixture("datasets-pull-handmade", "txt").split(separator: "\n", omittingEmptySubsequences: false) {
                job.say(.said(String(line)))
            }
            job.end()
            #expect(await eventually { model.pulled != nil })
            #expect(model.pulled?.path == Self.landed && model.pulled?.summary == "2 file(s), 3 MiB")
        }
    }
}
