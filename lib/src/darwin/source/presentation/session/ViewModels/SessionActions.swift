import DataProtocol
import Foundation

/// An act a session's window offers whose owner is the library around it:
/// what a slash command or a button asks of the Sessions place.
public enum SessionAction: Equatable, Sendable {
    case newSession
    case newPrivateSession
    case rename
    case delete
    case toggleInspector
    /// Open a past chat a turn recalled.
    case openChat(String)
}

/// What the window presents when an act needs the person: the file chooser,
/// the settings sheet.
public enum Presenting: String, Identifiable, Sendable {
    case files
    case settings

    public var id: String { rawValue }
}

/// How a private session is kept from being written.
public enum PrivateMode: Equatable, Sendable {
    /// `chat --private`: nothing of it is ever written (the CLI's 52d).
    case nothingWritten
    /// The fallback on a build before it: `--no-recall`, and the saved chat
    /// deleted when the session closes -- written meanwhile, and said so.
    case deletedOnClose
}

/// A slash command: an act of the app's own, since machine mode does not
/// carry the CLI's slash commands -- each maps onto a line or a command that
/// exists, never a capability of its own.
public struct SlashCommand: Equatable, Identifiable, Sendable {
    public enum Act: Equatable, Sendable {
        case library(SessionAction)
        case present(Presenting)
        case compact
        case detachAll
        case stop
    }

    public let name: String
    public let summary: String
    public let act: Act

    public var id: String { name }

    /// Every command, in the order the list shows them.
    public static let all: [SlashCommand] = [
        SlashCommand(name: "/new", summary: "Start a new session", act: .library(.newSession)),
        SlashCommand(name: "/private", summary: "Start a private session", act: .library(.newPrivateSession)),
        SlashCommand(name: "/model", summary: "Change the model, thinking, effort or system prompt", act: .present(.settings)),
        SlashCommand(name: "/attach", summary: "Attach a file, folder, image, recording or video", act: .present(.files)),
        SlashCommand(name: "/detach", summary: "Detach everything attached", act: .detachAll),
        SlashCommand(name: "/compact", summary: "Summarize the earlier turns now", act: .compact),
        SlashCommand(name: "/rename", summary: "Rename this session", act: .library(.rename)),
        SlashCommand(name: "/delete", summary: "Delete this session", act: .library(.delete)),
        SlashCommand(name: "/inspector", summary: "Show or hide the inspector", act: .library(.toggleInspector)),
        SlashCommand(name: "/stop", summary: "Stop the turn", act: .stop),
    ]
}

/// The context meter: the last turn's input tokens against the model's
/// window -- the app's arithmetic over what the CLI reports, unknown when
/// either is missing, never guessed.
public struct ContextMeter: Equatable, Sendable {
    public let used: Int?
    public let window: Int?

    public init(used: Int?, window: Int?) {
        self.used = used
        self.window = window
    }

    /// 0 to 100, or `nil` when it cannot be known.
    public var percent: Int? {
        guard let used, let window, window > 0 else { return nil }
        return min(100, Int((Double(used) / Double(window) * 100).rounded()))
    }

    /// At 80% and above the design warns.
    public var warns: Bool { (percent ?? 0) >= 80 }

    /// What the meter says in words.
    public var text: String {
        guard let percent, let used, let window else {
            return window == nil ? "unknown" : "not reported"
        }
        return "\(percent)% · \(used.formatted()) of \(window.formatted()) tokens"
    }
}
