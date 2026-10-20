import Foundation
import Observation

/// What the app's window shows before any session exists: its name, and the
/// build it is.
///
/// SwiftUI-free (ADR 0002): it reads the bundle's information dictionary as a
/// plain value, so a test hands it one.
@MainActor
@Observable
final class AppViewModel {
    let title = "Apogee"
    let versionLine: String

    init(info: [String: Any]?) {
        if let version = info?["CFBundleShortVersionString"] as? String, !version.isEmpty {
            versionLine = "Version \(version)"
        } else {
            // Only the release build (scripts/build.sh) stamps the version;
            // a build from Xcode leaves it empty.
            versionLine = "Development build"
        }
    }

    convenience init(bundle: Bundle) {
        self.init(info: bundle.infoDictionary)
    }
}
