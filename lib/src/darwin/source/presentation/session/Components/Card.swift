import PresentationStyles
import SwiftUI

/// The session's card: a raised surface with the design's radius and border.
struct Card<Content: View>: View {
    var accent: Color = Palette.border
    @ViewBuilder var content: Content

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.s) { content }
            .padding(Spacing.l)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(Palette.card, in: RoundedRectangle(cornerRadius: Radius.card))
            .overlay(RoundedRectangle(cornerRadius: Radius.card).strokeBorder(accent))
    }
}

/// A small capital label above a card's title, the design's label style.
struct CardLabel: View {
    let text: String

    var body: some View {
        Text(text.uppercased())
            .font(.system(size: TypeScale.label, weight: .medium, design: .monospaced))
            .tracking(0.8)
            .foregroundStyle(Palette.textTertiary)
    }
}
