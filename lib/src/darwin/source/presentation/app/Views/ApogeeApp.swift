import PresentationModels
import PresentationSessions
import PresentationShell
import PresentationStyles
import PresentationTasks
import SwiftUI

/// The application: one window -- the shell around every screen -- and the
/// Settings scene. Started by the `Apogee` target's entry point (source/main),
/// which holds nothing else.
public struct ApogeeApp: App {
    @State private var model = AppViewModel(bundle: .main)

    public init() {}

    public var body: some Scene {
        // One window: the app holds one live session, and a second window
        // would mean a second.
        Window(model.title, id: "main") {
            ShellView(model: model.shell) {
                SessionList(library: model.sessions, shell: model.shell)
            } screen: { destination in
                switch destination {
                case .sessions: SessionsScreen(library: model.sessions, shell: model.shell)
                case .models:
                    ModelsScreen(model: model.models, shell: model.shell) {
                        DownloadsView(model: model.downloads)
                    }
                case .tasks: TasksScreen(model: model.tasks)
                default: PlaceholderScreen(destination)
                }
            }
            .frame(minWidth: Layout.windowMinWidth, minHeight: Layout.windowMinHeight)
            .navigationSubtitle(model.versionLine)
            .task { await model.start() }
            // The window closing ends its session: the turn cancelled, the
            // child asked to go, terminated only if it will not.
            .onDisappear { Task { await model.sessions.shutDown() } }
        }
        .windowResizability(.contentMinSize)
        .commands {
            ShellCommands(model: model.shell)
            SessionCommands(library: model.sessions, shell: model.shell)
        }

        Settings {
            SettingsPlaceholder()
        }
    }
}
