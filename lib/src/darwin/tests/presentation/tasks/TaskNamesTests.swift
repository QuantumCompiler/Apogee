import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationTasks

/// Every positional of a Tasks line after `--` (GUI M23): the CLI mints ids
/// that never begin with a dash, but the rule holds for every positional, so
/// the line needs no idea of which words are ids -- and a recipe's name is
/// the person's. Before `--` the parser reads a dash-led one as an option.
@Suite("Task names", .timeLimit(.minutes(1)))
@MainActor
struct TaskNamesTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let interrupted = "task-20261026-060911"

    func model(_ jobs: [ScriptedJob] = []) async -> (TasksViewModel, ScriptedRunner, ScriptedJobs) {
        let runner = ScriptedRunner()
        runner.reading(["task", "list"], taskFixture("task-list-interrupted.json"))
        runner.reading(["task", "status", "--", Self.interrupted], taskFixture("task-status-interrupted.json"))
        let jobs = ScriptedJobs(jobs)
        let model = TasksViewModel(runner: runner, starter: jobs)
        await model.connect(binary: Self.binary)
        return (model, runner, jobs)
    }

    @Test("a task's page reads task status -- <id>, and its controls name it after --")
    func idsAfterTheEnd() async {
        let (model, runner, _) = await model()
        await model.select(Self.interrupted)
        #expect(runner.calls.contains("read task status -- \(Self.interrupted)"))
        #expect(model.status?.id == Self.interrupted && model.statusFailure == nil, "failure: \(model.statusFailure ?? "none")")
        await model.halt("-x")
        await model.cancel("-x")
        #expect(runner.calls.contains("run task halt -- -x") && runner.calls.contains("run task cancel -- -x"))
    }

    @Test("a resume is task resume --output-format stream-json -- <id>, and its target is read from after --")
    func resumeLine() async throws {
        let (model, _, jobs) = await model([ScriptedJob()])
        await model.select(Self.interrupted)
        model.resume(Self.interrupted)
        let start = try #require(jobs.starts.first)
        #expect(start.arguments == ["task", "resume", "--output-format", "stream-json", "--", Self.interrupted])
        #expect(TasksViewModel.resumedTask(in: start.arguments) == Self.interrupted)
        #expect(TasksViewModel.resumedTask(in: TasksViewModel.resumeArguments("-x")) == "-x", "a dash-led id is its target too")
        #expect(TasksViewModel.resumedTask(in: ["task", "run", "--", "task", "resume"]) == nil)
    }

    @Test("a new task's goal stays after --, the stream flag before it; the shown line is Argv.shown's")
    func newTaskLine() {
        var form = NewTaskForm()
        form.goal = "-x"
        form.rounds = 1
        form.tools = false
        form.model = "mock"
        #expect(form.arguments == ["task", "run", "--rounds", "1", "-m", "mock", "--output-format", "stream-json", "--", "-x"])
        #expect(NewTaskForm.typed(form.arguments ?? []) == "apogee task run --rounds 1 -m mock --output-format stream-json -- -x")
    }

    @Test("every line Tasks builds with -x, -h or --graph reaches the verb: each recorded from the built CLI, none the parser's refusal")
    func sweep() async {
        var lines: [[String]] = []
        for name in NameVerdicts.names {
            let job = ScriptedJob()
            let (model, runner, jobs) = await model([job])
            await model.select(name)
            await model.halt(name)
            await model.cancel(name)
            await model.choose(recipe: name)
            model.form.goal = name
            model.form.rounds = 1
            model.form.model = "mock"
            model.start()
            job.end(status: 0)
            lines += NameVerdicts.lines(ofCalls: runner.calls, separator: " ") + jobs.starts.map(\.arguments) + [TasksViewModel.resumeArguments(name)]
        }
        let (missing, refused) = NameVerdicts.judged(lines)
        #expect(missing.isEmpty, "never recorded -- add these rows to tests/presentation/shell/Fixtures/argv-name-verdicts.txt and run scripts/refresh-argv-name-verdicts.sh <the built apogee>:\n\(missing.joined(separator: "\n"))")
        #expect(refused.isEmpty, "the parser refuses:\n\(refused.joined(separator: "\n"))")
    }
}
