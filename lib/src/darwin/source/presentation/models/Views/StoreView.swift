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
                Button("Delete it and the backends that run from it", role: .destructive) { Task { store.andBackends = true; await store.confirm() } }
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
        case .deleteSet(let set): "Delete \(set.handle)?"
        case .deleteModel(let model): "Delete all of \(model)?"
        case .removeBackend(let name): "Remove the backend \(name)?"
        case .registerAll: "Register every stored model not registered?"
        case nil: ""
        }
    }

    private var pendingMessage: String {
        switch store.pending {
        case .deleteSet(let set):
            return "apogee models delete \(set.handle) --yes -- its folder leaves this Mac, and this can't be undone. A backend that runs from it stays unless you choose to remove it too."
        case .deleteModel(let model):
            return "apogee models delete \(model) --yes -- every set of it leaves this Mac, and this can't be undone. Its backends stay unless you choose to remove them too."
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
                    Text("FORMAT").frame(width: 96, alignment: .leading)
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
                Text("Every set in the model store, read with models list and models info. SafeTensors weights don't run as they are: convert them, and register what the conversion makes.")
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
                Text(set.formatName).font(.apogeeMeta).frame(width: 96, alignment: .leading)
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
/// its actions by format, the model it belongs to, and its command.
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
                            Text(([set.formatName.uppercased()] + (set.precision.map { [$0] } ?? [])).joined(separator: " · ")).font(.system(size: TypeScale.label, weight: .medium, design: .monospaced)).foregroundStyle(Palette.textTertiary)
                            Text(set.handle).font(.apogeeCode.weight(.semibold)).textSelection(.enabled)
                            Text(set.state).font(.apogeeSmall).foregroundStyle(Palette.textSecondary)
                        }
                        Spacer()
                        Button { store.close() } label: { Image(systemName: "xmark") }.buttonStyle(.plain)
                    }
                    actions(set)
                    if let advice = set.advice { Text(advice).font(.apogeeSmall).foregroundStyle(Palette.textSecondary).fixedSize(horizontal: false, vertical: true) }
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
                            Button("Delete all of it…") { store.askToDeleteModel(set.model) }
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

    @ViewBuilder private func actions(_ set: StoredSet) -> some View {
        HStack(spacing: Spacing.s) {
            if set.isRunnable && set.backend == nil && !set.consumed {
                Button("Register…") { Task { await store.askToRegister(set) } }
            }
            if let backend = set.backend {
                Button("Use in a new session") { useInSession(backend) }
                Button("Remove the backend…") { store.askToRemoveBackend(backend) }
            }
            if set.format == "pipeline" {
                Button("New clip with it") { shell.go(.video) }
                Button("Make it the default") { Task { await store.makeDefaultVideo(set) } }
            }
            if set.format == "voice" { Button("Open Voice") { shell.go(.voice) } }
        }
        HStack(spacing: Spacing.s) {
            if let path = store.path {
                Button("Show in Finder") { NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: path)]) }
            }
            Button("Delete…") { store.askToDelete(set) }
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
                Text("A backend sessions, suites and roles can use, running these weights on \(form.type == "mlx" ? "MLX" : "llama.cpp").")
                    .font(.apogeeSmall).foregroundStyle(Palette.textSecondary)
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
