import BusinessReads
import BusinessSession
import DataPreferences
import Foundation
import Observation
import PresentationSessions
import PresentationShell

/// The application's state: its name, the build it is, the window around its
/// screens, and the session library.
///
/// SwiftUI-free (ADR 0002): it reads the bundle's information dictionary as a
/// plain value, and builds the view-models over the client core and the app's
/// preferences -- the composition the views bind to, never reach past.
@MainActor
@Observable
final class AppViewModel {
    let title = "Apogee"
    let versionLine: String
    let shell: ShellViewModel
    let sessions: SessionLibraryViewModel

    init(
        info: [String: Any]?, preferences: Preferences, starter: (any ChatStarting)? = nil,
        runner: (any CommandRunning)? = nil
    ) {
        let version = (info?["CFBundleShortVersionString"] as? String).flatMap { $0.isEmpty ? nil : $0 }
        // Only the release build (scripts/build.sh) stamps the version; a
        // build from Xcode leaves it empty.
        versionLine = version.map { "Version \($0)" } ?? "Development build"
        let shell = ShellViewModel(preferences: preferences)
        let sessions = SessionLibraryViewModel(
            starter: starter ?? ApogeeClient(identity: ClientIdentity(version: version ?? "development")),
            runner: runner ?? ReadRunner(), preferences: preferences)
        sessions.say = { [weak shell] text in shell?.say(text) }
        // Sessions in view or not: the library keeps its one child to the
        // session on screen.
        shell.routeChanged = { [weak sessions] destination in
            Task {
                if destination == .sessions { await sessions?.cameIntoView() } else { await sessions?.leftView() }
            }
        }
        self.shell = shell
        self.sessions = sessions
    }

    convenience init(bundle: Bundle) {
        self.init(info: bundle.infoDictionary, preferences: .standard())
    }

    /// The app started: the library reads and opens what is in view.
    func start() async {
        await sessions.start(inView: shell.route == .sessions)
    }
}
