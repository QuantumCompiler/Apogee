import BusinessJobs
import BusinessMedia
import PresentationStyles
import SwiftUI

/// Compositions: the list, a composition's scenes as its project says them,
/// and the new and edit sheets.
public struct ComposeView: View {
    @Bindable private var model: ComposeViewModel

    public init(model: ComposeViewModel) {
        self.model = model
    }

    public var body: some View {
        HStack(spacing: 0) {
            VStack(spacing: 0) {
                HStack {
                    Spacer()
                    Button("New composition…") { model.newComposition() }.disabled(model.isRunning)
                }
                .padding(Spacing.s)
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 1) {
                        if let install = model.installing {
                            installCard(install, of: model)
                        }
                        ForEach(model.projects) { project in
                            let current = model.selected == project.output
                            Button {
                                model.select(project)
                            } label: {
                                VStack(alignment: .leading, spacing: 2) {
                                    HStack {
                                        Text(project.title.isEmpty ? project.name : project.title).font(.apogeeBody).lineLimit(1)
                                        if !project.edits.isEmpty {
                                            Text("EDIT \(project.edits.count)").font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary)
                                        }
                                    }
                                    Text("\(ComposeWords.status(project.status)) · \(project.made) of \(project.scenes.count) scenes")
                                        .font(.apogeeMeta)
                                        .foregroundStyle(Palette.textTertiary)
                                }
                                .padding(Spacing.m)
                                .frame(maxWidth: .infinity, alignment: .leading)
                                .background(current ? Palette.selection : .clear, in: RoundedRectangle(cornerRadius: Radius.row))
                                .contentShape(Rectangle())
                            }
                            .buttonStyle(.plain)
                        }
                        if model.projects.isEmpty {
                            Text("No compositions yet. A brief becomes a narrated, captioned video, its project kept beside it.")
                                .font(.apogeeSmall)
                                .foregroundStyle(Palette.textTertiary)
                                .padding(Spacing.m)
                        }
                        // What the scan passed over, and why -- as Clips and Voice list theirs.
                        ForEach(model.skipped, id: \.file) { skip in
                            Text("\((skip.file as NSString).lastPathComponent): \(skip.reason)")
                                .font(.apogeeMeta)
                                .foregroundStyle(Palette.textTertiary)
                                .padding(.horizontal, Spacing.m)
                        }
                    }
                    .padding(Spacing.s)
                }
            }
            .frame(width: 264)
            .background(Palette.sidebar)
            Divider()
            if let project = model.selectedProject {
                ProjectPage(model: model, project: project)
            } else {
                EmptyState(symbol: "film.stack", title: "Compositions", message: "A brief, planned into scenes -- each one narrated and rendered -- then joined into one video.")
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
        .sheet(item: Binding(get: { model.sheet.map(SheetItem.init) }, set: { if $0 == nil { model.sheet = nil } })) { _ in
            ComposeSheet(model: model)
        }
        .sheet(item: Binding(get: { model.consent.map(ConsentItem.init) }, set: { if $0 == nil && model.consent != nil { model.decline() } })) { item in
            InstallConsentSheet(
                set: item.consent.set, said: item.consent.said, command: item.consent.command, install: model.install,
                decline: model.decline)
        }
        .alert("That didn't work", isPresented: Binding(get: { model.failure != nil }, set: { if !$0 { model.dismissFailure() } })) {
            Button("OK") { model.dismissFailure() }
        } message: {
            Text(model.failure ?? "")
        }
    }
}

struct ConsentItem: Identifiable {
    let consent: InstallConsent
    var id: String { consent.command }
}

/// The install the person said yes to, as styles' card draws one: its line
/// and Stop while it runs, how it ended and Dismiss after (GUI 4f).
@MainActor
func installCard(_ install: RuntimeInstall, of model: ComposeViewModel) -> some View {
    InstallCard(
        headline: install.headline, command: install.command, state: InstallCardState.of(install.phase), detail: install.detail,
        stop: { Task { await model.stop() } }, dismiss: model.dismissInstall)
}

/// An install's phase as the card draws it.
enum InstallCardState {
    static func of(_ phase: RuntimeInstall.Phase) -> InstallCard.State {
        switch phase {
        case .running: .running
        case .installed: .installed
        case .stopped: .stopped
        case .failed: .failed
        }
    }
}

struct SheetItem: Identifiable {
    let sheet: ComposeViewModel.Sheet
    var id: String {
        switch sheet {
        case .new: "new"
        case .edit(let project): "edit:" + project.output
        }
    }
}

enum ComposeWords {
    static func status(_ status: String) -> String {
        switch status {
        case "rendering": "rendering"
        case "done": "done"
        case "planned": "planned"
        case "cancelled": "stopped"
        case "failed": "failed"
        default: status
        }
    }

    static func change(_ kind: SceneChange.Kind) -> String {
        switch kind {
        case .unchanged: "kept as it is"
        case .changed(let what): "changed: " + what.joined(separator: ", ")
        case .added: "new"
        case .removed: "removed"
        }
    }
}

/// One composition: its video when made, its scenes, what can be done.
struct ProjectPage: View {
    let model: ComposeViewModel
    let project: ProjectRecord

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: Spacing.l) {
                if project.status == "done", FileManager.default.fileExists(atPath: project.output) {
                    MediaPlayer(url: URL(fileURLWithPath: project.output))
                        .aspectRatio(16.0 / 9.0, contentMode: .fit)
                        .clipShape(RoundedRectangle(cornerRadius: Radius.card))
                        .id(project.output)
                }
                VStack(alignment: .leading, spacing: Spacing.xs) {
                    Text(project.title.isEmpty ? project.name : project.title).font(.apogeeHeading)
                    Text(project.brief).font(.apogeeBody).foregroundStyle(Palette.textSecondary).textSelection(.enabled)
                }
                HStack {
                    let mine = model.running == project.output
                    if mine {
                        ProgressView().controlSize(.small)
                        Text(model.run.map(ComposeRunWords.detail) ?? "").font(.apogeeMeta).foregroundStyle(Palette.textSecondary).lineLimit(1)
                        Button("Stop") { Task { await model.stop() } }
                    } else if ["cancelled", "failed", "planned", "rendering"].contains(project.status) {
                        Button(project.status == "planned" ? "Render it" : "Resume") { model.resume(project) }
                            .disabled(model.isRunning)
                            .help("The same command again: it renders only what the project lacks")
                    }
                    if project.status == "done" && !model.isRunning {
                        Button("Edit by instruction…") { model.edit(project) }
                        Button("Show in Finder") { NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: project.output)]) }
                    }
                }
                Text("\(ComposeWords.status(project.status)) · \(project.made) of \(project.scenes.count) scenes made"
                    + (project.status == "cancelled" ? " -- the finished scenes stand" : ""))
                    .font(.apogeeSmall)
                    .foregroundStyle(Palette.textSecondary)
                if let error = project.error {
                    Text(error).font(.apogeeSmall).foregroundStyle(Palette.error)
                }
                if let diff = model.diff(project) {
                    DiffList(changes: diff.changes, compared: diff.compared)
                }
                ForEach(project.scenes) { scene in
                    SceneCard(scene: scene, live: model.running == project.output && !scene.isMade)
                }
                if let captions = project.captionsFile {
                    Text(project.captionsBurned == false
                        ? "Captions kept beside the video as \((captions as NSString).lastPathComponent): this ffmpeg can't burn them in."
                        : "Captions burned in.")
                        .font(.apogeeMeta)
                        .foregroundStyle(Palette.textTertiary)
                }
            }
            .padding(Spacing.xl)
            .frame(maxWidth: Layout.mediaContentMax, alignment: .leading)
            .frame(maxWidth: .infinity)
        }
    }
}

enum ComposeRunWords {
    static func detail(_ run: JobProgress) -> String {
        if let scene = run.scene {
            return "scene \(scene.scene) of \(scene.scenes): \(scene.state)" + (run.step.map { " · step \($0.done) of \($0.of)" } ?? "")
        }
        return run.busy ?? run.notices.last ?? "composing"
    }
}

/// One scene: what it says, what it shows, and whether it is made.
struct SceneCard: View {
    let scene: ProjectRecord.Scene
    let live: Bool

    var body: some View {
        HStack(alignment: .top, spacing: Spacing.m) {
            Text("\(scene.index)").font(.apogeeMeta.monospacedDigit()).foregroundStyle(Palette.textTertiary).frame(width: 18)
            VStack(alignment: .leading, spacing: Spacing.xs) {
                Text(scene.narration).font(.apogeeBody)
                Text(scene.asset.map { "asset: \($0)" } ?? scene.visual).font(.apogeeSmall).foregroundStyle(Palette.textSecondary)
                HStack(spacing: Spacing.s) {
                    Label(scene.voiceFile == nil ? "not spoken" : "spoken", systemImage: scene.voiceFile == nil ? "circle" : "checkmark.circle")
                    Label(scene.clipFile != nil || scene.asset != nil ? "rendered" : "not rendered",
                        systemImage: scene.clipFile != nil || scene.asset != nil ? "checkmark.circle" : "circle")
                    if let seed = scene.seed { Text("seed \(seed)") }
                }
                .font(.apogeeMeta)
                .foregroundStyle(Palette.textTertiary)
            }
            Spacer()
            if live { ProgressView().controlSize(.small) }
        }
        .padding(Spacing.m)
        .background(Palette.card, in: RoundedRectangle(cornerRadius: Radius.card))
        .overlay(RoundedRectangle(cornerRadius: Radius.card).strokeBorder(live ? Palette.accent : Palette.border))
    }
}

/// An edit's scene diff.
struct DiffList: View {
    let changes: [SceneChange]
    let compared: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.xs) {
            Text("CHANGES").font(.system(size: TypeScale.label, weight: .medium, design: .monospaced)).foregroundStyle(Palette.textTertiary)
            if compared {
                Text("Compared scene by scene in order: this apogee's project does not name each scene's source.")
                    .font(.apogeeMeta)
                    .foregroundStyle(Palette.textTertiary)
            }
            ForEach(changes) { change in
                HStack(alignment: .firstTextBaseline, spacing: Spacing.s) {
                    Text("\(change.index)").font(.apogeeMeta.monospacedDigit()).frame(width: 18)
                    Text(ComposeWords.change(change.kind) + (change.moved ? ", moved" : ""))
                        .font(.apogeeSmall)
                        .foregroundStyle(change.kind == .unchanged ? Palette.textTertiary : Palette.textPrimary)
                    Text(change.narration).font(.apogeeMeta).foregroundStyle(Palette.textSecondary).lineLimit(1)
                }
            }
        }
    }
}

/// The new-composition sheet, or the edit sheet with its preview.
struct ComposeSheet: View {
    @Bindable var model: ComposeViewModel
    @State private var choosingAssets = false
    @State private var choosingMusic = false

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.m) {
            switch model.sheet {
            case .edit(let project):
                Text("Edit \(project.title.isEmpty ? project.name : project.title)").font(.apogeeHeading)
                TextField("What to change", text: $model.form.instruction, axis: .vertical).lineLimit(2...4)
                VStack(alignment: .leading, spacing: Spacing.xxs) {
                    Text("Render again on a fresh seed").font(.apogeeSmall).foregroundStyle(Palette.textSecondary)
                    ForEach(project.scenes) { scene in
                        Toggle(isOn: Binding(
                            get: { model.form.redo.contains(scene.index) },
                            set: { if $0 { model.form.redo.insert(scene.index) } else { model.form.redo.remove(scene.index) } })
                        ) {
                            Text("\(scene.index). \(scene.narration)").lineLimit(1)
                        }
                    }
                }
                if let diff = model.previewDiff {
                    DiffList(changes: diff.changes, compared: diff.compared)
                }
            default:
                Text("New composition").font(.apogeeHeading)
                Form {
                    TextField("The brief: what the video is about", text: $model.form.brief, axis: .vertical).lineLimit(3...6)
                    LabeledContent("Your stills and clips") {
                        HStack {
                            Text(model.form.assets ?? "none").lineLimit(1).truncationMode(.middle)
                            Button(model.form.assets == nil ? "Choose…" : "Clear") {
                                if model.form.assets == nil { choosingAssets = true } else { model.form.assets = nil }
                            }
                        }
                    }
                    LabeledContent("Music") {
                        HStack {
                            Text(model.form.music ?? "none").lineLimit(1).truncationMode(.middle)
                            Button(model.form.music == nil ? "Choose…" : "Clear") {
                                if model.form.music == nil { choosingMusic = true } else { model.form.music = nil }
                            }
                        }
                    }
                    Toggle("Captions, timed from the voice", isOn: $model.form.captions)
                    if model.namesVoiceModel {
                        // Two or more voice models: the CLI asks which rather
                        // than guess, so the sheet names the one picked.
                        Picker("Voice model", selection: Binding(get: { model.voiceModel ?? "" }, set: { model.choose(voiceModel: $0) })) {
                            ForEach(model.voiceModels) { voiceModel in
                                Text(model.label(voiceModel)).tag(voiceModel.handle)
                            }
                        }
                    }
                    TextField("Voice", text: $model.form.voice, prompt: Text("af_heart"))
                    TextField("Pipeline", text: $model.form.model, prompt: Text("the default video pipeline"))
                }
                if let line = model.captionsLine {
                    // The CLI's fact, said before the run: check's ffmpeg captions row.
                    VStack(alignment: .leading, spacing: Spacing.xxs) {
                        Label(line, systemImage: "captions.bubble")
                            .font(.apogeeSmall)
                            .foregroundStyle(Palette.textSecondary)
                            .fixedSize(horizontal: false, vertical: true)
                        if let remedy = model.captionsRemedy {
                            Text("To burn them in: \(remedy)").font(.apogeeCode).foregroundStyle(Palette.textTertiary).textSelection(.enabled)
                        }
                    }
                }
                Text(model.plansAlone()
                    ? "Plan first to see the scenes and the spend before anything renders."
                    : "This apogee plans and renders in one run; Stop keeps every finished scene.")
                    .font(.apogeeSmall)
                    .foregroundStyle(Palette.textSecondary)
            }
            if let arguments = model.plannedArguments {
                Text((["apogee"] + arguments).joined(separator: " ")).font(.apogeeCode).foregroundStyle(Palette.textTertiary).textSelection(.enabled)
            }
            HStack {
                Spacer()
                Button("Cancel", role: .cancel) { model.sheet = nil }.keyboardShortcut(.cancelAction)
                switch model.sheet {
                case .edit:
                    if model.previewing {
                        // The preview is a run of the place's: Stop reaches
                        // it, and its planner is given a bounded time.
                        ProgressView().controlSize(.small)
                        Text("Planning on paper -- at most \(ComposeViewModel.said(model.previewLimit))")
                            .font(.apogeeSmall)
                            .foregroundStyle(Palette.textSecondary)
                        Button("Stop") { Task { await model.stop() } }
                    } else {
                        Button("Preview the changes") { Task { await model.previewEdit() } }
                            .disabled(model.plannedArguments == nil || model.isRunning)
                            .help("Its dry run: the plan on paper, at most \(ComposeViewModel.said(model.previewLimit))")
                    }
                    Button("Render") { model.renderEdit() }
                        .keyboardShortcut(.return, modifiers: .command)
                        .disabled(model.plannedArguments == nil || model.isRunning)
                default:
                    if model.plansAlone() {
                        Button("Plan first") { model.compose(planFirst: true) }.disabled(model.plannedArguments == nil || model.isRunning)
                    }
                    Button("Compose") { model.compose() }
                        .keyboardShortcut(.return, modifiers: .command)
                        .disabled(model.plannedArguments == nil || model.isRunning)
                }
            }
        }
        .padding(Spacing.xl)
        .frame(width: 600)
        .fileImporter(isPresented: $choosingAssets, allowedContentTypes: [.folder]) { result in
            if case .success(let url) = result { model.form.assets = url.path }
        }
        .fileImporter(isPresented: $choosingMusic, allowedContentTypes: [.audio]) { result in
            if case .success(let url) = result { model.form.music = url.path }
        }
    }
}

#if DEBUG
#Preview("A composition made, its scenes") {
    ComposeView(model: ComposePreview.made("view-made"))
        .frame(width: 1100, height: 760)
}

#Preview("No compositions yet") {
    ComposeView(model: ComposePreview.compositions(ComposePreview.folder("view-empty")))
        .frame(width: 1000, height: 560)
}

#Preview("Rendering: scene 2 of 3") {
    ComposeView(model: ComposePreview.rendering("view-rendering"))
        .frame(width: 1100, height: 720)
}

#Preview("An edit, its changes") {
    ComposeView(model: ComposePreview.edited("view-edited"))
        .frame(width: 1100, height: 760)
}

#Preview("Made: its video, its scenes, its captions") {
    let model = ComposePreview.made("page-made")
    if let project = model.selectedProject {
        ProjectPage(model: model, project: project)
            .frame(width: 820, height: 860)
    }
}

#Preview("Stopped: the finished scenes stand") {
    let model = ComposePreview.tidesComposition("page-stopped", status: "cancelled", made: 2)
    if let project = model.selectedProject {
        ProjectPage(model: model, project: project)
            .frame(width: 820, height: 640)
    }
}

#Preview("Planned, not rendered yet") {
    let model = ComposePreview.tidesComposition("page-planned", status: "planned", made: 0)
    if let project = model.selectedProject {
        ProjectPage(model: model, project: project)
            .frame(width: 820, height: 640)
    }
}

#Preview("Rendering scene 2 of 3") {
    let model = ComposePreview.rendering("page-rendering")
    if let project = model.selectedProject {
        ProjectPage(model: model, project: project)
            .frame(width: 820, height: 640)
    }
}

#Preview("An edit, from its own comparison") {
    let model = ComposePreview.edited("page-edited")
    if let project = model.selectedProject {
        ProjectPage(model: model, project: project)
            .frame(width: 820, height: 720)
    }
}

#Preview("A scene spoken and rendered") {
    if let scene = ComposePreview.project(ComposePreview.heatPumps)?.scenes.first {
        SceneCard(scene: scene, live: false)
            .frame(width: 640)
            .padding(Spacing.l)
    }
}

#Preview("A scene spoken, not rendered") {
    if let scene = ComposePreview.project(ComposePreview.heatPumps)?.scenes.last {
        SceneCard(scene: scene, live: false)
            .frame(width: 640)
            .padding(Spacing.l)
    }
}

#Preview("A scene rendering now") {
    if let scene = ComposePreview.project(ComposePreview.heatPumps)?.scenes.last {
        SceneCard(scene: scene, live: true)
            .frame(width: 640)
            .padding(Spacing.l)
    }
}

#Preview("An edit's changes, from its own comparison (52c)") {
    if let edited = ComposePreview.project(ComposePreview.heatPumpsEdited) {
        let diff = MediaLibrary.diff(edited: edited, parent: ComposePreview.project(ComposePreview.heatPumps))
        DiffList(changes: diff.changes, compared: diff.compared)
            .frame(width: 640)
            .padding(Spacing.l)
    }
}

#Preview("Before 52c: compared scene by scene") {
    if let edited = ComposePreview.project(ComposePreview.heatPumpsCompared) {
        let diff = MediaLibrary.diff(edited: edited, parent: ComposePreview.project(ComposePreview.heatPumps))
        DiffList(changes: diff.changes, compared: diff.compared)
            .frame(width: 640)
            .padding(Spacing.l)
    }
}

#Preview("New composition") {
    let model = ComposePreview.compositions(ComposePreview.folder("sheet-new"))
    ComposeSheet(model: model)
        .task {
            model.newComposition()
            model.form.brief = "how a heat pump works"
        }
}

#Preview("New composition, planned first (52c)") {
    let model = ComposePreview.compositions(ComposePreview.folder("sheet-plan"), plansAlone: true)
    ComposeSheet(model: model)
        .task {
            model.newComposition()
            model.form.brief = "how a heat pump works"
        }
}

#Preview("An edit, its changes previewed") {
    let model = ComposePreview.editing("sheet-edit")
    ComposeSheet(model: model)
        .task { await model.previewEdit() }
}

#Preview("An edit's preview planning, Stop beside it") {
    ComposeSheet(model: ComposePreview.previewing("sheet-previewing"))
}

#Preview("New composition: two voice models, the one picked named") {
    @Previewable @State var model: ComposeViewModel?
    Group {
        if let model { ComposeSheet(model: model) } else { ProgressView() }
    }
    .task { model = await ComposePreview.sheet("sheet-voices", twoVoices: true) }
}

#Preview("New composition: this ffmpeg can't burn captions, kept as an .srt") {
    @Previewable @State var model: ComposeViewModel?
    Group {
        if let model { ComposeSheet(model: model) } else { ProgressView() }
    }
    .task { model = await ComposePreview.sheet("sheet-captions-kept") }
}

#Preview("New composition: this ffmpeg burns captions in") {
    @Previewable @State var model: ComposeViewModel?
    Group {
        if let model { ComposeSheet(model: model) } else { ProgressView() }
    }
    .task { model = await ComposePreview.sheet("sheet-captions-burned", burns: true) }
}

#Preview("Refused for its runtime: asking to install") {
    @Previewable @State var model: ComposeViewModel?
    Group {
        if let model { ComposeView(model: model) } else { ProgressView() }
    }
    .frame(width: 1100, height: 600)
    .task { model = await ComposePreview.refused("view-refused") }
}

#Preview("Installing the video set, Stop beside it") {
    @Previewable @State var model: ComposeViewModel?
    Group {
        if let model { ComposeView(model: model) } else { ProgressView() }
    }
    .frame(width: 1100, height: 600)
    .task { model = await ComposePreview.refused("view-installing", install: true) }
}

#Preview("Planned first: what it said, and its page offering Render it") {
    @Previewable @State var model: ComposeViewModel?
    @Previewable @State var said = PreviewSaid()
    VStack(alignment: .leading, spacing: 0) {
        if let model { ComposeView(model: model) } else { ProgressView() }
        Divider()
        Text("Said: \(said.last)").font(.apogeeMeta).foregroundStyle(Palette.textSecondary).padding(Spacing.s)
    }
    .frame(width: 1100, height: 680)
    .task { model = await ComposePreview.planned("view-planned") { said.last = $0 } }
}

#Preview("Projects passed over, and why") {
    ComposeView(model: ComposePreview.skipping("view-skipped"))
        .frame(width: 1100, height: 760)
}
#endif
