import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationTasks

/// GUI M16: a task killed mid-round reads interrupted in the task list, from
/// its row alone (the CLI's M32 puts `interrupted` on every `task list` row),
/// and its row's menu offers Resume -- where 4h left it reading running until
/// its page was opened. Recorded from the branch binary over a sandbox home
/// with 4h's mocks, the home written `/Users/someone`: a task SIGKILLed in its
/// first round beside one that spent its rounds (`task-list-killed.json`),
/// the same list while the first still ran (`task-list-live.json`), and the
/// installed binary's list over the same home (`task-list-killed-older.json`).
@Suite("The task list's interrupted row (GUI M16)", .timeLimit(.minutes(1)))
@MainActor
struct InterruptedRowTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let killed = "task-20261027-023023-2"
    static let exhausted = "task-20261027-023023"

    func model(list: String, jobs: [ScriptedJob] = []) async -> (TasksViewModel, ScriptedRunner, ScriptedJobs) {
        let runner = ScriptedRunner()
        runner.reading(["task", "list"], taskFixture(list))
        runner.reading(["task", "status", Self.killed], taskFixture("task-status-killed.json"))
        let jobs = ScriptedJobs(jobs)
        let model = TasksViewModel(runner: runner, starter: jobs)
        await model.connect(binary: Self.binary)
        return (model, runner, jobs)
    }

    func row(_ id: String, in model: TasksViewModel) throws -> TaskSummary {
        try #require(model.listed.first { $0.id == id }, "rows: \(model.listed.map(\.id))")
    }

    @Test("a killed task's row reads interrupted, not running, from the list alone: no task status is read for it")
    func killedRowReadsInterrupted() async throws {
        let (model, runner, _) = await model(list: "task-list-killed.json")
        let killed = try row(Self.killed, in: model)
        #expect(killed.interrupted == true, "the row says it")
        #expect(!killed.isRunning, "no running mark on a task no process runs")
        #expect(try row(Self.exhausted, in: model).interrupted == false)
        #expect(runner.calls == ["read task list"], "drawn with no per-row read: \(runner.calls)")
    }

    @Test("the killed row's menu offers Resume, and only it; Resume reads its page for the folder, then resumes there")
    func rowMenuResumes() async throws {
        let (model, runner, jobs) = await model(list: "task-list-killed.json", jobs: [ScriptedJob()])
        #expect(model.resumableRow(try row(Self.killed, in: model)))
        #expect(!model.resumableRow(try row(Self.exhausted, in: model)), "the CLI refuses an exhausted task")
        await model.resumeRow(Self.killed)
        #expect(runner.calls.contains("read task status \(Self.killed)"), "its folder is its page's")
        let start = try #require(jobs.starts.first)
        #expect(start.arguments == ["task", "resume", Self.killed, "--output-format", "stream-json"])
        #expect(start.folder?.path == "/Users/someone/work", "where it was started, as the page's Resume runs it")
        #expect(model.selected == Self.killed, "its page shows the run")
        #expect(!model.resumableRow(try row(Self.killed, in: model)), "not offered again while the app's run follows it")
    }

    @Test("while its process ran, the same row read running, with no Resume")
    func liveRowRuns() async throws {
        let (model, _, _) = await model(list: "task-list-live.json")
        let live = try row(Self.killed, in: model)
        #expect(live.interrupted == false && live.isRunning)
        #expect(!model.resumableRow(live))
    }

    @Test("a binary before M32 reads as today: the killed task's row reads running, no row Resume, no extra read")
    func olderRowsAsToday() async throws {
        let (model, runner, _) = await model(list: "task-list-killed-older.json")
        let killed = try row(Self.killed, in: model)
        #expect(killed.interrupted == nil && killed.isRunning, "the row can't say; its page still does")
        #expect(!model.resumableRow(killed))
        #expect(runner.calls == ["read task list"])
        await model.select(Self.killed)
        #expect(model.shown?.interrupted == true && model.resumable(Self.killed), "the page knows, as 4h made it")
    }
}
