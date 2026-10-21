import BusinessSession
import DataPreferences
import Foundation
import Observation
import PresentationSession
import PresentationShell

/// The application's state: its name, the build it is, the window around its
/// screens, and its one session.
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
    let session: SessionViewModel

    init(info: [String: Any]?, preferences: Preferences, starter: (any ChatStarting)? = nil) {
        let version = (info?["CFBundleShortVersionString"] as? String).flatMap { $0.isEmpty ? nil : $0 }
        // Only the release build (scripts/build.sh) stamps the version; a
        // build from Xcode leaves it empty.
        versionLine = version.map { "Version \($0)" } ?? "Development build"
        shell = ShellViewModel(preferences: preferences)
        session = SessionViewModel(
            starter: starter ?? ApogeeClient(identity: ClientIdentity(version: version ?? "development")))
    }

    convenience init(bundle: Bundle) {
        self.init(info: bundle.infoDictionary, preferences: .standard())
    }
}
