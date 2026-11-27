import SwiftUI

/// The said-card: what the CLI said, every line as printed, under a title,
/// until the person dismisses it. Neutral -- secondary text in the code font
/// on the code background, never a warning colour -- since the same voice
/// carries bad news, plain notes and good news alike.
///
/// Promoted from Models' delete narration card (GUI 4e, M16) once Knowledge
/// and the Code graph drew a run's notes on it too (GUI M22; ADR 0002); it
/// takes its title and its lines, so `styles` names no other module's type.
public struct SaidCard: View {
    private let title: String
    private let lines: [String]
    private let dismiss: () -> Void

    /// A card titled `title` showing `lines` as printed, with Dismiss.
    public init(title: String, lines: [String], dismiss: @escaping () -> Void) {
        self.title = title
        self.lines = lines
        self.dismiss = dismiss
    }

    public var body: some View {
        VStack(alignment: .leading, spacing: Spacing.s) {
            HStack(alignment: .firstTextBaseline) {
                Text(title).font(.apogeeBody.weight(.semibold)).foregroundStyle(Palette.textPrimary)
                    .lineLimit(1).truncationMode(.middle)
                Spacer()
                Button("Dismiss", action: dismiss)
            }
            Text(lines.joined(separator: "\n"))
                .font(.apogeeCode)
                .foregroundStyle(Palette.textSecondary)
                .textSelection(.enabled)
                .fixedSize(horizontal: false, vertical: true)
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(Spacing.s)
                .background(Palette.code, in: RoundedRectangle(cornerRadius: Radius.control))
        }
        .padding(Spacing.l)
        .background(Palette.card, in: RoundedRectangle(cornerRadius: Radius.card))
        .overlay(RoundedRectangle(cornerRadius: Radius.card).strokeBorder(Palette.border))
    }
}

#if DEBUG
#Preview("The said-card: a run's notes, as the CLI printed them") {
    SaidCard(
        title: "What the CLI said of the build",
        lines: [
            "apogee graph: could not register 'my notes' in config -- collection name 'my notes' is not one both config formats can edit -- a name is letters, digits, '_', '-', '.' and '/', first neither '-' nor '.', and not null",
            "apogee graph: could not set graph.enabled on 'my notes' -- collection 'my notes' is not one both config formats can edit -- a name is letters, digits, '_', '-', '.' and '/', first neither '-' nor '.', and not null",
        ]
    ) {}
    .padding(Spacing.xl)
    .frame(width: 860)
}
#endif
