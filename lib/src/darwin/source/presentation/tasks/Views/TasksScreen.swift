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
                ForEach(model.runs.filter { $0.isRunning && $0.state.taskID == nil }) { run in
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
                                if task.isRunning || model.run(for: task.id)?.isRunning == true {
                                    Circle().fill(Palette.accent).frame(width: 6, height: 6)
                                }
                                Text(TaskStatusWords.say(task.status))
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
            let run = model.run(for: id)
            let status = run?.state.status ?? view.status
            ScrollView {
                VStack(alignment: .leading, spacing: Spacing.l) {
                    VStack(alignment: .leading, spacing: Spacing.xs) {
                        Text(view.goal).font(.apogeeHeading).foregroundStyle(Palette.textPrimary)
                        Text([TaskStatusWords.say(status), "\(run?.state.roundsUsed ?? view.roundsUsed) of \(view.roundsBudget) rounds", model.elapsed(view), view.id]
                            .filter { !$0.isEmpty }.joined(separator: " · "))
                            .font(.apogeeMeta.monospaced())
                            .foregroundStyle(Palette.textTertiary)
                    }
                    HStack(spacing: Spacing.s) {
                        if TasksViewModel.canStop(status) {
                            Button("Stop after this round") { Task { await model.halt(id) } }
                            Button("Stop now") { Task { await model.cancel(id) } }
                        }
                        if TasksViewModel.canResume(status) && run?.isRunning != true {
                            Button("Resume") { model.resume(id) }
                        }
                    }
                    if let finished = run?.state.finished {
                        Text(finished.reason).font(.apogeeBody).foregroundStyle(finished.status == "done" ? Palette.accent : Palette.warning)
                    } else if !view.reason.isEmpty && !view.isRunning {
                        Text(view.reason).font(.apogeeBody).foregroundStyle(view.status == "done" ? Palette.accent : Palette.warning)
                    }
                    let plan = run?.state.plan ?? view.plan
                    if !plan.isEmpty {
                        section("Plan") { Text(plan).font(.apogeeBody).textSelection(.enabled) }
                    }
                    let checks = run.map(\.state.checks).flatMap { $0.isEmpty ? nil : $0 } ?? view.checks
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
                    let rounds = run.map(\.state.orderedRounds).flatMap { $0.isEmpty ? nil : $0 } ?? view.rounds
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
                                    if !round.denied.isEmpty {
                                        Text("denied: \(round.denied.joined(separator: ", "))").font(.apogeeMeta).foregroundStyle(Palette.warning)
                                    }
                                    Spacer()
                                    Text(round.tokens > 0 ? "\(round.tokensEstimated ? "~" : "")\(round.tokens) tokens" : "")
                                        .font(.apogeeMeta.monospacedDigit())
                                        .foregroundStyle(Palette.textTertiary)
                                }
                            }
                        }
                    }
                    if let run, !run.state.grants.isEmpty {
                        section("Let through by its grants") {
                            ForEach(Array(run.state.grants.enumerated()), id: \.offset) { _, grant in
                                Text("round \(grant.round): \(grant.tool) \(grant.target)").font(.apogeeCode)
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
