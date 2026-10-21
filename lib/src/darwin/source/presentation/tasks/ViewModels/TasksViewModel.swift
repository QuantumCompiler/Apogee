import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Observation

/// The Tasks place: the tasks the ledger holds, a live run's plan, rounds,
/// checks and activity, the controls, and the new-task sheet.
///
/// SwiftUI-free (ADR 0002). A run's view is its events -- the ledger's
/// transitions, one for one -- never inference; tasks started elsewhere are
/// reads, refreshed on coming into view, never polled; every control is the
/// verb a person types.
@MainActor
@Observable
public final class TasksViewModel {
    public enum Listing: Equatable {
        case notRead
        case reading
        case read
        case failed(String)
        case missing
    }

    /// One run the app started (`task run` / `task resume`), followed live.
    @Observable
    public final class Run: Identifiable {
        public let id: Int
        public let arguments: [String]
        public fileprivate(set) var state = TaskRunState()
        fileprivate var job: (any JobRunning)?

        init(id: Int, arguments: [String]) {
            self.id = id
            self.arguments = arguments
        }

        public var isRunning: Bool { job != nil }
    }

    public private(set) var tasks: [TaskSummary] = []
    public private(set) var listing: Listing = .notRead
    public private(set) var selected: String?
    /// The selected task's `task status`, when it is not one followed live.
    public private(set) var status: TaskView?
    public private(set) var statusFailure: String?
    public private(set) var runs: [Run] = []
    public var sheetOpen = false
    public var form = NewTaskForm()
    public private(set) var failure: String?
    public var say: (@MainActor (String) -> Void)?

    private let runner: any CommandRunning
    private let starter: any JobStarting
    private let now: () -> Date
    private var binary: URL?
    private var nextRun = 0

    public init(runner: any CommandRunning, starter: any JobStarting, now: @escaping () -> Date = Date.init) {
        self.runner = runner
        self.starter = starter
        self.now = now
    }

    // MARK: - Reading

    public func connect(binary: URL?) async {
        self.binary = binary
        if binary == nil {
            listing = .missing
            return
        }
        await refresh()
    }

    /// Tasks came into view: read again.
    public func appeared() async {
        guard binary != nil else { return }
        await refresh()
    }

    /// `task list`, and the selected task's `task status`.
    public func refresh() async {
        guard let binary else { return }
        if listing != .read { listing = .reading }
        let outcome = await runner.read(binary: binary, ["task", "list"])
        if let document = outcome.document {
            tasks = TaskSummary.list(from: document)
            listing = .read
        } else {
            listing = .failed(outcome.failure ?? "the list could not be read")
        }
        if let selected { await readStatus(selected) }
    }

    private func readStatus(_ id: String) async {
        guard let binary else { return }
        let outcome = await runner.read(binary: binary, ["task", "status", id])
        guard selected == id else { return }
        if let document = outcome.document, let view = TaskView(document: document) {
            status = view
            statusFailure = nil
        } else {
            statusFailure = outcome.failure ?? "the task's status could not be read"
        }
    }

    public func select(_ id: String) async {
        selected = id
        status = nil
        statusFailure = nil
        await readStatus(id)
    }

    /// The running tasks' children, for the monitor.
    public var children: [(label: String, pid: Int32)] {
        runs.compactMap { run in
            run.job?.processIdentifier.map { ("Task: \(run.state.view?.goal ?? run.state.taskID ?? "starting")", $0) }
        }
    }

    /// The live run following `id`, when the app started it.
    public func run(for id: String) -> Run? {
        runs.first { $0.state.taskID == id }
    }

    /// What the detail pane shows for the selection: the live run's view
    /// while one follows it, else the read.
    public var shown: TaskView? {
        if let selected, let run = run(for: selected), let view = run.state.view { return view }
        return status
    }

    /// The tasks to list: the ledger's, and any run the app started that the
    /// ledger's last read has not caught yet.
    public var listed: [TaskSummary] {
        let known = Set(tasks.map(\.id))
        let live = runs.compactMap { run -> TaskSummary? in
            guard let view = run.state.view, !known.contains(view.id) else { return nil }
            return TaskSummary(
                id: view.id, goal: view.goal, status: run.state.status, roundsUsed: run.state.roundsUsed ?? view.roundsUsed,
                roundsBudget: view.roundsBudget)
        }
        return live + tasks
    }

    public var emptyText: String? {
        switch listing {
        case .notRead, .reading: return nil
        case .missing: return "Apogee isn't installed, so there are no tasks to list."
        case .failed(let message): return "The tasks could not be read: \(message)"
        case .read: return tasks.isEmpty && runs.isEmpty ? "No tasks yet. Start one: a goal, the checks that say it is done, and what it may do." : nil
        }
    }

    // MARK: - Starting

    /// Starts the sheet's task: `task run …`, followed live.
    public func start() {
        guard let arguments = form.arguments else { return }
        sheetOpen = false
        follow(arguments, folder: form.folder)
        form = NewTaskForm()
    }

    /// `task resume <id>`, followed live.
    public func resume(_ id: String) {
        follow(["task", "resume", id, "--output-format", "stream-json"], folder: nil)
    }

    private func follow(_ arguments: [String], folder: URL?) {
        guard let binary else { return }
        nextRun += 1
        let run = Run(id: nextRun, arguments: arguments)
        runs.insert(run, at: 0)
        let job = starter.startJob(binary: binary, arguments: arguments, streaming: true, folder: folder)
        run.job = job
        Task {
            for await update in job.updates {
                run.state.apply(update)
                if let id = run.state.taskID, selected == nil || selected == "" {
                    selected = id
                }
            }
            run.job = nil
            await refresh()
        }
    }

    // MARK: - Controls

    /// Stop after this round: `task halt <id>`.
    public func halt(_ id: String) async {
        await command(["task", "halt", id], done: "It stops when this round ends")
    }

    /// Stop now: `task cancel <id>`.
    public func cancel(_ id: String) async {
        await command(["task", "cancel", id], done: "Stopping now")
    }

    private func command(_ arguments: [String], done: String) async {
        guard let binary else { return }
        let outcome = await runner.command(binary: binary, arguments)
        await refresh()
        if let failure = outcome.failure {
            self.failure = failure
            say?("That didn't work: \(failure)")
        } else {
            failure = nil
            say?(done)
        }
    }

    public func dismissFailure() {
        failure = nil
    }

    /// Time so far: the app's own clock, from the task's creation to now --
    /// or to its last change once it has stopped.
    public func elapsed(_ view: TaskView) -> String {
        guard let start = Self.date(view.createdAt) else { return "" }
        let end = view.isRunning ? now() : (Self.date(view.updatedAt) ?? now())
        let seconds = max(0, Int(end.timeIntervalSince(start)))
        return seconds < 60 ? "\(seconds) s" : seconds < 3600 ? "\(seconds / 60) min \(seconds % 60) s" : "\(seconds / 3600) h \(seconds % 3600 / 60) min"
    }

    static func date(_ text: String) -> Date? {
        try? Date(text, strategy: .iso8601)
    }

    /// Whether a task can be stopped, resumed.
    public static func canStop(_ status: String) -> Bool { TaskView.running.contains(status) }
    public static func canResume(_ status: String) -> Bool {
        ["halted", "cancelled", "stalled", "failed", "exhausted"].contains(status)
    }
}

/// The new-task sheet: a goal, the checks that say it is done, the round
/// budget, and what it may do -- granted up front, since a task never asks.
public struct NewTaskForm: Equatable {
    public var goal = ""
    /// Text the answer must contain, one per line.
    public var requires = ""
    /// Files that must exist, not empty, one per line.
    public var requireFiles = ""
    /// 1 to 32.
    public var rounds = 8
    /// Let the model call tools; what reads needs no grant.
    public var tools = true
    /// Grant writing and editing files (`--allow write_file`, `edit_file`).
    public var allowWrites = false
    /// Grant running commands (`--allow run_command`).
    public var allowCommands = false
    /// A backend or model; empty is the default.
    public var model = ""
    /// The folder it works in; the home folder when `nil`.
    public var folder: URL?

    public init() {}

    static func lines(_ text: String) -> [String] {
        text.split(whereSeparator: \.isNewline).map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty }
    }

    /// The run, as a person would type it; `nil` until it has a goal.
    public var arguments: [String]? {
        let goal = goal.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !goal.isEmpty else { return nil }
        var arguments = ["task", "run", goal]
        for text in Self.lines(requires) { arguments += ["--require", text] }
        for file in Self.lines(requireFiles) { arguments += ["--require-file", file] }
        arguments += ["--rounds", String(min(32, max(1, rounds)))]
        if tools || allowWrites || allowCommands {
            arguments.append("--tools")
            if allowWrites { arguments += ["--allow", "write_file", "--allow", "edit_file"] }
            if allowCommands { arguments += ["--allow", "run_command"] }
        }
        let model = model.trimmingCharacters(in: .whitespaces)
        if !model.isEmpty { arguments += ["-m", model] }
        return arguments + ["--output-format", "stream-json"]
    }

    /// Whether it has a check: a task with none is done when the model says so.
    public var hasChecks: Bool { !Self.lines(requires).isEmpty || !Self.lines(requireFiles).isEmpty }
}

/// A live run folded from its stream, and nothing else: the ledger's
/// transitions as they arrive, the plan, each round, the checks, the grants,
/// and what the turn in flight is saying.
public struct TaskRunState: Equatable {
    public struct Transition: Equatable {
        public let at: String
        public let event: String
        public let status: String
        public let round: Int?
        public let detail: String?
    }

    /// How the run stopped, as `task_finished` said it.
    public struct Finished: Equatable {
        public let status: String
        public let reason: String
    }

    public struct Grant: Equatable {
        public let round: Int
        public let tool: String
        public let target: String
    }

    public private(set) var taskID: String?
    /// The view as `task_started` gave it, kept current by each event.
    public private(set) var view: TaskView?
    public private(set) var transitions: [Transition] = []
    public private(set) var plan: String?
    public private(set) var rounds: [Int: TaskView.Round] = [:]
    public private(set) var checks: [TaskView.Check] = []
    public private(set) var grants: [Grant] = []
    public private(set) var roundsUsed: Int?
    /// The answer streaming in the turn now.
    public private(set) var answer = ""
    /// Lines the run said: notices, tool lines, a turn's error.
    public private(set) var activity: [String] = []
    public private(set) var finished: Finished?
    /// How the child ended, once it has.
    public private(set) var exit: JobEnd?

    public init() {}

    /// The status now: the last transition's.
    public var status: String { transitions.last?.status ?? view?.status ?? "starting" }

    public mutating func apply(_ update: JobUpdate) {
        switch update {
        case .ended(let end):
            exit = end
            if finished == nil, !end.succeeded {
                activity.append(end.stderrTail.lastLine ?? end.detail ?? "the task's apogee ended (\(end.exit.map { "\($0)" } ?? "did not start"))")
            }
        case .said(let line):
            activity.append(line)
        case .busy:
            break
        case .event(let event):
            apply(event)
        }
    }

    private mutating func apply(_ event: JobEvent) {
        switch event {
        case .notice(let text): activity.append(text)
        case .error(let message): activity.append(message)
        case .other(let type, let fields): apply(type, fields)
        default: break
        }
    }

    private mutating func apply(_ type: String, _ fields: [String: JSONValue]) {
        if let id = fields["task_id"]?.stringValue { taskID = id }
        if let transition = fields["transition"] { record(transition) }
        if let round = fields["round"], round.objectValue != nil {
            let parsed = TaskView.round(round)
            rounds[parsed.round] = parsed
        }
        if fields["checks"] != nil { checks = TaskView.checks(fields["checks"]) }
        if let used = fields["rounds_used"]?.intValue { roundsUsed = used }
        switch type {
        case "task_started":
            for entry in fields["history"]?.arrayValue ?? [] { record(entry, at: transitions.count - (fields["transition"] == nil ? 0 : 1)) }
            view = fields["task"].flatMap(TaskView.init)
            if let view, checks.isEmpty { checks = view.checks }
        case "task_plan":
            if let text = fields["plan"]?.stringValue { plan = text }
        case "task_round":
            if fields["transition"]?["event"]?.stringValue == "round_started" { answer = "" }
        case "task_grant":
            grants.append(
                Grant(
                    round: fields["round"]?.intValue ?? 0, tool: fields["tool"]?.stringValue ?? "",
                    target: fields["target"]?.stringValue ?? ""))
        case "task_finished":
            finished = Finished(status: fields["status"]?.stringValue ?? status, reason: fields["reason"]?.stringValue ?? "")
            view = fields["task"].flatMap(TaskView.init) ?? view
        case "answer_start":
            answer = ""
        case "answer_delta":
            answer += fields["text"]?.stringValue ?? ""
        case "tool_status":
            if let text = fields["text"]?.stringValue { activity.append(text) }
        default:
            break
        }
    }

    private mutating func record(_ value: JSONValue, at index: Int? = nil) {
        let transition = Transition(
            at: value["at"]?.stringValue ?? "", event: value["event"]?.stringValue ?? "",
            status: value["status"]?.stringValue ?? "", round: value["round"]?.intValue,
            detail: value["detail"]?.stringValue)
        if let index, index >= 0, index <= transitions.count {
            transitions.insert(transition, at: index)
        } else {
            transitions.append(transition)
        }
    }

    /// The rounds in order: the plan first.
    public var orderedRounds: [TaskView.Round] {
        rounds.keys.sorted().map { rounds[$0]! }
    }
}
