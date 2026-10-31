import BusinessReads
import PresentationShell
import PresentationStyles
import SwiftUI

/// The Settings window: its panes beside their list.
public struct SettingsView: View {
    @Bindable private var model: SettingsViewModel

    public init(model: SettingsViewModel) {
        self.model = model
    }

    public var body: some View {
        HStack(spacing: 0) {
            VStack(alignment: .leading, spacing: 2) {
                ForEach(SettingsViewModel.Pane.allCases) { pane in
                    Button {
                        model.pane = pane
                    } label: {
                        Label(pane.title, systemImage: pane.symbol)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .padding(.horizontal, Spacing.m)
                            .frame(height: 30)
                            .background(model.pane == pane ? Palette.selection : .clear, in: RoundedRectangle(cornerRadius: Radius.row))
                            .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                }
                Spacer()
                if let working = model.working {
                    Text(working).font(.apogeeMeta).foregroundStyle(Palette.textTertiary).padding(Spacing.s)
                }
            }
            .padding(Spacing.s)
            .frame(width: 220)
            .background(Palette.sidebar)
            Divider()
            ScrollView {
                VStack(alignment: .leading, spacing: Spacing.xl) {
                    Text(model.pane.title).font(.apogeeHeading)
                    if let failure = model.failure {
                        Text(failure).font(.apogeeSmall).foregroundStyle(Palette.error).textSelection(.enabled)
                    }
                    switch model.pane {
                    case .general: GeneralPane(model: model)
                    case .appearance: AppearancePane(theme: model.theme)
                    case .backends: BackendsPane(model: model)
                    case .media: MediaPane(model: model)
                    case .permissions: PermissionsPane(model: model)
                    }
                }
                .padding(Spacing.xl)
                .frame(maxWidth: .infinity, alignment: .leading)
            }
            .background(Palette.window)
        }
        .frame(minWidth: 860, minHeight: 600)
        .task { await model.refresh() }
        .task { await model.readChildPath() }
    }
}

/// A labelled row, the design's two columns.
struct SettingRow<Content: View>: View {
    let label: String
    @ViewBuilder let content: Content

    var body: some View {
        HStack(alignment: .firstTextBaseline, spacing: Spacing.l) {
            Text(label).font(.apogeeTitle).frame(width: 200, alignment: .leading)
            VStack(alignment: .leading, spacing: Spacing.xs) { content }
                .frame(maxWidth: .infinity, alignment: .leading)
        }
    }
}

struct GeneralPane: View {
    let model: SettingsViewModel
    @State private var locating = false

    var body: some View {
        SettingRow(label: "Command-line tool") {
            Text(model.binary ?? "not found").font(.apogeeCode).textSelection(.enabled)
            if let version = model.version {
                Text("apogee \(version)").font(.apogeeMeta).foregroundStyle(Palette.textSecondary)
            }
            if let skipped = model.skippedOverrideLine {
                Text(skipped)
                    .font(.apogeeMeta)
                    .foregroundStyle(Palette.warning)
                    .textSelection(.enabled)
                    .fixedSize(horizontal: false, vertical: true)
            }
            Text("The app and the command line share models, sessions, keys and settings.")
                .font(.apogeeMeta)
                .foregroundStyle(Palette.textTertiary)
            HStack {
                Button("Use another apogee…") { locating = true }
                if model.binaryOverride != nil {
                    Button("Use the installed one") { model.setBinaryOverride(nil) }
                }
            }
            if let override = model.pendingOverride {
                Text("From the next launch: \(override)").font(.apogeeMeta).foregroundStyle(Palette.textSecondary)
            }
        }
        .fileImporter(isPresented: $locating, allowedContentTypes: [.unixExecutable, .item]) { result in
            if case .success(let url) = result { model.setBinaryOverride(url) }
        }
        SettingRow(label: "Sessions' PATH") {
            if let origin = model.childPathOrigin {
                Text(model.childPathDirectories.joined(separator: "\n")).font(.apogeeCode).textSelection(.enabled)
                Text(origin)
                    .font(.apogeeMeta)
                    .foregroundStyle(model.childPathFromShell ? Palette.textSecondary : Palette.warning)
                    .fixedSize(horizontal: false, vertical: true)
            } else {
                Text("Asking your login shell…").font(.apogeeSmall).foregroundStyle(Palette.textTertiary)
            }
            Text("Where the app's sessions, and every apogee command it runs, look for the tools their backends use. Read from your login shell once, when the app opens.")
                .font(.apogeeMeta)
                .foregroundStyle(Palette.textTertiary)
                .fixedSize(horizontal: false, vertical: true)
        }
        SettingRow(label: "Keyboard") {
            Grid(alignment: .leading, horizontalSpacing: Spacing.l, verticalSpacing: Spacing.xs) {
                ForEach(Array(KeyMap.reference.enumerated()), id: \.offset) { _, key in
                    GridRow {
                        Text(key.label).font(.apogeeSmall)
                        Text(key.keys).font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textSecondary)
                    }
                }
            }
        }
    }
}

struct AppearancePane: View {
    @Bindable var theme: ThemeSettings

    var body: some View {
        SettingRow(label: "Theme") {
            Picker("Theme", selection: $theme.choice) {
                ForEach(ThemeSettings.Choice.allCases) { Text($0.title).tag($0) }
            }
            .pickerStyle(.radioGroup)
            .labelsHidden()
        }
        SettingRow(label: "Text size") {
            Picker("Text size", selection: $theme.size) {
                ForEach(ThemeSettings.Size.allCases) { Text($0.title).tag($0) }
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            .fixedSize()
            Text("Every size in the app scales with it.").font(.apogeeMeta).foregroundStyle(Palette.textTertiary)
        }
    }
}

struct BackendsPane: View {
    @Bindable var model: SettingsViewModel
    @State private var keyFor: String?

    var body: some View {
        SettingRow(label: "Backends") {
            ForEach(model.backends) { row in
                VStack(alignment: .leading, spacing: 2) {
                    HStack {
                        Text(row.backend).font(.apogeeBody)
                        Text(row.backendType).font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary)
                        Spacer()
                        Text(row.state).font(.apogeeMeta).foregroundStyle(Palette.textSecondary)
                    }
                    // The vendor's own tools this entry consented to (the
                    // CLI's 47a), shown where the backend is (GUI 4k).
                    if let tools = model.serverToolsLine(row) {
                        Text(tools)
                            .font(.apogeeMeta)
                            .foregroundStyle(Self.consentTint(model.serverTools[row.backend]))
                            .textSelection(.enabled)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                }
            }
            if model.backends.isEmpty {
                Text("None configured.").font(.apogeeSmall).foregroundStyle(Palette.textTertiary)
            }
        }
        SettingRow(label: "API keys") {
            ForEach(SettingsViewModel.keyProviders, id: \.self) { provider in
                HStack {
                    Text(SettingsViewModel.providerName(provider)).font(.apogeeBody)
                    Text(model.keyed.contains(provider) ? "key set · billed per use" : "not set")
                        .font(.apogeeMeta)
                        .foregroundStyle(model.keyed.contains(provider) ? Palette.api : Palette.textTertiary)
                    Spacer()
                    Button(model.keyed.contains(provider) ? "Replace key…" : "Add key…") { keyFor = provider }
                    if model.keyed.contains(provider) {
                        Button("Remove") { Task { await model.removeKey(for: provider) } }
                    }
                }
            }
            Text("A key goes to apogee on its standard input and is kept by apogee alone; the app holds no copy.")
                .font(.apogeeMeta)
                .foregroundStyle(Palette.textTertiary)
        }
        .sheet(item: Binding(get: { keyFor.map(ProviderItem.init) }, set: { keyFor = $0?.id })) { item in
            KeySheet(provider: item.id) { key in Task { await model.addKey(key, for: item.id) } }
        }
        SettingRow(label: "Providers on this machine") {
            ForEach(model.providers) { provider in
                HStack {
                    Text(provider.label).font(.apogeeBody)
                    Spacer()
                    Text(provider.tier + (provider.backend.isEmpty ? "" : " · " + provider.backend))
                        .font(.apogeeMeta)
                        .foregroundStyle(Palette.textSecondary)
                    if provider.registers {
                        Button("Register…") { model.registering = provider }
                            .disabled(model.working != nil)
                    }
                }
            }
            // What the last registration said beyond its one line -- the
            // default it set, the roster it fetched (the CLI's M33) -- every
            // line as the CLI said it, until dismissed.
            if let said = model.registered {
                VStack(alignment: .leading, spacing: Spacing.xs) {
                    HStack(alignment: .firstTextBaseline) {
                        Text(said.title).font(.apogeeSmall.weight(.semibold)).foregroundStyle(Palette.textPrimary)
                        Spacer()
                        Button("Dismiss") { model.dismissRegistered() }
                    }
                    Text(said.lines.joined(separator: "\n"))
                        .font(.apogeeCode)
                        .foregroundStyle(Palette.textSecondary)
                        .textSelection(.enabled)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
        }
        .sheet(item: $model.registering) { provider in
            RegisterProviderSheet(model: model, provider: provider) { chosen in
                Task { await model.register(provider, model: chosen) }
            }
        }
        SettingRow(label: "Health") {
            if let summary = model.checkSummary {
                Text(summary).font(.apogeeBody).foregroundStyle(model.checks.contains { $0.status == "fail" } ? Palette.error : Palette.textPrimary)
            }
            // What fails and warns, then each optional row skipped with what
            // brings it (GUI 4f): the captions' ffmpeg-full among them.
            ForEach(model.healthRows) { row in
                VStack(alignment: .leading, spacing: 2) {
                    Text((row.status == "skipped" ? "Not set up · " : "") + "\(row.section) · \(row.name): \(row.detail)")
                        .font(.apogeeSmall)
                        .foregroundStyle(Self.tint(row.status))
                        .fixedSize(horizontal: false, vertical: true)
                    if let remedy = row.remedy {
                        Text(remedy).font(.apogeeCode).foregroundStyle(Palette.textSecondary).textSelection(.enabled)
                    }
                }
            }
            VStack(alignment: .leading, spacing: Spacing.xs) {
                Button("Fix what can be fixed (check --fix)") { model.fix(["check", "--fix"]) }
                Button("Install runtimes and register providers (bootstrap)") { model.fix(["bootstrap"]) }
            }
            .disabled(model.fixing != nil)
            if let fixing = model.fixing {
                Text(fixing.busy ?? fixing.notices.last ?? "working").font(.apogeeMeta).foregroundStyle(Palette.textSecondary)
            }
        }
    }
}

extension BackendsPane {
    /// A backend's consent line's colour: the billed colour where a vendor
    /// runs tools under the key, the warning's where it can't be read.
    static func consentTint(_ consent: SettingsViewModel.ServerToolsConsent?) -> Color {
        switch consent {
        case .consented(let tools)? where !tools.isEmpty: Palette.api
        case .unreadable?: Palette.warning
        default: Palette.textTertiary
        }
    }

    /// A Health row's colour: a failure's, a warning's, or -- an optional
    /// row skipped -- the secondary text's.
    static func tint(_ status: String) -> Color {
        switch status {
        case "fail": Palette.error
        case "warn": Palette.warning
        default: Palette.textSecondary
        }
    }
}

struct ProviderItem: Identifiable {
    let id: String
}

/// One scanned provider registered as a backend, asked in the TUI's words,
/// the command shown as typed: `providers register` -- the TUI's `r` itself --
/// where the binary has it, else the entry its `r` writes through `config
/// add-backend` (GUI M16) -- and, for a type that runs only with one, its
/// model.
struct RegisterProviderSheet: View {
    let model: SettingsViewModel
    let provider: SettingsViewModel.Provider
    let register: (String) -> Void
    @State private var modelName = ""
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.m) {
            Text("Found \(provider.provider) -- register it as a backend?").font(.apogeeHeading)
            Text("\(provider.label) · \(provider.tier)").font(.apogeeSmall).foregroundStyle(Palette.textSecondary)
            if provider.needsModel {
                TextField(
                    model.registersOne ? "The model it runs, or none for apogee to choose" : "The model it runs, as the provider names it",
                    text: $modelName
                )
                .textFieldStyle(.roundedBorder)
            }
            Text("apogee " + model.registration(for: provider, model: modelName).joined(separator: " "))
                .font(.apogeeCode)
                .foregroundStyle(Palette.textTertiary)
                .textSelection(.enabled)
            HStack {
                Spacer()
                Button("Cancel", role: .cancel) { dismiss() }.keyboardShortcut(.cancelAction)
                Button("Register") {
                    register(modelName)
                    dismiss()
                }
                .keyboardShortcut(.defaultAction)
                .disabled(!model.canRegister(provider, model: modelName))
            }
        }
        .padding(Spacing.xl)
        .frame(width: 520)
    }
}

/// The key, typed once: held by this sheet only until it is sent.
struct KeySheet: View {
    let provider: String
    let save: (String) -> Void
    @State private var key = ""
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.m) {
            Text("\(SettingsViewModel.providerName(provider)) API key").font(.apogeeHeading)
            SecureField("Key", text: $key).textFieldStyle(.roundedBorder)
            Text("apogee auth add \(provider) --stdin -- the key on its input, never on a command line.")
                .font(.apogeeCode)
                .foregroundStyle(Palette.textTertiary)
            HStack {
                Spacer()
                Button("Cancel", role: .cancel) {
                    key = ""
                    dismiss()
                }
                .keyboardShortcut(.cancelAction)
                Button("Save") {
                    save(key)
                    key = ""
                    dismiss()
                }
                .keyboardShortcut(.defaultAction)
                .disabled(key.trimmingCharacters(in: .whitespaces).isEmpty)
            }
        }
        .padding(Spacing.xl)
        .frame(width: 460)
    }
}

struct MediaPane: View {
    let model: SettingsViewModel

    var body: some View {
        SettingRow(label: "Default video pipeline") {
            Picker("Pipeline", selection: Binding(get: { "" }, set: { choice in
                Task { await model.setDefaultVideo(choice == "off" ? nil : choice) }
            })) {
                Text("Choose…").tag("")
                ForEach(model.pipelines, id: \.self) { Text($0).tag($0) }
                Text("None").tag("off")
            }
            .labelsHidden()
            .fixedSize()
            Text("What video generate runs when it names no pipeline: config set-default-video.")
                .font(.apogeeMeta)
                .foregroundStyle(Palette.textTertiary)
        }
        SettingRow(label: "Where media goes") {
            Text("Clips and compositions: ~/Movies/Apogee. Speeches: ~/Music/Apogee. Nothing is ever written over; a taken name gets a number. Folders added in Video and Voice are read too.")
                .font(.apogeeSmall)
                .foregroundStyle(Palette.textSecondary)
                .fixedSize(horizontal: false, vertical: true)
        }
    }
}

struct PermissionsPane: View {
    @Bindable var model: SettingsViewModel
    @State private var host = ""
    @State private var webSearch = ""

    var body: some View {
        SettingRow(label: "Tools that change things") {
            ForEach(model.permissions, id: \.tool) { entry in
                HStack {
                    Text(entry.tool).font(.apogeeCode)
                    Spacer()
                    Picker(entry.tool, selection: Binding(get: { entry.level }, set: { level in
                        Task { await model.setPermission(entry.tool, level) }
                    })) {
                        ForEach(SettingsViewModel.levels, id: \.self) { Text($0).tag($0) }
                    }
                    .pickerStyle(.segmented)
                    .labelsHidden()
                    .fixedSize()
                }
            }
        }
        SettingRow(label: "Websites fetch_url reaches without asking") {
            if let hosts = model.allowedHosts {
                // The list as the CLI reads it (its M33), read back after
                // each change.
                ForEach(hosts, id: \.self) { allowed in
                    HStack {
                        Text(allowed).font(.apogeeCode)
                        Spacer()
                        Button("Remove") { Task { await model.forbidHost(allowed) } }
                            .disabled(model.working != nil)
                    }
                }
                if hosts.isEmpty {
                    Text("None yet.").font(.apogeeSmall).foregroundStyle(Palette.textTertiary)
                }
                HStack {
                    TextField("docs.python.org", text: $host).textFieldStyle(.roundedBorder)
                    Button("Allow") { Task { await model.allowHost(host) } }
                        .disabled(model.working != nil)
                }
                Text("By exact host name.")
                    .font(.apogeeMeta)
                    .foregroundStyle(Palette.textTertiary)
            } else {
                HStack {
                    TextField("docs.python.org", text: $host).textFieldStyle(.roundedBorder)
                    Button("Allow") { Task { await model.allowHost(host) } }
                    Button("Remove") { Task { await model.forbidHost(host) } }
                }
                Text(
                    model.allowedHostsUnread.map { "By exact host name. This apogee can't read the list -- \($0) -- so each change shows its own words." }
                        ?? "By exact host name. This apogee has no read of the list, so each change shows its own words."
                )
                .font(.apogeeMeta)
                .foregroundStyle(Palette.textTertiary)
                .fixedSize(horizontal: false, vertical: true)
            }
        }
        SettingRow(label: "Web search address") {
            Text(model.webSearch ?? "not set").font(.apogeeCode)
            if let hint = model.configSetHint {
                Text(hint)
                    .font(.apogeeMeta)
                    .foregroundStyle(Palette.textTertiary)
                    .fixedSize(horizontal: false, vertical: true)
            } else {
                HStack {
                    TextField("http://localhost:8888", text: $webSearch).textFieldStyle(.roundedBorder)
                    Button("Set") { Task { await model.setConfig("tools.search.url", webSearch) } }
                }
            }
        }
        SettingRow(label: "Recall earlier sessions") {
            Text(model.recall ?? "on (the default)").font(.apogeeCode)
            if let hint = model.configSetHint {
                Text(hint)
                    .font(.apogeeMeta)
                    .foregroundStyle(Palette.textTertiary)
                    .fixedSize(horizontal: false, vertical: true)
            } else {
                if let holds = model.recallHolds {
                    Text(holds).font(.apogeeMeta).foregroundStyle(Palette.textSecondary).textSelection(.enabled)
                }
                if let listing = model.recallListing, !listing.summaries.isEmpty {
                    RecalledList(listing: listing)
                }
                HStack {
                    Button("On") { Task { await model.setConfig("memory.recall", "true") } }
                    Button("Off") { Task { await model.setConfig("memory.recall", "false") } }
                    Button("Forget every recalled summary…") { Task { await model.askToForget() } }
                        .disabled(!model.canForget)
                }
            }
        }
        .confirmationDialog("Forget what recall holds?", isPresented: $model.forgetting, titleVisibility: .visible) {
            Button("Forget every summary", role: .destructive) { Task { await model.forgetRecall() } }
            Button("Cancel", role: .cancel) { model.forgetting = false }
        } message: {
            Text(model.forgetMessage)
        }
    }
}

/// What recall holds, by name and date, as the binary lists it (GUI M15):
/// what Forget would erase, seen before it is asked.
struct RecalledList: View {
    let listing: RecallListing

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.xs) {
            ForEach(listing.summaries) { summary in
                HStack(alignment: .firstTextBaseline, spacing: Spacing.s) {
                    Text(summary.title).font(.apogeeSmall).foregroundStyle(Palette.textPrimary)
                    Text(summary.updated.map { $0.formatted(date: .abbreviated, time: .shortened) } ?? "undated")
                        .font(.apogeeMeta)
                        .foregroundStyle(Palette.textTertiary)
                }
                .help(summary.summary)
            }
        }
    }
}

#if DEBUG
#Preview("General, read") {
    let model = SettingsPreview.settings()
    SettingsView(model: model)
        .task { await SettingsPreview.read(model) }
}

#Preview("Backends & keys: a key set, a check failing") {
    let model = SettingsPreview.settings(.backends)
    SettingsView(model: model)
        .task { await SettingsPreview.read(model) }
}

#Preview("Apogee not installed") {
    let model = SettingsPreview.settings()
    SettingsView(model: model)
        .task { await model.connect(binary: nil, version: nil) }
}

#Preview("A labelled row") {
    SettingRow(label: "Command-line tool") {
        Text(previewBinary.path).font(.apogeeCode)
    }
    .padding(Spacing.xl)
    .frame(width: 640)
}

#Preview("General: the installed apogee") {
    let model = SettingsPreview.settings()
    VStack(alignment: .leading, spacing: Spacing.xl) { GeneralPane(model: model) }
        .padding(Spacing.xl)
        .frame(width: 680)
        .task { await SettingsPreview.read(model) }
}

#Preview("General: your apogee passed over, the installed one running") {
    let model = SettingsPreview.settings(override: SettingsPreview.skippedOverride.path)
    VStack(alignment: .leading, spacing: Spacing.xl) { GeneralPane(model: model) }
        .padding(Spacing.xl)
        .frame(width: 680)
        .task { await SettingsPreview.read(model, skippedOverride: SettingsPreview.skippedOverride) }
}

#Preview("General: not read yet") {
    VStack(alignment: .leading, spacing: Spacing.xl) { GeneralPane(model: SettingsPreview.settings()) }
        .padding(Spacing.xl)
        .frame(width: 680)
}

#Preview("General: the PATH from the login shell") {
    let model = SettingsPreview.settings(childPath: SettingsPreview.shellPath)
    VStack(alignment: .leading, spacing: Spacing.xl) { GeneralPane(model: model) }
        .padding(Spacing.xl)
        .frame(width: 680)
        .task { await SettingsPreview.read(model) }
}

#Preview("General: the PATH's fallback, the shell past its deadline") {
    let model = SettingsPreview.settings(childPath: SettingsPreview.fallbackPath)
    VStack(alignment: .leading, spacing: Spacing.xl) { GeneralPane(model: model) }
        .padding(Spacing.xl)
        .frame(width: 680)
        .task { await SettingsPreview.read(model) }
}

#Preview("Appearance: following the system") {
    VStack(alignment: .leading, spacing: Spacing.xl) { AppearancePane(theme: SettingsPreview.settings().theme) }
        .padding(Spacing.xl)
        .frame(width: 680)
}

#Preview("Appearance: high contrast, large") {
    let theme = SettingsPreview.settings().theme
    VStack(alignment: .leading, spacing: Spacing.xl) { AppearancePane(theme: theme) }
        .padding(Spacing.xl)
        .frame(width: 680)
        .task {
            theme.choice = .contrast
            theme.size = .large
        }
}

#Preview("Backends: a key set, a check failing") {
    let model = SettingsPreview.settings(.backends)
    VStack(alignment: .leading, spacing: Spacing.xl) { BackendsPane(model: model) }
        .padding(Spacing.xl)
        .frame(width: 680)
        .task { await SettingsPreview.read(model) }
}

#Preview("Backends: Health with skipped rows and their remedies, captions among them") {
    let model = SettingsPreview.settings(.backends, check: SettingsDocuments.checkCaptionsSkipped)
    ScrollView {
        VStack(alignment: .leading, spacing: Spacing.xl) { BackendsPane(model: model) }
            .padding(Spacing.xl)
    }
    .frame(width: 680, height: 900)
    .task { await SettingsPreview.read(model) }
}

#Preview("Backends: check --fix running") {
    let model = SettingsPreview.settings(.backends)
    VStack(alignment: .leading, spacing: Spacing.xl) { BackendsPane(model: model) }
        .padding(Spacing.xl)
        .frame(width: 680)
        .task {
            await SettingsPreview.read(model)
            model.fix(["check", "--fix"])
        }
}

#Preview("Backends: each vendor backend's server_tools, a provider's Register") {
    let model = SettingsPreview.settings(.backends, runner: SettingsPreview.vendorRunner())
    ScrollView {
        VStack(alignment: .leading, spacing: Spacing.xl) { BackendsPane(model: model) }
            .padding(Spacing.xl)
    }
    .frame(width: 720, height: 900)
    .task { await SettingsPreview.read(model) }
}

#Preview("Backends: server_tools on an apogee before 55g, said in its words") {
    let model = SettingsPreview.settings(.backends, runner: SettingsPreview.vendorRunner(older: true))
    ScrollView {
        VStack(alignment: .leading, spacing: Spacing.xl) { BackendsPane(model: model) }
            .padding(Spacing.xl)
    }
    .frame(width: 720, height: 900)
    .task { await SettingsPreview.read(model) }
}

#Preview("Registering a scanned provider") {
    @Previewable @State var model = SettingsPreview.settings(.backends, runner: SettingsPreview.vendorRunner())
    Group {
        if let claude = model.providers.first(where: { $0.provider == "claude" }) {
            RegisterProviderSheet(model: model, provider: claude) { _ in }
        } else {
            ProgressView().padding(Spacing.xl)
        }
    }
    .task { await SettingsPreview.read(model) }
}

#Preview("Registering Ollama: the model it runs named") {
    @Previewable @State var model = SettingsPreview.settings(.backends, runner: SettingsPreview.vendorRunner())
    Group {
        if let ollama = model.providers.first(where: { $0.provider == "ollama" }) {
            RegisterProviderSheet(model: model, provider: ollama) { _ in }
        } else {
            ProgressView().padding(Spacing.xl)
        }
    }
    .task { await SettingsPreview.read(model) }
}

#Preview("Backends after Register on an apogee that registers one provider: its lines -- the default it set, the roster it fetched") {
    let model = SettingsPreview.settings(.backends, runner: SettingsPreview.registeringCodex(), vocabulary: "2026-10-26")
    ScrollView {
        VStack(alignment: .leading, spacing: Spacing.xl) { BackendsPane(model: model) }
            .padding(Spacing.xl)
    }
    .frame(width: 720, height: 900)
    .task { await SettingsPreview.register(model, "codex") }
}

#Preview("Registering a scanned provider on an apogee that registers one: providers register, the TUI's r") {
    @Previewable @State var model = SettingsPreview.settings(.backends, runner: SettingsPreview.m33Runner(), vocabulary: "2026-10-26")
    Group {
        if let codex = model.providers.first(where: { $0.provider == "codex" }) {
            RegisterProviderSheet(model: model, provider: codex) { _ in }
        } else {
            ProgressView().padding(Spacing.xl)
        }
    }
    .task { await SettingsPreview.read(model) }
}

#Preview("Registering Ollama on an apogee that registers one: no model named, apogee chooses") {
    @Previewable @State var model = SettingsPreview.settings(.backends, runner: SettingsPreview.m33Runner(), vocabulary: "2026-10-26")
    Group {
        if let ollama = model.providers.first(where: { $0.provider == "ollama" }) {
            RegisterProviderSheet(model: model, provider: ollama) { _ in }
        } else {
            ProgressView().padding(Spacing.xl)
        }
    }
    .task { await SettingsPreview.read(model) }
}

#Preview("Backends after Register for Ollama: the CLI says it needs a model, and how to add one") {
    let model = SettingsPreview.settings(.backends, runner: SettingsPreview.m33Runner(), vocabulary: "2026-10-26")
    ScrollView {
        VStack(alignment: .leading, spacing: Spacing.xl) { BackendsPane(model: model) }
            .padding(Spacing.xl)
    }
    .frame(width: 720, height: 900)
    .task { await SettingsPreview.register(model, "ollama") }
}

#Preview("An API key, typed once") {
    KeySheet(provider: "anthropic") { _ in }
}

#Preview("Video & voice: the store's pipeline") {
    let model = SettingsPreview.settings(.media)
    VStack(alignment: .leading, spacing: Spacing.xl) { MediaPane(model: model) }
        .padding(Spacing.xl)
        .frame(width: 680)
        .task { await SettingsPreview.read(model) }
}

#Preview("Permissions: Forget asked, with what recall holds") {
    let model = SettingsPreview.settings(.permissions, track52: true, known: true)
    VStack(alignment: .leading, spacing: Spacing.xl) { PermissionsPane(model: model) }
        .padding(Spacing.xl)
        .frame(width: 680, height: 520)
        .task {
            await SettingsPreview.read(model)
            await model.askToForget()
        }
}

#Preview("Permissions on the branch binary: recall's summaries listed, Forget naming each") {
    let model = SettingsPreview.settings(.permissions, track52: true, runner: SettingsPreview.listingRunner, vocabulary: "2026-10-26")
    VStack(alignment: .leading, spacing: Spacing.xl) { PermissionsPane(model: model) }
        .padding(Spacing.xl)
        .frame(width: 680, height: 560)
        .task {
            await SettingsPreview.read(model)
            await model.askToForget()
        }
}

#Preview("What recall holds, by name and date") {
    @Previewable @State var model = SettingsPreview.settings(
        .permissions, track52: true, runner: SettingsPreview.listingRunner, vocabulary: "2026-10-26")
    Group {
        if let listing = model.recallListing {
            RecalledList(listing: listing)
                .padding(Spacing.xl)
                .frame(width: 480)
        } else {
            ProgressView().padding(Spacing.xl)
        }
    }
    .task { await SettingsPreview.read(model) }
}

#Preview("Permissions: the allowed hosts, read through the CLI") {
    let model = SettingsPreview.settings(.permissions, track52: true, runner: SettingsPreview.m33Runner(), vocabulary: "2026-10-26")
    VStack(alignment: .leading, spacing: Spacing.xl) { PermissionsPane(model: model) }
        .padding(Spacing.xl)
        .frame(width: 680)
        .task { await SettingsPreview.read(model) }
}

#Preview("Permissions: no host allowed yet, as the CLI reads it") {
    let model = SettingsPreview.settings(
        .permissions, track52: true, runner: SettingsPreview.m33Runner(hosts: SettingsDocuments.allowedHostsEmpty), vocabulary: "2026-10-26")
    VStack(alignment: .leading, spacing: Spacing.xl) { PermissionsPane(model: model) }
        .padding(Spacing.xl)
        .frame(width: 680)
        .task { await SettingsPreview.read(model) }
}

#Preview("Permissions: the binary hasn't said what it can do") {
    let model = SettingsPreview.settings(.permissions, known: false)
    VStack(alignment: .leading, spacing: Spacing.xl) { PermissionsPane(model: model) }
        .padding(Spacing.xl)
        .frame(width: 680)
        .task { await SettingsPreview.read(model) }
}

#Preview("Permissions, before 52e: set in the config file") {
    let model = SettingsPreview.settings(.permissions)
    VStack(alignment: .leading, spacing: Spacing.xl) { PermissionsPane(model: model) }
        .padding(Spacing.xl)
        .frame(width: 680)
        .task { await SettingsPreview.read(model) }
}

#Preview("Permissions on a capable binary, whatever place was restored: Set, On/Off, Forget") {
    // The app asks the binary what it can do at launch (GUI 4k), so these
    // stand before any session has spoken.
    let model = SettingsPreview.settings(.permissions, track52: true)
    VStack(alignment: .leading, spacing: Spacing.xl) { PermissionsPane(model: model) }
        .padding(Spacing.xl)
        .frame(width: 680)
        .task { await SettingsPreview.read(model) }
}
#endif
