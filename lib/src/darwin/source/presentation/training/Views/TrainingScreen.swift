import BusinessJobs
import BusinessReads
import PresentationShell
import PresentationStyles
import SwiftUI
import UniformTypeIdentifiers

/// The Training place (⌘9): runs with their gates and promotions, the
/// regimes, the datasets and their kits.
public struct TrainingScreen: View {
    @Bindable private var model: TrainingViewModel
    @State private var preparing = false

    public init(model: TrainingViewModel) {
        self.model = model
    }

    public var body: some View {
        VStack(spacing: 0) {
            ScreenHeader(title: "Training") {
                Picker("Show", selection: $model.tab) {
                    ForEach(TrainingViewModel.Tab.allCases) { Text($0.title).tag($0) }
                }
                .pickerStyle(.segmented)
                .labelsHidden()
                .fixedSize()
                switch model.tab {
                case .runs: Button("New run…") { model.newRun() }.disabled(model.isRunning)
                case .regimes: EmptyView()
                case .datasets:
                    Button("New dataset…") { model.newDataset() }
                    Button("Synthesize…") { model.newSynthesis() }.disabled(model.isRunning)
                    Button("Prepare a file…") { preparing = true }.disabled(model.isRunning)
                }
            }
            Divider()
            if let progress = model.run {
                HStack(spacing: Spacing.s) {
                    ProgressView().controlSize(.small)
                    Text(model.running ?? "Working").font(.apogeeSmall)
                    Text(progress.busy ?? progress.notices.last ?? "")
                        .font(.apogeeMeta.monospaced())
                        .foregroundStyle(Palette.textSecondary)
                        .lineLimit(1)
                        .truncationMode(.middle)
                    Spacer()
                    Button("Stop") { Task { await model.stop() } }
                }
                .padding(.horizontal, Spacing.l)
                .padding(.vertical, Spacing.s)
                Divider()
            }
            switch model.tab {
            case .runs: RunsView(model: model)
            case .regimes: RegimesView(model: model)
            case .datasets: DatasetsView(model: model)
            }
        }
        .sheet(item: Binding(get: { model.sheet.map(SheetID.init) }, set: { if $0 == nil { model.sheet = nil } })) { which in
            switch which.sheet {
            case .run: RunSheet(model: model)
            case .promote: PromoteSheet(model: model)
            case .synth: SynthSheet(model: model)
            case .create: CreateSheet(model: model)
            }
        }
        .confirmationDialog(
            pendingTitle,
            isPresented: Binding(get: { model.pending != nil }, set: { if !$0 { model.pending = nil } }),
            titleVisibility: .visible
        ) {
            Button("Go ahead", role: .destructive) { Task { await model.confirm() } }
            Button("Cancel", role: .cancel) { model.pending = nil }
        } message: {
            Text("apogee " + (model.pending?.arguments.joined(separator: " ") ?? ""))
        }
        .fileImporter(isPresented: $preparing, allowedContentTypes: [.item]) { result in
            if case .success(let url) = result { model.prepare(url.path) }
        }
        .alert("That didn't work", isPresented: Binding(get: { model.failure != nil }, set: { if !$0 { model.dismissFailure() } })) {
            Button("OK") { model.dismissFailure() }
        } message: {
            Text(model.failure ?? "")
        }
    }

    private var pendingTitle: String {
        switch model.pending {
        case .rollback(let backend): "Point \(backend) back at its previous version?"
        case .deleteDataset(let name): "Delete the dataset \(name)?"
        case nil: ""
        }
    }

    struct SheetID: Identifiable {
        let sheet: TrainingViewModel.Sheet
        var id: String { "\(sheet)" }
    }
}

/// The runs: each with its trainer, its loss and its gate; the promoted
/// versions under them.
struct RunsView: View {
    @Bindable var model: TrainingViewModel

    var body: some View {
        HStack(spacing: 0) {
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 1) {
                    ForEach(model.runs) { run in
                        Button {
                            model.selectedRun = run.id
                        } label: {
                            VStack(alignment: .leading, spacing: 2) {
                                HStack {
                                    Text(run.id).font(.apogeeBody.monospacedDigit())
                                    Spacer()
                                    Text(run.status).font(.apogeeMeta).foregroundStyle(Palette.textSecondary)
                                }
                                Text([run.method, run.trainer, run.finalLoss.map { String(format: "loss %.4f", $0) } ?? ""].filter { !$0.isEmpty }.joined(separator: " · ")
                                    + " · " + TrainingViewModel.gate(run))
                                    .font(.apogeeMeta)
                                    .foregroundStyle(run.evalPassed == false ? Palette.error : Palette.textTertiary)
                                    .lineLimit(1)
                            }
                            .padding(Spacing.m)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .background(model.selectedRun == run.id ? Palette.selection : .clear, in: RoundedRectangle(cornerRadius: Radius.row))
                            .contentShape(Rectangle())
                        }
                        .buttonStyle(.plain)
                    }
                    if model.runs.isEmpty {
                        Text("No runs yet. A run fine-tunes a stored SafeTensors set on a dataset -- hours on a local trainer -- and only starts when you launch it.")
                            .font(.apogeeSmall)
                            .foregroundStyle(Palette.textTertiary)
                            .padding(Spacing.m)
                    }
                    if !model.ledgers.isEmpty {
                        Text("PROMOTED VERSIONS").font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary).padding(.horizontal, Spacing.m).padding(.top, Spacing.l)
                        ForEach(model.ledgers) { ledger in
                            VStack(alignment: .leading, spacing: 2) {
                                HStack {
                                    Text(ledger.backend).font(.apogeeBody)
                                    Spacer()
                                    Button("Roll back…") { model.pending = .rollback(backend: ledger.backend) }
                                        .buttonStyle(.borderless)
                                        .disabled(ledger.versions.count < 2)
                                }
                                ForEach(ledger.versions) { version in
                                    Text("v\(version.version) · run \(version.runID)\(version.version == ledger.active ? " · active" : "")\(version.pruned ? " · pruned" : "")")
                                        .font(.apogeeMeta.monospaced())
                                        .foregroundStyle(version.version == ledger.active ? Palette.textPrimary : Palette.textTertiary)
                                }
                            }
                            .padding(Spacing.m)
                        }
                    }
                }
                .padding(Spacing.s)
            }
            .frame(minWidth: 380)
            Divider()
            Group {
                if let run = model.currentRun {
                    RunPage(model: model, run: run)
                } else {
                    Text("Choose a run to see what it trained and how it did.")
                        .font(.apogeeSmall)
                        .foregroundStyle(Palette.textTertiary)
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                }
            }
            .frame(width: 380)
            .background(Palette.inspector)
        }
    }
}

/// One run: its facts, its gate, and Gate and Promote.
struct RunPage: View {
    let model: TrainingViewModel
    let run: TrainingRun

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: Spacing.m) {
                Text(run.id).font(.apogeeHeading.monospacedDigit())
                Text(TrainingViewModel.gate(run))
                    .font(.apogeeSmall)
                    .foregroundStyle(run.evalPassed == false ? Palette.error : Palette.textSecondary)
                fact("Trained", run.baseModel)
                fact("On", run.dataset)
                fact("How", [run.method, run.trainer, run.iterations.map { "\($0) iterations" } ?? ""].filter { !$0.isEmpty }.joined(separator: " · "))
                fact("Final loss", run.finalLoss.map { String(format: "%.4f", $0) } ?? "—")
                fact("Ran", [run.startedAt, run.finishedAt].filter { !$0.isEmpty }.joined(separator: " → "))
                HStack {
                    Button("Gate it") { model.gate(run) }
                        .help("apogee train eval \(run.id): the run's answers against its suite")
                    Button("Promote…") { model.newPromotion(of: run) }
                        .help("apogee train promote: fuse, verify and register it as a backend")
                }
                .disabled(model.isRunning)
            }
            .padding(Spacing.l)
        }
    }

    private func fact(_ label: String, _ value: String) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(label.uppercased()).font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary)
            Text(value.isEmpty ? "—" : value).font(.apogeeBody).textSelection(.enabled)
        }
    }
}

/// The regimes: each one's kits in order, and Run.
struct RegimesView: View {
    @Bindable var model: TrainingViewModel

    var body: some View {
        HStack(spacing: 0) {
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 1) {
                    ForEach(model.regimes) { regime in
                        Button {
                            model.selectedRegime = regime.name
                        } label: {
                            VStack(alignment: .leading, spacing: 2) {
                                Text(regime.name).font(.apogeeBody)
                                Text("\(regime.kits.count) kit\(regime.kits.count == 1 ? "" : "s") · \(regime.source)").font(.apogeeMeta).foregroundStyle(Palette.textTertiary)
                            }
                            .padding(Spacing.m)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .background(model.selectedRegime == regime.name ? Palette.selection : .clear, in: RoundedRectangle(cornerRadius: Radius.row))
                            .contentShape(Rectangle())
                        }
                        .buttonStyle(.plain)
                    }
                }
                .padding(Spacing.s)
            }
            .frame(width: 280)
            .background(Palette.sidebar)
            Divider()
            if let regime = model.currentRegime {
                ScrollView {
                    VStack(alignment: .leading, spacing: Spacing.m) {
                        HStack {
                            Text(regime.name).font(.apogeeHeading)
                            Spacer()
                            Button("Run this regime") { model.runRegime(regime) }
                                .disabled(model.isRunning)
                                .help("apogee train regime run \(regime.name): a dataset per kit from the teacher, one gated pipeline, the last passing stage promoted")
                        }
                        Text(regime.description).font(.apogeeBody).textSelection(.enabled)
                        Text("KITS, IN ORDER").font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary)
                        ForEach(Array(regime.kits.enumerated()), id: \.offset) { index, kit in
                            Text("\(index + 1). \(kit)").font(.apogeeBody)
                        }
                        if regime.teacher.isEmpty || regime.student.isEmpty {
                            Text("The spec names no \(regime.teacher.isEmpty ? "teacher" : "student") -- the run says what it needs, in its words.")
                                .font(.apogeeSmall)
                                .foregroundStyle(Palette.textTertiary)
                        }
                    }
                    .padding(Spacing.xl)
                    .frame(maxWidth: .infinity, alignment: .leading)
                }
            } else {
                Text("Choose a regime to see its kits.").font(.apogeeSmall).foregroundStyle(Palette.textTertiary).frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
    }
}

/// The datasets, and the kits a synthesis draws on.
struct DatasetsView: View {
    @Bindable var model: TrainingViewModel

    var body: some View {
        HStack(spacing: 0) {
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 1) {
                    ForEach(model.datasets) { dataset in
                        Button {
                            model.selectedDataset = dataset.name
                        } label: {
                            VStack(alignment: .leading, spacing: 2) {
                                Text(dataset.name).font(.apogeeBody)
                                Text("\(dataset.lines) example\(dataset.lines == 1 ? "" : "s") · \(dataset.shape)").font(.apogeeMeta).foregroundStyle(Palette.textTertiary)
                            }
                            .padding(Spacing.m)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .background(model.selectedDataset == dataset.name ? Palette.selection : .clear, in: RoundedRectangle(cornerRadius: Radius.row))
                            .contentShape(Rectangle())
                        }
                        .buttonStyle(.plain)
                    }
                    if model.datasets.isEmpty {
                        Text("No datasets yet.").font(.apogeeSmall).foregroundStyle(Palette.textTertiary).padding(Spacing.m)
                    }
                    Text("KITS").font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary).padding(.horizontal, Spacing.m).padding(.top, Spacing.l)
                    ForEach(model.kits) { kit in
                        VStack(alignment: .leading, spacing: 2) {
                            Text(kit.name).font(.apogeeSmall)
                            Text("\(kit.evalItems) eval items").font(.apogeeMeta).foregroundStyle(Palette.textTertiary)
                        }
                        .padding(.horizontal, Spacing.m)
                        .padding(.vertical, Spacing.xs)
                        .help(kit.description)
                    }
                }
                .padding(Spacing.s)
            }
            .frame(width: 300)
            .background(Palette.sidebar)
            Divider()
            if let dataset = model.currentDataset {
                VStack(alignment: .leading, spacing: Spacing.m) {
                    HStack {
                        Text(dataset.name).font(.apogeeHeading)
                        Spacer()
                        Button("Delete…", role: .destructive) { model.pending = .deleteDataset(dataset.name) }
                    }
                    Text("\(dataset.lines) examples · \(dataset.shape) · \(ByteCountFormatter.string(fromByteCount: Int64(dataset.bytes), countStyle: .file))")
                        .font(.apogeeBody)
                    Text(dataset.path).font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary).textSelection(.enabled)
                    Spacer()
                }
                .padding(Spacing.xl)
                .frame(maxWidth: .infinity, alignment: .leading)
            } else {
                Text("Choose a dataset.").font(.apogeeSmall).foregroundStyle(Palette.textTertiary).frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
    }
}

/// A new run: the stored set to train, the dataset, how.
struct RunSheet: View {
    @Bindable var model: TrainingViewModel

    var body: some View {
        SheetFrame(title: "Train a model", arguments: model.runForm.arguments, action: "Start the run", act: model.startRun, cancel: { model.sheet = nil }) {
            Text("A run fine-tunes a stored SafeTensors set -- hours on a local trainer. It starts only now, when you say.")
                .font(.apogeeSmall)
                .foregroundStyle(Palette.textSecondary)
            Picker("Student", selection: $model.runForm.student) {
                ForEach(model.students, id: \.self) { Text($0).tag($0) }
            }
            Picker("Dataset", selection: $model.runForm.dataset) {
                ForEach(model.datasets) { Text($0.name).tag($0.name) }
            }
            Picker("Method", selection: $model.runForm.method) {
                Text("LoRA (the default)").tag(String?.none)
                Text("QLoRA").tag(String?.some("qlora"))
            }
            HStack {
                TextField("Iterations", text: $model.runForm.iters)
                TextField("Batch size", text: $model.runForm.batchSize)
                TextField("Layers", text: $model.runForm.numLayers)
            }
            .textFieldStyle(.roundedBorder)
            Toggle("Gradient checkpointing: slower, less memory", isOn: $model.runForm.gradCheckpoint)
            Toggle("Completion-only loss: the prompt is not trained on", isOn: $model.runForm.maskPrompt)
            if model.students.isEmpty {
                Text("No stored SafeTensors set to train: pull one with its full weights first.").font(.apogeeSmall).foregroundStyle(Palette.textTertiary)
            }
        }
    }
}

/// A promotion: the run fused and registered as a backend, the gate honoured.
struct PromoteSheet: View {
    @Bindable var model: TrainingViewModel

    var body: some View {
        SheetFrame(title: "Promote \(model.promoteForm.run)", arguments: model.promoteForm.arguments, action: "Promote", act: model.promote, cancel: { model.sheet = nil }) {
            if let run = model.runs.first(where: { $0.id == model.promoteForm.run }) {
                Text("This run " + TrainingViewModel.gate(run) + ".")
                    .font(.apogeeSmall)
                    .foregroundStyle(run.evalPassed == false ? Palette.error : Palette.textSecondary)
            }
            TextField("As backend (new, or an existing llamacpp or mlx entry)", text: $model.promoteForm.backend).textFieldStyle(.roundedBorder)
            Picker("Target", selection: $model.promoteForm.target) {
                Text("What the backend runs (else GGUF)").tag(String?.none)
                Text("GGUF, for llama.cpp").tag(String?.some("gguf"))
                Text("MLX, as fused").tag(String?.some("mlx"))
            }
            Picker("Quantize", selection: $model.promoteForm.quantize) {
                Text("Keep F16").tag(String?.none)
                ForEach(TrainingViewModel.PromoteForm.levels, id: \.self) { Text($0).tag(String?.some($0)) }
            }
            Toggle("Skip the gate (--force): a failing candidate reaches inference", isOn: $model.promoteForm.force)
        }
    }
}

/// A dataset distilled from a teacher for a kit's skill.
struct SynthSheet: View {
    @Bindable var model: TrainingViewModel

    var body: some View {
        SheetFrame(title: "Synthesize a dataset", arguments: model.synthForm.arguments, action: "Synthesize", act: model.synthesize, cancel: { model.sheet = nil }) {
            TextField("Dataset name", text: $model.synthForm.name).textFieldStyle(.roundedBorder)
            Picker("Teacher", selection: $model.synthForm.teacher) {
                ForEach(model.backends, id: \.self) { Text($0).tag($0) }
            }
            Picker("Kit", selection: $model.synthForm.kit) {
                ForEach(model.kits) { Text($0.name).tag($0.name) }
            }
            HStack {
                TextField("Examples (the kit's own)", text: $model.synthForm.count)
                TextField("Topic (optional)", text: $model.synthForm.topic)
            }
            .textFieldStyle(.roundedBorder)
            Text("A cloud teacher is billed per call.").font(.apogeeSmall).foregroundStyle(Palette.textTertiary)
        }
    }
}

/// A new dataset scaffolded.
struct CreateSheet: View {
    @Bindable var model: TrainingViewModel

    var body: some View {
        let name = model.createName.trimmingCharacters(in: .whitespaces)
        SheetFrame(title: "New dataset", arguments: name.isEmpty ? nil : ["datasets", "create", name, "--from", model.createFrom], action: "Create", act: { Task { await model.createDataset() } }, cancel: { model.sheet = nil }) {
            TextField("Dataset name", text: $model.createName).textFieldStyle(.roundedBorder)
            Picker("From", selection: $model.createFrom) {
                Text("A template (two example lines)").tag("template")
                Text("Your chats").tag("sessions")
                Text("Empty").tag("empty")
            }
        }
    }
}

/// A sheet's frame: its title, its fields, the command as typed, and its two
/// buttons.
struct SheetFrame<Fields: View>: View {
    let title: String
    let arguments: [String]?
    let action: String
    let act: () -> Void
    let cancel: () -> Void
    @ViewBuilder let fields: () -> Fields

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.m) {
            Text(title).font(.apogeeHeading)
            fields()
            if let arguments {
                Text((["apogee"] + arguments).joined(separator: " ")).font(.apogeeCode).foregroundStyle(Palette.textTertiary).lineLimit(3).textSelection(.enabled)
            }
            HStack {
                Spacer()
                Button("Cancel", role: .cancel, action: cancel).keyboardShortcut(.cancelAction)
                Button(action, action: act).keyboardShortcut(.defaultAction).disabled(arguments == nil)
            }
        }
        .padding(Spacing.xl)
        .frame(width: 560)
    }
}

#if DEBUG
#Preview("Runs: one, failed its gate") {
    let training = TrainingPreview.training()
    TrainingScreen(model: training)
        .frame(width: 1100, height: 640)
        .task { await TrainingPreview.read(training) }
}

#Preview("A run training") {
    let training = TrainingPreview.training(said: TrainingPreview.running)
    TrainingScreen(model: training)
        .frame(width: 1100, height: 640)
        .task {
            await training.connect(binary: previewBinary)
            training.newRun()
            training.startRun()
        }
}

#Preview("A promotion the gate refused") {
    let training = TrainingPreview.training(said: TrainingPreview.refused, finishes: true)
    TrainingScreen(model: training)
        .frame(width: 1100, height: 640)
        .task {
            await TrainingPreview.read(training)
            TrainingPreview.promoting(training)
            training.promote()
        }
}

#Preview("Apogee not installed") {
    let training = TrainingPreview.training()
    TrainingScreen(model: training)
        .frame(width: 1100, height: 480)
        .task { await training.connect(binary: nil) }
}

#Preview("Runs: read, one open") {
    let training = TrainingPreview.training()
    RunsView(model: training)
        .frame(width: 900, height: 520)
        .task { await TrainingPreview.read(training) }
}

#Preview("Runs: none read") {
    RunsView(model: TrainingPreview.training())
        .frame(width: 900, height: 400)
}

#Preview("A run that failed its gate") {
    RunPage(model: TrainingPreview.training(), run: TrainingPreview.run)
        .frame(width: 380, height: 520)
        .background(Palette.inspector)
}

#Preview("Regimes: foundation open") {
    let training = TrainingPreview.training()
    RegimesView(model: training)
        .frame(width: 900, height: 520)
        .task {
            await training.connect(binary: previewBinary)
            training.selectedRegime = "foundation"
        }
}

#Preview("Datasets: heat-notes open, and the kits") {
    let training = TrainingPreview.training()
    DatasetsView(model: training)
        .frame(width: 900, height: 640)
        .task {
            await training.connect(binary: previewBinary)
            training.selectedDataset = "heat-notes"
        }
}

#Preview("New run: the first stored set, on heat-notes") {
    let training = TrainingPreview.training()
    RunSheet(model: training)
        .task {
            await training.connect(binary: previewBinary)
            training.newRun()
        }
}

#Preview("New run: QLoRA, tuned") {
    let training = TrainingPreview.training()
    RunSheet(model: training)
        .task {
            await training.connect(binary: previewBinary)
            training.newRun()
            training.runForm.method = "qlora"
            training.runForm.iters = "200"
            training.runForm.batchSize = "2"
            training.runForm.gradCheckpoint = true
            training.runForm.maskPrompt = true
        }
}

#Preview("Promote: a run that failed its gate") {
    let training = TrainingPreview.training()
    PromoteSheet(model: training)
        .task {
            await training.connect(binary: previewBinary)
            TrainingPreview.promoting(training)
        }
}

#Preview("Promote: forced past the gate, quantized") {
    let training = TrainingPreview.training()
    PromoteSheet(model: training)
        .task {
            await training.connect(binary: previewBinary)
            TrainingPreview.promoting(training)
            training.promoteForm.target = "gguf"
            training.promoteForm.quantize = "Q4_K_M"
            training.promoteForm.force = true
        }
}

#Preview("Synthesize: a teacher, a kit") {
    let training = TrainingPreview.training()
    SynthSheet(model: training)
        .task {
            await training.connect(binary: previewBinary)
            training.newSynthesis()
            training.synthForm.name = "clerk-data"
            training.synthForm.count = "50"
        }
}

#Preview("New dataset: nothing named yet") {
    let training = TrainingPreview.training()
    CreateSheet(model: training)
        .task { training.newDataset() }
}

#Preview("New dataset: from your chats") {
    let training = TrainingPreview.training()
    CreateSheet(model: training)
        .task {
            training.newDataset()
            training.createName = "notes"
            training.createFrom = "sessions"
        }
}

#Preview("A sheet's frame: its command as typed") {
    SheetFrame(title: "New dataset", arguments: ["datasets", "create", "notes", "--from", "sessions"], action: "Create", act: {}, cancel: {}) {
        TextField("Dataset name", text: .constant("notes")).textFieldStyle(.roundedBorder)
    }
}

#Preview("A sheet's frame: nothing to run yet") {
    SheetFrame(title: "New dataset", arguments: nil, action: "Create", act: {}, cancel: {}) {
        TextField("Dataset name", text: .constant("")).textFieldStyle(.roundedBorder)
    }
}
#endif
