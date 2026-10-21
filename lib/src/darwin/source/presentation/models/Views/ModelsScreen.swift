import AppKit
import BusinessReads
import PresentationShell
import PresentationStyles
import SwiftUI

/// The Models place: the backends and a backend's details, the stored
/// weights apart (2h), the jobs, and the roles.
public struct ModelsScreen<Downloads: View>: View {
    @Bindable private var model: ModelsViewModel
    @Bindable private var store: StoreViewModel
    private let shell: ShellViewModel
    private let downloads: Downloads

    public init(model: ModelsViewModel, store: StoreViewModel, shell: ShellViewModel, @ViewBuilder downloads: () -> Downloads) {
        self.model = model
        self.store = store
        self.shell = shell
        self.downloads = downloads()
    }

    public var body: some View {
        VStack(spacing: 0) {
            ScreenHeader(title: "Models") {
                Picker("Show", selection: $model.tab) {
                    ForEach(ModelsViewModel.Tab.allCases) { tab in
                        Text((tab == .store ? store.sets.count : model.count(tab)).map { "\(tab.title) \($0)" } ?? tab.title).tag(tab)
                    }
                }
                .pickerStyle(.segmented)
                .labelsHidden()
                .fixedSize()
                if let working = model.working ?? store.working {
                    ProgressView().controlSize(.small)
                    Text(working).font(.apogeeMeta).foregroundStyle(Palette.textSecondary)
                }
            }
            Divider()
            switch model.tab {
            case .roles:
                RolesView(model: model)
            case .jobs:
                downloads
            case .store:
                StoreView(store: store, shell: shell) { backend in model.useInSession?(backend) }
            case .backends:
                HStack(spacing: 0) {
                    ModelTable(model: model)
                    if model.selectedRow != nil && shell.layout.sidePanes {
                        Divider()
                        ModelDetails(model: model) { backend in
                            guard let handle = store.handle(forBackend: backend) else { return }
                            model.tab = .store
                            Task { await store.select(handle) }
                        }
                        .frame(width: 380)
                    }
                }
            }
        }
        .confirmationDialog(
            model.pendingDelete.map { "Delete \($0.row.name)?" } ?? "",
            isPresented: Binding(get: { model.pendingDelete != nil }, set: { if !$0 { model.pendingDelete = nil } }),
            titleVisibility: .visible
        ) {
            Button("Delete its files", role: .destructive) { Task { await model.confirmDelete(andBackends: false) } }
            Button("Delete its files and the backends that use them", role: .destructive) {
                Task { await model.confirmDelete(andBackends: true) }
            }
            Button("Cancel", role: .cancel) { model.pendingDelete = nil }
        } message: {
            Text("apogee models delete \(model.pendingDelete?.row.name ?? "") -- its files are removed from the store, and this can't be undone.")
        }
        .confirmationDialog(
            "Remove the backend \(model.removing ?? "")?",
            isPresented: Binding(get: { model.removing != nil }, set: { if !$0 { model.removing = nil } }),
            titleVisibility: .visible
        ) {
            Button("Remove the backend", role: .destructive) { Task { await model.confirmRemove() } }
            Button("Cancel", role: .cancel) { model.removing = nil }
        } message: {
            Text("apogee config delete-backend \(model.removing ?? "") -- its config entry goes; the weights stay under Stored weights, ready to register again.")
        }
        .sheet(item: $model.renaming) { row in
            RenameModelSheet(row: row) { name in Task { await model.rename(row, to: name) } }
        }
        .alert("That didn't work", isPresented: Binding(get: { model.failure != nil }, set: { if !$0 { model.dismissFailure() } })) {
            Button("OK") { model.dismissFailure() }
        } message: {
            Text(model.failure ?? "")
        }
    }
}

/// The library's table: the CLI's groups, a row per model.
struct ModelTable: View {
    @Bindable var model: ModelsViewModel

    var body: some View {
        ScrollView {
            LazyVStack(alignment: .leading, spacing: 0) {
                HStack(spacing: Spacing.m) {
                    Text("NAME").frame(maxWidth: .infinity, alignment: .leading)
                    Text("FORMAT").frame(width: 110, alignment: .leading)
                    Text("ON DISK").frame(width: 80, alignment: .trailing)
                    Text("STATUS").frame(width: 130, alignment: .leading)
                }
                .font(.system(size: TypeScale.label, weight: .medium, design: .monospaced))
                .foregroundStyle(Palette.textTertiary)
                .padding(.horizontal, Spacing.l)
                .padding(.vertical, Spacing.s)
                ForEach(model.groups) { group in
                    Text(group.title.uppercased())
                        .font(.system(size: TypeScale.label, weight: .medium, design: .monospaced))
                        .foregroundStyle(Palette.textTertiary)
                        .padding(.horizontal, Spacing.l)
                        .padding(.top, Spacing.m)
                        .padding(.bottom, Spacing.xs)
                    ForEach(group.rows) { row in rowView(row) }
                }
                if let empty = model.emptyText {
                    Text(empty)
                        .font(.apogeeBody)
                        .foregroundStyle(Palette.textSecondary)
                        .padding(Spacing.l)
                }
                Text("On disk is what a stored pipeline or voice model holds; a model's own size, window and memory are in its details, from its files.")
                    .font(.apogeeMeta)
                    .foregroundStyle(Palette.textTertiary)
                    .padding(Spacing.l)
            }
        }
        .frame(maxWidth: .infinity)
    }

    private func rowView(_ row: ModelRow) -> some View {
        let current = model.selected == row.name
        return Button {
            Task { await model.select(row) }
        } label: {
            HStack(spacing: Spacing.m) {
                HStack(spacing: Spacing.s) {
                    KindMark(kind: row.kind)
                    Text(row.name).foregroundStyle(Palette.textPrimary).lineLimit(1).truncationMode(.middle)
                    if !row.roles.isEmpty {
                        Text(row.roles).font(.apogeeMeta.monospaced()).foregroundStyle(Palette.accent).lineLimit(1)
                    }
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                Text([row.format, row.quant].compactMap { $0 }.joined(separator: " · "))
                    .font(.apogeeMeta.monospaced())
                    .foregroundStyle(Palette.textSecondary)
                    .frame(width: 110, alignment: .leading)
                Text(row.bytes.map(ModelsViewModel.size) ?? "")
                    .font(.apogeeMeta.monospacedDigit())
                    .foregroundStyle(Palette.textSecondary)
                    .frame(width: 80, alignment: .trailing)
                Text(ModelsViewModel.status(row))
                    .font(.apogeeSmall)
                    .foregroundStyle(row.state == "cannot load" ? Palette.error : Palette.textSecondary)
                    .lineLimit(1)
                    .frame(width: 130, alignment: .leading)
            }
            .font(.apogeeBody)
            .padding(.horizontal, Spacing.l)
            .frame(height: 34)
            .background(current ? Palette.selection : .clear)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .help(row.note ?? row.model)
    }
}

/// A model's details: its record, the typed facts where there are some, its
/// lineage, and what can be done with it.
struct ModelDetails: View {
    @Bindable var model: ModelsViewModel
    /// Opens the backend's set under Stored weights.
    var showWeights: (String) -> Void = { _ in }

    var body: some View {
        ScrollView {
            if let row = model.selectedRow {
                VStack(alignment: .leading, spacing: Spacing.l) {
                    HStack(alignment: .firstTextBaseline) {
                        VStack(alignment: .leading, spacing: Spacing.xs) {
                            Text(row.name).font(.apogeeHeading).foregroundStyle(Palette.textPrimary).textSelection(.enabled)
                            Text((row.isStored ? [ModelsViewModel.status(row), row.format ?? ""] : [row.backendType, row.state]).filter { !$0.isEmpty }.joined(separator: " · "))
                                .font(.apogeeMeta.monospaced())
                                .foregroundStyle(Palette.textTertiary)
                        }
                        Spacer()
                        Button {
                            model.close()
                        } label: {
                            Image(systemName: "xmark")
                        }
                        .buttonStyle(.plain)
                        .accessibilityLabel("Close details")
                    }
                    actions(row)
                    if let note = row.note {
                        Text(note).font(.apogeeSmall).foregroundStyle(Palette.warning)
                    }
                    if let failure = model.infoFailure {
                        Text(failure).font(.apogeeSmall).foregroundStyle(Palette.error)
                    }
                    if !model.typed.isEmpty {
                        section("Facts from its files", model.typed)
                    }
                    if !model.facts.isEmpty {
                        section("Record", model.facts)
                    }
                    if !model.lineage.isEmpty {
                        VStack(alignment: .leading, spacing: Spacing.s) {
                            label("Lineage")
                            ForEach(Array(model.lineage.enumerated()), id: \.offset) { _, step in
                                VStack(alignment: .leading, spacing: 2) {
                                    Text(step.kind + (step.from.map { " from \($0)" } ?? ""))
                                        .font(.apogeeCode)
                                        .foregroundStyle(step.missing ? Palette.textTertiary : Palette.textPrimary)
                                    if let detail = step.detail {
                                        Text(detail).font(.apogeeMeta).foregroundStyle(Palette.textSecondary)
                                    }
                                    if step.inferred || step.missing {
                                        Text(step.missing ? "no longer in the store" : "inferred, not recorded")
                                            .font(.apogeeMeta)
                                            .foregroundStyle(Palette.textTertiary)
                                    }
                                }
                            }
                        }
                    }
                }
                .padding(Spacing.l)
            }
        }
        .background(Palette.inspector)
    }

    private func actions(_ row: ModelRow) -> some View {
        HStack(spacing: Spacing.s) {
            if !row.isStored && (row.roles.contains("chat") || row.section == "cloud" || row.group == "gguf" || row.group == "mlx") {
                Button("Use in a new session") { model.useInSession?(row.backend) }
            }
            if row.group == "gguf" || row.group == "mlx" {
                Button("Show its weights") { showWeights(row.backend) }
            }
            Menu("More") {
                Button("Assign a role…") { model.tab = .roles }
                if row.section == "local" {
                    Button("Rename…") { model.renaming = row }
                    Button("Check its files") { model.repair(row) }
                }
                if let path = model.path {
                    Button("Show in Finder") {
                        NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: path)])
                    }
                }
                if row.section == "local" {
                    Divider()
                    Button("Remove the backend…") { model.askToRemove(row) }
                    Button("Delete with its weights…", role: .destructive) { model.askToDelete(row) }
                }
            }
            .fixedSize()
        }
        .disabled(model.working != nil)
    }

    private func section(_ title: String, _ fields: [Field]) -> some View {
        VStack(alignment: .leading, spacing: Spacing.s) {
            label(title)
            Grid(alignment: .leadingFirstTextBaseline, horizontalSpacing: Spacing.m, verticalSpacing: Spacing.xs) {
                ForEach(Array(fields.enumerated()), id: \.offset) { _, field in
                    GridRow {
                        Text(field.label).foregroundStyle(Palette.textTertiary)
                        Text(field.value)
                            .foregroundStyle(Palette.textPrimary)
                            .textSelection(.enabled)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                }
            }
            .font(.apogeeSmall)
        }
    }

    private func label(_ text: String) -> some View {
        Text(text.uppercased())
            .font(.system(size: TypeScale.label, weight: .medium, design: .monospaced))
            .tracking(0.8)
            .foregroundStyle(Palette.textTertiary)
    }
}

/// The roles: which model does which job, each moved through its verb.
struct RolesView: View {
    @Bindable var model: ModelsViewModel

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: Spacing.l) {
                Text("Which model does which job. Helpers run beside the chat model; an unset role falls back as shown.")
                    .font(.apogeeBody)
                    .foregroundStyle(Palette.textSecondary)
                if let failure = model.rolesFailure {
                    Text(failure).font(.apogeeSmall).foregroundStyle(Palette.error)
                }
                ForEach(model.roles) { role in
                    HStack(alignment: .top, spacing: Spacing.l) {
                        VStack(alignment: .leading, spacing: 2) {
                            Text(role.role.prefix(1).uppercased() + role.role.dropFirst())
                                .font(.apogeeTitle)
                                .foregroundStyle(Palette.textPrimary)
                            Text((role.verb + ["<backend>"]).joined(separator: " "))
                                .font(.apogeeMeta.monospaced())
                                .foregroundStyle(Palette.textTertiary)
                        }
                        .frame(width: 220, alignment: .leading)
                        VStack(alignment: .leading, spacing: 2) {
                            Text(ModelsViewModel.about(role.role)).font(.apogeeSmall).foregroundStyle(Palette.textSecondary)
                            Text(role.said).font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary)
                        }
                        .frame(maxWidth: .infinity, alignment: .leading)
                        Picker(
                            "Model for \(role.role)",
                            selection: Binding(
                                get: { role.isSet ? (role.backend ?? "") : "" },
                                set: { backend in
                                    if !backend.isEmpty { Task { await model.assign(role, to: backend) } }
                                })
                        ) {
                            Text("Not set").tag("")
                            ForEach(model.backends, id: \.self) { Text($0).tag($0) }
                        }
                        .labelsHidden()
                        .frame(width: 220)
                        .disabled(model.working != nil)
                    }
                    Divider()
                }
            }
            .padding(Spacing.xl)
            .frame(maxWidth: Layout.mediaContentMax)
            .frame(maxWidth: .infinity)
        }
    }
}

/// A model's kind, as the design marks it.
struct KindMark: View {
    let kind: ModelKind?

    var body: some View {
        Image(systemName: kind == .api ? "cloud" : kind == .subscription ? "arrow.triangle.2.circlepath" : "cpu")
            .font(.system(size: 11))
            .foregroundStyle(kind == .api ? Palette.api : kind == .subscription ? Palette.subscription : kind == .local ? Palette.local : Palette.textTertiary)
            .frame(width: 14)
            .accessibilityLabel(kind == .api ? "cloud API, billed" : kind == .subscription ? "subscription" : kind == .local ? "on this machine" : "model")
    }
}

/// A new name for a stored model.
struct RenameModelSheet: View {
    let row: ModelRow
    let rename: (String) -> Void
    @State private var name: String
    @Environment(\.dismiss) private var dismiss

    init(row: ModelRow, rename: @escaping (String) -> Void) {
        self.row = row
        self.rename = rename
        _name = State(initialValue: row.renameName)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.m) {
            Text("Rename model").font(.apogeeHeading)
            Text("Every quantization's backend, and the stored directory they run from, take the new name.")
                .font(.apogeeSmall)
                .foregroundStyle(Palette.textSecondary)
            TextField("Name", text: $name).textFieldStyle(.roundedBorder)
            Text("apogee models rename \(row.renameName) …").font(.apogeeCode).foregroundStyle(Palette.textTertiary)
            HStack {
                Spacer()
                Button("Cancel", role: .cancel) { dismiss() }.keyboardShortcut(.cancelAction)
                Button("Rename") {
                    rename(name)
                    dismiss()
                }
                .keyboardShortcut(.defaultAction)
                .disabled(name.trimmingCharacters(in: .whitespaces).isEmpty)
            }
        }
        .padding(Spacing.xl)
        .frame(width: 460)
    }
}
