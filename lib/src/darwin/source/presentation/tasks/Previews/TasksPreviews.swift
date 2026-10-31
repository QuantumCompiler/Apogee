#if DEBUG
import BusinessJobs
import Foundation

/// The Tasks place's view-model for its previews, over the recorded documents
/// (ADR 0004): nothing starts the binary.
@MainActor
enum TasksPreview {
    /// The recorded task's id.
    static let task = "task-20261021-011456"

    /// `task list` once the recorded task has run, as
    /// tests/presentation/tasks/TasksViewModelTests.swift holds it.
    static let listed = #"{"object":"list","data":[{"goal":"say hello","id":"task-20261021-011456","rounds_budget":2,"rounds_used":2,"status":"exhausted"}],"total":1}"#

    /// `task list` on an empty ledger, as the same tests hold it.
    static let empty = #"{"object":"list","data":[],"total":0}"#

    /// The clock at the recorded run's own stamps: every one of its
    /// transitions is at 2026-10-21T01:14:56Z.
    static let recorded = Date(timeIntervalSince1970: 1_792_545_296)

    static func runner(listing list: String) -> PreviewRunner {
        PreviewRunner(reads: [
            (["task", "list"], list),
            (["task", "status"], TasksDocuments.taskStatus),
            (["task", "recipes"], TasksDocuments.recipes),
            (["task", "recipes", "show", "digest-folder"], TasksDocuments.recipeShow),
        ])
    }

    /// The recorded run in its first round -- the plan recorded, round 1
    /// begun, its answer half streamed: the first 16 lines of its stream, each
    /// decoded as the jobs core decodes a streaming job's stdout.
    static var firstRound: [JobUpdate] {
        TasksDocuments.taskRun.split(separator: "\n").prefix(16).map { .event(JobEvent.decode(line: String($0))) }
    }

    /// The two tasks GUI 4h recorded from the branch's binary: one killed
    /// (SIGKILL) in its first round, and one that spent its one round.
    static let interrupted = "task-20261026-060911"
    static let exhausted = "task-20261026-060918"

    /// Tasks over 4h's recordings: `task list` with the killed task, and
    /// each task's `task status`.
    static func stopped(listing list: String = TasksDocuments.listInterrupted) -> TasksViewModel {
        let runner = PreviewRunner(reads: [
            (["task", "list"], list),
            (["task", "status", interrupted], TasksDocuments.statusInterrupted),
            (["task", "status", exhausted], TasksDocuments.statusExhausted),
        ])
        return TasksViewModel(runner: runner, starter: PreviewJobs(), now: { recorded })
    }

    /// The two tasks GUI M16 recorded from the branch's binary (the CLI's
    /// M32): one killed (SIGKILL) in its first round -- its `task list` row
    /// saying `interrupted` -- beside one that spent its rounds.
    static let killed = "task-20261027-023023-2"

    /// Tasks over M16's recordings: the list with the killed task's row, and
    /// its `task status`.
    static func killedRows() -> TasksViewModel {
        let runner = PreviewRunner(reads: [
            (["task", "list"], TasksDocuments.listKilled),
            (["task", "status", killed], TasksDocuments.statusKilled),
        ])
        return TasksViewModel(runner: runner, starter: PreviewJobs(), now: { recorded })
    }

    static func tasks(listing list: String = listed, said: [JobUpdate] = []) -> TasksViewModel {
        TasksViewModel(runner: runner(listing: list), starter: PreviewJobs(said: said), now: { recorded })
    }

    /// The new-task sheet filled as the recorded run was typed (`task run
    /// "say hello" --require mock --rounds 2`), and started.
    static func startRecorded(_ tasks: TasksViewModel) {
        fillRecorded(tasks)
        tasks.start()
    }

    static func fillRecorded(_ tasks: TasksViewModel) {
        tasks.form.goal = "say hello"
        tasks.form.requires = "mock"
        tasks.form.rounds = 2
        tasks.form.tools = false
    }
}
#endif
