#if DEBUG
import BusinessJobs
import Foundation
import InfrastructureProcess

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

    /// GUI 6g's tasks, recorded from `apogee-dev` on the mock backend over
    /// a sandbox home (2026-11-08): one with a call its grant let through
    /// and one refused with nobody present; one stalled after its plan and
    /// two rounds, then resumed to done; one granted a write and stalled,
    /// whose resume the config then refused; one halted after round 1 by
    /// the app's run, then finished from a terminal after round 3; and 56
    /// more, so the ledger holds 60.
    static let uses = "task-20261109-021211"
    static let stalled = "task-20261109-021224"
    static let stalledGranted = "task-20261109-021236"
    static let halted = "task-20261109-021308"

    /// The clock at those recordings.
    static let swept = Date(timeIntervalSince1970: 1_794_190_400)

    /// Tasks over 6g's recordings: the newest 50 of 60, every one on Show
    /// all, and each recorded task's `task status`.
    static func sweep(said: [JobUpdate] = []) -> TasksViewModel {
        let runner = PreviewRunner(reads: [
            (["task", "list"], TasksDocuments.listNewest50),
            (["task", "list", "--all"], TasksDocuments.listAll),
            (["task", "status", uses], TasksDocuments.statusUses),
            (["task", "status", stalled], TasksDocuments.statusStalled),
            (["task", "status", stalledGranted], TasksDocuments.statusStalledGranted),
            (["task", "status", halted], TasksDocuments.statusHaltedDone),
        ])
        return TasksViewModel(runner: runner, starter: PreviewJobs(said: said), now: { swept })
    }

    /// The first `count` lines of a recorded stream -- all of it when
    /// `nil` -- decoded as the jobs core decodes a streaming job's stdout.
    static func stream(_ text: String, prefix count: Int? = nil) -> [JobUpdate] {
        let lines = text.split(separator: "\n").map(String.init)
        return lines.prefix(count ?? lines.count).map { .event(JobEvent.decode(line: $0)) }
    }

    /// A run that ended `status` with `stderr` as its tail: a refusal, in
    /// machine mode, writes nothing else.
    static func ended(_ status: Int32, stderr: String = "") -> JobUpdate {
        var tail = BoundedTail()
        tail.append(Data(stderr.utf8))
        return .ended(JobEnd(exit: ChildExit(reason: .exited, status: status), stderrTail: tail, interrupted: false))
    }

    /// The new-task sheet filled as the refused run was recorded -- "Allow
    /// it to write and edit files" ticked, `--tools --allow write_file
    /// --allow edit_file`, against a config whose `permissions.write_file`
    /// is `deny` -- and started.
    static func startRefused(_ tasks: TasksViewModel) {
        tasks.form.goal = "x"
        tasks.form.allowWrites = true
        tasks.start()
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
