import PresentationSession
import PresentationStyles
import SwiftUI

/// The application: one window, one chat session. Started by the `Apogee`
/// target's entry point (source/main), which holds nothing else.
public struct ApogeeApp: App {
    @State private var model = AppViewModel(bundle: .main)

    public init() {}

    public var body: some Scene {
        WindowGroup(model.title) {
            SessionWindow(model: model.session)
                .frame(minWidth: Layout.windowMinWidth, minHeight: Layout.windowMinHeight)
                .navigationSubtitle(model.versionLine)
                .task { model.session.start() }
                // The window closing ends its session: the turn cancelled,
                // the child asked to go, terminated only if it will not.
                .onDisappear { Task { await model.session.shutDown() } }
        }
    }
}
