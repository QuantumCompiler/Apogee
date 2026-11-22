import SwiftUI

/// The toast: one line at the bottom centre of a window. Promoted from the
/// shell (GUI M19) once the Settings window drew one too (ADR 0002); it takes
/// its text, so `styles` names no other module's type. Its owner places it
/// and ends it on its own time.
public struct ToastView: View {
    private let text: String

    /// A toast saying `text`.
    public init(text: String) {
        self.text = text
    }

    public var body: some View {
        Text(text)
            .font(.apogeeBody)
            .foregroundStyle(Palette.textPrimary)
            .padding(.horizontal, Spacing.l)
            .padding(.vertical, Spacing.s)
            .background(Palette.card, in: Capsule())
            .overlay(Capsule().strokeBorder(Palette.border))
            .shadow(color: .black.opacity(0.2), radius: 12, y: 4)
            .accessibilityAddTraits(.updatesFrequently)
    }
}

#if DEBUG
#Preview("The toast") {
    ToastView(text: "Renamed to qwen3-8b-chat")
        .padding(Spacing.xl)
}
#endif
