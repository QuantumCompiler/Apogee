import SwiftUI

/// The one-time install a media verb's refusal named, asked first (GUI 1m,
/// 1o; shared by Video, Voice and Compose since 4f): the CLI's own words for
/// why, what installing does, and the command -- nothing installed unless the
/// person says yes.
public struct InstallConsentSheet: View {
    private let set: String
    private let said: String
    private let command: String
    private let install: () -> Void
    private let decline: () -> Void

    /// `set` is what the install brings (`video`, `voice`), `said` the
    /// refusal in the CLI's words, `command` the install as typed.
    public init(set: String, said: String, command: String, install: @escaping () -> Void, decline: @escaping () -> Void) {
        self.set = set
        self.said = said
        self.command = command
        self.install = install
        self.decline = decline
    }

    public var body: some View {
        VStack(alignment: .leading, spacing: Spacing.m) {
            Text("Install what \(set) needs?").font(.apogeeHeading)
            Text(said).font(.apogeeBody).foregroundStyle(Palette.textSecondary).textSelection(.enabled)
            Text("This downloads and installs Python packages into Apogee's own environment, once. Nothing is installed unless you say yes; the install runs here, with Stop, and what was refused runs again once it is in.")
                .font(.apogeeSmall)
                .foregroundStyle(Palette.textSecondary)
                .fixedSize(horizontal: false, vertical: true)
            Text(command).font(.apogeeCode).foregroundStyle(Palette.textTertiary).textSelection(.enabled)
            HStack {
                Spacer()
                Button("Not now", role: .cancel, action: decline).keyboardShortcut(.cancelAction)
                Button("Install", action: install).keyboardShortcut(.defaultAction)
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
#endif
