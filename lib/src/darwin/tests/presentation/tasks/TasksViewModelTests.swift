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
        // Halted, as the ledger now reads it: resumed, in its folder (4h).
        let halted = taskFixture("task-status-interrupted.json")
            .replacingOccurrences(of: "task-20261026-060911", with: "task-1")
            .replacingOccurrences(of: #""status":"running""#, with: #""status":"halted""#)
            .replacingOccurrences(of: #""interrupted":true"#, with: #""interrupted":false"#)
        runner.reading(["task", "status"], halted)
        await model.refresh()
        #expect(model.resumable("task-1") && !model.stoppable("task-1"))
        model.resume("task-1")
        #expect(jobs.starts.first?.arguments == ["task", "resume", "task-1", "--output-format", "stream-json"])
        #expect(jobs.starts.first?.folder?.path == "/Users/someone/work")
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

/// A recorded stream's lines, the first `count` or all of them, as the jobs
/// core hands a streaming job's stdout over.
func streamed(_ name: String, prefix count: Int? = nil) -> [JobUpdate] {
    let lines = taskFixture(name).split(separator: "\n").map(String.init)
    return lines.prefix(count ?? lines.count).map { .event(JobEvent.decode(line: $0)) }
}

/// GUI 6g: the Tasks place shows what the ledger holds. Recorded from
/// `apogee-dev` on the mock backend over a sandbox home, the home written
/// `/Users/someone` (2026-11-08): a task whose round 1 ran `ls` on its
/// grant and was refused `write_file report.txt` with nobody present
/// (`task-status-uses.json`, `.txt`, and its run, `task-run-uses.jsonl`);
/// a task stalled after its plan and two rounds (`task-status-stalled.json`)
/// and its resume to done (`task-resume.jsonl`, `task-status-resumed.json`);
/// a run halted after round 1 (`task-run-halted.jsonl`) that a terminal then
/// finished done after round 3 (`task-status-halted-done.json`); the ledger
/// at 60 tasks (`task-list-newest-50.json`, `task-list-all.json`); and a
/// run the config refused before it started (`task-run-refused.err`).
@Suite("The Tasks screen tells the truth (GUI 6g)", .timeLimit(.minutes(1)))
@MainActor
struct TasksTruthTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let uses = "task-20261109-021211"
    static let stalled = "task-20261109-021224"
    static let halted = "task-20261109-021308"

    func model(_ jobs: [ScriptedJob] = [], list: String = "task-list-all.json") async -> (TasksViewModel, ScriptedRunner, ScriptedJobs, Said) {
        let runner = ScriptedRunner()
        runner.reading(["task", "list"], taskFixture(list))
        runner.reading(["task", "status", Self.uses], taskFixture("task-status-uses.json"))
        runner.reading(["task", "status", Self.stalled], taskFixture("task-status-stalled.json"))
        let jobs = ScriptedJobs(jobs)
        let model = TasksViewModel(runner: runner, starter: jobs)
        let said = Said()
        model.say = { said.lines.append($0) }
        await model.connect(binary: Self.binary)
        return (model, runner, jobs, said)
    }

    final class Said {
        var lines: [String] = []
    }

    // MARK: - Entry 1 and 6: the uses and the grants, read

    @Test("a refused call reads in task status's words on its Rounds row and in Denied, never as JSON")
    func deniedInTheCLIsWords() async throws {
        let (model, _, _, _) = await model()
        await model.select(Self.uses)
        let round = try #require(model.rounds(of: Self.uses).last)
        #expect(TasksViewModel.deniedOnRow(round) == "denied: write_file on report.txt -- nobody present to allow it")
        #expect(model.denied(of: Self.uses).map(\.line) == ["round 1: write_file on report.txt -- nobody present to allow it"])
        #expect(model.denied(of: Self.uses).allSatisfy { !$0.line.contains("{") })
        #expect(TasksViewModel.deniedOnRow(try #require(model.rounds(of: Self.uses).first)) == nil, "the plan refused nothing")
    }

    @Test("with no run, the read draws the Grants line and every call they let through")
    func grantsAndAllowedFromTheRead() async throws {
        let (model, _, _, _) = await model()
        await model.select(Self.uses)
        #expect(model.run(for: Self.uses) == nil)
        #expect(model.grants(of: Self.uses) == "run_command")
        #expect(model.allowed(of: Self.uses).map(\.line) == ["round 1: run_command on ls -- this task's grant"])
    }

    @Test("each Allowed and Denied row is a line task status printed for the same task")
    func theCLIsLines() async throws {
        let (model, _, _, _) = await model()
        await model.select(Self.uses)
        let printed = taskFixture("task-status-uses.txt").split(separator: "\n").map(String.init)
        func section(_ heading: String) -> [String] {
            guard let start = printed.firstIndex(of: heading + ":") else { return [] }
            return printed[(start + 1)...].prefix { $0.hasPrefix("  ") }.map { $0.trimmingCharacters(in: .whitespaces) }
        }
        #expect(model.allowed(of: Self.uses).map(\.line) == section("allowed"))
        #expect(model.denied(of: Self.uses).map(\.line) == section("denied"))
        #expect(printed.contains("grants:        " + (model.grants(of: Self.uses) ?? "")))
    }

    @Test("a task with tools and no grant says none, as the CLI does; one without tools has no Grants line")
    func noGrants() async throws {
        let (model, runner, _, _) = await model()
        let granted = taskFixture("task-status-uses.json").replacingOccurrences(of: #""grants":["run_command"]"#, with: #""grants":[]"#)
        runner.reading(["task", "status", Self.uses], granted)
        await model.select(Self.uses)
        #expect(model.grants(of: Self.uses) == "none -- with nobody present, a tool that asks is denied")
        await model.select(Self.stalled)
        #expect(model.grants(of: Self.stalled) == nil)
    }

    @Test("live, a call its grant let through is Allowed while its round is in flight, once, and from the round once it lands")
    func liveGrantUntilItsRoundLands() async throws {
        let job = ScriptedJob()
        let (model, _, _, _) = await model([job])
        model.form.goal = "Write the report"
        model.form.allowCommands = true
        model.start()
        let lines = streamed("task-run-uses.jsonl")
        for line in lines.prefix(32) { job.say(line) }
        let expected = ["round 1: run_command on ls -- this task's grant"]
        #expect(await eventually { model.allowed(of: Self.uses).map(\.line) == expected })
        let run = try #require(model.following(Self.uses))
        #expect(run.state.grants.count == 1, "the round in flight's task_grant")
        job.say(lines[32])
        #expect(await eventually { run.state.grants.isEmpty }, "its round landed: its allowed carries it")
        #expect(model.allowed(of: Self.uses).map(\.line) == expected, "never twice")
        job.say(lines[33])
        job.end(status: 1)
        #expect(await eventually { model.following(Self.uses) == nil && model.status?.id == Self.uses })
        #expect(model.allowed(of: Self.uses).map(\.line) == expected)
        #expect(model.denied(of: Self.uses).map(\.line) == ["round 1: write_file on report.txt -- nobody present to allow it"])
    }

    @Test("the words for an authority the CLI has none for: shown as written when allowed, left out when denied")
    func unknownAuthority() {
        #expect(TaskUseWords.allowed(TaskView.Use(tool: "write_file", target: "a.txt", by: "person")) == "write_file on a.txt -- the person at the terminal")
        #expect(TaskUseWords.allowed(TaskView.Use(tool: "write_file", target: "a.txt", by: "config")) == "write_file on a.txt -- the config")
        #expect(TaskUseWords.allowed(TaskView.Use(tool: "write_file", target: "a.txt", by: "a-later-authority")) == "write_file on a.txt -- a-later-authority")
        #expect(TaskUseWords.allowed(TaskView.Use(tool: "fetch_url")) == "fetch_url")
        #expect(TaskUseWords.denied(TaskView.Use(tool: "run_command", target: "rm -rf x", by: "config")) == "run_command on rm -rf x -- the config denies it")
        #expect(TaskUseWords.denied(TaskView.Use(tool: "run_command", target: "ls", by: "person")) == "run_command on ls -- refused at the prompt")
        #expect(TaskUseWords.denied(TaskView.Use(tool: "run_command", target: "ls", by: "a-later-reason")) == "run_command on ls")
        #expect(TaskUseWords.denied(TaskView.Use(tool: "write_file")) == "write_file")
    }

    // MARK: - Entry 2: a resumed task keeps its plan and rounds

    @Test("a resume's stream folds to the plan, rounds 1-2 and round 3, through task_finished and after")
    func resumeFoldKeepsEveryRound() {
        var state = TaskRunState()
        let lines = streamed("task-resume.jsonl")
        #expect(lines.count == 15)
        for line in lines.prefix(2) { state.apply(line) }
        #expect(state.orderedRounds.map(\.round) == [0, 1, 2], "task_started carried three turns")
        #expect(state.orderedRounds.map(\.kind) == ["plan", "execute", "correct"])
        state.apply(lines[2])
        #expect(state.orderedRounds.map(\.round) == [0, 1, 2, 3], "round 3 added, never in place of the others")
        for line in lines.dropFirst(3) { state.apply(line) }
        state.apply(.ended(JobEnd(exit: ChildExit(reason: .exited, status: 0), stderrTail: BoundedTail(), interrupted: false)))
        #expect(state.orderedRounds.map(\.round) == [0, 1, 2, 3])
        #expect(state.orderedRounds.last?.outcome == "completed")
        #expect(state.view?.rounds.count == 4, "task_finished's view has all four")
    }

    @Test("resumed in the app, the page keeps the plan and every round, during the run -- over the read it outran -- and after")
    func resumedInTheApp() async throws {
        let job = ScriptedJob()
        let (model, runner, _, _) = await model([job])
        await model.select(Self.stalled)
        #expect(model.ending(of: Self.stalled)?.text.hasPrefix("no progress in 2 rounds") == true)
        model.resume(Self.stalled)
        let lines = streamed("task-resume.jsonl")
        for line in lines.prefix(8) { job.say(line) }
        #expect(await eventually { model.rounds(of: Self.stalled).map(\.round) == [0, 1, 2, 3] })
        #expect(model.following(Self.stalled) != nil)
        #expect(model.plan(of: Self.stalled) == "1. Look for the word.\n2. Say it.")
        #expect(model.ending(of: Self.stalled) == nil, "the run in flight wins over the stalled read it has outrun")
        #expect(model.rounds(of: Self.stalled).last?.outcome == "in_flight")
        runner.reading(["task", "status", Self.stalled], taskFixture("task-status-resumed.json"))
        for line in lines.dropFirst(8) { job.say(line) }
        job.end()
        #expect(await eventually { model.following(Self.stalled) == nil && model.status?.status == "done" })
        #expect(model.rounds(of: Self.stalled).map(\.round) == [0, 1, 2, 3])
        #expect(model.plan(of: Self.stalled) == "1. Look for the word.\n2. Say it.")
        #expect(model.ending(of: Self.stalled) == TasksViewModel.Ending(text: "every check passed and the model reported the task done, in 3 of 8 rounds", done: true))
    }

    // MARK: - Entry 3: an ended run never overrides a fresh read

    @Test("a run that ended halted after round 1, then a read saying done after round 3: the page is the read's")
    func endedRunNeverOverridesAFreshRead() async throws {
        let job = ScriptedJob()
        let (model, runner, _, _) = await model([job])
        model.form.goal = "Answer when asked"
        model.form.requires = "42"
        model.form.tools = false
        model.start()
        let lines = streamed("task-run-halted.jsonl")
        for line in lines.prefix(14) { job.say(line) }
        #expect(await eventually { model.selected == Self.halted })
        #expect(model.rounds(of: Self.halted).map(\.round) == [0, 1])
        runner.reading(["task", "status", Self.halted], taskFixture("task-status-halted-done.json"))
        for line in lines.dropFirst(14) { job.say(line) }
        job.end(status: 1)
        #expect(await eventually { model.following(Self.halted) == nil && model.status?.id == Self.halted })
        let run = try #require(model.run(for: Self.halted))
        #expect(run.state.finished?.reason == "halted by 'apogee task halt' after round 1", "the run's own record")
        #expect(model.shown?.status == "done")
        #expect(model.ending(of: Self.halted) == TasksViewModel.Ending(text: "every check passed and the model reported the task done, in 3 of 8 rounds", done: true))
        #expect(model.checks(of: Self.halted).map(\.passed) == [true])
        #expect(model.rounds(of: Self.halted).map(\.round) == [0, 1, 2, 3])
        #expect(!model.stoppable(Self.halted) && !model.resumable(Self.halted), "done: nothing to stop, nothing to resume")
    }

    @Test("a read that failed never hides what the ended run said: its last view stands")
    func failedReadKeepsTheRunsWord() async throws {
        let job = ScriptedJob()
        let (model, _, _, _) = await model([job])
        model.form.goal = "Answer when asked"
        model.start()
        for line in streamed("task-run-halted.jsonl") { job.say(line) }
        job.end(status: 1)
        #expect(await eventually { model.following(Self.halted) == nil && model.statusFailure != nil })
        #expect(model.ending(of: Self.halted) == TasksViewModel.Ending(text: "halted by 'apogee task halt' after round 1", done: false))
        #expect(model.rounds(of: Self.halted).map(\.round) == [0, 1])
    }

    @Test("the newer of two views is by updated_at; a tie or an unreadable stamp is the read's")
    func newerByUpdatedAt() throws {
        let done = try #require(TaskView(document: ReadDocument(text: taskFixture("task-status-halted-done.json"))!))
        let stalled = try #require(TaskView(document: ReadDocument(text: taskFixture("task-status-stalled.json"))!))
        #expect(TasksViewModel.newer(done, than: stalled))
        #expect(!TasksViewModel.newer(stalled, than: done))
        #expect(!TasksViewModel.newer(done, than: done))
    }

    // MARK: - Entry 4: every task past the newest 50

    @Test("past the newest 50 the list says how many exist, and Show all reads task list --all from then on")
    func listPast50() async throws {
        let (model, runner, _, _) = await model(list: "task-list-newest-50.json")
        #expect(model.tasks.count == 50 && model.listTotal == 60)
        #expect(model.truncation == "The newest 50 of 60 tasks")
        runner.reading(["task", "list", "--all"], taskFixture("task-list-all.json"))
        await model.showAll()
        #expect(runner.calls.last == "read task list --all")
        #expect(model.tasks.count == 60 && model.truncation == nil)
        #expect(model.tasks.contains { $0.id == Self.uses }, "an older task, now reachable")
        await model.appeared()
        #expect(runner.calls.last == "read task list --all", "stays on it")
    }

    @Test("a list that holds every task says nothing more")
    func wholeList() async {
        let (model, _, _, _) = await model()
        #expect(model.tasks.count == 60 && model.truncation == nil)
    }

    // MARK: - Entry 5: a refused start says why

    @Test("a start the CLI refuses: the alert and the toast say its words, and it leaves no row")
    func refusedStart() async throws {
        let job = ScriptedJob()
        let (model, _, jobs, said) = await model([job], list: "task-list-all.json")
        let before = model.listed.count
        model.form.goal = "x"
        model.form.allowWrites = true
        model.start()
        #expect(jobs.starts.first?.arguments.contains("write_file") == true)
        #expect(model.starting.count == 1)
        job.end(status: 1, stderr: taskFixture("task-run-refused.err"))
        let line = "apogee task: --allow write_file: the config says permissions.write_file: deny -- a task's grant is never wider than the config"
        #expect(await eventually { model.failure == line })
        #expect(said.lines == ["That didn't work: \(line)"])
        #expect(model.starting.isEmpty && model.listed.count == before, "the ledger holds no task, so no row")
    }

    @Test("a refused start on an empty ledger still says there are no tasks")
    func refusedStartEmpty() async {
        let runner = ScriptedRunner()
        runner.reading(["task", "list"], #"{"object":"list","data":[],"total":0}"#)
        let job = ScriptedJob()
        let model = TasksViewModel(runner: runner, starter: ScriptedJobs([job]))
        await model.connect(binary: Self.binary)
        model.form.goal = "x"
        model.start()
        #expect(model.emptyText == nil, "starting: its row stands")
        job.end(status: 1, stderr: taskFixture("task-run-refused.err"))
        #expect(await eventually { model.failure != nil })
        #expect(model.emptyText?.hasPrefix("No tasks yet.") == true)
    }

    @Test("a run that ends without success after task_started, or one stopped, raises no alert")
    func onlyARefusalAlerts() async {
        let job = ScriptedJob()
        let (model, _, _, said) = await model([job])
        model.form.goal = "say hello"
        model.start()
        for line in recordedRun { job.say(.event(JobEvent.decode(line: line))) }
        job.end(status: 1, stderr: "task task-20261021-011456 exhausted -- the round budget (2) is spent\n")
        #expect(await eventually { model.runs.first?.isRunning == false })
        try? await Task.sleep(for: .milliseconds(50))
        #expect(model.failure == nil && said.lines.isEmpty)
        var stopped = TaskRunState()
        var tail = BoundedTail()
        tail.append(Data("apogee task: interrupted\n".utf8))
        stopped.apply(.ended(JobEnd(exit: ChildExit(reason: .exited, status: 130), stderrTail: tail, interrupted: true)))
        #expect(stopped.refusal == nil, "stopped by its Stop is not a refusal")
    }
}
