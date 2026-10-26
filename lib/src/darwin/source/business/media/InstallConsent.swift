import Foundation

/// The install a media verb's refusal named -- `train setup --with video`,
/// `--with voice` -- waiting on the person's yes (GUI 1m, 1o; since 4f
/// Compose's too).
///
/// It is read from the run's own words, never guessed: the CLI refuses a
/// clip, a speech or a composition whose runtime is not installed by naming
/// the command that installs it (`apogee video compose: torch and diffusers
/// are not installed in Apogee's Python environment -- apogee train setup
/// --with video`). That line is stderr's alone -- a 52b stream says nothing
/// of it -- so the failure and the stderr the run left are both read.
public struct InstallConsent: Sendable, Equatable {
    /// What the install runs: `train setup --with video`.
    public let arguments: [String]
    /// The refusal, in the CLI's words: the line that named the install.
    public let said: String

    public init(arguments: [String], said: String) {
        self.arguments = arguments
        self.said = said
    }

    /// The consent a failed run asks for, when its words -- `failure`, the
    /// run's last word, and the `stderr` it left -- name an install; `nil`
    /// for a failure of any other kind.
    public init?(failure: String, stderr: String) {
        let text = failure + "\n" + stderr
        guard let arguments = MediaLibrary.installCommand(in: text) else { return nil }
        self.arguments = arguments
        let naming = text.split(whereSeparator: \.isNewline).last { $0.contains("apogee train setup") }
        said = naming.map { $0.trimmingCharacters(in: .whitespaces) } ?? failure
    }

    /// The command as a person would type it: `apogee train setup --with video`.
    public var command: String { (["apogee"] + arguments).joined(separator: " ") }

    /// The set it installs, as the CLI names it: `video`, `voice`.
    public var set: String { arguments.last ?? "" }
}
