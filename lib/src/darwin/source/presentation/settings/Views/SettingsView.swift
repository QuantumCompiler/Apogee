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
            Text("The app and the command line share models, sessions, keys and settings.")
                .font(.apogeeMeta)
                .foregroundStyle(Palette.textTertiary)
            HStack {
                Button("Use another apogee…") { locating = true }
                if model.binaryOverride != nil {
                    Button("Use the installed one") { model.setBinaryOverride(nil) }
                }
            }
            if let override = model.binaryOverride {
                Text("From the next launch: \(override)").font(.apogeeMeta).foregroundStyle(Palette.textSecondary)
            }
        }
        .fileImporter(isPresented: $locating, allowedContentTypes: [.unixExecutable, .item]) { result in
            if case .success(let url) = result { model.setBinaryOverride(url) }
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
    let model: SettingsViewModel
    @State private var keyFor: String?

    var body: some View {
        SettingRow(label: "Backends") {
            ForEach(model.backends) { row in
                HStack {
                    Text(row.backend).font(.apogeeBody)
                    Text(row.backendType).font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary)
                    Spacer()
                    Text(row.state).font(.apogeeMeta).foregroundStyle(Palette.textSecondary)
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
                    Text(provider.tier + " · " + provider.backend).font(.apogeeMeta).foregroundStyle(Palette.textSecondary)
                }
            }
        }
        SettingRow(label: "Health") {
            if let summary = model.checkSummary {
                Text(summary).font(.apogeeBody).foregroundStyle(model.checks.contains { $0.status == "fail" } ? Palette.error : Palette.textPrimary)
            }
            ForEach(model.checks.filter { $0.status == "fail" || $0.status == "warn" }) { row in
                VStack(alignment: .leading, spacing: 2) {
                    Text("\(row.section) · \(row.name): \(row.detail)").font(.apogeeSmall)
                        .foregroundStyle(row.status == "fail" ? Palette.error : Palette.warning)
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

struct ProviderItem: Identifiable {
    let id: String
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
    let model: SettingsViewModel
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
            HStack {
                TextField("docs.python.org", text: $host).textFieldStyle(.roundedBorder)
                Button("Allow") { Task { await model.allowHost(host) } }
                Button("Remove") { Task { await model.forbidHost(host) } }
            }
            Text("By exact host name. This apogee has no read of the list, so each change shows its own words.")
                .font(.apogeeMeta)
                .foregroundStyle(Palette.textTertiary)
        }
        SettingRow(label: "Web search address") {
            Text(model.webSearch ?? "not set").font(.apogeeCode)
            if model.hasTrack52() {
                HStack {
                    TextField("http://localhost:8888", text: $webSearch).textFieldStyle(.roundedBorder)
                    Button("Set") { Task { await model.setConfig("tools.search.url", webSearch) } }
                }
            } else {
                Text("Read here, set in the config file: this apogee has no config set.")
                    .font(.apogeeMeta)
                    .foregroundStyle(Palette.textTertiary)
            }
        }
        SettingRow(label: "Recall earlier sessions") {
            Text(model.recall ?? "on (the default)").font(.apogeeCode)
            if model.hasTrack52() {
                HStack {
                    Button("On") { Task { await model.setConfig("memory.recall", "true") } }
                    Button("Off") { Task { await model.setConfig("memory.recall", "false") } }
                    Button("Forget every recalled summary") { Task { await model.forgetRecall() } }
                }
            } else {
                Text("Read here, set in the config file: this apogee has no config set.")
                    .font(.apogeeMeta)
                    .foregroundStyle(Palette.textTertiary)
            }
        }
    }
}
