import Foundation

/// How a command line carries a person's text so the binary never reads it
/// as an option (GUI 4g): the text goes after `--`, the end of options, and
/// every option goes before it.
///
/// Without it, a brief, a prompt, a goal or a title that begins with a dash
/// is parsed as a flag: on the branch binary `video compose "-m cats brief"`
/// took the brief as `-m` and composed nothing, and `voice say "--draft"`
/// answered "text is required". Every verb the app passes free text to takes
/// `--` (each probed on the binary), and an option placed before it is still
/// the verb's -- so a flag added to a line that already holds text goes
/// through `adding(_:to:)`, never on the end.
public enum Argv {
    /// The word after which the binary reads every word as text.
    public static let endOfOptions = "--"

    /// The person's `text`, after the end of options: the last words of a
    /// line whose options all come before them.
    public static func text(_ text: String...) -> [String] {
        [endOfOptions] + text
    }

    /// `arguments` with `options` where the binary reads options: just before
    /// the end of options when the line has one, else at its end.
    public static func adding(_ options: [String], to arguments: [String]) -> [String] {
        guard let end = arguments.firstIndex(of: endOfOptions) else { return arguments + options }
        var line = arguments
        line.insert(contentsOf: options, at: end)
        return line
    }

    /// The words the binary reads as the command and its options -- all of
    /// them before the end of options, the whole line when there is none.
    public static func options(of arguments: [String]) -> ArraySlice<String> {
        arguments.prefix { $0 != endOfOptions }
    }
}
