/// The modifier keys of a chord.
public struct KeyModifiers: OptionSet, Hashable, Sendable {
    public let rawValue: Int

    public init(rawValue: Int) {
        self.rawValue = rawValue
    }

    public static let command = KeyModifiers(rawValue: 1 << 0)
    public static let shift = KeyModifiers(rawValue: 1 << 1)
    public static let option = KeyModifiers(rawValue: 1 << 2)
    public static let control = KeyModifiers(rawValue: 1 << 3)
}

/// One key with its modifiers: `⌥⌘M`.
public struct KeyChord: Hashable, Sendable {
    /// The key: one character, lowercase for a letter.
    public let key: String
    public let modifiers: KeyModifiers

    public init(_ key: String, _ modifiers: KeyModifiers) {
        self.key = key
        self.modifiers = modifiers
    }

    /// The chord as the Mac's menus write it, modifiers in their order: ⌃⌥⇧⌘.
    public var symbols: String {
        var text = ""
        if modifiers.contains(.control) { text += "⌃" }
        if modifiers.contains(.option) { text += "⌥" }
        if modifiers.contains(.shift) { text += "⇧" }
        if modifiers.contains(.command) { text += "⌘" }
        return text + key.uppercased()
    }
}

/// What the window does at a key, wherever its focus is.
public enum ShellCommand: Hashable, Sendable {
    /// Show a place.
    case go(Destination)
    /// The models library: Models, by its own key.
    case modelsLibrary
    /// Open or close the system monitor.
    case toggleMonitor
    /// Open or close the console.
    case toggleConsole
    /// Hide or show the sidebar.
    case toggleSidebar
    /// Open Settings -- the Settings scene's own command carries its key.
    case openSettings
}

/// A command and its key.
public struct KeyBinding: Hashable, Sendable {
    public let command: ShellCommand
    public let chord: KeyChord
    /// The menu item's words.
    public let label: String

    public init(_ command: ShellCommand, _ chord: KeyChord, _ label: String) {
        self.command = command
        self.chord = chord
        self.label = label
    }
}

/// One line of the design's keyboard list (Settings › Keyboard).
public struct KeyReference: Equatable, Sendable {
    public let label: String
    public let keys: String

    public init(_ label: String, _ keys: String) {
        self.label = label
        self.keys = keys
    }
}

/// The keys, as the design gives them (`main.html`'s `set.keys`,
/// `BuildLayout.dc.html`'s Navigation and keys).
public enum KeyMap {
    /// The window's own keys: every place, and every overlay the shell opens.
    /// The keys a screen owns -- New session, the inspector, Send -- arrive
    /// with that screen.
    public static let shell: [KeyBinding] =
        Destination.allCases.map { KeyBinding(.go($0), $0.key, $0.title) } + [
            KeyBinding(.modelsLibrary, KeyChord("l", [.shift, .command]), "Models Library"),
            KeyBinding(.toggleMonitor, KeyChord("m", [.option, .command]), "System Monitor"),
            KeyBinding(.toggleConsole, KeyChord("k", [.shift, .command]), "Console"),
            KeyBinding(.toggleSidebar, KeyChord("s", [.control, .command]), "Hide Sidebar"),
            KeyBinding(.openSettings, KeyChord(",", .command), "Settings"),
        ]

    /// The key for `command`, when it has one.
    public static func chord(for command: ShellCommand) -> KeyChord? {
        shell.first { $0.command == command }?.chord
    }

    /// The design's whole keyboard list, in its order and words.
    public static let reference: [KeyReference] = [
        KeyReference("New session", "⌘N"),
        KeyReference("New private session", "⇧⌘N"),
        KeyReference("Switch model", "⇧⌘M"),
        KeyReference("Show or hide the inspector", "⌥⌘I"),
        KeyReference("System monitor", "⌥⌘M"),
        KeyReference("Console", "⇧⌘K"),
        KeyReference("Settings", "⌘,"),
        KeyReference("Stop the answer", "⌘."),
        KeyReference("Search sessions", "⇧⌘F"),
        KeyReference("Sessions, Models, Suites, Tasks, Code graph, Video, Voice, Knowledge, Training, Library", "⌘1 – ⌘9, ⌘0"),
        KeyReference("Models library", "⇧⌘L"),
        KeyReference("Send", "⏎"),
        KeyReference("New line", "⇧⏎"),
        KeyReference("Commands", "/"),
        KeyReference("Mention a file", "@"),
        KeyReference("Allow once on a permission card", "⏎"),
        KeyReference("Deny on a permission card", "esc"),
        KeyReference("Pick an answer on a card", "1 – 4"),
        KeyReference("Play or pause the focused player", "space"),
        KeyReference("Generate, Speak, Plan or Render, from its form", "⌘⏎"),
        KeyReference("Hide the sidebar", "⌃⌘S"),
    ]
}
