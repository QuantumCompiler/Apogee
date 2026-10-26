import BusinessJobs
import BusinessReads
import Foundation
import Testing

@testable import PresentationTasks

/// A person's goal is never an option (GUI 4g): on the branch binary `task
/// run "-x goal"` was "not expected"; the goal goes after `--`, every flag --
/// the stream's among them -- before it, and the expansion a person reads is
/// the same line.
@Suite("Task text after --", .timeLimit(.minutes(1)))
@MainActor
struct TaskTextTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    @Test("a dash-leading goal is the goal: the checks, rounds, tools, model and stream flags before --, the goal after it")
    func dashGoal() {
        var form = NewTaskForm()
        form.goal = "-x make the parser pass"
        form.requires = "--rounds 1"
        form.model = "local"
        #expect(form.arguments == [
            "task", "run", "--require", "--rounds 1", "--rounds", "8", "--tools", "-m", "local", "--output-format", "stream-json",
            "--", "-x make the parser pass",
        ])
    }

    @Test("started, the run is that line on the jobs core")
    func started() async {
        let runner = ScriptedRunner()
        runner.reading(["task", "list"], #"{"object":"list","data":[],"total":0}"#)
        let job = ScriptedJob()
        let jobs = ScriptedJobs([job])
        let model = TasksViewModel(runner: runner, starter: jobs)
        await model.connect(binary: Self.binary)
        model.form.goal = "--help"
        model.start()
        #expect(jobs.starts.first?.arguments == ["task", "run", "--rounds", "8", "--tools", "--output-format", "stream-json", "--", "--help"])
        job.end()
    }
}
