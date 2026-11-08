import AppKit
import BusinessReads
import PresentationStyles
import SwiftUI

/// Settings › Xcode (GUI 5a): what `check` says of Xcode, Xcode's tools given
/// to Apogee's agents through the CLI's `xcode` preset, and the fields
/// Xcode's Add an Agent sheet takes, each with Copy -- all in the CLI's
/// words. The app writes nothing into Xcode: the person adds the agent in
/// Xcode's own sheet.
struct XcodePane: View {
    @Bindable var xcode: XcodeSettings

    var body: some View {
        SettingRow(label: "What check says of Xcode") {
            XcodeCheckRows(xcode: xcode)
        }
        SettingRow(label: "Let Apogee use Xcode's tools") {
            XcodeToolsRoute(xcode: xcode)
        }
        .confirmationDialog("Remove Xcode's tools from Apogee?", isPresented: $xcode.removing, titleVisibility: .visible) {
            Button("Remove", role: .destructive) { Task { await xcode.remove() } }
            Button("Cancel", role: .cancel) { xcode.removing = false }
        } message: {
            Text("apogee " + (xcode.removes ?? []).joined(separator: " ") + " -- its config entry goes; Xcode is left as it is.")
        }
        SettingRow(label: "Use Apogee as an agent in Xcode") {
            XcodeAgentFields(xcode: xcode)
        }
        .task(id: xcode.binaryPath) { await xcode.read() }
    }
}

/// Every row of `check`'s Xcode section, ok rows included, each with its
/// remedy -- or what the binary's `check` lacks.
struct XcodeCheckRows: View {
    let xcode: XcodeSettings

    var body: some View {
        switch xcode.section {
        case .notRead:
            Text("Not read yet.").font(.apogeeSmall).foregroundStyle(Palette.textTertiary)
        case .none:
            Text(xcode.sectionLacking ?? "").font(.apogeeSmall).foregroundStyle(Palette.textSecondary)
        case .rows(let rows):
            ForEach(rows) { row in
                VStack(alignment: .leading, spacing: 2) {
                    HStack(alignment: .firstTextBaseline, spacing: Spacing.s) {
                        Image(systemName: row.status == "ok" ? "checkmark.circle" : "minus.circle")
                            .foregroundStyle(row.status == "ok" ? Palette.accent : Palette.textTertiary)
                            .accessibilityLabel(row.status)
                        Text("\(row.name): \(row.detail)")
                            .font(.apogeeSmall)
                            .foregroundStyle(row.status == "ok" ? Palette.textPrimary : Palette.textSecondary)
                            .textSelection(.enabled)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                    if let remedy = row.remedy {
                        Text(remedy).font(.apogeeCode).foregroundStyle(Palette.textSecondary).textSelection(.enabled)
                    }
                }
            }
        }
    }
}

/// The `xcode` preset: registered or not, as `config get` reads it, with
/// Register or Remove -- and the preset's note, in the CLI's words, before
/// either.
struct XcodeToolsRoute: View {
    let xcode: XcodeSettings

    var body: some View {
        if let lacking = xcode.presetLacking {
            Text(lacking).font(.apogeeSmall).foregroundStyle(Palette.textSecondary).textSelection(.enabled)
                .fixedSize(horizontal: false, vertical: true)
        }
        if let preset = xcode.xcodePreset {
            Text(preset.description).font(.apogeeSmall).foregroundStyle(Palette.textSecondary)
                .fixedSize(horizontal: false, vertical: true)
            switch xcode.entry {
            case .registered(let command)?:
                Text("In Apogee's config as \(preset.serverName ?? preset.name): \(command)")
                    .font(.apogeeBody).textSelection(.enabled)
            case .notRegistered?:
                Text("Not in Apogee's config.").font(.apogeeBody).foregroundStyle(Palette.textSecondary)
            case .unreadable(let why)?:
                Text("Its config entry couldn't be read: \(why)").font(.apogeeSmall).foregroundStyle(Palette.warning)
                    .textSelection(.enabled).fixedSize(horizontal: false, vertical: true)
            case nil:
                EmptyView()
            }
            if let missing = xcode.presetMissing {
                Text("It can't run here: \(missing)").font(.apogeeSmall).foregroundStyle(Palette.warning)
                    .textSelection(.enabled).fixedSize(horizontal: false, vertical: true)
            }
            if let note = preset.note {
                Text(note).font(.apogeeMeta).foregroundStyle(Palette.textSecondary).textSelection(.enabled)
                    .fixedSize(horizontal: false, vertical: true)
            }
            if let register = xcode.registers {
                Text("Register writes Apogee's config and connects to nothing: Xcode is first reached when a session starts the server, and asks you then.")
                    .font(.apogeeMeta).foregroundStyle(Palette.textTertiary)
                    .fixedSize(horizontal: false, vertical: true)
                HStack {
                    Button("Register") { Task { await xcode.register() } }
                        .disabled(xcode.working != nil)
                    Text("apogee " + register.joined(separator: " ")).font(.apogeeCode).foregroundStyle(Palette.textTertiary)
                        .textSelection(.enabled)
                }
            }
            if xcode.removes != nil {
                Button("Remove…") { xcode.askToRemove() }
                    .disabled(xcode.working != nil)
            }
        }
        if let failure = xcode.failure {
            Text(failure).font(.apogeeSmall).foregroundStyle(Palette.error).textSelection(.enabled)
                .fixedSize(horizontal: false, vertical: true)
        }
        if let said = xcode.said {
            VStack(alignment: .leading, spacing: Spacing.xs) {
                HStack(alignment: .firstTextBaseline) {
                    Text(said.title).font(.apogeeSmall.weight(.semibold)).foregroundStyle(Palette.textPrimary)
                    Spacer()
                    Button("Dismiss") { xcode.dismissSaid() }
                }
                Text(said.lines.joined(separator: "\n"))
                    .font(.apogeeCode)
                    .foregroundStyle(Palette.textSecondary)
                    .textSelection(.enabled)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
    }
}

/// The fields `acp registration` printed, each with Copy, and where they go
/// in Xcode -- or why there are none.
struct XcodeAgentFields: View {
    let xcode: XcodeSettings

    var body: some View {
        if let lacking = xcode.registrationLacking {
            Text(lacking).font(.apogeeSmall).foregroundStyle(Palette.textSecondary).textSelection(.enabled)
                .fixedSize(horizontal: false, vertical: true)
        }
        if let fields = xcode.fields {
            if let heading = fields.heading {
                Text(heading).font(.apogeeSmall).foregroundStyle(Palette.textPrimary).textSelection(.enabled)
                    .fixedSize(horizontal: false, vertical: true)
            }
            Grid(alignment: .leadingFirstTextBaseline, horizontalSpacing: Spacing.m, verticalSpacing: Spacing.xs) {
                ForEach(Array(RegistrationField.fields(of: fields).enumerated()), id: \.offset) { _, field in
                    GridRow {
                        Text(field.label).font(.apogeeSmall).foregroundStyle(Palette.textSecondary)
                        Text(field.shown).font(.apogeeCode)
                            .foregroundStyle(field.copies ? Palette.textPrimary : Palette.textTertiary)
                            .textSelection(.enabled)
                            .fixedSize(horizontal: false, vertical: true)
                        if field.copies {
                            Button("Copy") { field.copy() }
                                .accessibilityLabel("Copy \(field.label)")
                        } else {
                            Color.clear.frame(width: 1, height: 1)
                        }
                    }
                }
            }
            if let approval = fields.approval {
                Text(approval).font(.apogeeMeta).foregroundStyle(Palette.textSecondary).textSelection(.enabled)
                    .fixedSize(horizontal: false, vertical: true)
            }
            Text("The app writes nothing into Xcode: add the agent in Xcode's own sheet.")
                .font(.apogeeMeta).foregroundStyle(Palette.textTertiary)
        } else if case .read = xcode.registration {
            Text("Not offered: check's Xcode rows above say why.").font(.apogeeSmall).foregroundStyle(Palette.textSecondary)
        }
    }
}

/// One line of Xcode's Add an Agent sheet, as `acp registration` printed it:
/// a field's value, or one entry of Arguments or Environment Variables. Copy
/// writes the pasteboard here, from the view -- never from the view-model --
/// as `MarkdownCode.copy(to:)` does.
struct RegistrationField: Equatable {
    /// The sheet's field, in Xcode's words.
    let label: String
    /// What goes in it, exactly as printed; `nil` for a field left empty.
    let value: String?

    /// What the row shows: the value, or the CLI's own "(leave empty)".
    var shown: String { value ?? "(leave empty)" }

    /// Whether it has anything to copy.
    var copies: Bool { value != nil }

    /// The sheet's lines in its order: Name, Executable, Interpreter, each
    /// argument, each environment variable as `NAME=value` -- the CLI's text
    /// form, one entry a line.
    static func fields(of registration: AgentRegistration) -> [RegistrationField] {
        [
            RegistrationField(label: "Name", value: registration.name),
            RegistrationField(label: "Executable", value: registration.executable),
            RegistrationField(label: "Interpreter", value: registration.interpreter),
        ]
            + registration.arguments.enumerated().map { RegistrationField(label: $0.offset == 0 ? "Arguments" : "", value: $0.element) }
            + registration.environment.enumerated().map {
                RegistrationField(label: $0.offset == 0 ? "Environment Variables" : "", value: $0.element.entry)
            }
    }

    /// Puts the value on `pasteboard` -- the person's, unless a test names
    /// its own.
    @MainActor
    func copy(to pasteboard: NSPasteboard = .general) {
        guard let value else { return }
        pasteboard.clearContents()
        pasteboard.setString(value, forType: .string)
    }
}

#if DEBUG
/// A pane over the Xcode pane's recordings, read as the window reads it.
@MainActor
private func xcodePreview(_ runner: any CommandRunning, then: @escaping @MainActor (XcodeSettings) async -> Void = { _ in }) -> some View {
    let model = SettingsPreview.settings(.xcode, runner: runner)
    return ScrollView {
        VStack(alignment: .leading, spacing: Spacing.xl) { XcodePane(xcode: model.xcode) }
            .padding(Spacing.xl)
    }
    .frame(width: 760, height: 820)
    .task {
        await SettingsPreview.read(model)
        await model.xcode.read()
        await then(model.xcode)
    }
}

#Preview("Xcode: Xcode found, its tools not yet given to Apogee") {
    xcodePreview(SettingsPreview.xcodeRunner())
}

#Preview("Xcode: the preset registered") {
    xcodePreview(SettingsPreview.xcodeRunner(registered: true))
}

#Preview("Xcode: after Register, the CLI's words") {
    xcodePreview(SettingsPreview.xcodeRegistering()) { await $0.register() }
}

#Preview("Xcode: Remove asked") {
    xcodePreview(SettingsPreview.xcodeRunner(registered: true)) { $0.askToRemove() }
}

#Preview("Xcode: Xcode absent") {
    xcodePreview(SettingsPreview.xcodeRunner(check: SettingsDocuments.xcodeCheckAbsent))
}

#Preview("Xcode: xcrun not on the PATH, the preset can't run") {
    xcodePreview(SettingsPreview.xcodeRunner(presets: SettingsDocuments.xcodeMCPPresetsNoXcrun))
}

#Preview("Xcode: an older apogee, before the CLI's 56d") {
    xcodePreview(SettingsPreview.xcodeOlderRunner)
}

#Preview("Xcode: not read yet") {
    ScrollView {
        VStack(alignment: .leading, spacing: Spacing.xl) { XcodePane(xcode: SettingsPreview.settings(.xcode).xcode) }
            .padding(Spacing.xl)
    }
    .frame(width: 760, height: 480)
}

#Preview("Xcode's check rows") {
    @Previewable @State var model = SettingsPreview.settings(.xcode, runner: SettingsPreview.xcodeRunner())
    XcodeCheckRows(xcode: model.xcode)
        .padding(Spacing.xl)
        .frame(width: 640)
        .task { await SettingsPreview.read(model) }
}

#Preview("Xcode's tools for Apogee: Register offered") {
    @Previewable @State var model = SettingsPreview.settings(.xcode, runner: SettingsPreview.xcodeRunner())
    VStack(alignment: .leading, spacing: Spacing.xs) { XcodeToolsRoute(xcode: model.xcode) }
        .padding(Spacing.xl)
        .frame(width: 640)
        .task {
            await SettingsPreview.read(model)
            await model.xcode.read()
        }
}

#Preview("Add an Agent's fields, each with Copy") {
    @Previewable @State var model = SettingsPreview.settings(.xcode, runner: SettingsPreview.xcodeRunner())
    VStack(alignment: .leading, spacing: Spacing.xs) { XcodeAgentFields(xcode: model.xcode) }
        .padding(Spacing.xl)
        .frame(width: 640)
        .task {
            await SettingsPreview.read(model)
            await model.xcode.read()
        }
}
#endif
