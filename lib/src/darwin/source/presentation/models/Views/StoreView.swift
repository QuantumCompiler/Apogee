import AppKit
import BusinessReads
import PresentationShell
import PresentationStyles
import SwiftUI

/// Stored weights: every set by handle, grouped by model, and the open set's
/// details beside it.
struct StoreView: View {
    @Bindable var store: StoreViewModel
    let shell: ShellViewModel
    /// Opens a new session on a backend.
    let useInSession: (String) -> Void

    var body: some View {
        HStack(spacing: 0) {
            StoreTable(store: store)
            if store.current != nil && shell.layout.sidePanes {
                Divider()
                StoreDetails(store: store, shell: shell, useInSession: useInSession)
                    .frame(width: 380)
            }
        }
        .task { await store.refresh() }
        .sheet(item: Binding(get: { store.register.map(RegisterID.init) }, set: { if $0 == nil { store.register = nil } })) { _ in
            RegisterSheet(store: store)
        }
        .sheet(isPresented: Binding(get: { store.convertForm != nil }, set: { if !$0 { store.convertForm = nil } })) {
            ConvertSheet(store: store)
        }
        .sheet(isPresented: Binding(get: { store.quantizeForm != nil }, set: { if !$0 { store.quantizeForm = nil } })) {
            QuantizeSheet(store: store)
        }
        .sheet(isPresented: Binding(get: { store.pairing != nil }, set: { if !$0 { store.pairing = nil } })) {
            PairSheet(store: store)
        }
        .sheet(item: Binding(get: { store.renaming.map(RenameID.init) }, set: { if $0 == nil { store.renaming = nil } })) { id in
            RenameStoredSheet(model: id.model) { name in Task { await store.rename(id.model, to: name) } }
        }
        .confirmationDialog(
            pendingTitle, isPresented: Binding(get: { store.pending != nil }, set: { if !$0 { store.dismissPending() } }),
            titleVisibility: .visible
        ) {
            switch store.pending {
            case .deleteSet, .deleteModel:
                Button("Delete", role: .destructive) { Task { store.andBackends = false; await store.confirm() } }
                // Only where the CLI's plan names a backend pointing in.
                if store.offersAndBackends {
                    Button("Delete it and the backends that run from it", role: .destructive) { Task { store.andBackends = true; await store.confirm() } }
                }
            case .removeBackend:
                Button("Remove the backend", role: .destructive) { Task { await store.confirm() } }
            case .registerAll:
                Button("Register them") { Task { await store.confirm() } }
            case nil:
                EmptyView()
            }
            Button("Cancel", role: .cancel) { store.dismissPending() }
        } message: {
            Text(pendingMessage)
        }
        .alert("That didn't work", isPresented: Binding(get: { store.failure != nil }, set: { if !$0 { store.dismissFailure() } })) {
            Button("OK") { store.dismissFailure() }
        } message: {
            Text(store.failure ?? "")
        }
    }

    private var pendingTitle: String {
        switch store.pending {
        case .deleteSet(let set, _): "Delete \(set.handle)?"
        case .deleteModel(let model, _): "Delete all of \(model)?"
        case .removeBackend(let name): "Remove the backend \(name)?"
        case .registerAll: "Register every stored model not registered?"
        case nil: ""
        }
    }

    private var pendingMessage: String {
        switch store.pending {
        case .deleteSet(let set, let plan):
            return plan.question(running: "apogee models delete \(set.handle) --yes")
        case .deleteModel(let model, let plan):
            return plan.question(running: "apogee models delete \(model) --yes")
        case .removeBackend(let name):
            return "apogee config delete-backend \(name) -- its config entry goes; the weights stay under Stored weights, ready to register again."
        case .registerAll(let report):
            return report + "\nRuns apogee config scan --register."
        case nil:
            return ""
        }
    }

    struct RegisterID: Identifiable {
        let form: StoreViewModel.RegisterForm
        var id: String { form.handle }
    }

    struct RenameID: Identifiable {
        let model: String
        var id: String { model }
    }
}

/// The table: a group per model folder, a row per set.
struct StoreTable: View {
    @Bindable var store: StoreViewModel

    var body: some View {
        ScrollView {
            LazyVStack(alignment: .leading, spacing: 0) {
                if let narration = store.narration {
                    DeleteNarrationCard(narration: narration) { store.dismissNarration() }
                        .padding(.horizontal, Spacing.l)
                        .padding(.top, Spacing.m)
                }
                HStack(spacing: Spacing.m) {
                    Text(store.summary).font(.apogeeSmall).foregroundStyle(Palette.textSecondary)
                    Spacer()
                    if store.unregistered > 0 {
                        Button("Register the \(store.unregistered) not registered…") { Task { await store.askToRegisterAll() } }
                            .help("apogee config scan --register")
                    }
                }
                .padding(.horizontal, Spacing.l)
                .padding(.vertical, Spacing.s)
                HStack(spacing: Spacing.m) {
                    Text("SET").frame(maxWidth: .infinity, alignment: .leading)
                    Text("FORMAT").frame(width: 128, alignment: .leading)
                    Text("PRECISION").frame(width: 120, alignment: .leading)
                    Text("SIZE").frame(width: 72, alignment: .trailing)
                    Text("STATE").frame(width: 200, alignment: .leading)
                }
                .font(.system(size: TypeScale.label, weight: .medium, design: .monospaced))
                .foregroundStyle(Palette.textTertiary)
                .padding(.horizontal, Spacing.l)
                .padding(.vertical, Spacing.s)
                ForEach(store.groups, id: \.model) { group in
                    HStack(alignment: .firstTextBaseline) {
                        Text(group.model).font(.apogeeBody.weight(.semibold))
                        Spacer()
                        Text(store.summary(of: group.sets)).font(.apogeeMeta).foregroundStyle(Palette.textTertiary)
                    }
                        .padding(.horizontal, Spacing.l)
                        .padding(.top, Spacing.m)
                        .padding(.bottom, Spacing.xs)
                    ForEach(group.sets) { set in row(set) }
                }
                if let folded = store.foldedText {
                    HStack(spacing: Spacing.s) {
                        Text(folded).font(.apogeeSmall).foregroundStyle(Palette.textTertiary)
                        Button("Show them") { store.showingAll = true }.buttonStyle(.link).font(.apogeeSmall)
                    }
                    .padding(.horizontal, Spacing.l)
                    .padding(.top, Spacing.m)
                } else if store.showingAll {
                    Button("Fold the snapshots conversions consumed") { store.showingAll = false }
                        .buttonStyle(.link).font(.apogeeSmall).padding(.horizontal, Spacing.l).padding(.top, Spacing.m)
                }
                Text("Every set in the model store, read with models list and models info. SafeTensors weights don't run as they are: convert them, and register what the conversion makes. An MLX set says what kind it is beside its format, and offers what runs that kind.")
                    .font(.apogeeSmall)
                    .foregroundStyle(Palette.textTertiary)
                    .padding(Spacing.l)
            }
        }
        .frame(maxWidth: .infinity)
    }

    private func row(_ set: StoredSet) -> some View {
        Button { Task { await store.select(set.handle) } } label: {
            HStack(spacing: Spacing.m) {
                VStack(alignment: .leading, spacing: 1) {
                    Text(set.handle.split(separator: "/").dropFirst().joined(separator: "/"))
                        .font(.apogeeCode).lineLimit(1).truncationMode(.middle)
                    if let from = store.madeFromLine(set) {
                        Text(from).font(.apogeeMeta).foregroundStyle(Palette.textTertiary).lineLimit(1).truncationMode(.middle)
                    }
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                Text(set.formatLabel).font(.apogeeMeta).lineLimit(1).frame(width: 128, alignment: .leading)
                Text(set.precision ?? "—").font(.apogeeMeta).foregroundStyle(Palette.textSecondary).lineLimit(1).frame(width: 120, alignment: .leading)
                Text(store.bytes(set).map(Self.size) ?? "—").font(.apogeeMeta.monospacedDigit()).foregroundStyle(Palette.textSecondary).frame(width: 72, alignment: .trailing)
                Text(set.state).font(.apogeeSmall).foregroundStyle(stateColor(set)).lineLimit(1).frame(width: 200, alignment: .leading)
            }
            .padding(.horizontal, Spacing.l)
            .frame(minHeight: 36)
            .background(store.selected == set.handle ? Palette.selection : .clear)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }

    private func stateColor(_ set: StoredSet) -> Color {
        if set.backend != nil { return Palette.accent }
        if set.cliState.hasPrefix("cannot") { return Palette.error }
        return Palette.textSecondary
    }

    static func size(_ bytes: Int) -> String { StoreViewModel.size(bytes) }
}

/// One set's details: its facts, where it came from and what came of it,
/// its actions by format and kind, the model it belongs to, and its command.
struct StoreDetails: View {
    @Bindable var store: StoreViewModel
    let shell: ShellViewModel
    let useInSession: (String) -> Void

    var body: some View {
        if let set = store.current {
            ScrollView {
                VStack(alignment: .leading, spacing: Spacing.m) {
                    HStack(alignment: .top) {
                        VStack(alignment: .leading, spacing: 2) {
                            Text(header(set)).font(.system(size: TypeScale.label, weight: .medium, design: .monospaced)).foregroundStyle(Palette.textTertiary)
                            Text(set.handle).font(.apogeeCode.weight(.semibold)).textSelection(.enabled)
                            Text(set.state).font(.apogeeSmall).foregroundStyle(Palette.textSecondary)
                            if set.mlxKind == .vision {
                                Label("Reads images, through mlx-vlm", systemImage: "photo").font(.apogeeSmall).foregroundStyle(Palette.textSecondary)
                            }
                        }
                        Spacer()
                        Button { store.close() } label: { Image(systemName: "xmark") }.buttonStyle(.plain)
                    }
                    actions(set)
                    if let advice = store.advice(set) { Text(advice).font(.apogeeSmall).foregroundStyle(Palette.textSecondary).fixedSize(horizontal: false, vertical: true) }
                    if let note = set.note { Text(note).font(.apogeeSmall).foregroundStyle(Palette.textTertiary) }
                    section("FACTS") {
                        Grid(alignment: .leadingFirstTextBaseline, horizontalSpacing: Spacing.m, verticalSpacing: Spacing.xs) {
                            ForEach(Array(facts.enumerated()), id: \.offset) { _, fact in
                                GridRow {
                                    Text(fact.label).foregroundStyle(Palette.textTertiary)
                                    Text(fact.value).textSelection(.enabled).fixedSize(horizontal: false, vertical: true)
                                }
                            }
                        }
                        .font(.apogeeSmall)
                    }
                    if !store.madeFrom.isEmpty {
                        section("MADE FROM") {
                            ForEach(Array(store.madeFrom.enumerated()), id: \.offset) { _, step in
                                lineage(step)
                            }
                        }
                    }
                    if !store.madeFromIt.isEmpty {
                        section("MADE FROM IT") {
                            ForEach(store.madeFromIt, id: \.self) { handle in link(handle) }
                        }
                    }
                    section("THE MODEL · \(set.model)") {
                        HStack {
                            Button("Rename…") { store.renaming = set.model }
                            Button("Delete all of it…") { Task { await store.askToDeleteModel(set.model) } }
                        }
                    }
                    section("ON THE COMMAND LINE") {
                        Text("apogee models info \(set.handle)")
                            .font(.apogeeCode).foregroundStyle(Palette.textSecondary).textSelection(.enabled)
                            .padding(Spacing.s).frame(maxWidth: .infinity, alignment: .leading)
                            .background(Palette.code, in: RoundedRectangle(cornerRadius: Radius.control))
                    }
                }
                .padding(Spacing.l)
            }
            .background(Palette.inspector)
        }
    }

    /// The format, an MLX set's kind as the CLI wrote it, and the precision:
    /// `MLX · vision · 4-bit (affine, group 64)`.
    private func header(_ set: StoredSet) -> String {
        var parts = [set.formatName.uppercased()]
        if set.format == "mlx", let kind = set.kind { parts.append(kind.word) }
        if let precision = set.precision { parts.append(precision) }
        return parts.joined(separator: " · ")
    }

    /// The typed facts where the CLI states them, then its record's lines.
    private var facts: [Field] {
        guard let info = store.currentInfo else { return [] }
        var out: [Field] = []
        if let bytes = info.data?.bytes { out.append(Field("size", StoreTable.size(bytes))) }
        if let window = info.data?.window {
            out.append(Field("window", "\(window.tokens.formatted()) tokens" + (window.trained.map { " (trained for \($0.formatted()))" } ?? "")))
        }
        if let cache = info.data?.cache { out.append(Field("cache", StoreTable.size(cache.bytes) + (cache.type.map { " at \($0)" } ?? ""))) }
        let skip: Set<String> = ["weights", "made from it", "lineage", "listing"]
        return out + info.fields.filter { !skip.contains($0.label) && !(info.data != nil && ["size", "window", "cache"].contains($0.label)) }
    }

    /// The set's own actions on one line, its files' on the next: only what
    /// the view-model offers for its format and kind.
    @ViewBuilder private func actions(_ set: StoredSet) -> some View {
        let offered = store.actions(set)
        let files: [StoreViewModel.Action] = [.checkFiles, .showInFinder, .delete]
        HStack(spacing: Spacing.s) {
            ForEach(Array(offered.filter { !files.contains($0) }.enumerated()), id: \.offset) { _, action in
                button(action, set)
            }
        }
        HStack(spacing: Spacing.s) {
            ForEach(Array(offered.filter { files.contains($0) }.enumerated()), id: \.offset) { _, action in
                button(action, set)
            }
        }
    }

    @ViewBuilder private func button(_ action: StoreViewModel.Action, _ set: StoredSet) -> some View {
        switch action {
        case .convertToGGUF:
            Button("Convert to GGUF…") { store.askToConvert(set, mlx: false) }
        case .convertToMLX:
            Button("Convert to MLX…") { store.askToConvert(set, mlx: true) }
        case .quantize:
            Button("Quantize…") { store.askToQuantize(set) }
        case .register:
            Button("Register…") { Task { await store.askToRegister(set) } }
        case .useInSession(let backend):
            Button("Use in a new session") { useInSession(backend) }
        case .useForEmbeddings(let backend):
            Button("Use for embeddings") { Task { await store.useForEmbeddings(backend) } }
                .help("apogee config set-default-embedding \(backend)")
        case .removeBackend(let backend):
            Button("Remove the backend…") { store.askToRemoveBackend(backend) }
        case .newClip:
            Button("New clip with it") { shell.go(.video) }
        case .makeDefaultVideo:
            Button("Make it the default") { Task { await store.makeDefaultVideo(set) } }
                .help("apogee config set-default-video \(set.handle)")
        case .openVoice:
            Button("Open Voice") { shell.go(.voice) }
        case .pairWithBase:
            Button("Pair with its base…") { Task { await store.askToPair(set) } }
                .help("apogee config set-backend <its base's backend> --draft \(store.path ?? set.handle)")
        case .unpair(let backend):
            Button("Unpair from \(backend)") { Task { await store.unpair(backend) } }
                .help("apogee config set-backend \(backend) --draft \"\"")
        case .checkFiles:
            Button("Check the files") { store.check(set) }
                .help("apogee models repair \(set.handle)")
        case .showInFinder:
            if let path = store.path {
                Button("Show in Finder") { NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: path)]) }
            }
        case .delete:
            Button("Delete…") { Task { await store.askToDelete(set) } }
        }
    }

    private func lineage(_ step: ModelInfo.Step) -> some View {
        HStack(alignment: .firstTextBaseline, spacing: Spacing.xs) {
            Text(step.kind).foregroundStyle(Palette.textSecondary)
            if let from = step.from, store.isListed(from) {
                Button(from) { Task { await store.select(from) } }.buttonStyle(.link).font(.apogeeCode)
            } else {
                Text(step.from ?? "").font(.apogeeCode).textSelection(.enabled)
            }
            if let detail = step.detail, !detail.isEmpty { Text(detail).foregroundStyle(Palette.textTertiary) }
            if step.inferred { Text("inferred").foregroundStyle(Palette.warning) }
            if step.missing { Text("gone").foregroundStyle(Palette.error) }
        }
        .font(.apogeeSmall)
    }

    private func link(_ handle: String) -> some View {
        Group {
            if store.isListed(handle) {
                Button(handle) { Task { await store.select(handle) } }.buttonStyle(.link)
            } else {
                Text(handle).textSelection(.enabled)
            }
        }
        .font(.apogeeCode)
    }

    private func section<Content: View>(_ title: String, @ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: Spacing.xs) {
            Text(title).font(.system(size: TypeScale.label, weight: .medium, design: .monospaced)).tracking(0.8).foregroundStyle(Palette.textTertiary)
            content()
        }
    }
}

/// `config add-backend`, as typed.
struct RegisterSheet: View {
    @Bindable var store: StoreViewModel
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        if let form = store.register {
            VStack(alignment: .leading, spacing: Spacing.m) {
                Text("Register \(form.handle)").font(.apogeeHeading)
                Text(form.about)
                    .font(.apogeeSmall).foregroundStyle(Palette.textSecondary).fixedSize(horizontal: false, vertical: true)
                Form {
                    TextField("Name", text: Binding(get: { store.register?.name ?? "" }, set: { store.register?.name = $0 }))
                    TextField("Window", text: Binding(get: { store.register?.window ?? "" }, set: { store.register?.window = $0 }), prompt: Text("the model's own"))
                    if form.type == "llamacpp" {
                        Picker("Attention cache", selection: Binding(get: { store.register?.cache }, set: { store.register?.cache = $0 })) {
                            Text("q8_0 (the default)").tag(String?.none)
                            ForEach(StoreViewModel.RegisterForm.caches.filter { $0 != "q8_0" }, id: \.self) { Text($0).tag(String?.some($0)) }
                        }
                    }
                    if let projector = form.projector {
                        LabeledContent("Projector") { Text(projector).font(.apogeeCode).lineLimit(1).truncationMode(.middle) }
                    }
                }
                if let arguments = store.register?.arguments {
                    Text((["apogee"] + arguments).joined(separator: " ")).font(.apogeeCode).foregroundStyle(Palette.textTertiary).textSelection(.enabled)
                }
                HStack {
                    Spacer()
                    Button("Cancel", role: .cancel) { dismiss() }.keyboardShortcut(.cancelAction)
                    Button("Register") { Task { await store.registerIt() } }
                        .keyboardShortcut(.defaultAction)
                        .disabled(store.register?.arguments == nil)
                }
            }
            .padding(Spacing.xl)
            .frame(width: 560)
        }
    }
}

/// `config set-backend <base> --draft <drafter>`: a drafter paired with one
/// of the bases the binary offers it, or -- none offered -- why, in the CLI's
/// words for the drafter.
struct PairSheet: View {
    @Bindable var store: StoreViewModel
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        if let form = store.pairing {
            VStack(alignment: .leading, spacing: Spacing.m) {
                Text("Pair \(form.drafter) with its base").font(.apogeeHeading)
                Text("A drafter proposes the next tokens and its base checks them, so every answer is still the base's own. It runs beside an mlx backend over its base, through mlx-vlm's speculative path.")
                    .font(.apogeeSmall).foregroundStyle(Palette.textSecondary).fixedSize(horizontal: false, vertical: true)
                if let said = form.said {
                    Text(said).font(.apogeeSmall).foregroundStyle(Palette.textTertiary).fixedSize(horizontal: false, vertical: true)
                }
                if form.bases.isEmpty {
                    Text("No backend runs a base it pairs with. Register mlx-vlm's build of its base -- a vision set of its family and width, the one its record names -- then pair it.")
                        .font(.apogeeSmall).foregroundStyle(Palette.warning).fixedSize(horizontal: false, vertical: true)
                } else {
                    Picker("Base", selection: Binding(get: { store.pairing?.base }, set: { store.pairing?.base = $0 })) {
                        ForEach(form.bases) { base in
                            Text(base.handle.map { "\(base.backend) · \($0)" } ?? base.backend).tag(String?.some(base.backend))
                        }
                    }
                    .pickerStyle(.radioGroup)
                }
                if let arguments = store.pairing?.arguments {
                    Text((["apogee"] + arguments).joined(separator: " ")).font(.apogeeCode).foregroundStyle(Palette.textTertiary).textSelection(.enabled)
                }
                HStack {
                    Spacer()
                    Button("Cancel", role: .cancel) { dismiss() }.keyboardShortcut(.cancelAction)
                    Button("Pair") { Task { await store.pair() } }
                        .keyboardShortcut(.defaultAction)
                        .disabled(store.pairing?.arguments == nil)
                }
            }
            .padding(Spacing.xl)
            .frame(width: 560)
        }
    }
}

/// `models rename <model> <new>`.
struct RenameStoredSheet: View {
    let model: String
    let rename: (String) -> Void
    @State private var name: String
    @Environment(\.dismiss) private var dismiss

    init(model: String, rename: @escaping (String) -> Void) {
        self.model = model
        self.rename = rename
        _name = State(initialValue: model)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.m) {
            Text("Rename \(model)").font(.apogeeHeading)
            TextField("Name", text: $name).textFieldStyle(.roundedBorder)
            Text("apogee models rename \(model) \(name) -- its backends follow.").font(.apogeeCode).foregroundStyle(Palette.textTertiary)
            HStack {
                Spacer()
                Button("Cancel", role: .cancel) { dismiss() }.keyboardShortcut(.cancelAction)
                Button("Rename") { rename(name); dismiss() }.keyboardShortcut(.defaultAction)
            }
        }
        .padding(Spacing.xl)
        .frame(width: 460)
    }
}

/// The convert sheet: `models convert`, one command -- the precision, further
/// levels, registering what is made -- with the steps it takes, each sized
/// as an estimate; and for MLX, which converter runs: a towered set's towers
/// kept through mlx-vlm, or Text only.
struct ConvertSheet: View {
    @Bindable var store: StoreViewModel
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        if let form = store.convertForm {
            VStack(alignment: .leading, spacing: Spacing.m) {
                Text(form.mlx ? "Convert to MLX" : "Convert to GGUF").font(.apogeeHeading)
                Text(form.handle).font(.apogeeCode).foregroundStyle(Palette.textTertiary)
                Text(form.converterNote)
                    .font(.apogeeSmall).foregroundStyle(Palette.textSecondary).fixedSize(horizontal: false, vertical: true)
                Form {
                    if form.textOnlyOffered {
                        Toggle("Text only: its language model alone, built by mlx-lm", isOn: Binding(get: { store.convertForm?.textOnly ?? false }, set: { store.convertForm?.textOnly = $0 }))
                            .disabled(form.textOnlyOnly)
                        if let said = form.towersSaid {
                            Text(said).font(.apogeeSmall).foregroundStyle(Palette.textTertiary).fixedSize(horizontal: false, vertical: true)
                        }
                    }
                    Picker("Precision", selection: Binding(get: { store.convertForm?.precision }, set: { store.convertForm?.precision = $0 })) {
                        Text("\(form.defaultPrecision), the default").tag(String?.none)
                        ForEach(form.precisions.dropFirst(), id: \.self) { Text($0 == "auto" ? "auto, as the weights are" : $0).tag(String?.some($0)) }
                    }
                    if let note = form.aloneNote {
                        Text(note).font(.apogeeSmall).foregroundStyle(Palette.textSecondary).fixedSize(horizontal: false, vertical: true)
                    } else if !form.levelChoices.isEmpty {
                        LabeledContent(form.mlx ? "Also make, one conversion each" : "Then quantize to") {
                            HStack(spacing: Spacing.xs) {
                                ForEach(form.levelChoices, id: \.self) { level in
                                    Toggle(level, isOn: Binding(
                                        get: { store.convertForm?.levels.contains(level) ?? false },
                                        set: { on in
                                            if on { store.convertForm?.levels.insert(level) } else { store.convertForm?.levels.remove(level) }
                                        }))
                                        .toggleStyle(.button)
                                }
                            }
                            .font(.apogeeMeta.monospaced())
                        }
                    }
                    if form.registers {
                        Toggle("Register what's made", isOn: Binding(get: { store.convertForm?.register ?? false }, set: { store.convertForm?.register = $0 }))
                            .disabled(!form.levels.isEmpty)
                        TextField("Names begin with", text: Binding(get: { store.convertForm?.baseName ?? "" }, set: { store.convertForm?.baseName = $0 }), prompt: Text(modelBaseName(form.handle.split(separator: "/").first.map(String.init) ?? "")))
                        if form.mlx {
                            TextField("Model type", text: Binding(get: { store.convertForm?.modelType ?? "" }, set: { store.convertForm?.modelType = $0 }), prompt: Text("as its config says"))
                        }
                    }
                }
                StepsList(steps: form.steps)
                Text("Sizes are estimated from bits per weight. Whether the converter takes this architecture is learned by converting: if it can't, the job stops with the converter's own reason.")
                    .font(.apogeeSmall).foregroundStyle(Palette.textTertiary).fixedSize(horizontal: false, vertical: true)
                Text((["apogee"] + form.arguments).joined(separator: " ")).font(.apogeeCode).foregroundStyle(Palette.textTertiary).textSelection(.enabled)
                HStack {
                    Spacer()
                    Button("Cancel", role: .cancel) { dismiss() }.keyboardShortcut(.cancelAction)
                    Button("Convert") { store.convert() }.keyboardShortcut(.defaultAction)
                }
            }
            .padding(Spacing.xl)
            .frame(width: 620)
        }
    }
}

/// The quantize sheet: `models quantize`, and -- asked -- the backend that
/// registers what it made.
struct QuantizeSheet: View {
    @Bindable var store: StoreViewModel
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        if let form = store.quantizeForm {
            VStack(alignment: .leading, spacing: Spacing.m) {
                Text("Quantize \(form.handle.split(separator: "/").first.map(String.init) ?? form.handle)").font(.apogeeHeading)
                Text(form.handle).font(.apogeeCode).foregroundStyle(Palette.textTertiary)
                Text("A smaller copy beside it, made by llama.cpp. The unquantized GGUF stays.")
                    .font(.apogeeSmall).foregroundStyle(Palette.textSecondary)
                Form {
                    Picker("Level", selection: Binding(get: { store.quantizeForm?.level ?? "Q4_K_M" }, set: { store.quantizeForm?.choose($0) })) {
                        ForEach(QuantizeForm.levels, id: \.self) { level in
                            Text(level + (level == "Q4_K_M" ? ", the default" : "") + (Estimate.size(level, from: form.bytes).map { " · \($0)" } ?? "")).tag(level)
                        }
                    }
                    Toggle("Register it", isOn: Binding(get: { store.quantizeForm?.register ?? false }, set: { store.quantizeForm?.register = $0 }))
                    if form.register {
                        TextField("Name", text: Binding(get: { store.quantizeForm?.name ?? "" }, set: { store.quantizeForm?.rename($0) }))
                    }
                }
                StepsList(steps: form.steps)
                Text("Sizes are estimated from bits per weight against the unquantized GGUF's 16.")
                    .font(.apogeeSmall).foregroundStyle(Palette.textTertiary)
                Text((["apogee"] + form.arguments).joined(separator: " ") + (form.register ? "\nthen  apogee config add-backend \(form.name) --type llamacpp --model-path <the new file>" : ""))
                    .font(.apogeeCode).foregroundStyle(Palette.textTertiary).textSelection(.enabled)
                HStack {
                    Spacer()
                    Button("Cancel", role: .cancel) { dismiss() }.keyboardShortcut(.cancelAction)
                    Button("Quantize") { store.quantize() }
                        .keyboardShortcut(.defaultAction)
                        .disabled(form.register && form.name.trimmingCharacters(in: .whitespaces).isEmpty)
                }
            }
            .padding(Spacing.xl)
            .frame(width: 560)
        }
    }
}

/// A store job's steps as its sheet lists them, sizes as estimates.
struct StepsList: View {
    let steps: [StoreStep]

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.xs) {
            Text("STEPS").font(.system(size: TypeScale.label, weight: .medium, design: .monospaced)).foregroundStyle(Palette.textTertiary)
            ForEach(Array(steps.enumerated()), id: \.offset) { index, step in
                HStack(spacing: Spacing.s) {
                    Text("\(index + 1)").font(.apogeeMeta.monospacedDigit()).foregroundStyle(Palette.textTertiary)
                    Text(step.label).font(.apogeeSmall)
                    if let estimate = step.estimate { Text(estimate).font(.apogeeMeta).foregroundStyle(Palette.textTertiary) }
                }
            }
        }
    }
}

#if DEBUG
#Preview("Stored weights: two snapshots folded") {
    let store = ModelsPreview.store()
    StoreTable(store: store)
        .frame(width: 900, height: 560)
        .task {
            await store.connect(binary: previewBinary)
            await store.refresh()
        }
}

#Preview("Stored weights: every set, the consumed shown, the Q4_K_M open") {
    let store = ModelsPreview.store(showingAll: true)
    StoreTable(store: store)
        .frame(width: 900, height: 640)
        .task {
            await store.connect(binary: previewBinary)
            await store.refresh()
            await store.select(ModelsPreview.q4)
        }
}

#Preview("Register the MLX model, named as config scan names it") {
    let store = ModelsPreview.store()
    RegisterSheet(store: store)
        .task {
            await store.connect(binary: previewBinary)
            await store.refresh()
            if let mlx = store.sets.first(where: { $0.handle == ModelsPreview.mlx }) { await store.askToRegister(mlx) }
        }
}

#Preview("Register the F16 GGUF: a window and an attention cache chosen") {
    let store = ModelsPreview.store()
    RegisterSheet(store: store)
        .task {
            await store.connect(binary: previewBinary)
            await store.refresh()
            if let f16 = store.sets.first(where: { $0.handle == ModelsPreview.f16 }) {
                await store.askToRegister(f16)
                store.register?.window = "16384"
                store.register?.cache = "f16"
            }
        }
}

#Preview("Rename a model's folder") {
    RenameStoredSheet(model: "Llama3.2-1B-Instruct") { _ in }
}

#Preview("Convert to GGUF, then quantize to two levels") {
    let store = ModelsPreview.store(showingAll: true)
    ConvertSheet(store: store)
        .task {
            await store.connect(binary: previewBinary)
            await store.refresh()
            if let snapshot = store.sets.first(where: { $0.handle == ModelsPreview.snapshot }) {
                store.askToConvert(snapshot, mlx: false)
                store.convertForm?.levels = ["Q4_K_M", "Q8_0"]
            }
        }
}

#Preview("Convert to MLX, 4bit the default") {
    let store = ModelsPreview.store(showingAll: true)
    ConvertSheet(store: store)
        .task {
            await store.connect(binary: previewBinary)
            await store.refresh()
            if let snapshot = store.sets.first(where: { $0.handle == ModelsPreview.snapshot }) {
                store.askToConvert(snapshot, mlx: true)
            }
        }
}

#Preview("Quantize the F16 GGUF to Q4_K_M, registered") {
    let store = ModelsPreview.store()
    QuantizeSheet(store: store)
        .task {
            await store.connect(binary: previewBinary)
            await store.refresh()
            if let f16 = store.sets.first(where: { $0.handle == ModelsPreview.f16 }) { store.askToQuantize(f16) }
        }
}

#Preview("Quantize to Q8_0, not registered") {
    let store = ModelsPreview.store()
    QuantizeSheet(store: store)
        .task {
            await store.connect(binary: previewBinary)
            await store.refresh()
            if let f16 = store.sets.first(where: { $0.handle == ModelsPreview.f16 }) {
                store.askToQuantize(f16)
                store.quantizeForm?.choose("Q8_0")
                store.quantizeForm?.register = false
            }
        }
}

#Preview("A conversion's steps, each size an estimate") {
    StepsList(steps: ModelsPreview.conversion.steps)
        .padding(Spacing.xl)
}

#Preview("Stored weights beside the open set") {
    let store = ModelsPreview.store()
    StoreView(store: store, shell: ModelsPreview.shell()) { _ in }
        .frame(width: 1300, height: 680)
        .task {
            await store.connect(binary: previewBinary)
            await store.select(ModelsPreview.q4)
        }
}

#Preview("A registered GGUF: made from, its backend") {
    let store = ModelsPreview.store()
    StoreDetails(store: store, shell: ModelsPreview.shell()) { _ in }
        .frame(width: 380, height: 760)
        .task {
            await store.connect(binary: previewBinary)
            await store.refresh()
            await store.select(ModelsPreview.q4)
        }
}

#Preview("A diffusion model: no conversion it takes, said in the CLI's words") {
    let store = ModelsPreview.conversionsStore()
    StoreDetails(store: store, shell: ModelsPreview.shell()) { _ in }
        .frame(width: 380, height: 640)
        .task {
            await store.connect(binary: previewBinary)
            await store.refresh()
            await store.select(ModelsPreview.wan)
        }
}

#Preview("A draft head: no conversion, its base said by its width") {
    let store = ModelsPreview.conversionsStore()
    StoreDetails(store: store, shell: ModelsPreview.shell()) { _ in }
        .frame(width: 380, height: 640)
        .task {
            await store.connect(binary: previewBinary)
            await store.refresh()
            await store.select(ModelsPreview.draftHead)
        }
}

#Preview("A SafeTensors snapshot: what was made from it") {
    let store = ModelsPreview.store(showingAll: true)
    StoreDetails(store: store, shell: ModelsPreview.shell()) { _ in }
        .frame(width: 380, height: 760)
        .task {
            await store.connect(binary: previewBinary)
            await store.refresh()
            await store.select(ModelsPreview.snapshot)
        }
}
#Preview("Stored weights: every MLX kind, said beside its format") {
    let store = ModelsPreview.kindsStore()
    StoreTable(store: store)
        .frame(width: 960, height: 560)
        .task {
            await store.connect(binary: previewBinary)
            await store.refresh()
        }
}

#Preview("Stored weights: the kinds config scan --register registered") {
    let store = ModelsPreview.kindsRegisteredStore()
    StoreTable(store: store)
        .frame(width: 960, height: 560)
        .task {
            await store.connect(binary: previewBinary)
            await store.refresh()
        }
}

#Preview("A chat set: MLX · chat, registered as before") {
    let store = ModelsPreview.kindsStore()
    StoreDetails(store: store, shell: ModelsPreview.shell()) { _ in }
        .frame(width: 380, height: 720)
        .task {
            await store.connect(binary: previewBinary)
            await store.refresh()
            await store.select(ModelsPreview.kindChat)
        }
}

#Preview("A vision set: it reads images, and registers as a chat model does") {
    let store = ModelsPreview.kindsStore()
    StoreDetails(store: store, shell: ModelsPreview.shell()) { _ in }
        .frame(width: 380, height: 720)
        .task {
            await store.connect(binary: previewBinary)
            await store.refresh()
            await store.select(ModelsPreview.kindVision)
        }
}

#Preview("An embedding set: registered for the embedding role") {
    let store = ModelsPreview.kindsStore()
    StoreDetails(store: store, shell: ModelsPreview.shell()) { _ in }
        .frame(width: 380, height: 720)
        .task {
            await store.connect(binary: previewBinary)
            await store.refresh()
            await store.select(ModelsPreview.kindEmbedding)
        }
}

#Preview("A registered embedding set: Use for embeddings, no session") {
    let store = ModelsPreview.kindsRegisteredStore()
    StoreDetails(store: store, shell: ModelsPreview.shell()) { _ in }
        .frame(width: 380, height: 720)
        .task {
            await store.connect(binary: previewBinary)
            await store.refresh()
            await store.select(ModelsPreview.kindEmbedding)
        }
}

#Preview("A registered vision set: a session, and it reads images") {
    let store = ModelsPreview.kindsRegisteredStore()
    StoreDetails(store: store, shell: ModelsPreview.shell()) { _ in }
        .frame(width: 380, height: 720)
        .task {
            await store.connect(binary: previewBinary)
            await store.refresh()
            await store.select(ModelsPreview.kindVision)
        }
}

#Preview("A drafter, no backend running a base: nothing runs it as chat") {
    let store = ModelsPreview.kindsStore()
    StoreDetails(store: store, shell: ModelsPreview.shell()) { _ in }
        .frame(width: 380, height: 720)
        .task {
            await store.connect(binary: previewBinary)
            await store.refresh()
            await store.select(ModelsPreview.kindDrafter)
        }
}

#Preview("A drafter, its base registered: Pair with its base") {
    let store = StoreViewModel(runner: ModelsPreview.draftsRunner(paired: false))
    StoreDetails(store: store, shell: ModelsPreview.shell()) { _ in }
        .frame(width: 380, height: 720)
        .task {
            await store.connect(binary: previewBinary)
            await store.refresh()
            await store.select(ModelsPreview.drafter)
        }
}

#Preview("Pair a drafter: the one base the binary offers it") {
    let store = StoreViewModel(runner: ModelsPreview.draftsRunner(paired: false))
    PairSheet(store: store)
        .task {
            await store.connect(binary: previewBinary)
            await store.refresh()
            if let set = store.sets.first(where: { $0.handle == ModelsPreview.drafter }) { await store.askToPair(set) }
        }
}

#Preview("Pair a drafter: no backend runs its base") {
    let store = ModelsPreview.kindsStore()
    PairSheet(store: store)
        .task {
            await store.connect(binary: previewBinary)
            await store.refresh()
            if let set = store.sets.first(where: { $0.handle == ModelsPreview.kindDrafter }) { await store.askToPair(set) }
        }
}

#Preview("A paired drafter: the draft of Gemma4-E4B-4bit, Unpair") {
    let store = StoreViewModel(runner: ModelsPreview.draftsRunner(paired: true))
    StoreDetails(store: store, shell: ModelsPreview.shell()) { _ in }
        .frame(width: 380, height: 720)
        .task {
            await store.connect(binary: previewBinary)
            await store.refresh()
            await store.select(ModelsPreview.drafter)
        }
}

#Preview("A video model: New clip with it, Make it the default") {
    let store = ModelsPreview.kindsStore()
    StoreDetails(store: store, shell: ModelsPreview.shell()) { _ in }
        .frame(width: 380, height: 720)
        .task {
            await store.connect(binary: previewBinary)
            await store.refresh()
            await store.select(ModelsPreview.kindVideo)
        }
}

#Preview("A voice model: Open Voice") {
    let store = ModelsPreview.kindsStore()
    StoreDetails(store: store, shell: ModelsPreview.shell()) { _ in }
        .frame(width: 380, height: 720)
        .task {
            await store.connect(binary: previewBinary)
            await store.refresh()
            await store.select(ModelsPreview.kindVoice)
        }
}

#Preview("A kind this app does not know: shown as written, only its delete") {
    let store = ModelsPreview.kindsStore()
    StoreDetails(store: store, shell: ModelsPreview.shell()) { _ in }
        .frame(width: 380, height: 720)
        .task {
            await store.connect(binary: previewBinary)
            await store.refresh()
            await store.select(ModelsPreview.kindUnknown)
        }
}

#Preview("Register an embedding set: an entry the embedding role can name") {
    let store = ModelsPreview.kindsStore()
    RegisterSheet(store: store)
        .task {
            await store.connect(binary: previewBinary)
            await store.refresh()
            if let set = store.sets.first(where: { $0.handle == ModelsPreview.kindEmbedding }) { await store.askToRegister(set) }
        }
}

#Preview("Register a vision set: it reads images") {
    let store = ModelsPreview.kindsStore()
    RegisterSheet(store: store)
        .task {
            await store.connect(binary: previewBinary)
            await store.refresh()
            if let set = store.sets.first(where: { $0.handle == ModelsPreview.kindVision }) { await store.askToRegister(set) }
        }
}

#Preview("Convert a towered set to MLX: its towers kept through mlx-vlm, Text only offered") {
    let store = ModelsPreview.routesStore()
    ConvertSheet(store: store)
        .task { await ModelsPreview.openConversion(store, ModelsPreview.routeVLM) }
}

#Preview("Convert a towered set to MLX, Text only chosen: --text-only") {
    let store = ModelsPreview.routesStore()
    ConvertSheet(store: store)
        .task {
            await ModelsPreview.openConversion(store, ModelsPreview.routeVLM)
            store.convertForm?.textOnly = true
        }
}

#Preview("Convert where only mlx-lm's text-only build is offered: --text-only always") {
    let store = ModelsPreview.routesStore()
    ConvertSheet(store: store)
        .task { await ModelsPreview.openConversion(store, ModelsPreview.routeTextOnly) }
}

#Preview("Convert a draft head: a drafter, bf16 the default, nothing registered") {
    let store = ModelsPreview.routesStore()
    ConvertSheet(store: store)
        .task { await ModelsPreview.openConversion(store, ModelsPreview.routeDrafter) }
}

#Preview("Convert Wan through mlx-video: 4bit or 8bit, nothing registered") {
    let store = ModelsPreview.routesStore()
    ConvertSheet(store: store)
        .task { await ModelsPreview.openConversion(store, ModelsPreview.routeVideo) }
}

#Preview("Convert a voice model through mlx-audio: nothing registered") {
    let store = ModelsPreview.routesStore()
    ConvertSheet(store: store)
        .task { await ModelsPreview.openConversion(store, ModelsPreview.routeVoice) }
}

#Preview("Convert an embedding model through mlx-embeddings: registered for the embedding role") {
    let store = ModelsPreview.routesStore()
    ConvertSheet(store: store)
        .task { await ModelsPreview.openConversion(store, ModelsPreview.routeEmbeddings) }
}

#Preview("Delete a registered GGUF: the CLI's plan names the backend that runs from it") {
    let store = StoreViewModel(runner: ModelsPreview.deletesStore())
    StoreView(store: store, shell: ModelsPreview.shell()) { _ in }
        .frame(width: 1300, height: 680)
        .task { await ModelsPreview.askToDelete(store, ModelsPreview.deleteLlamaQ4) }
}

#Preview("Delete a GGUF nothing runs from: Delete alone") {
    let store = StoreViewModel(runner: ModelsPreview.deletesStore())
    StoreView(store: store, shell: ModelsPreview.shell()) { _ in }
        .frame(width: 1300, height: 680)
        .task { await ModelsPreview.askToDelete(store, ModelsPreview.deleteLlamaF16) }
}

#Preview("After a delete: its backend kept, with the CLI's two doors") {
    let runner = ModelsPreview.deletesStore()
    let store = StoreViewModel(runner: runner)
    StoreTable(store: store)
        .frame(width: 900, height: 600)
        .task { await ModelsPreview.deleteKept(store, runner) }
}

#Preview("After a delete with its backends: the backend removed, as the CLI said it") {
    let runner = ModelsPreview.deletesStore()
    let store = StoreViewModel(runner: runner)
    StoreTable(store: store)
        .frame(width: 900, height: 600)
        .task { await ModelsPreview.deleteWithBackends(store, runner) }
}
#endif
