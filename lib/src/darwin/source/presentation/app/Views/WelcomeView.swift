import PresentationStyles
import SwiftUI

/// The window's content until a session exists: the app's name and build.
struct WelcomeView: View {
    let model: AppViewModel

    var body: some View {
        EmptyState(symbol: "sparkle", title: model.title, message: model.versionLine)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(Palette.window)
    }
}
