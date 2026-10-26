import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Synchronization
import Testing

@testable import PresentationTasks

/// The real binary's stream for a task on the mock backend: a plan, two
/// rounds, one check passing, the budget spent (`task run "say hello"
/// --require mock --rounds 2 --output-format stream-json`).
let recordedRun: [String] = {
    let fixture = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("Fixtures/task-run.jsonl")
    return ((try? String(contentsOf: fixture, encoding: .utf8)) ?? "").split(separator: "\n").map(String.init)
}()

let recordedStatus: String = {
    let fixture = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("Fixtures/task-status.json")
    return (try? String(contentsOf: fixture, encoding: .utf8)) ?? ""
}()

@Suite("TasksViewModel", .timeLimit(.minutes(1)))
@MainActor
struct TasksViewModelTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    @Test("the sheet assembles exactly the flags a hand-typed run takes")
    func sheetArgv() {
        var form = NewTaskForm()
        #expect(form.arguments == nil)
        form.goal = "Make tests/test_parsers.py pass on Python 3.12"
        #expect(form.arguments == ["task", "run", "--rounds", "8", "--tools", "--output-format", "stream-json", "--", "Make tests/test_parsers.py pass on Python 3.12"])
        #expect(!form.hasChecks)
        form.requires = "62 passed\n\n  ruff clean "
        form.requireFiles = "report.txt"
        form.rounds = 40
        form.allowWrites = true
        form.allowCommands = true
        form.model = "local"
        #expect(form.arguments == [
            "task", "run",
            "--require", "62 passed", "--require", "ruff clean", "--require-file", "report.txt",
            "--rounds", "32", "--tools", "--allow", "write_file", "--allow", "edit_file", "--allow", "run_command",
            "-m", "local", "--output-format", "stream-json", "--", "Make tests/test_parsers.py pass on Python 3.12",
        ])
        #expect(form.hasChecks)
        form.tools = false
        form.allowWrites = false
        form.allowCommands = false
        form.rounds = 0
        #expect(form.arguments?.contains("--tools") == false)
        #expect(form.arguments?[form.arguments!.firstIndex(of: "--rounds")! + 1] == "1")
    }

    @Test("a recorded run folds into its ledger: the history, the plan, the rounds, the check, the finish")
    func recordedFold() {
        var state = TaskRunState()
        #expect(recordedRun.count == 30)
        for line in recordedRun { state.apply(.event(JobEvent.decode(line: line))) }
        state.apply(.ended(JobEnd(exit: ChildExit(reason: .exited, status: 1), stderrTail: BoundedTail(), interrupted: false)))
        #expect(state.taskID?.hasPrefix("task-") == true)
        #expect(state.transitions.first?.event == "created", "the history comes first, as the ledger wrote it")
        #expect(state.transitions.map(\.event).contains("plan_recorded"))
        #expect(state.transitions.last?.status == "exhausted")
        #expect(state.plan == "mock response")
        #expect(state.orderedRounds.map(\.kind) == ["plan", "execute", "correct"])
        #expect(state.checks.map(\.passed) == [true])
        #expect(state.roundsUsed == 2)
        #expect(state.finished?.status == "exhausted")
        #expect(state.finished?.reason.hasPrefix("the round budget (2) is spent") == true)
        #expect(state.view?.goal == "say hello")
        #expect(state.answer == "mock response")
    }

    @Test("a task is started from the sheet, followed live, and the list read back when it ends")
    func startFollowAndReadBack() async {
        let runner = ScriptedRunner()
        runner.reading(["task", "list"], #"{"object":"list","data":[],"total":0}"#)
        let job = ScriptedJob()
        let jobs = ScriptedJobs([job])
        let model = TasksViewModel(runner: runner, starter: jobs)
        await model.connect(binary: Self.binary)
        #expect(model.emptyText?.hasPrefix("No tasks yet.") == true)
        model.form.goal = "say hello"
        model.form.requires = "mock"
        model.form.folder = URL(fileURLWithPath: "/Users/someone/work")
        model.sheetOpen = true
        model.start()
        #expect(!model.sheetOpen)
        #expect(jobs.starts.first?.arguments == ["task", "run", "--require", "mock", "--rounds", "8", "--tools", "--output-format", "stream-json", "--", "say hello"])
        #expect(jobs.starts.first?.folder?.path == "/Users/someone/work")
        #expect(model.form == NewTaskForm())
        for line in recordedRun.prefix(3) { job.say(.event(JobEvent.decode(line: line))) }
        #expect(await eventually { model.selected?.hasPrefix("task-") == true })
        let id = model.selected!
        #expect(model.run(for: id)?.isRunning == true)
        #expect(model.shown?.goal == "say hello", "the live view, from task_started")
        #expect(model.listed.first?.id == id, "listed before the ledger's next read catches it")
        runner.reading(["task", "list"], #"{"object":"list","data":[{"goal":"say hello","id":"\#(id)","rounds_budget":8,"rounds_used":2,"status":"exhausted"}],"total":1}"#)
        runner.reading(["task", "status"], recordedStatus)
        job.end(status: 1)
        #expect(await eventually { model.tasks.count == 1 })
        #expect(runner.calls.suffix(2) == ["read task list", "read task status \(id)"])
    }

    @Test("halt, cancel and resume are the verbs, read back after")
    func controls() async {
        let runner = ScriptedRunner()
        runner.reading(["task", "list"], #"{"object":"list","data":[{"goal":"g","id":"task-1","rounds_budget":8,"rounds_used":3,"status":"running"}],"total":1}"#)
        runner.reading(["task", "status"], recordedStatus)
        let job = ScriptedJob()
        let jobs = ScriptedJobs([job])
        var said: [String] = []
        let model = TasksViewModel(runner: runner, starter: jobs)
        model.say = { said.append($0) }
        await model.connect(binary: Self.binary)
        await model.select("task-1")
        await model.halt("task-1")
        #expect(runner.calls.contains("run task halt task-1"))
        await model.cancel("task-1")
        #expect(runner.calls.contains("run task cancel task-1"))
        #expect(said == ["It stops when this round ends", "Stopping now"])
        model.resume("task-1")
        #expect(jobs.starts.first?.arguments == ["task", "resume", "task-1", "--output-format", "stream-json"])
        #expect(TasksViewModel.canStop("running") && !TasksViewModel.canStop("done"))
        #expect(TasksViewModel.canResume("halted") && !TasksViewModel.canResume("done"))
    }

    @Test("tasks from elsewhere are reads, on coming into view; the status document is the read's")
    func readsOnFocus() async throws {
        let runner = ScriptedRunner()
        runner.reading(["task", "list"], #"{"object":"list","data":[{"goal":"say hello","id":"task-20261021-011456","rounds_budget":2,"rounds_used":2,"status":"exhausted"}],"total":1}"#)
        runner.reading(["task", "status"], recordedStatus)
        let model = TasksViewModel(runner: runner, starter: ScriptedJobs([]), now: { Date(timeIntervalSince1970: 1_792_545_000) })
        await model.connect(binary: Self.binary)
        await model.select("task-20261021-011456")
        let view = try #require(model.shown)
        #expect(view.goal == "say hello" && view.status == "exhausted")
        #expect(view.rounds.map(\.kind) == ["plan", "execute", "correct"])
        #expect(view.checks.first?.description == "the answer contains \"mock\"")
        #expect(!view.isRunning)
        #expect(model.elapsed(view).hasSuffix(" s"), "time so far is the app's clock over the ledger's stamps")
        let before = runner.calls.count
        await model.appeared()
        #expect(runner.calls.count == before + 2)
    }
}
