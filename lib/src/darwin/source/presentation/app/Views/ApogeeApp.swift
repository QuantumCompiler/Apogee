import PresentationStyles
import SwiftUI

/// The application: one window. Started by the `Apogee` target's entry point
/// (source/main), which holds nothing else.
public struct ApogeeApp: App {
    @State private var model = AppViewModel(bundle: .main)

    public init() {}

    public var body: some Scene {
        WindowGroup("Apogee") {
            WelcomeView(model: model)
                .frame(minWidth: Layout.windowMinWidth, minHeight: Layout.windowMinHeight)
        }
    }
}
