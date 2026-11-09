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
        /// The task a resume continues, read once from its arguments when it
        /// was started (`resumedTask(in:)`): known from the click, before its
        /// stream names it. `nil` for a new run.
        public let resumes: String?
        public fileprivate(set) var state = TaskRunState()
        fileprivate var job: (any JobRunning)?

        init(id: Int, arguments: [String]) {
            self.id = id
            self.arguments = arguments
            resumes = TasksViewModel.resumedTask(in: arguments)
        }

        public var isRunning: Bool { job != nil }

        /// The task this run is for: the one its stream names once
        /// `task_started` arrives, else the one a resume was started on --
        /// so a resume still starting up, or one the CLI refused before it
        /// started, is that task's run. `nil` for a new run until its stream
        /// names its task, and for one refused before it had one.
        public var target: String? { state.taskID ?? resumes }
    }

    /// One gated call on a task's page, as `task status` prints it under
    /// `allowed:` or `denied:` -- `round 1: write_file on report.txt --
    /// nobody present to allow it`.
    public struct TaskUse: Equatable, Sendable {
        public enum Kind: Equatable, Sendable {
            case allowed
            case denied
        }

        public let kind: Kind
        /// The turn it was made in: 0 for the plan.
        public let round: Int
        /// Whether that turn was the plan, which `task status` names `plan`.
        public let inPlan: Bool
        public let use: TaskView.Use

        /// The call and its authority in `task status`'s words.
        public var words: String { kind == .allowed ? TaskUseWords.allowed(use) : TaskUseWords.denied(use) }

        /// The row as `task status` prints it: its turn, then its words.
        public var line: String { (inPlan ? "plan" : "round \(round)") + ": " + words }
    }

    /// Why a task stopped, or that its process is gone, as the page says it
    /// beneath the header.
    public struct Ending: Equatable, Sendable {
        public let text: String
        /// Done says it in the accent; every other ending as a warning.
        public let done: Bool
    }

    public private(set) var tasks: [TaskSummary] = []
    /// How many tasks the ledger holds in all, as `task list` says
    /// (`total`); it lists only the newest 50 unless asked for `--all`.
    public private(set) var listTotal: Int?
    /// Whether the list reads `task list --all`: once Show all is asked, for
    /// the life of the place. The first read is the CLI's default, as a
    /// person typing `task list` sees it.
    public private(set) var showingAll = false
    public private(set) var listing: Listing = .notRead
    public private(set) var selected: String?
    /// The selected task's `task status`, when it is not one followed live.
    public private(set) var status: TaskView?
    public private(set) var statusFailure: String?
    public private(set) var runs: [Run] = []
    public var sheetOpen = false
    public var form = NewTaskForm()
    /// The recipes the sheet offers, `task recipes` as last read.
    public private(set) var recipes: [TaskRecipe] = []
    /// Why the recipes, or the one chosen, could not be read -- the CLI's words.
    public private(set) var recipeFailure: String?
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

    /// `task list` -- with `--all` once Show all was asked -- and the
    /// selected task's `task status`.
    public func refresh() async {
        guard let binary else { return }
        if listing != .read { listing = .reading }
        let outcome = await runner.read(binary: binary, ["task", "list"] + (showingAll ? ["--all"] : []))
        if let document = outcome.document {
            tasks = TaskSummary.list(from: document)
            listTotal = document.total
            listing = .read
        } else {
            listing = .failed(outcome.failure ?? "the list could not be read")
        }
        if let selected { await readStatus(selected) }
    }

    /// Show all: every task the ledger holds, `task list --all`, from now on.
    public func showAll() async {
        showingAll = true
        await refresh()
    }

    /// The list's last line when it shows fewer tasks than the ledger holds:
    /// "The newest 50 of 60 tasks", beside Show all. `nil` when it lists
    /// every one.
    public var truncation: String? {
        guard listing == .read, !showingAll, let total = listTotal, total > tasks.count else { return nil }
        return "The newest \(tasks.count) of \(total) tasks"
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

    /// The app's newest run for `id` -- in flight or ended, a resume the CLI
    /// refused among them -- matched on the task it targets (`Run.target`).
    public func run(for id: String) -> Run? {
        runs.first { $0.target == id }
    }

    /// The app's run of `id` while its child still runs -- a resume from the
    /// click on, before its stream names the task.
    private func live(_ id: String) -> Run? {
        runs.first { $0.isRunning && $0.target == id }
    }

    /// The app's run of `id` in flight with its stream under way: its child
    /// running, and its `task_started` in, so its own view is the newest
    /// word on the task. Before that -- a resume starting up -- the page
    /// still draws the read.
    public func following(_ id: String) -> Run? {
        live(id).flatMap { $0.state.view == nil ? nil : $0 }
    }

    /// Whether the app's own run of `id` is in flight, starting up or under
    /// way: its row's running mark.
    public func inFlight(_ id: String) -> Bool { live(id) != nil }

    /// Whether the app's resume of `id` is starting up: its child running,
    /// its stream not yet at `task_started`. The page says "Resuming…" in
    /// place of Resume, and a second click starts nothing.
    public func resuming(_ id: String) -> Bool {
        live(id).map { $0.resumes == id && $0.state.view == nil } ?? false
    }

    /// The runs starting a new task whose stream has not named it yet: each
    /// a "Starting a task…" row. A resume is its task's own row's, never one
    /// of these.
    public var starting: [Run] {
        runs.filter { $0.isRunning && $0.target == nil }
    }

    /// What the detail pane shows for the selection: the app's run's view
    /// while it follows the task, else the last word on it (`lastView`) --
    /// which takes over once the run ends, so a run killed mid-round, or a
    /// task finished elsewhere after the app's run ended, reads as the
    /// ledger says it.
    public var shown: TaskView? {
        guard let selected else { return nil }
        if let view = following(selected)?.state.view { return view }
        return lastView(of: selected)
    }

    /// The last word on `id` when no run of the app's follows it: the newer
    /// of its `task status` and the view the app's newest run of it last
    /// held, by `updated_at` -- the read on a tie, or when either stamp
    /// can't be read. So an ended run's stale view never stands over a
    /// fresh read, and a read that failed never hides what the run said.
    private func lastView(of id: String) -> TaskView? {
        let read = status.flatMap { $0.id == id ? $0 : nil }
        let ran = runs.first { $0.target == id && $0.state.view != nil }?.state.view
        guard let ran else { return read }
        guard let read else { return ran }
        return Self.newer(ran, than: read) ? ran : read
    }

    /// Whether `view` was written after `other`, by their `updated_at`;
    /// `false` when they tie or either can't be read.
    static func newer(_ view: TaskView, than other: TaskView) -> Bool {
        guard let at = date(view.updatedAt), let otherAt = date(other.updatedAt) else { return false }
        return at > otherAt
    }

    /// Whether the detail offers the stops: the app's own run in flight --
    /// a resume still starting up among them, whose stop the CLI keeps
    /// (its 57u) -- or a task the ledger says a process runs now, never one
    /// killed mid-round.
    public func stoppable(_ id: String) -> Bool {
        if let live = live(id) {
            guard live.state.view != nil else { return live.resumes == id }
            return TaskView.running.contains(live.state.status)
        }
        return lastView(of: id).map(Self.canStop) ?? false
    }

    /// Whether the detail offers Resume: exactly the states `task resume`
    /// continues, with the folder it resumes in known, and no run of the
    /// app's in flight -- a resume starting up included.
    public func resumable(_ id: String) -> Bool {
        guard live(id) == nil else { return false }
        return lastView(of: id).map(Self.canResume) ?? false
    }

    // MARK: - The page's parts
    //
    // Each part of a task's page from one source: the app's run while it
    // follows the task (`following`), else the last word on it (`lastView`).
    // An ended run keeps only its Activity, its own record of what it said.

    /// Why it stopped, or that its process is gone: the run's `task_finished`
    /// while it follows the task, else the last word's reason once it is no
    /// longer running.
    public func ending(of id: String) -> Ending? {
        if let run = following(id) {
            return run.state.finished.map { Ending(text: $0.reason, done: $0.status == "done") }
        }
        guard let view = lastView(of: id) else { return nil }
        if view.interrupted {
            return Ending(text: "Its process is gone: it was killed mid-round, and Resume runs that round again.", done: false)
        }
        guard !view.reason.isEmpty, !view.isRunning else { return nil }
        return Ending(text: view.reason, done: view.status == "done")
    }

    /// The plan: recorded by the run's `task_plan`, else its view's -- a
    /// resumed task's from `task_started` -- else the last word's.
    public func plan(of id: String) -> String {
        if let run = following(id) { return run.state.plan ?? run.state.view?.plan ?? "" }
        return lastView(of: id)?.plan ?? ""
    }

    /// The checks, as the run last scored them, else the last word's.
    public func checks(of id: String) -> [TaskView.Check] {
        if let run = following(id) { return run.state.checks.isEmpty ? (run.state.view?.checks ?? []) : run.state.checks }
        return lastView(of: id)?.checks ?? []
    }

    /// Every turn in order, the plan first: the run's -- seeded with the
    /// turns its `task_started` carried, so a resumed task keeps its plan
    /// and earlier rounds -- else the last word's.
    public func rounds(of id: String) -> [TaskView.Round] {
        if let run = following(id) {
            let rounds = run.state.orderedRounds
            return rounds.isEmpty ? (run.state.view?.rounds ?? []) : rounds
        }
        return lastView(of: id)?.rounds ?? []
    }

    /// Every call let through, round by round, as `task status` lists them
    /// under `allowed:` -- and, while the run follows the task, its round in
    /// flight's `task_grant` lines until that round lands.
    public func allowed(of id: String) -> [TaskUse] {
        let landed = uses(rounds(of: id), \.allowed, .allowed)
        guard let run = following(id) else { return landed }
        return landed
            + run.state.grants.map {
                TaskUse(kind: .allowed, round: $0.round, inPlan: $0.round == 0, use: TaskView.Use(tool: $0.tool, target: $0.target, by: $0.by))
            }
    }

    /// Every call refused, round by round, as `task status` lists them under
    /// `denied:`.
    public func denied(of id: String) -> [TaskUse] {
        uses(rounds(of: id), \.denied, .denied)
    }

    /// A Rounds row's refusals: `denied:` and each in `task status`'s words,
    /// the row's own round not repeated; `nil` for a round that refused
    /// nothing.
    public static func deniedOnRow(_ round: TaskView.Round) -> String? {
        round.denied.isEmpty ? nil : "denied: " + round.denied.map(TaskUseWords.denied).joined(separator: "; ")
    }

    private func uses(_ rounds: [TaskView.Round], _ key: KeyPath<TaskView.Round, [TaskView.Use]>, _ kind: TaskUse.Kind) -> [TaskUse] {
        rounds.flatMap { round in
            round[keyPath: key].map { TaskUse(kind: kind, round: round.round, inPlan: round.kind == "plan", use: $0) }
        }
    }

    /// The Grants line, as `task status` words its `grants:`: the tools
    /// granted up front, or "none" with what that means for a task with
    /// tools; `nil` for a task without tools, which `task status` gives no
    /// such line.
    public func grants(of id: String) -> String? {
        guard let view = following(id)?.state.view ?? lastView(of: id) else { return nil }
        if !view.grants.isEmpty { return view.grants.joined(separator: ", ") }
        return view.tools ? "none -- with nobody present, a tool that asks is denied" : nil
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

    /// The list's line when it has no row to show: a run the CLI refused
    /// before it made a task leaves none.
    public var emptyText: String? {
        switch listing {
        case .notRead, .reading: return nil
        case .missing: return "Apogee isn't installed, so there are no tasks to list."
        case .failed(let message): return "The tasks could not be read: \(message)"
        case .read: return listed.isEmpty && starting.isEmpty ? "No tasks yet. Start one: a goal, the checks that say it is done, and what it may do." : nil
        }
    }

    // MARK: - Starting

    /// The sheet opened: `task recipes`, read for its picker.
    public func readRecipes() async {
        guard let binary else { return }
        let outcome = await runner.read(binary: binary, ["task", "recipes"])
        if let document = outcome.document {
            recipes = TaskRecipe.list(from: document)
            recipeFailure = nil
        } else {
            recipeFailure = outcome.failure
        }
    }

    /// Picks a recipe for the sheet -- `task recipes show <name>`, for its
    /// goal -- or none, for a goal written here. Its rounds become the
    /// sheet's; none of its suggested grants is ticked.
    public func choose(recipe name: String?) async {
        guard let name else {
            form.recipe = nil
            form.grants = []
            return
        }
        guard let binary else { return }
        let outcome = await runner.read(binary: binary, ["task", "recipes", "show", name])
        guard let document = outcome.document, let recipe = TaskRecipe(document: document) else {
            recipeFailure = outcome.failure ?? "the recipe could not be read"
            return
        }
        recipeFailure = nil
        form.recipe = recipe
        form.input = ""
        form.grants = []
        form.rounds = recipe.rounds ?? 8
    }

    /// Starts the sheet's task: `task run …`, followed live.
    public func start() {
        guard let arguments = form.arguments else { return }
        sheetOpen = false
        follow(arguments, folder: form.folder)
        form = NewTaskForm()
    }

    /// `task resume <id>`, followed live, in the folder the task was started
    /// in -- its read's: the CLI resumes a task nowhere else
    /// (`task_resume_refusal`), so a resume from the home folder failed for
    /// every task started elsewhere. Only a task `resumable` names.
    public func resume(_ id: String) {
        guard resumable(id), let view = lastView(of: id) else { return }
        follow(["task", "resume", id, "--output-format", "stream-json"], folder: URL(fileURLWithPath: view.folder, isDirectory: true))
    }

    /// The task a run's arguments resume: the id after `task resume`, as
    /// `resume(_:)` writes the line; `nil` for any other run.
    public nonisolated static func resumedTask(in arguments: [String]) -> String? {
        guard arguments.count > 2, arguments.starts(with: ["task", "resume"]) else { return nil }
        return arguments[2]
    }

    /// Whether a row says its task was killed mid-round: its `interrupted`,
    /// as the CLI's M32 puts it on every row (GUI M16) -- read with the list,
    /// no task's page asked -- while no run of the app's is in flight for
    /// it, a resume whose stream hasn't named it yet among them (its target,
    /// the same lookup the page's Resume asks). A row that can't say, from a
    /// binary before M32, reads as its status: there the task's page still
    /// says it, as 4h made it.
    public func interrupted(_ task: TaskSummary) -> Bool {
        task.interrupted == true && live(task.id) == nil
    }

    /// Whether a row's menu offers Resume: exactly the rows that say their
    /// task was killed mid-round (the default taken, GUI M16).
    public func resumableRow(_ task: TaskSummary) -> Bool { interrupted(task) }

    /// Resume from a row's menu: the task opened -- its `task status` read,
    /// for the folder it resumes in and whether it still resumes -- then
    /// `task resume` there, as its page's Resume runs it.
    public func resumeRow(_ id: String) async {
        await select(id)
        resume(id)
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
            // Refused before it started: machine mode writes the refusal to
            // stderr alone, so the run has no task and its row would vanish
            // with nothing said. Its words go to the alert, as halt's and
            // cancel's failures do; a refused resume's stay in its Activity.
            if let refusal = run.state.refusal {
                failure = refusal
                say?("That didn't work: \(refusal)")
            }
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

    /// Whether a task runs now, by its view: Stop after this round and Stop
    /// now are for a process running it.
    public static func canStop(_ view: TaskView) -> Bool { view.isRunning }

    /// Whether `task resume` continues a task, by its view: halted,
    /// cancelled, stalled, failed, or killed mid-round -- never done or
    /// exhausted -- with the folder it resumes in named.
    public static func canResume(_ view: TaskView) -> Bool { view.isResumable && !view.folder.isEmpty }
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
    /// The recipe the run is, `show`'s document -- its goal, checks and tools
    /// in place of the typed ones; `nil` for a goal written here.
    public var recipe: TaskRecipe?
    /// The recipe's input, read into its goal.
    public var input = ""
    /// The recipe's suggested grants the person gave: each an `--allow` on
    /// the line, as typing it would be -- never one they did not tick.
    public var grants: Set<String> = []

    public init() {}

    static func lines(_ text: String) -> [String] {
        text.split(whereSeparator: \.isNewline).map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty }
    }

    /// The run, as a person would type it; `nil` until it has a goal -- or,
    /// with a recipe, its input when the recipe reads one. The goal is the
    /// last word, after `--`, so the binary never reads it as a flag
    /// (`Argv`, GUI 4g); every flag goes before it.
    public var arguments: [String]? {
        if let recipe { return recipeArguments(recipe) }
        let goal = goal.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !goal.isEmpty else { return nil }
        var arguments = ["task", "run"]
        for text in Self.lines(requires) { arguments += ["--require", text] }
        for file in Self.lines(requireFiles) { arguments += ["--require-file", file] }
        arguments += ["--rounds", String(min(32, max(1, rounds)))]
        if tools || allowWrites || allowCommands {
            arguments.append("--tools")
            if allowWrites { arguments += ["--allow", "write_file", "--allow", "edit_file"] }
            if allowCommands { arguments += ["--allow", "run_command"] }
        }
        return arguments + modelArguments + ["--output-format", "stream-json"] + Argv.text(goal)
    }

    /// `task run --recipe <name>`: the recipe's own checks, tools and rounds
    /// are the CLI's to add; the line carries what the person changed.
    private func recipeArguments(_ recipe: TaskRecipe) -> [String]? {
        let input = input.trimmingCharacters(in: .whitespacesAndNewlines)
        if recipe.takesInput && input.isEmpty { return nil }
        var arguments = ["task", "run", "--recipe", recipe.name]
        if recipe.takesInput { arguments += ["--input", input] }
        for text in Self.lines(requires) { arguments += ["--require", text] }
        for file in Self.lines(requireFiles) { arguments += ["--require-file", file] }
        if let rounds = typedRounds(recipe) { arguments += ["--rounds", rounds] }
        for grant in givenGrants(recipe) { arguments += ["--allow", grant] }
        return arguments + modelArguments + ["--output-format", "stream-json"]
    }

    /// The same run as a hand-typed line -- the recipe expanded as the CLI
    /// expands it: its goal rendered, its checks before the typed ones,
    /// `--tools`, its rounds unless changed, its agent; the goal after `--`,
    /// as a written goal's. Shown, never run.
    public var expansion: [String]? {
        guard let recipe, recipeArguments(recipe) != nil,
            let goal = recipe.renderedGoal(input: input.trimmingCharacters(in: .whitespacesAndNewlines))
        else { return nil }
        var arguments = ["task", "run"]
        for text in recipe.require + Self.lines(requires) { arguments += ["--require", text] }
        for file in recipe.requireFiles + Self.lines(requireFiles) { arguments += ["--require-file", file] }
        if recipe.tools { arguments.append("--tools") }
        if let rounds = typedRounds(recipe) ?? recipe.rounds.map(String.init) { arguments += ["--rounds", rounds] }
        if !recipe.agent.isEmpty { arguments += ["--agent", recipe.agent] }
        for grant in givenGrants(recipe) { arguments += ["--allow", grant] }
        return arguments + modelArguments + Argv.text(goal)
    }

    /// The recipe's suggested grants left ungiven: said, as the CLI says them.
    public var ungivenGrants: [String] {
        guard let recipe else { return [] }
        return recipe.allow.filter { !grants.contains($0) }
    }

    private func givenGrants(_ recipe: TaskRecipe) -> [String] {
        recipe.allow.filter { grants.contains($0) }
    }

    /// `--rounds` only when the person moved it off the recipe's.
    private func typedRounds(_ recipe: TaskRecipe) -> String? {
        let rounds = min(32, max(1, rounds))
        return rounds == (recipe.rounds ?? 8) ? nil : String(rounds)
    }

    private var modelArguments: [String] {
        let model = model.trimmingCharacters(in: .whitespaces)
        return model.isEmpty ? [] : ["-m", model]
    }

    /// Whether it has a check: a task with none is done when the model says so.
    public var hasChecks: Bool {
        !Self.lines(requires).isEmpty || !Self.lines(requireFiles).isEmpty
            || !(recipe?.require.isEmpty ?? true) || !(recipe?.requireFiles.isEmpty ?? true)
    }

    /// `arguments` as a person would type them at a shell: `apogee task run
    /// 'Digest docs' --rounds 8`.
    public static func typed(_ arguments: [String]) -> String {
        (["apogee"] + arguments).map(quoted).joined(separator: " ")
    }

    static func quoted(_ word: String) -> String {
        let plain = word.allSatisfy { $0.isLetter || $0.isNumber || "-_./:=@%+,".contains($0) }
        if plain && !word.isEmpty { return word }
        return "'" + word.replacingOccurrences(of: "'", with: "'\\''") + "'"
    }
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

    /// A call a grant let through in the round in flight, as `task_grant`
    /// said it; kept only until that round's `task_round` lands, whose
    /// `allowed` then carries it.
    public struct Grant: Equatable {
        public let round: Int
        public let tool: String
        public let target: String
        /// On whose authority, as the event says: `grant`.
        public let by: String
    }

    public private(set) var taskID: String?
    /// The view as `task_started` gave it, kept current by each event.
    public private(set) var view: TaskView?
    public private(set) var transitions: [Transition] = []
    public private(set) var plan: String?
    /// Every turn by its round: those `task_started` carried -- a resumed
    /// task's plan and earlier rounds -- then each a `task_round` or
    /// `task_plan` brought.
    public private(set) var rounds: [Int: TaskView.Round] = [:]
    public private(set) var checks: [TaskView.Check] = []
    /// The round in flight's `task_grant` lines.
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

    /// Why the CLI refused the run before it started, in its words: a run
    /// that ended without success, not stopped by its Stop, whose stream
    /// never named its task -- machine mode writes such a refusal to stderr
    /// alone (`task_cmd.cpp`'s `fail_user`), so it is the bounded tail's
    /// last line, `apogee task: ` and all. `nil` for any other end.
    public var refusal: String? {
        guard let exit, !exit.succeeded, !exit.interrupted, taskID == nil else { return nil }
        return Self.endLine(exit)
    }

    /// The one line a failed end says: stderr's last, else why it never
    /// started, else its exit.
    static func endLine(_ end: JobEnd) -> String {
        end.stderrTail.lastLine ?? end.detail ?? "the task's apogee ended (\(end.exit.map { "\($0)" } ?? "did not start"))"
    }

    public mutating func apply(_ update: JobUpdate) {
        switch update {
        case .ended(let end):
            exit = end
            if finished == nil, !end.succeeded {
                activity.append(Self.endLine(end))
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
            // Landed: its `allowed` now carries what its grants let through.
            if parsed.outcome != "in_flight" { grants.removeAll { $0.round == parsed.round } }
        }
        if fields["checks"] != nil { checks = TaskView.checks(fields["checks"]) }
        if let used = fields["rounds_used"]?.intValue { roundsUsed = used }
        switch type {
        case "task_started":
            for entry in fields["history"]?.arrayValue ?? [] { record(entry, at: transitions.count - (fields["transition"] == nil ? 0 : 1)) }
            view = fields["task"].flatMap(TaskView.init)
            if let view, checks.isEmpty { checks = view.checks }
            // A resumed task's plan and earlier rounds, keyed by round: the
            // stream's own rounds add to them, never replace them.
            for turn in view?.rounds ?? [] where rounds[turn.round] == nil { rounds[turn.round] = turn }
        case "task_plan":
            if let text = fields["plan"]?.stringValue { plan = text }
        case "task_round":
            if fields["transition"]?["event"]?.stringValue == "round_started" { answer = "" }
        case "task_grant":
            grants.append(
                Grant(
                    round: fields["round"]?.intValue ?? 0, tool: fields["tool"]?.stringValue ?? "",
                    target: fields["target"]?.stringValue ?? "", by: fields["by"]?.stringValue ?? "grant"))
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

/// A gated call in `task status`'s words (`task_cmd.cpp`'s `on_target`,
/// `authority_words` and `refusal_words`), never new ones: the tool on its
/// target, then on whose authority it ran or why it was refused.
public enum TaskUseWords {
    /// `run_command on ls -- this task's grant`. An authority the CLI has no
    /// words for is shown as it is written; none at all, as the call alone.
    public static func allowed(_ use: TaskView.Use) -> String {
        let authority = authority(use.by)
        return onTarget(use) + (authority.isEmpty ? "" : " -- " + authority)
    }

    /// `write_file on report.txt -- nobody present to allow it`. A reason
    /// the CLI has no words for is left out, as it leaves it out.
    public static func denied(_ use: TaskView.Use) -> String {
        let why = refusal(use.by)
        return onTarget(use) + (why.isEmpty ? "" : " -- " + why)
    }

    /// The tool, and what it was called on when it names something.
    public static func onTarget(_ use: TaskView.Use) -> String {
        use.target.isEmpty ? use.tool : "\(use.tool) on \(use.target)"
    }

    static func authority(_ by: String) -> String {
        switch by {
        case "grant": "this task's grant"
        case "person": "the person at the terminal"
        case "config": "the config"
        default: by
        }
    }

    static func refusal(_ by: String) -> String {
        switch by {
        case "config": "the config denies it"
        case "nobody": "nobody present to allow it"
        case "person": "refused at the prompt"
        default: ""
        }
    }
}
