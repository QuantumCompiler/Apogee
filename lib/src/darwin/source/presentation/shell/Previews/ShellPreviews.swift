#if DEBUG
import DataPreferences

/// The shell's view-model for its previews (ADR 0004): preferences held in
/// memory, never the person's own defaults.
@MainActor
enum ShellPreview {
    static func shell(at destination: Destination = .sessions) -> ShellViewModel {
        let shell = ShellViewModel(preferences: Preferences(store: MemoryStore()))
        shell.go(destination)
        return shell
    }

    /// The honest stand-in a place shows before its screen is built, in the
    /// words such a placeholder used (1b).
    static let placeholder = Placeholder(
        about: "Every model on this Mac and in the cloud: what each holds, and the roles it plays.",
        command: "apogee models list")
}
#endif
