import Foundation

/// How a command line carries a person's text and every name, so the binary
/// never reads one as an option (GUI 4g, and GUI M21 for names): they go
/// after `--`, the end of options, and every option goes before it.
///
/// Without it, a brief, a prompt, a goal or a title that begins with a dash
/// is parsed as a flag: on the branch binary `video compose "-m cats brief"`
/// took the brief as `-m` and composed nothing, and `voice say "--draft"`
/// answered "text is required". A listed name fares the same (GUI M21): the
/// CLI's parser (CLI11) classifies `-x`, `-h` or `--graph` as an option
/// before the verb runs, so `embed info -x` exits 106 and `datasets delete
/// -h --yes` prints the verb's help with exit 0. Every verb the app passes
/// text or a name to takes `--` (each probed on the binary), and an option
/// placed before it is still the verb's -- so a line is built by `line(_:
/// options:names:)`, and a flag added to a line that already holds names
/// goes through `adding(_:to:)`, never on the end.
public enum Argv {
    /// The word after which the binary reads every word as text.
    public static let endOfOptions = "--"

    /// One option of a line: a flag alone, or an option and its value.
    public enum Option: Equatable, Sendable {
        /// A flag that takes no value: `--yes`.
        case flag(String)
        /// An option and the one value it takes: `--retriever lexical`.
        case value(String, String)

        /// The option's own name: `--yes`, `--retriever`.
        public var name: String {
            switch self {
            case .flag(let name), .value(let name, _): name
            }
        }

        /// The words it is on a line: the option, then its value -- which
        /// the binary takes whole, whatever it begins with -- or, for a value
        /// that is itself `--`, one word `--name=--`, so the line's one end
        /// of options stays the word `adding` and `positionals` find.
        public var words: [String] {
            switch self {
            case .flag(let name): [name]
            case .value(let name, let value) where value == Argv.endOfOptions && name.hasPrefix("--"): ["\(name)=\(value)"]
            case .value(let name, let value): [name, value]
            }
        }
    }

    /// The options that take a `--` directly after them as their own end, by
    /// the command they belong to (its words joined by a space). CLI11 lets
    /// an option that collects several values end its list with `--` and
    /// eats it (`App_inl.hpp`'s "Allow -- to end an unlimited list"), so
    /// `graph build --source F -- -x` exits 109. The set is the binary's
    /// recorded verdicts (`ArgvTests`, `argv-end-of-options-verdicts.txt`),
    /// never the help's ` ...`: `agents create --mcp` shows one and takes no
    /// `--`.
    public static let endOfOptionsEaters: [String: Set<String>] = [
        "graph build": ["--source", "--lang"],
        "models convert": ["--register-with"],
        "review targets add": ["--skills"],
        "mcp create": ["--args"],
        "video generate": ["--lora"],
    ]

    /// `command`, then `options`, then -- when there are any -- `--` and
    /// `names`: so a line holds one end of options, and every name, path or
    /// id is read as a name whatever it begins with. An option that would
    /// take the `--` as its own end (`endOfOptionsEaters`) is moved before
    /// the others, which keep their order; a line whose only options are such
    /// is the caller's to give a name some other way (New graph's `--graph`).
    public static func line(_ command: [String], options: [Option] = [], names: [String] = []) -> [String] {
        let eaters = names.isEmpty ? [] : endOfOptionsEaters[command.joined(separator: " ")] ?? []
        let ordered = options.filter { eaters.contains($0.name) } + options.filter { !eaters.contains($0.name) }
        return command + ordered.flatMap(\.words) + (names.isEmpty ? [] : [endOfOptions] + names)
    }

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

    /// The words the binary reads as names and text: every word after the
    /// end of options, none when the line has no end of options.
    public static func positionals(of arguments: [String]) -> ArraySlice<String> {
        guard let end = arguments.firstIndex(of: endOfOptions) else { return arguments[arguments.endIndex...] }
        return arguments[(end + 1)...]
    }

    /// The line as a person types it at the prompt, without the binary's
    /// name: its options, then its names -- with `--` before them only when
    /// one begins with a dash, the one time a person needs it -- and each
    /// word a shell would split or lose quoted: an empty one `""`, one
    /// holding a space or a shell character in single quotes. So a delete
    /// reads `embed delete --yes notes`, and only a dash-led name shows its
    /// `--` (GUI M21).
    public static func shown(_ arguments: [String]) -> String {
        let names = positionals(of: arguments)
        let options = Array(options(of: arguments))
        let end = names.contains { $0.hasPrefix("-") } ? [endOfOptions] : []
        return (options + end + names).map(quoted).joined(separator: " ")
    }

    /// The characters a shell reads as more than the word: whitespace, its
    /// quotes and escapes, expansions, globs and the operators -- zsh's `^`
    /// among them.
    private static let shellCharacters = CharacterSet.whitespacesAndNewlines
        .union(CharacterSet(charactersIn: "'\"\\$`;&|<>()*?[]{}!#~^"))

    /// `word` as a shell takes it back as one word; one that begins with `=`
    /// quoted too, which zsh would expand as a command's path.
    private static func quoted(_ word: String) -> String {
        if word.isEmpty { return "\"\"" }
        guard word.hasPrefix("=") || word.unicodeScalars.contains(where: shellCharacters.contains) else { return word }
        return "'" + word.replacingOccurrences(of: "'", with: "'\\''") + "'"
    }
}
