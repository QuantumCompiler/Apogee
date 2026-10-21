/// A place in the sidebar: one screen of the app, reached by its key.
///
/// The design's seven places, in its order (`main.html`'s places bar,
/// `BuildLayout.dc.html`'s keys): ⌘1 to ⌘7. Compositions live under Video,
/// as the design draws them -- its second tab -- not as a place of their own.
public enum Destination: String, CaseIterable, Sendable, Identifiable {
    case sessions
    case models
    case suites
    case tasks
    case graph
    case video
    case voice

    public var id: String { rawValue }

    public var title: String {
        switch self {
        case .sessions: "Sessions"
        case .models: "Models"
        case .suites: "Suites"
        case .tasks: "Tasks"
        case .graph: "Code graph"
        case .video: "Video"
        case .voice: "Voice"
        }
    }

    /// An SF Symbols name, drawn for the design's icon.
    public var symbol: String {
        switch self {
        case .sessions: "bubble.left"
        case .models: "cube"
        case .suites: "square.stack.3d.up"
        case .tasks: "checklist"
        case .graph: "point.3.connected.trianglepath.dotted"
        case .video: "video"
        case .voice: "waveform"
        }
    }

    /// ⌘1 for the first place, ⌘7 for the last.
    public var key: KeyChord {
        KeyChord(String(Destination.allCases.firstIndex(of: self)! + 1), .command)
    }

    /// What a place says while its screen is not built: what will live there,
    /// and the command that does it today. `nil` for a place that is built.
    public var placeholder: Placeholder? {
        switch self {
        case .sessions:
            nil
        case .models:
            nil
        case .suites:
            nil
        case .tasks:
            nil
        case .graph:
            Placeholder(
                about: "The code graph's symbols, their callers and callees, and its files will be here.",
                command: "apogee graph stats")
        case .video:
            Placeholder(
                about: "Your clips and compositions, and making new ones, will be here.",
                command: "apogee video --help")
        case .voice:
            Placeholder(
                about: "Your speeches, and saying something new, will be here.",
                command: "apogee voice say --help")
        }
    }
}

/// An honest stand-in for a screen that is not built: it says so, what will
/// be there, and the command that does it in the meantime -- never a dead
/// control.
public struct Placeholder: Equatable, Sendable {
    public let title = "Not built yet"
    /// What will live here.
    public let about: String
    /// The command line's way to it today.
    public let command: String

    public init(about: String, command: String) {
        self.about = about
        self.command = command
    }
}

/// The overlays the window opens over whatever screen it shows.
public enum Overlay: String, Sendable, Identifiable {
    /// The system monitor, ⌥⌘M.
    case monitor

    public var id: String { rawValue }

    public var title: String {
        switch self {
        case .monitor: "System monitor"
        }
    }
}
