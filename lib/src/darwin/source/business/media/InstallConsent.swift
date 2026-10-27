import Foundation

/// The install a media verb's refusal named -- `train setup --with video`,
/// `--with voice` -- waiting on the person's yes (GUI 1m, 1o; since 4f
/// Compose's too).
///
/// It is read from the run's own words, never guessed: the CLI refuses a
/// clip, a speech or a composition whose runtime is not installed by naming
/// the command that installs it (`torch and diffusers are not installed in
/// Apogee's Python environment -- apogee train setup --with video`). Since the
/// CLI's M29 that refusal ends the job's stream on an `error` event, which is
/// read first (GUI M13); a binary before it says it on stderr alone, its
/// stream silent, so there the failure and the stderr the run left are read.
public struct InstallConsent: Sendable, Equatable {
    /// What the install runs: `train setup --with video`.
    public let arguments: [String]
    /// The refusal, in the CLI's words: the line that named the install.
    public let said: String

    public init(arguments: [String], said: String) {
        self.arguments = arguments
        self.said = said
    }

    /// The consent a failed run asks for, read where the run said why: the
    /// stream's own `error` when one arrived -- its words alone, stderr only
    /// repeating them -- else `failure`, the run's last word, with the
    /// `stderr` it left, as a binary before the CLI's M29 says it. `nil` for
    /// a failure of any other kind.
    public init?(error: String?, failure: String, stderr: String) {
        guard let error else {
            self.init(failure: failure, stderr: stderr)
            return
        }
        guard let arguments = MediaLibrary.installCommand(in: error) else { return nil }
        self.init(arguments: arguments, said: Self.naming(in: error) ?? error)
    }

    /// The consent a failed run asks for, when its words -- `failure`, the
    /// run's last word, and the `stderr` it left -- name an install; `nil`
    /// for a failure of any other kind. The fallback where no `error` event
    /// arrived.
    public init?(failure: String, stderr: String) {
        let text = failure + "\n" + stderr
        guard let arguments = MediaLibrary.installCommand(in: text) else { return nil }
        self.init(arguments: arguments, said: Self.naming(in: text) ?? failure)
    }

    /// The line of `text` that names the install, trimmed.
    private static func naming(in text: String) -> String? {
        text.split(whereSeparator: \.isNewline).last { $0.contains("apogee train setup") }
            .map { $0.trimmingCharacters(in: .whitespaces) }
    }

    /// The command as a person would type it: `apogee train setup --with video`.
    public var command: String { (["apogee"] + arguments).joined(separator: " ") }

    /// The set it installs, as the CLI names it: `video`, `voice`.
    public var set: String { arguments.last ?? "" }
}
