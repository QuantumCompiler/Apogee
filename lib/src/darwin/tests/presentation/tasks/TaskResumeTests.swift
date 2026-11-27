import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationTasks

/// A document recorded from the branch's binary over a sandbox home, the
/// home written `/Users/someone` (tests/presentation/tasks/Fixtures).
func taskFixture(_ name: String) -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("Fixtures/\(name)")
    return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
}

/// Resume as the CLI takes it (GUI 4h): run in the folder the task was
/// started in -- `task resume` refuses anywhere else (`task_cmd.cpp`'s
/// `task_resume_refusal`) -- and offered for exactly the states it
/// continues: halted, cancelled, stalled, failed, or killed mid-round
/// (`interrupted`), never done or exhausted (`runner.cpp`'s
/// `resume_refusal`). Over two recorded views: a task SIGKILLed in its first
/// round, and one that spent its one round.
@Suite("Task resume", .timeLimit(.minutes(1)))
@MainActor
struct TaskResumeTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let interrupted = "task-20261026-060911"
    static let exhausted = "task-20261026-060918"

    func model(_ jobs: [ScriptedJob] = []) async -> (TasksViewModel, ScriptedRunner, ScriptedJobs) {
        let runner = ScriptedRunner()
        runner.reading(["task", "list"], taskFixture("task-list-interrupted.json"))
        runner.reading(["task", "status", Self.interrupted], taskFixture("task-status-interrupted.json"))
        runner.reading(["task", "status", Self.exhausted], taskFixture("task-status-exhausted.json"))
        let jobs = ScriptedJobs(jobs)
        let model = TasksViewModel(runner: runner, starter: jobs)
        await model.connect(binary: Self.binary)
        return (model, runner, jobs)
    }

    /// The recorded interrupted view with its status, interruption and
    /// process changed: the other states `task status` prints.
    static func view(status: String, interrupted: Bool = false, process: Int? = nil) -> TaskView {
        let text = taskFixture("task-status-interrupted.json")
            .replacingOccurrences(of: #""status":"running""#, with: #""status":"\#(status)""#)
            .replacingOccurrences(of: #""interrupted":true"#, with: #""interrupted":\#(interrupted)"#)
            .replacingOccurrences(of: #""process":null"#, with: #""process":\#(process.map(String.init) ?? "null")"#)
        return TaskView(document: ReadDocument(text: text)!)!
    }

    @Test("Resume runs in the folder the task was started in, as its read names it")
    func resumeRunsInItsFolder() async throws {
        let (model, _, jobs) = await model([ScriptedJob()])
        await model.select(Self.interrupted)
        model.resume(Self.interrupted)
        let start = try #require(jobs.starts.first)
        #expect(start.arguments == ["task", "resume", Self.interrupted, "--output-format", "stream-json"])
        #expect(start.folder?.path == "/Users/someone/work", "the CLI resumes a task only where it was started")
    }

    @Test("a task killed mid-round reads as interrupted: Resume offered, never the stops of a running one")
    func killedMidRoundResumes() async throws {
        let (model, _, _) = await model()
        await model.select(Self.interrupted)
        let view = try #require(model.shown)
        #expect(view.status == "running" && view.process == nil)
        #expect(TasksViewModel.canResume(view))
        #expect(!TasksViewModel.canStop(view))
        #expect(model.resumable(Self.interrupted) && !model.stoppable(Self.interrupted))
    }

    @Test("an exhausted task is never offered Resume: the CLI refuses it by construction")
    func exhaustedDoesNotResume() async throws {
        let (model, _, _) = await model()
        await model.select(Self.exhausted)
        let view = try #require(model.shown)
        #expect(view.status == "exhausted")
        #expect(!TasksViewModel.canResume(view))
        #expect(!model.resumable(Self.exhausted) && !model.stoppable(Self.exhausted))
    }

    @Test("Resume is offered for exactly the states task resume continues")
    func theCLIsSet() {
        for status in ["halted", "cancelled", "stalled", "failed"] {
            #expect(TasksViewModel.canResume(Self.view(status: status)), "\(status) resumes")
            #expect(!TasksViewModel.canStop(Self.view(status: status)), "\(status) runs nothing to stop")
        }
        for status in ["done", "exhausted", "a-status-a-later-cli-adds"] {
            #expect(!TasksViewModel.canResume(Self.view(status: status)), "\(status) does not")
        }
        for status in ["planning", "running"] {
            let live = Self.view(status: status, process: 4242)
            #expect(!TasksViewModel.canResume(live) && TasksViewModel.canStop(live), "\(status), its process running")
            let killed = Self.view(status: status, interrupted: true)
            #expect(TasksViewModel.canResume(killed) && !TasksViewModel.canStop(killed), "\(status), killed mid-round")
        }
    }

    @Test("the app's own run killed mid-round: once its child is gone, the read takes over and offers Resume")
    func ownRunKilled() async throws {
        let job = ScriptedJob()
        let (model, runner, jobs) = await model([job, ScriptedJob()])
        model.form.goal = "say hello"
        model.form.folder = URL(fileURLWithPath: "/Users/someone/work")
        model.start()
        let started = taskFixture("task-status-interrupted.json")
            .replacingOccurrences(of: #""interrupted":true"#, with: #""interrupted":false"#)
            .replacingOccurrences(of: #""process":null"#, with: #""process":4242"#)
        job.say(.event(JobEvent.decode(line: #"{"type":"task_started","task_id":"\#(Self.interrupted)","task":\#(started.trimmingCharacters(in: .newlines)),"transition":{"at":"2026-10-26T06:09:11Z","event":"started","status":"planning"}}"#)))
        #expect(await eventually { model.selected == Self.interrupted })
        #expect(model.stoppable(Self.interrupted) && !model.resumable(Self.interrupted), "its own run, in flight")
        job.end(status: 137)
        #expect(await eventually { model.run(for: Self.interrupted)?.isRunning == false && runner.calls.last == "read task status \(Self.interrupted)" })
        #expect(await eventually { model.shown?.process == nil })
        #expect(model.resumable(Self.interrupted) && !model.stoppable(Self.interrupted))
        model.resume(Self.interrupted)
        #expect(jobs.starts.count == 2)
    }
}

/// GUI 6g, entries 5 and 7: a resume is its task's run from the click --
/// the id after `task resume`, before its stream names it -- so Resume can't
/// start a second one while the first starts up (the CLI takes the task
/// lock, then builds its providers and tools, and only then emits
/// `task_started`), the stops are offered in that window (the CLI keeps
/// them since its 57u), and a resume the CLI refuses keeps its words on the
/// task's page. Recorded from `apogee-dev` on the mock backend over a
/// sandbox home: a task stalled after two rounds (`task-status-stalled.json`)
/// and one granted `write_file` and stalled (`task-status-stalled-granted.json`)
/// whose resume the config then refused (`task-resume-refused.err`).
@Suite("One resume per click, and a refused one said (GUI 6g)", .timeLimit(.minutes(1)))
@MainActor
struct ResumeStartingTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let stalled = "task-20261109-021224"
    static let granted = "task-20261109-021236"

    func model(_ jobs: [ScriptedJob]) async -> (TasksViewModel, ScriptedRunner, ScriptedJobs) {
        let runner = ScriptedRunner()
        runner.reading(["task", "list"], taskFixture("task-list-all.json"))
        runner.reading(["task", "status", Self.stalled], taskFixture("task-status-stalled.json"))
        runner.reading(["task", "status", Self.granted], taskFixture("task-status-stalled-granted.json"))
        let jobs = ScriptedJobs(jobs)
        let model = TasksViewModel(runner: runner, starter: jobs)
        await model.connect(binary: Self.binary)
        return (model, runner, jobs)
    }

    @Test("while a resume starts up, Resume is not offered, the page says Resuming, and a second click starts nothing")
    func onePerClick() async throws {
        let job = ScriptedJob()
        let (model, _, jobs) = await model([job, ScriptedJob()])
        await model.select(Self.stalled)
        #expect(model.resumable(Self.stalled) && !model.resuming(Self.stalled))
        model.resume(Self.stalled)
        #expect(jobs.starts.count == 1)
        let run = try #require(model.run(for: Self.stalled), "its run from the click")
        #expect(run.target == Self.stalled && run.state.taskID == nil)
        #expect(!model.resumable(Self.stalled) && model.resuming(Self.stalled))
        #expect(model.inFlight(Self.stalled) && model.starting.isEmpty, "its own row's, never a Starting a task row")
        model.resume(Self.stalled)
        await model.resumeRow(Self.stalled)
        #expect(jobs.starts.count == 1, "the scripted starter was asked once")
        #expect(model.shown?.status == "stalled", "the read stands until the stream names the task")
        job.say(streamed("task-resume.jsonl", prefix: 2)[1])
        #expect(await eventually { model.following(Self.stalled) != nil })
        #expect(!model.resuming(Self.stalled) && !model.resumable(Self.stalled))
        #expect(model.stoppable(Self.stalled))
        job.end()
    }

    @Test("the stops are offered while the resume starts up, sent as task halt and task cancel of the task")
    func stopsWhileStarting() async throws {
        let job = ScriptedJob()
        let (model, runner, _) = await model([job])
        await model.select(Self.stalled)
        #expect(!model.stoppable(Self.stalled), "stalled: nothing runs it")
        model.resume(Self.stalled)
        #expect(model.stoppable(Self.stalled))
        await model.halt(Self.stalled)
        #expect(runner.calls.contains("run task halt \(Self.stalled)"))
        await model.cancel(Self.stalled)
        #expect(runner.calls.contains("run task cancel \(Self.stalled)"))
        job.end(status: 130)
    }

    @Test("a resume the CLI refuses: the alert says its words, and the line stays in that task's Activity")
    func refusedResume() async throws {
        let job = ScriptedJob()
        let (model, _, jobs) = await model([job])
        var said: [String] = []
        model.say = { said.append($0) }
        await model.select(Self.granted)
        model.resume(Self.granted)
        #expect(jobs.starts.first?.arguments == ["task", "resume", Self.granted, "--output-format", "stream-json"])
        job.end(status: 1, stderr: taskFixture("task-resume-refused.err"))
        let line = "apogee task: task \(Self.granted) cannot resume: --allow write_file: the config says permissions.write_file: deny -- a task's grant is never wider than the config (its grants are held to the config and its agent as they are now)"
        #expect(await eventually { model.failure == line })
        #expect(said == ["That didn't work: \(line)"])
        let run = try #require(model.run(for: Self.granted), "the refused resume is that task's run")
        #expect(!run.isRunning && run.state.activity == [line])
        #expect(model.shown?.status == "stalled" && model.resumable(Self.granted), "the ledger is as it was: Resume offered again")
        #expect(model.grants(of: Self.granted) == "write_file")
    }
}
