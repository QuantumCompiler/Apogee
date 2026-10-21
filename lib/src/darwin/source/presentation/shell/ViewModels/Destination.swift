/// A place in the sidebar: one screen of the app, reached by its key.
///
/// The design's eleven places, in its order (`main.html`'s places bar,
/// `BuildLayout.dc.html`'s keys): ⌘1 to ⌘9, then ⌘0 -- Knowledge the eighth
/// since 2c, Training the ninth since 2d, Library the tenth since 2e -- and
/// Review the eleventh since 2g, on ⇧⌘R, the number keys spent. Compositions live under Video,
/// as the design draws them -- its second tab -- not as a place of their own.
public enum Destination: String, CaseIterable, Sendable, Identifiable {
    case sessions
    case models
    case suites
    case tasks
    case graph
    case video
    case voice
    case knowledge
    case training
    case library
    case review

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
        case .knowledge: "Knowledge"
        case .training: "Training"
        case .library: "Library"
        case .review: "Review"
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
        case .knowledge: "books.vertical"
        case .training: "graduationcap"
        case .library: "puzzlepiece.extension"
        case .review: "arrow.triangle.pull"
        }
    }

    /// ⌘1 for the first place, ⌘9 for the ninth, ⌘0 for the tenth; the
    /// eleventh, Review, ⇧⌘R.
    public var key: KeyChord {
        if self == .review { return KeyChord("r", [.shift, .command]) }
        let number = Destination.allCases.firstIndex(of: self)! + 1
        return KeyChord(number == 10 ? "0" : String(number), .command)
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
            nil
        case .video:
            nil
        case .voice:
            nil
        case .knowledge:
            nil
        case .training:
            nil
        case .library:
            nil
        case .review:
            nil
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
    /// The console, ⇧⌘K: any command run as typed (ADR 0003's catch-all).
    case console

    public var id: String { rawValue }

    public var title: String {
        switch self {
        case .monitor: "System monitor"
        case .console: "Console"
        }
    }
}
