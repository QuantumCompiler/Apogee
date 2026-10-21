import SwiftUI

/// The shell's keys, as menu commands: a Go menu with every place, and the
/// View menu's sidebar, monitor and console. Settings' ⌘, is the Settings scene's own.
public struct ShellCommands: Commands {
    private let model: ShellViewModel

    public init(model: ShellViewModel) {
        self.model = model
    }

    public var body: some Commands {
        CommandMenu("Go") {
            ForEach(KeyMap.shell.filter(\.isPlace), id: \.self) { binding in
                item(binding)
            }
            Divider()
            if let binding = KeyMap.shell.first(where: { $0.command == .modelsLibrary }) {
                item(binding)
            }
        }
        CommandGroup(after: .sidebar) {
            if let binding = KeyMap.shell.first(where: { $0.command == .toggleSidebar }) {
                Button(model.sidebarHidden ? "Show Sidebar" : "Hide Sidebar") { model.perform(binding.command) }
                    .keyboardShortcut(binding.chord.equivalent, modifiers: binding.chord.eventModifiers)
            }
            if let binding = KeyMap.shell.first(where: { $0.command == .toggleMonitor }) {
                item(binding)
            }
            if let binding = KeyMap.shell.first(where: { $0.command == .toggleConsole }) {
                item(binding)
            }
        }
    }

    private func item(_ binding: KeyBinding) -> some View {
        Button(binding.label) { model.perform(binding.command) }
            .keyboardShortcut(binding.chord.equivalent, modifiers: binding.chord.eventModifiers)
    }
}

extension KeyBinding {
    var isPlace: Bool {
        if case .go = command { true } else { false }
    }
}

extension KeyChord {
    /// The key, as SwiftUI names it.
    public var equivalent: KeyEquivalent { KeyEquivalent(Character(key)) }

    /// The modifiers, as SwiftUI names them.
    public var eventModifiers: EventModifiers {
        var result: EventModifiers = []
        if modifiers.contains(.command) { result.insert(.command) }
        if modifiers.contains(.shift) { result.insert(.shift) }
        if modifiers.contains(.option) { result.insert(.option) }
        if modifiers.contains(.control) { result.insert(.control) }
        return result
    }
}
