import BusinessJobs
import BusinessMedia
import PresentationShell
import PresentationStyles
import SwiftUI

/// The Video place: clips and compositions.
public struct VideoScreen<Compositions: View>: View {
    @Bindable private var model: VideoViewModel
    private let compositions: Compositions

    public init(model: VideoViewModel, @ViewBuilder compositions: () -> Compositions) {
        self.model = model
        self.compositions = compositions()
    }

    public var body: some View {
        VStack(spacing: 0) {
            ScreenHeader(title: "Video") {
                Picker("Show", selection: $model.tab) {
                    ForEach(VideoViewModel.Tab.allCases) { Text($0.title).tag($0) }
                }
                .pickerStyle(.segmented)
                .labelsHidden()
                .fixedSize()
                if model.tab == .clips {
                    Button("New clip…") { model.newClip() }
                        .disabled(model.isRunning)
                }
            }
            Divider()
            switch model.tab {
            case .clips: ClipsView(model: model)
            case .compositions: compositions
            }
        }
        .sheet(isPresented: $model.sheetOpen) { ClipSheet(model: model) }
        .sheet(item: Binding(get: { model.consent.map(ConsentItem.init) }, set: { if $0 == nil && model.consent != nil { model.decline() } })) { item in
            InstallConsentSheet(
                set: item.consent.set, said: item.consent.said, command: item.consent.command, install: model.install,
                decline: model.decline)
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
func installCard(_ install: RuntimeInstall, of model: VideoViewModel) -> some View {
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

/// The clips list, and the selected clip's page.
struct ClipsView: View {
    @Bindable var model: VideoViewModel
    @State private var addingFolder = false

    var body: some View {
        HStack(spacing: 0) {
            VStack(spacing: 0) {
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 1) {
                        if let install = model.installing {
                            installCard(install, of: model)
                        }
                        if let run = model.run {
                            RunRow(model: model, run: run)
                        }
                        ForEach(model.clips) { clip in
                            let current = model.selected == clip.output
                            Button {
                                model.select(clip)
                            } label: {
                                VStack(alignment: .leading, spacing: 2) {
                                    HStack {
                                        Text(clip.name).font(.apogeeBody).lineLimit(1)
                                        if let preset = clip.preset {
                                            Text(preset.uppercased()).font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary)
                                        }
                                    }
                                    Text(clip.prompt).font(.apogeeMeta).foregroundStyle(Palette.textTertiary).lineLimit(2)
                                }
                                .padding(Spacing.m)
                                .frame(maxWidth: .infinity, alignment: .leading)
                                .background(current ? Palette.selection : .clear, in: RoundedRectangle(cornerRadius: Radius.row))
                                .contentShape(Rectangle())
                            }
                            .buttonStyle(.plain)
                        }
                        if model.clips.isEmpty && model.run == nil && model.installing == nil {
                            Text("No clips yet. A clip is made from a prompt or a seed image, with its record beside it in ~/Movies/Apogee.")
                                .font(.apogeeSmall)
                                .foregroundStyle(Palette.textTertiary)
                                .padding(Spacing.m)
                        }
                        ForEach(model.skipped, id: \.file) { skip in
                            Text("\((skip.file as NSString).lastPathComponent): \(skip.reason)")
                                .font(.apogeeMeta)
                                .foregroundStyle(Palette.textTertiary)
                                .padding(.horizontal, Spacing.m)
                        }
                    }
                    .padding(Spacing.s)
                }
                Divider()
                HStack {
                    Text("\(model.folders.count) folder\(model.folders.count == 1 ? "" : "s")")
                        .font(.apogeeMeta)
                        .foregroundStyle(Palette.textTertiary)
                        .help(model.folders.map(\.path).joined(separator: "\n"))
                    Spacer()
                    Button("Add a folder…") { addingFolder = true }.font(.apogeeSmall)
                }
                .padding(Spacing.s)
            }
            .frame(width: 264)
            .background(Palette.sidebar)
            Divider()
            if let clip = model.selectedClip {
                ClipPage(model: model, clip: clip)
            } else {
                EmptyState(symbol: "video", title: "Clips", message: "Pick a clip to play it and see how it was made, or make a new one.")
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
        .fileImporter(isPresented: $addingFolder, allowedContentTypes: [.folder]) { result in
            if case .success(let url) = result { model.add(folder: url) }
        }
    }
}

/// The run in progress, or the last one until dismissed.
struct RunRow: View {
    let model: VideoViewModel
    let run: JobProgress

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.xs) {
            HStack {
                Text(RunWords.status(run)).font(.apogeeSmall.weight(.medium))
                Spacer()
                if model.isRunning {
                    Button("Stop") { Task { await model.stop() } }
                } else {
                    Button("Dismiss") { model.dismissRun() }
                }
            }
            if model.isRunning {
                if let step = run.step {
                    ProgressView(value: Double(step.done), total: Double(max(step.of, 1)))
                } else {
                    ProgressView().progressViewStyle(.linear)
                }
            }
            Text(RunWords.detail(run)).font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary).lineLimit(2)
        }
        .padding(Spacing.m)
        .background(Palette.card, in: RoundedRectangle(cornerRadius: Radius.card))
    }
}

/// A run's state in words: the stream's step where the build has it, else the
/// narration's last line (shown, never decided on).
enum RunWords {
    static func status(_ run: JobProgress) -> String {
        switch run.status {
        case .running: run.step.map { "Step \($0.done) of \($0.of)" } ?? "Generating"
        case .done: "Done"
        case .stopped: "Stopped: nothing was written"
        case .failed: "Failed"
        }
    }

    static func detail(_ run: JobProgress) -> String {
        if case .failed(let message) = run.status { return message }
        return run.busy ?? run.notices.last ?? (run.device.map { "on \($0)" } ?? "")
    }
}

/// One clip: the player, how it was made, where it came from, and what to do next.
struct ClipPage: View {
    let model: VideoViewModel
    let clip: ClipRecord

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: Spacing.l) {
                MediaPlayer(url: URL(fileURLWithPath: clip.output))
                    .aspectRatio(CGFloat(clip.width ?? 16) / CGFloat(max(clip.height ?? 9, 1)), contentMode: .fit)
                    .clipShape(RoundedRectangle(cornerRadius: Radius.card))
                    .id(clip.output)
                Text(clip.prompt).font(.apogeeProse).textSelection(.enabled)
                HStack {
                    Button("Run again…") { model.newClip(from: clip) }
                    Button("Continue it…") { model.newClip(from: clip, extend: true) }
                    if clip.preset == "draft" {
                        Button("Make the final") {
                            var form = ClipForm.from(clip, extend: false)
                            form.preset = .final
                            model.form = form
                            model.sheetOpen = true
                        }
                    }
                    Button("Show in Finder") { NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: clip.output)]) }
                }
                .disabled(model.isRunning)
                Grid(alignment: .leadingFirstTextBaseline, horizontalSpacing: Spacing.m, verticalSpacing: Spacing.xs) {
                    ForEach(Self.facts(clip), id: \.0) { fact in
                        GridRow {
                            Text(fact.0).foregroundStyle(Palette.textTertiary)
                            Text(fact.1).textSelection(.enabled)
                        }
                    }
                }
                .font(.apogeeSmall)
                let chain = model.chain(clip)
                if !chain.isEmpty {
                    VStack(alignment: .leading, spacing: Spacing.xs) {
                        Text("MADE FROM").font(.system(size: TypeScale.label, weight: .medium, design: .monospaced)).foregroundStyle(Palette.textTertiary)
                        ForEach(chain) { parent in
                            Button(parent.name) { model.select(parent) }.buttonStyle(.link).font(.apogeeSmall)
                        }
                    }
                }
            }
            .padding(Spacing.xl)
            .frame(maxWidth: Layout.mediaContentMax, alignment: .leading)
            .frame(maxWidth: .infinity)
        }
    }

    static func facts(_ clip: ClipRecord) -> [(String, String)] {
        // An MLX video model is no pipeline: its fact is named for what it is.
        var facts: [(String, String)] = [(clip.engine == .mlx ? "Model" : "Pipeline", clip.model), ("Mode", clip.mode == "i2v" ? "from an image" : "from text")]
        // What it ran on, where the record says (the CLI's 53l); a record
        // from before there were two engines is drawn as it always was.
        if let engine = clip.recordedEngine { facts.insert(("Engine", engine.said(versions: clip.versions)), at: 1) }
        if let width = clip.width, let height = clip.height { facts.append(("Size", "\(width) × \(height)")) }
        if let duration = clip.duration, let fps = clip.fps { facts.append(("Length", "\(ClipFormWords.number(duration)) s at \(fps) fps")) }
        if let steps = clip.steps { facts.append(("Steps", String(steps) + (clip.preset.map { " (\($0))" } ?? ""))) }
        if let seed = clip.seed { facts.append(("Seed", "\(seed)" + (clip.seedSource.map { " · \($0)" } ?? ""))) }
        if !clip.loras.isEmpty { facts.append(("LoRAs", clip.loras.map { ($0.path as NSString).lastPathComponent }.joined(separator: ", "))) }
        if let image = clip.image { facts.append(("Seed image", image)) }
        if let seconds = clip.seconds { facts.append(("Made in", "\(Int(seconds)) s")) }
        if let codec = clip.codec { facts.append(("Codec", codec)) }
        return facts
    }
}

enum ClipFormWords {
    static func number(_ value: Double) -> String {
        value.rounded() == value ? String(Int(value)) : String(value)
    }
}

/// The generate sheet: what the clip shows, and how.
struct ClipSheet: View {
    @Bindable var model: VideoViewModel
    @State private var choosingImage = false

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.m) {
            Text(title).font(.apogeeHeading)
            Form {
                TextField(promptLabel, text: $model.form.prompt, axis: .vertical).lineLimit(2...5)
                if case .new = model.form.start {
                    LabeledContent("Seed image") {
                        HStack {
                            Text(model.form.image ?? "none").lineLimit(1).truncationMode(.middle)
                            Button(model.form.image == nil ? "Choose…" : "Clear") {
                                if model.form.image == nil { choosingImage = true } else { model.form.image = nil }
                            }
                        }
                    }
                    if model.models.isEmpty {
                        TextField("Pipeline", text: $model.form.model, prompt: Text("the default video pipeline"))
                    } else {
                        Picker("Model", selection: Binding(get: { model.choice }, set: { model.choose($0) })) {
                            Text(model.defaultModel.map { "The default (\($0))" } ?? "The default").tag(VideoViewModel.ModelChoice.byDefault)
                            ForEach(model.models) { video in
                                Text(model.label(video)).tag(VideoViewModel.ModelChoice.model(video.handle))
                            }
                            Text("Another…").tag(VideoViewModel.ModelChoice.another)
                        }
                        if model.choice == .another {
                            TextField("Model", text: $model.form.model, prompt: Text("a store handle, id or model, or a path"))
                        }
                    }
                }
                Picker("Quality", selection: $model.form.preset) {
                    Text("As set").tag(ClipForm.Preset.none)
                    Text("Draft: 16 steps").tag(ClipForm.Preset.draft)
                    Text("Final: 50 steps").tag(ClipForm.Preset.final)
                }
                TextField("Seed", value: $model.form.seed, format: .number.grouping(.never), prompt: Text(seedPrompt))
                TextField("Seconds (at most 10)", value: $model.form.duration, format: .number, prompt: Text("5"))
                TextField("Size", text: Binding(get: { model.form.resolution ?? "" }, set: { model.form.resolution = $0.isEmpty ? nil : $0 }), prompt: Text("1280x704"))
                TextField("LoRAs (path:strength, one per line)", text: Binding(
                    get: { model.form.loras.joined(separator: "\n") },
                    set: { model.form.loras = $0.split(whereSeparator: \.isNewline).map(String.init) }), axis: .vertical)
                    .lineLimit(1...4)
                    .disabled(model.form.noLoRAs)
                Toggle("No LoRAs", isOn: $model.form.noLoRAs)
            }
            if let line = model.engineLine {
                Label(line, systemImage: "cpu")
                    .font(.apogeeSmall)
                    .foregroundStyle(Palette.textSecondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
            Text("Written to \(model.plannedOutput.path) -- never over a file that is there.")
                .font(.apogeeSmall)
                .foregroundStyle(Palette.textSecondary)
            if let arguments = model.plannedArguments {
                Text((["apogee"] + arguments).joined(separator: " ")).font(.apogeeCode).foregroundStyle(Palette.textTertiary).textSelection(.enabled)
            }
            HStack {
                Spacer()
                Button("Cancel", role: .cancel) { model.sheetOpen = false }.keyboardShortcut(.cancelAction)
                Button("Generate") { model.generate() }
                    .keyboardShortcut(.return, modifiers: .command)
                    .disabled(model.plannedArguments == nil || model.isInstalling)
            }
        }
        .padding(Spacing.xl)
        .frame(width: 600)
        .fileImporter(isPresented: $choosingImage, allowedContentTypes: [.image]) { result in
            if case .success(let url) = result { model.form.image = url.path }
        }
    }

    private var title: String {
        switch model.form.start {
        case .new: "New clip"
        case .again: "Run again"
        case .extend: "Continue the clip"
        }
    }

    private var promptLabel: String {
        if case .new = model.form.start { return "What the clip shows" }
        return "A new prompt (empty keeps the recorded one)"
    }

    private var seedPrompt: String {
        if case .new = model.form.start { return "drawn, and kept in the record" }
        return "held from the record"
    }
}

#if DEBUG
#Preview("Clips: a final, made from its draft") {
    VideoScreen(model: VideoPreview.library("screen-read", selecting: "lighthouse.mp4")) {
        Text("The compositions").font(.apogeeSmall).padding(Spacing.m)
    }
    .frame(width: 1100, height: 720)
}

#Preview("No clips yet") {
    VideoScreen(model: VideoPreview.empty("screen-empty")) {
        Text("The compositions").font(.apogeeSmall).padding(Spacing.m)
    }
    .frame(width: 1100, height: 600)
}

#Preview("Generating: step 3 of 50") {
    VideoScreen(model: VideoPreview.generating("screen-running", starter: PreviewJobs(said: VideoPreview.step))) {
        Text("The compositions").font(.apogeeSmall).padding(Spacing.m)
    }
    .frame(width: 1100, height: 600)
}

#Preview("Refused for its runtime: asking to install") {
    let model = VideoPreview.library("screen-refused", starter: VideoPreview.runs([VideoPreview.refusedRun]))
    VideoScreen(model: model) {
        Text("The compositions").font(.apogeeSmall).padding(Spacing.m)
    }
    .frame(width: 1100, height: 600)
    .task { await VideoPreview.refuse(model) }
}

#Preview("Clips beside a clip's page, a record whose clip is gone") {
    ClipsView(model: VideoPreview.library("clips-gone", gone: true, selecting: "lighthouse.mp4"))
        .frame(width: 1000, height: 640)
}

#Preview("Clips: none picked yet") {
    ClipsView(model: VideoPreview.library("clips-unpicked"))
        .frame(width: 1000, height: 480)
}

#Preview("A run: step 3 of 50") {
    let model = VideoPreview.generating("run-running", starter: PreviewJobs(said: VideoPreview.step))
    RunRow(model: model, run: VideoPreview.progress(VideoPreview.step))
        .frame(width: 264)
        .padding(Spacing.s)
}

#Preview("A run made") {
    RunRow(model: VideoPreview.library("run-made"), run: VideoPreview.progress(VideoPreview.step + [VideoPreview.ended(0)]))
        .frame(width: 264)
        .padding(Spacing.s)
}

#Preview("A run failed, in the CLI's words") {
    RunRow(model: VideoPreview.library("run-failed"), run: VideoPreview.progress(VideoPreview.refusedRun.updates))
    .frame(width: 264)
    .padding(Spacing.s)
}

#Preview("A final, made from its draft") {
    let model = VideoPreview.library("page-final")
    if let clip = VideoPreview.clip("lighthouse.mp4", of: model) {
        ClipPage(model: model, clip: clip)
            .frame(width: 800, height: 760)
    }
}

#Preview("A draft, its final not made") {
    let model = VideoPreview.library("page-draft")
    if let clip = VideoPreview.clip("lighthouse-draft.mp4", of: model) {
        ClipPage(model: model, clip: clip)
            .frame(width: 800, height: 760)
    }
}

#Preview("New clip") {
    let model = VideoPreview.library("sheet-new")
    ClipSheet(model: model)
        .task {
            model.newClip()
            model.form.prompt = "A lighthouse at dusk"
        }
}

#Preview("Run again, from a record") {
    let model = VideoPreview.library("sheet-again")
    ClipSheet(model: model)
        .task {
            if let clip = VideoPreview.clip("lighthouse-draft.mp4", of: model) { model.newClip(from: clip) }
        }
}

#Preview("Continue the clip") {
    let model = VideoPreview.library("sheet-extend")
    ClipSheet(model: model)
        .task {
            if let clip = VideoPreview.clip("lighthouse.mp4", of: model) { model.newClip(from: clip, extend: true) }
        }
}

#Preview("New clip: the default, an MLX video model") {
    let model = VideoPreview.library("sheet-default")
    ClipSheet(model: model)
        .task { await VideoPreview.newClip(model, choosing: .byDefault) }
}

#Preview("New clip: no default set, so a model is to be picked") {
    let model = VideoPreview.library("sheet-no-default")
    ClipSheet(model: model)
        .task { await VideoPreview.newClip(model, choosing: .byDefault, over: VideoPreview.storeNoDefault) }
}

#Preview("New clip: a pipeline beside its MLX conversion, MLX picked: --engine mlx") {
    let model = VideoPreview.library("sheet-both")
    ClipSheet(model: model)
        .task { await VideoPreview.newClip(model, choosing: .model(VideoPreview.mlxVideo)) }
}

#Preview("New clip: the same folder's pipeline picked: --engine torch") {
    let model = VideoPreview.library("sheet-both-torch")
    ClipSheet(model: model)
        .task { await VideoPreview.newClip(model, choosing: .model(VideoPreview.pipeline)) }
}

#Preview("New clip: a pipeline of its own, its engine said") {
    let model = VideoPreview.library("sheet-pipeline")
    ClipSheet(model: model)
        .task { await VideoPreview.newClip(model, choosing: .model(VideoPreview.smallPipeline)) }
}

#Preview("New clip: another model, named as typed") {
    let model = VideoPreview.library("sheet-another")
    ClipSheet(model: model)
        .task { await VideoPreview.newClip(model, choosing: .another) }
}

#Preview("Run again, from a clip made on MLX") {
    let model = VideoPreview.engines("sheet-again-mlx")
    ClipSheet(model: model)
        .task {
            if let clip = VideoPreview.clip("lighthouse-mlx.mp4", of: model) { model.newClip(from: clip) }
        }
}

#Preview("A clip made on MLX") {
    let model = VideoPreview.engines("page-mlx")
    if let clip = VideoPreview.clip("lighthouse-mlx.mp4", of: model) {
        ClipPage(model: model, clip: clip)
            .frame(width: 800, height: 760)
    }
}

#Preview("A clip made on PyTorch, its engine recorded") {
    let model = VideoPreview.engines("page-torch")
    if let clip = VideoPreview.clip("lighthouse-pytorch.mp4", of: model) {
        ClipPage(model: model, clip: clip)
            .frame(width: 800, height: 760)
    }
}

#Preview("Installing the video set, Stop beside it") {
    let model = VideoPreview.library("install-running", starter: VideoPreview.runs([VideoPreview.refusedRun, VideoPreview.installRunning]))
    ClipsView(model: model)
        .frame(width: 1000, height: 480)
        .task { await VideoPreview.install(model) }
}

#Preview("The install failed, in the CLI's words") {
    let model = VideoPreview.library("install-failed", starter: VideoPreview.runs([VideoPreview.refusedRun, VideoPreview.installFailed]))
    ClipsView(model: model)
        .frame(width: 1000, height: 480)
        .task { await VideoPreview.install(model) }
}

#Preview("Installed, the refused clip made again") {
    let model = VideoPreview.library("install-done", starter: VideoPreview.runs([VideoPreview.refusedRun, VideoPreview.installDone]))
    ClipsView(model: model)
        .frame(width: 1000, height: 480)
        .task { await VideoPreview.install(model) }
}
#endif
