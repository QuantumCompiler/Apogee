import SwiftUI

/// The one-time install a media verb's refusal named, asked first (GUI 1m,
/// 1o; shared by Video, Voice and Compose since 4f): the CLI's own words for
/// why, what installing does, and the command -- nothing installed unless the
/// person says yes.
public struct InstallConsentSheet: View {
    private let set: String
    private let said: String
    private let command: String
    private let rebuild: String?
    private let install: () -> Void
    private let decline: () -> Void

    /// `set` is what the install brings (`video`, `voice`), `said` the
    /// refusal in the CLI's words, `command` the install as typed; `rebuild`
    /// what a rebuild costs, when the command replaces the environment
    /// rather than adding to it (the CLI's M19).
    public init(set: String, said: String, command: String, rebuild: String? = nil, install: @escaping () -> Void, decline: @escaping () -> Void) {
        self.set = set
        self.said = said
        self.command = command
        self.rebuild = rebuild
        self.install = install
        self.decline = decline
    }

    public var body: some View {
        VStack(alignment: .leading, spacing: Spacing.m) {
            Text(rebuild == nil ? "Install what \(set) needs?" : "Rebuild Apogee's Python environment?").font(.apogeeHeading)
            Text(said).font(.apogeeBody).foregroundStyle(Palette.textSecondary).textSelection(.enabled)
            Text(rebuild ?? "This downloads and installs Python packages into Apogee's own environment, once. Nothing is installed unless you say yes; the install runs here, with Stop, and what was refused runs again once it is in.")
                .font(.apogeeSmall)
                .foregroundStyle(Palette.textSecondary)
                .fixedSize(horizontal: false, vertical: true)
            Text(command).font(.apogeeCode).foregroundStyle(Palette.textTertiary).textSelection(.enabled)
            HStack {
                Spacer()
                Button("Not now", role: .cancel, action: decline).keyboardShortcut(.cancelAction)
                Button(rebuild == nil ? "Install" : "Rebuild", action: install).keyboardShortcut(.defaultAction)
            }
        }
        .padding(Spacing.xl)
        .frame(width: 580)
    }
}

#if DEBUG
#Preview("Install what video needs") {
    InstallConsentSheet(
        set: "video", said: StylesDocuments.generateRefusedErr.trimmingCharacters(in: .whitespacesAndNewlines),
        command: StylesPreview.installCommand, install: {}, decline: {})
}

#Preview("Rebuild the environment") {
    InstallConsentSheet(
        set: "voice",
        said: "apogee train: the Python environment at /Users/someone/.apogee/training/venv is on Python 3.14.5, and the 'voice' set runs on >=3.10,<3.13 -- rebuild it on a Python that fits: apogee train setup --rebuild --with voice",
        command: "apogee train setup --rebuild --yes --with voice",
        rebuild: "This replaces Apogee's own Python environment with one on a Python every runtime runs on, downloading every set it held again -- several gigabytes, once. If anything fails, the environment that was there is put back. Nothing is replaced unless you say yes.",
        install: {}, decline: {})
}
#endif
