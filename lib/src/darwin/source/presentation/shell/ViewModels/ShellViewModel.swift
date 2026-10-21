import DataPreferences
import Foundation
import Observation

/// The window around every screen: which place it shows, which overlay is
/// open, how the sidebar stands, and the toast.
///
/// SwiftUI-free (ADR 0002). Navigation is state: the place shown is this
/// model's `route`, kept in the app's preferences so a relaunch opens where
/// the window was left -- never window juggling.
@MainActor
@Observable
public final class ShellViewModel {
    public private(set) var route: Destination
    public private(set) var overlay: Overlay?
    /// Hidden by the person (⌃⌘S); remembered across launches.
    public private(set) var sidebarHidden: Bool
    /// The window's width, as last laid out; `nil` before the first layout.
    public private(set) var width: Double?
    /// The one line at the bottom of the window, if one is showing.
    public private(set) var toast: Toast?

    private let preferences: Preferences
    private var nextToast = 0

    public init(preferences: Preferences) {
        self.preferences = preferences
        route = preferences.route.flatMap(Destination.init(rawValue:)) ?? .sessions
        sidebarHidden = preferences.sidebarHidden
    }

    /// How the window lays out at its width.
    public var layout: ShellLayout { ShellLayout(width: width, sidebarHidden: sidebarHidden) }

    // MARK: - Acts

    public func perform(_ command: ShellCommand) {
        switch command {
        case .go(let destination): go(destination)
        case .modelsLibrary: go(.models)
        case .toggleMonitor: overlay = overlay == .monitor ? nil : .monitor
        case .toggleSidebar: setSidebarHidden(!sidebarHidden)
        case .openSettings: break  // the Settings scene answers its own command
        }
    }

    /// Shows `destination`, and remembers it for the next launch.
    public func go(_ destination: Destination) {
        route = destination
        preferences.route = destination.rawValue
    }

    public func open(_ overlay: Overlay) {
        self.overlay = overlay
    }

    public func closeOverlay() {
        overlay = nil
    }

    public func setSidebarHidden(_ hidden: Bool) {
        sidebarHidden = hidden
        preferences.sidebarHidden = hidden
    }

    /// The window was laid out `width` points wide.
    public func windowResized(width: Double) {
        if self.width != width {
            self.width = width
        }
    }

    /// Says `text` in the toast. Never the only place something is said: the
    /// screen that did it shows it too.
    public func say(_ text: String) {
        nextToast += 1
        toast = Toast(id: nextToast, text: text)
    }

    /// The toast `id` has had its time; a newer one stays.
    public func dismissToast(_ id: Int) {
        if toast?.id == id {
            toast = nil
        }
    }
}

/// One line, gone after about five seconds.
public struct Toast: Equatable, Identifiable, Sendable {
    public let id: Int
    public let text: String

    /// How long it shows.
    public static let lifetime: Duration = .seconds(5)
}

/// The window law (the design's decision 10): below 900 points wide the
/// sidebar becomes a column of icons and the side panes close; the person can
/// hide the sidebar at any width.
public struct ShellLayout: Equatable, Sendable {
    /// `tokens.json` -> `layoutPx.collapseBelow`.
    public static let collapseBelow = 900.0

    public enum Sidebar: Equatable, Sendable {
        /// Places, sessions, the system strip and Settings, 256 points wide.
        case full
        /// A 56-point column of icons.
        case icons
        case hidden
    }

    public let sidebar: Sidebar
    /// Whether side panes -- the inspector, a model's details -- may open.
    public let sidePanes: Bool

    public init(width: Double?, sidebarHidden: Bool) {
        let narrow = (width ?? .infinity) < Self.collapseBelow
        sidebar = sidebarHidden ? .hidden : narrow ? .icons : .full
        sidePanes = !narrow
    }
}
