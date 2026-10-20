import SwiftUI

/// A centered symbol, title and message: what a view shows when it has
/// nothing else to -- no session yet, no binary found.
public struct EmptyState: View {
    private let symbol: String
    private let title: String
    private let message: String

    /// `symbol` is an SF Symbols name.
    public init(symbol: String, title: String, message: String) {
        self.symbol = symbol
        self.title = title
        self.message = message
    }

    public var body: some View {
        VStack(spacing: Spacing.m) {
            Image(systemName: symbol)
                .font(.system(size: 32, weight: .light))
                .foregroundStyle(Palette.accent)
            Text(title)
                .font(.apogeeDisplay)
                .foregroundStyle(Palette.textPrimary)
            Text(message)
                .font(.apogeeBody)
                .foregroundStyle(Palette.textSecondary)
                .multilineTextAlignment(.center)
        }
        .padding(Spacing.xxl)
        .frame(maxWidth: Layout.conversationMax)
    }
}
