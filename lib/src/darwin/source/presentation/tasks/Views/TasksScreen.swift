import BusinessReads
import PresentationShell
import PresentationStyles
import SwiftUI

/// The Tasks place: the list, a task's plan, rounds, checks and activity,
/// and the new-task sheet.
public struct TasksScreen: View {
    @Bindable private var model: TasksViewModel

    public init(model: TasksViewModel) {
        self.model = model
    }

    public var body: some View {
        VStack(spacing: 0) {
            ScreenHeader(title: "Tasks") {
                Button("New task…") { model.sheetOpen = true }
                    .keyboardShortcut("t", modifiers: [.command, .shift])
            }
            Divider()
            HStack(spacing: 0) {
                TaskList(model: model).frame(width: 300)
                Divider()
                TaskDetail(model: model).frame(maxWidth: .infinity)
            }
        }
        .sheet(isPresented: $model.sheetOpen) {
            NewTaskSheet(model: model)
        }
        .alert("That didn't work", isPresented: Binding(get: { model.failure != nil }, set: { if !$0 { model.dismissFailure() } })) {
            Button("OK") { model.dismissFailure() }
        } message: {
            Text(model.failure ?? "")
        }
    }
}

/// Every task, running first as the ledger lists them.
struct TaskList: View {
    @Bindable var model: TasksViewModel

    var body: some View {
        ScrollView {
            LazyVStack(alignment: .leading, spacing: 1) {
                ForEach(model.starting) { run in
                    HStack {
                        ProgressView().controlSize(.small)
                        Text("Starting a task…").font(.apogeeSmall).foregroundStyle(Palette.textSecondary)
                    }
                    .padding(Spacing.m)
                }
                ForEach(model.listed) { task in
                    let current = model.selected == task.id
                    Button {
                        Task { await model.select(task.id) }
                    } label: {
                        VStack(alignment: .leading, spacing: 2) {
                            Text(task.goal).font(.apogeeBody).foregroundStyle(Palette.textPrimary).lineLimit(2)
                            HStack(spacing: Spacing.xs) {
                                let interrupted = model.interrupted(task)
                                if task.isRunning || model.inFlight(task.id) {
                                    Circle().fill(Palette.accent).frame(width: 6, height: 6)
                                }
                                // A task killed mid-round says so from its row
                                // (the CLI's M32), as its page does.
                                Text(TaskStatusWords.say(task.status) + (interrupted ? "," : ""))
                                if interrupted {
                                    Text("interrupted").foregroundStyle(Palette.warning)
                                }
                                Text("· \(task.roundsUsed) of \(task.roundsBudget) rounds")
                            }
                            .font(.apogeeMeta)
                            .foregroundStyle(Palette.textTertiary)
                        }
                        .padding(.horizontal, Spacing.m)
                        .padding(.vertical, Spacing.s)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .background(current ? Palette.selection : .clear, in: RoundedRectangle(cornerRadius: Radius.row))
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                    .contextMenu {
                        if model.resumableRow(task) {
                            Button("Resume") { Task { await model.resumeRow(task.id) } }
                        }
                    }
                }
                if let truncation = model.truncation {
                    // Past the newest 50 (`task list`'s `total`): every task
                    // the ledger holds is a click away.
                    HStack(spacing: Spacing.xs) {
                        Text(truncation + " ·").font(.apogeeSmall).foregroundStyle(Palette.textTertiary)
                        Button("Show all") { Task { await model.showAll() } }
                            .buttonStyle(.link)
                            .font(.apogeeSmall)
                    }
                    .padding(Spacing.m)
                }
                if let empty = model.emptyText {
                    Text(empty).font(.apogeeSmall).foregroundStyle(Palette.textTertiary).padding(Spacing.m)
                }
            }
            .padding(Spacing.s)
        }
        .background(Palette.sidebar)
    }
}

/// One task: its goal and status, the controls, the plan, the rounds, the
/// checks, the grants and what the turn in flight says.
struct TaskDetail: View {
    @Bindable var model: TasksViewModel

    var body: some View {
        if let id = model.selected, let view = model.shown {
            // Every part below is the view-model's answer: the app's run
            // while it follows the task, else the newer of the read and the
            // run's last view. The run itself keeps only its Activity.
            let run = model.run(for: id)
            let live = model.following(id)
            let status = live?.state.status ?? view.status
            ScrollView {
                VStack(alignment: .leading, spacing: Spacing.l) {
                    VStack(alignment: .leading, spacing: Spacing.xs) {
                        Text(view.goal).font(.apogeeHeading).foregroundStyle(Palette.textPrimary)
                        Text([live == nil ? TaskStatusWords.say(view) : TaskStatusWords.say(status), "\(live?.state.roundsUsed ?? view.roundsUsed) of \(view.roundsBudget) rounds", model.elapsed(view), view.id]
                            .filter { !$0.isEmpty }.joined(separator: " · "))
                            .font(.apogeeMeta.monospaced())
                            .foregroundStyle(Palette.textTertiary)
                    }
                    HStack(spacing: Spacing.s) {
                        if model.stoppable(id) {
                            Button("Stop after this round") { Task { await model.halt(id) } }
                            Button("Stop now") { Task { await model.cancel(id) } }
                        }
                        if model.resuming(id) {
                            // The app's resume is starting up: one per click.
                            Button("Resuming…") {}.disabled(true)
                            Text("in \(view.folder), where it was started").font(.apogeeMeta).foregroundStyle(Palette.textTertiary).lineLimit(1).truncationMode(.middle)
                        } else if model.resumable(id) {
                            Button("Resume") { model.resume(id) }
                            Text("in \(view.folder), where it was started").font(.apogeeMeta).foregroundStyle(Palette.textTertiary).lineLimit(1).truncationMode(.middle)
                        }
                    }
                    if let ending = model.ending(of: id) {
                        Text(ending.text).font(.apogeeBody).foregroundStyle(ending.done ? Palette.accent : Palette.warning)
                    }
                    if let grants = model.grants(of: id) {
                        section("Grants") { Text(grants).font(.apogeeCode).textSelection(.enabled) }
                    }
                    let plan = model.plan(of: id)
                    if !plan.isEmpty {
                        section("Plan") { Text(plan).font(.apogeeBody).textSelection(.enabled) }
                    }
                    let checks = model.checks(of: id)
                    if !checks.isEmpty {
                        section("Checks") {
                            ForEach(Array(checks.enumerated()), id: \.offset) { _, check in
                                Label {
                                    VStack(alignment: .leading, spacing: 1) {
                                        Text(check.description).font(.apogeeBody)
                                        if !check.detail.isEmpty {
                                            Text(check.detail).font(.apogeeMeta).foregroundStyle(Palette.textTertiary)
                                        }
                                    }
                                } icon: {
                                    Image(systemName: !check.ran ? "circle" : check.passed ? "checkmark.circle.fill" : "xmark.circle")
                                        .foregroundStyle(!check.ran ? Palette.textTertiary : check.passed ? Palette.accent : Palette.error)
                                }
                            }
                        }
                    }
                    let rounds = model.rounds(of: id)
                    if !rounds.isEmpty {
                        section("Rounds") {
                            ForEach(rounds, id: \.round) { round in
                                HStack(alignment: .firstTextBaseline, spacing: Spacing.m) {
                                    Text(round.round == 0 ? "plan" : "\(round.round)")
                                        .font(.apogeeMeta.monospaced())
                                        .frame(width: 36, alignment: .leading)
                                    Text(round.kind).font(.apogeeSmall).frame(width: 64, alignment: .leading)
                                    Text(round.outcome.replacingOccurrences(of: "_", with: " "))
                                        .font(.apogeeSmall)
                                        .foregroundStyle(round.outcome == "in_flight" ? Palette.accent : Palette.textSecondary)
                                    Text("\(round.checksPassed) checks passed").font(.apogeeMeta).foregroundStyle(Palette.textTertiary)
                                    if let denied = TasksViewModel.deniedOnRow(round) {
                                        Text(denied).font(.apogeeMeta).foregroundStyle(Palette.warning)
                                    }
                                    Spacer()
                                    Text(round.tokens > 0 ? "\(round.tokensEstimated ? "~" : "")\(round.tokens) tokens" : "")
                                        .font(.apogeeMeta.monospacedDigit())
                                        .foregroundStyle(Palette.textTertiary)
                                }
                            }
                        }
                    }
                    let allowed = model.allowed(of: id)
                    if !allowed.isEmpty {
                        section("Allowed") {
                            ForEach(Array(allowed.enumerated()), id: \.offset) { _, use in
                                Text(use.line).font(.apogeeCode).textSelection(.enabled)
                            }
                        }
                    }
                    let denied = model.denied(of: id)
                    if !denied.isEmpty {
                        section("Denied") {
                            ForEach(Array(denied.enumerated()), id: \.offset) { _, use in
                                Text(use.line).font(.apogeeCode).foregroundStyle(Palette.warning).textSelection(.enabled)
                            }
                        }
                    }
                    if let run, run.isRunning || !run.state.activity.isEmpty {
                        section("Activity") {
                            ForEach(Array(run.state.activity.suffix(12).enumerated()), id: \.offset) { _, line in
                                Text(line).font(.apogeeCode).foregroundStyle(Palette.textSecondary).textSelection(.enabled)
                            }
                            if run.state.activity.isEmpty && run.state.answer.isEmpty {
                                Text("Waiting for the turn to say something.").font(.apogeeSmall).foregroundStyle(Palette.textTertiary)
                            }
                            if !run.state.answer.isEmpty {
                                Text(run.state.answer).font(.apogeeBody).foregroundStyle(Palette.textPrimary).textSelection(.enabled)
                            }
                        }
                    }
                    if let failure = model.statusFailure {
                        Text(failure).font(.apogeeSmall).foregroundStyle(Palette.error)
                    }
                }
                .padding(Spacing.xl)
                .frame(maxWidth: Layout.conversationMax, alignment: .leading)
                .frame(maxWidth: .infinity)
            }
        } else {
            EmptyState(symbol: "checklist", title: "Tasks", message: "A goal, the checks that say it is done, and what it may do -- then Apogee plans, works in rounds, checks and corrects.")
                .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }

    private func section<Content: View>(_ title: String, @ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: Spacing.s) {
            Text(title.uppercased())
                .font(.system(size: TypeScale.label, weight: .medium, design: .monospaced))
                .tracking(0.8)
                .foregroundStyle(Palette.textTertiary)
            content()
        }
    }
}

/// The new task: the goal -- written here, or a recipe's -- its checks, its
/// budget, and what it may do. A recipe's expansion is shown before it runs.
struct NewTaskSheet: View {
    @Bindable var model: TasksViewModel
    @State private var choosingFolder = false

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.m) {
            Text("New task").font(.apogeeHeading)
            Form {
                Picker("Recipe", selection: Binding(get: { model.form.recipe?.name }, set: { name in Task { await model.choose(recipe: name) } })) {
                    Text("None: write the goal").tag(String?.none)
                    ForEach(model.recipes) { recipe in
                        Text(recipe.name).tag(String?.some(recipe.name))
                    }
                }
                if let recipe = model.form.recipe {
                    LabeledContent("Does") {
                        Text(recipe.description).fixedSize(horizontal: false, vertical: true)
                    }
                    if recipe.takesInput {
                        LabeledContent("Input") {
                            VStack(alignment: .leading, spacing: Spacing.xs) {
                                TextField("Input", text: $model.form.input, axis: .vertical).labelsHidden().lineLimit(1...3)
                                Text(recipe.input).font(.apogeeSmall).foregroundStyle(Palette.textTertiary)
                            }
                        }
                    }
                    if !recipe.allow.isEmpty {
                        LabeledContent("Suggests") {
                            VStack(alignment: .leading, spacing: Spacing.xs) {
                                ForEach(recipe.allow, id: \.self) { grant in
                                    Toggle("Allow \(grant)", isOn: Binding(
                                        get: { model.form.grants.contains(grant) },
                                        set: { if $0 { model.form.grants.insert(grant) } else { model.form.grants.remove(grant) } }))
                                }
                                Text("Given only if ticked: a grant the recipe suggests is never assumed.")
                                    .font(.apogeeSmall)
                                    .foregroundStyle(Palette.textTertiary)
                            }
                        }
                    }
                } else {
                    TextField("Goal", text: $model.form.goal, axis: .vertical).lineLimit(2...4)
                }
                TextField("The answer must contain (one per line)", text: $model.form.requires, axis: .vertical).lineLimit(1...4)
                TextField("These files must exist (one per line)", text: $model.form.requireFiles, axis: .vertical).lineLimit(1...4)
                Stepper("Up to \(model.form.rounds) rounds after the plan", value: $model.form.rounds, in: 1...32)
                if model.form.recipe == nil {
                    Toggle("Let the model use tools", isOn: $model.form.tools)
                    Toggle("Allow it to write and edit files", isOn: $model.form.allowWrites)
                    Toggle("Allow it to run commands", isOn: $model.form.allowCommands)
                }
                TextField("Model", text: $model.form.model, prompt: Text("the configured default"))
                LabeledContent("Works in") {
                    HStack {
                        Text(model.form.folder?.path ?? "your home folder").lineLimit(1).truncationMode(.middle)
                        Button("Choose…") { choosingFolder = true }
                    }
                }
            }
            if let failure = model.recipeFailure {
                Text(failure).font(.apogeeSmall).foregroundStyle(Palette.error)
            }
            Text("Tasks never ask. A tool that would ask is denied unless it is allowed here, and every use is recorded.")
                .font(.apogeeSmall)
                .foregroundStyle(Palette.textSecondary)
            if !model.form.hasChecks {
                Text("With no check, the task is done when the model says it is.")
                    .font(.apogeeSmall)
                    .foregroundStyle(Palette.warning)
            }
            if let arguments = model.form.arguments {
                Text(NewTaskForm.typed(arguments))
                    .font(.apogeeCode)
                    .foregroundStyle(Palette.textTertiary)
                    .textSelection(.enabled)
            }
            if let expansion = model.form.expansion {
                VStack(alignment: .leading, spacing: Spacing.xs) {
                    Text("The same run as typing")
                        .font(.apogeeSmall)
                        .foregroundStyle(Palette.textSecondary)
                    Text(NewTaskForm.typed(expansion))
                        .font(.apogeeCode)
                        .foregroundStyle(Palette.textSecondary)
                        .textSelection(.enabled)
                        .lineLimit(8)
                }
                .padding(Spacing.s)
                .frame(maxWidth: .infinity, alignment: .leading)
                .background(Palette.code, in: RoundedRectangle(cornerRadius: Radius.control))
            }
            HStack {
                Spacer()
                Button("Cancel", role: .cancel) { model.sheetOpen = false }.keyboardShortcut(.cancelAction)
                Button("Start") { model.start() }
                    .keyboardShortcut(.return, modifiers: .command)
                    .disabled(model.form.arguments == nil)
            }
        }
        .padding(Spacing.xl)
        .frame(width: 640)
        .task { await model.readRecipes() }
        .fileImporter(isPresented: $choosingFolder, allowedContentTypes: [.folder]) { result in
            if case .success(let url) = result { model.form.folder = url }
        }
    }
}

/// A task's status in plain words.
enum TaskStatusWords {
    /// A read's status: one killed mid-round says so, as `task status` does.
    static func say(_ view: TaskView) -> String {
        view.interrupted ? "\(say(view.status)), interrupted" : say(view.status)
    }

    static func say(_ status: String) -> String {
        switch status {
        case "planning": "planning"
        case "running": "running"
        case "done": "done"
        case "exhausted": "out of rounds"
        case "stalled": "stalled"
        case "failed": "failed"
        case "halted": "stopped after a round"
        case "cancelled": "stopped"
        case "starting": "starting"
        default: status
        }
    }
}

#if DEBUG
#Preview("A task out of rounds, read") {
    let tasks = TasksPreview.tasks()
    TasksScreen(model: tasks)
        .frame(width: 1100, height: 680)
        .task {
            await tasks.connect(binary: previewBinary)
            await tasks.select(TasksPreview.task)
        }
}

#Preview("No tasks yet") {
    let tasks = TasksPreview.tasks(listing: TasksPreview.empty)
    TasksScreen(model: tasks)
        .frame(width: 1100, height: 600)
        .task { await tasks.connect(binary: previewBinary) }
}

#Preview("The list: a task, read") {
    let tasks = TasksPreview.tasks()
    TaskList(model: tasks)
        .frame(width: 300, height: 360)
        .task {
            await tasks.connect(binary: previewBinary)
            await tasks.select(TasksPreview.task)
        }
}

#Preview("The list: a task starting") {
    let tasks = TasksPreview.tasks(listing: TasksPreview.empty)
    TaskList(model: tasks)
        .frame(width: 300, height: 360)
        .task {
            await tasks.connect(binary: previewBinary)
            TasksPreview.startRecorded(tasks)
        }
}

#Preview("The list: Apogee not installed") {
    let tasks = TasksPreview.tasks()
    TaskList(model: tasks)
        .frame(width: 300, height: 360)
        .task { await tasks.connect(binary: nil) }
}

#Preview("A run in its first round") {
    let tasks = TasksPreview.tasks(listing: TasksPreview.empty, said: TasksPreview.firstRound)
    TaskDetail(model: tasks)
        .frame(width: 800, height: 680)
        .task {
            await tasks.connect(binary: previewBinary)
            TasksPreview.startRecorded(tasks)
        }
}

#Preview("A task out of rounds, read from the ledger") {
    let tasks = TasksPreview.tasks()
    TaskDetail(model: tasks)
        .frame(width: 800, height: 680)
        .task {
            await tasks.connect(binary: previewBinary)
            await tasks.select(TasksPreview.task)
        }
}

#Preview("A task killed mid-round: Resume, in its folder") {
    let tasks = TasksPreview.stopped()
    TasksScreen(model: tasks)
        .frame(width: 1100, height: 560)
        .task {
            await tasks.connect(binary: previewBinary)
            await tasks.select(TasksPreview.interrupted)
        }
}

#Preview("The list: a task killed mid-round, its row saying interrupted") {
    let tasks = TasksPreview.killedRows()
    TaskList(model: tasks)
        .frame(width: 300, height: 360)
        .task { await tasks.connect(binary: previewBinary) }
}

#Preview("A killed task's row chosen: interrupted in the list and on its page") {
    let tasks = TasksPreview.killedRows()
    TasksScreen(model: tasks)
        .frame(width: 1100, height: 560)
        .task {
            await tasks.connect(binary: previewBinary)
            await tasks.select(TasksPreview.killed)
        }
}

#Preview("A task killed mid-round, its page") {
    let tasks = TasksPreview.stopped()
    TaskDetail(model: tasks)
        .frame(width: 800, height: 520)
        .task {
            await tasks.connect(binary: previewBinary)
            await tasks.select(TasksPreview.interrupted)
        }
}

#Preview("An exhausted task: no Resume") {
    let tasks = TasksPreview.stopped(listing: TasksPreview.empty)
    TaskDetail(model: tasks)
        .frame(width: 800, height: 560)
        .task {
            await tasks.connect(binary: previewBinary)
            await tasks.select(TasksPreview.exhausted)
        }
}

#Preview("A denied round, its grants and the calls they let through, read") {
    let tasks = TasksPreview.sweep()
    TaskDetail(model: tasks)
        .frame(width: 800, height: 640)
        .task {
            await tasks.connect(binary: previewBinary)
            await tasks.select(TasksPreview.uses)
        }
}

#Preview("A run live: a call its grant let through, its round in flight") {
    let tasks = TasksPreview.sweep(said: TasksPreview.stream(TasksDocuments.runUses, prefix: 32))
    TaskDetail(model: tasks)
        .frame(width: 800, height: 640)
        .task {
            await tasks.connect(binary: previewBinary)
            tasks.form.goal = "Write the report"
            tasks.form.allowCommands = true
            tasks.start()
        }
}

#Preview("The list: the newest 50 of 60, with Show all") {
    let tasks = TasksPreview.sweep()
    TaskList(model: tasks)
        .frame(width: 300, height: 520)
        .task { await tasks.connect(binary: previewBinary) }
}

#Preview("A refused start: the CLI's words in the alert") {
    let tasks = TasksPreview.sweep(said: [TasksPreview.ended(1, stderr: TasksDocuments.runRefused)])
    TasksScreen(model: tasks)
        .frame(width: 1100, height: 560)
        .task {
            await tasks.connect(binary: previewBinary)
            TasksPreview.startRefused(tasks)
        }
}

#Preview("A resume starting: Resuming…, and the stops") {
    let tasks = TasksPreview.sweep()
    TaskDetail(model: tasks)
        .frame(width: 800, height: 560)
        .task {
            await tasks.connect(binary: previewBinary)
            await tasks.select(TasksPreview.stalled)
            tasks.resume(TasksPreview.stalled)
        }
}

#Preview("A resumed task live: its plan and every earlier round kept") {
    let tasks = TasksPreview.sweep(said: TasksPreview.stream(TasksDocuments.resume, prefix: 8))
    TaskDetail(model: tasks)
        .frame(width: 800, height: 640)
        .task {
            await tasks.connect(binary: previewBinary)
            await tasks.select(TasksPreview.stalled)
            tasks.resume(TasksPreview.stalled)
        }
}

#Preview("A refused resume: its line kept on the task's page") {
    let tasks = TasksPreview.sweep(said: [TasksPreview.ended(1, stderr: TasksDocuments.resumeRefused)])
    TaskDetail(model: tasks)
        .frame(width: 800, height: 560)
        .task {
            await tasks.connect(binary: previewBinary)
            await tasks.select(TasksPreview.stalledGranted)
            tasks.resume(TasksPreview.stalledGranted)
        }
}

#Preview("The app's run ended halted; a fresh read says done") {
    let tasks = TasksPreview.sweep(said: TasksPreview.stream(TasksDocuments.runHalted) + [TasksPreview.ended(1)])
    TaskDetail(model: tasks)
        .frame(width: 800, height: 640)
        .task {
            await tasks.connect(binary: previewBinary)
            tasks.form.goal = "Answer when asked"
            tasks.form.requires = "42"
            tasks.form.tools = false
            tasks.start()
        }
}

#Preview("No task chosen") {
    TaskDetail(model: TasksPreview.tasks())
        .frame(width: 800, height: 500)
}

#Preview("New task: nothing written yet") {
    let tasks = TasksPreview.tasks()
    NewTaskSheet(model: tasks)
        .task {
            await tasks.connect(binary: previewBinary)
            await tasks.readRecipes()
        }
}

#Preview("New task: a goal and its check") {
    let tasks = TasksPreview.tasks()
    NewTaskSheet(model: tasks)
        .task {
            await tasks.connect(binary: previewBinary)
            await tasks.readRecipes()
            TasksPreview.fillRecorded(tasks)
        }
}

#Preview("New task: a recipe, its input given") {
    let tasks = TasksPreview.tasks()
    NewTaskSheet(model: tasks)
        .task {
            await tasks.connect(binary: previewBinary)
            await tasks.readRecipes()
            await tasks.choose(recipe: "digest-folder")
            tasks.form.input = "docs"
        }
}
#endif
