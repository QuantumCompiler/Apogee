import SwiftUI

/// A runtime install as a card in a media place's list (GUI 4f): what it
/// installs, the line it is on and Stop while it runs; once ended, how --
/// installed, stopped, or failed in the CLI's own words -- and Dismiss.
/// Video, Voice and Compose draw their installs through it, each from its
/// view-model's install; the words are the view-model's, the look this one.
public struct InstallCard: View {
    /// How the install stands.
    public enum State: Sendable, Equatable {
        case running
        case installed
        case stopped
        case failed
    }

    private let headline: String
    private let command: String
    private let state: State
    private let detail: String?
    private let stop: () -> Void
    private let dismiss: () -> Void

    /// `headline` says what it is doing, `detail` the line it is on (or, failed,
    /// why), `command` the install as typed. `stop` is offered while it runs,
    /// `dismiss` once it has ended.
    public init(
        headline: String, command: String, state: State, detail: String?, stop: @escaping () -> Void,
        dismiss: @escaping () -> Void
    ) {
        self.headline = headline
        self.command = command
        self.state = state
        self.detail = detail
        self.stop = stop
        self.dismiss = dismiss
    }

    public var body: some View {
        VStack(alignment: .leading, spacing: Spacing.xs) {
            HStack(alignment: .firstTextBaseline, spacing: Spacing.s) {
                Image(systemName: symbol)
                    .foregroundStyle(tint)
                Text(headline)
                    .font(.apogeeSmall.weight(.medium))
                    .fixedSize(horizontal: false, vertical: true)
                Spacer(minLength: Spacing.s)
                if state == .running {
                    Button("Stop", action: stop)
                } else {
                    Button("Dismiss", action: dismiss)
                }
            }
            if state == .running {
                ProgressView().progressViewStyle(.linear)
            }
            if let detail, !detail.isEmpty {
                Text(detail)
                    .font(.apogeeMeta.monospaced())
                    .foregroundStyle(state == .failed ? Palette.error : Palette.textTertiary)
                    .lineLimit(state == .failed ? 8 : state == .running ? 2 : 3)
                    .textSelection(.enabled)
            }
            Text(command)
                .font(.apogeeMeta.monospaced())
                .foregroundStyle(Palette.textTertiary)
                .lineLimit(1)
                .truncationMode(.middle)
                .textSelection(.enabled)
        }
        .padding(Spacing.m)
        .background(Palette.card, in: RoundedRectangle(cornerRadius: Radius.card))
        .overlay(RoundedRectangle(cornerRadius: Radius.card).strokeBorder(state == .failed ? Palette.error : Palette.border))
    }

    private var symbol: String {
        switch state {
        case .running: "arrow.down.circle"
        case .installed: "checkmark.circle"
        case .stopped: "stop.circle"
        case .failed: "exclamationmark.triangle"
        }
    }

    private var tint: Color {
        switch state {
        case .running: Palette.accent
        case .installed: Palette.accent
        case .stopped: Palette.textTertiary
        case .failed: Palette.error
        }
    }
}

#if DEBUG
#Preview("Installing the video set") {
    InstallCard(
        headline: "Installing the video set", command: StylesPreview.installCommand, state: .running,
        detail: StylesPreview.installLine(StylesDocuments.installVideoDone, startingWith: "installing"), stop: {}, dismiss: {}
    )
    .frame(width: 264)
    .padding(Spacing.s)
}

#Preview("The video set did not install") {
    InstallCard(
        headline: "The video set did not install", command: StylesPreview.installCommand, state: .failed,
        detail: StylesDocuments.installVideoFailedErr.trimmingCharacters(in: .whitespacesAndNewlines), stop: {}, dismiss: {}
    )
    .frame(width: 264)
    .padding(Spacing.s)
}

#Preview("Installed the video set") {
    InstallCard(
        headline: "Installed the video set", command: StylesPreview.installCommand, state: .installed,
        detail: StylesPreview.installLine(StylesDocuments.installVideoDone, startingWith: "python env"), stop: {}, dismiss: {}
    )
    .frame(width: 264)
    .padding(Spacing.s)
}

#Preview("Stopped before it finished") {
    InstallCard(
        headline: "The video set's install was stopped before it finished", command: StylesPreview.installCommand,
        state: .stopped, detail: StylesPreview.installLine(StylesDocuments.installVideoDone, startingWith: "installing"),
        stop: {}, dismiss: {}
    )
    .frame(width: 264)
    .padding(Spacing.s)
}
#endif
