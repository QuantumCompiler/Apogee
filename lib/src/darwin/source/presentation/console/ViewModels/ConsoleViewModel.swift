import BusinessJobs
import BusinessReads
import Foundation
import InfrastructureProcess
import Observation
import PresentationShell

/// The console: any `apogee` command run as typed -- the parity law's
/// catch-all (ADR 0003) while screens accrete, as the CLI's exec line was the
/// shell's.
///
/// SwiftUI-free (ADR 0002). A line is split as the CLI's exec line splits it
/// (`cli/line_tokens`, GUI 6e), and run with every word it passes. A run is
/// the shipped jobs core's: its stdout
/// streamed as the supervisor reads it, the busy line its stderr's last line,
/// the stderr kept at the end, Stop as Ctrl-C. Completion is the binary's own
/// (`apogee __complete`), never a list the app carries, and it edits the line
/// as it was typed -- only while it is the line it was asked about (GUI 6e).
/// A command the parity table refuses -- a carve-out, a door the app already
/// is, an `$EDITOR` verb -- is refused with the table's reason and never
/// started, whatever root options precede it: the command is read past them
/// as the binary reads it (GUI 4g).
@MainActor
@Observable
public final class ConsoleViewModel {
    /// One line of the transcript.
    public struct Line: Identifiable, Equatable, Sendable {
        public enum Kind: Equatable, Sendable {
            /// The command as it was run.
            case command
            /// What the command wrote on stdout.
            case out
            /// What it wrote on stderr, kept when it ended.
            case err
            /// The console's own word: how it ended, why it was refused.
            case note
            /// A refusal or a failure.
            case problem
        }

        public let id: Int
        public let kind: Kind
        public let text: String
    }

    public var input = ""
    public private(set) var lines: [Line] = []
    /// The binary's candidates for the word being typed.
    public private(set) var suggestions: [String] = []
    /// The command line running now, and its busy line.
    public private(set) var running: String?
    public private(set) var busy: String?
    /// The lines run, newest last, for the arrow keys.
    public private(set) var history: [String] = []

    private let runner: any CommandRunning
    private let starter: any JobStarting
    private var binary: URL?
    private var job: (any JobRunning)?
    private var next = 0

    public init(runner: any CommandRunning, starter: any JobStarting) {
        self.runner = runner
        self.starter = starter
    }

    public func connect(binary: URL?) {
        self.binary = binary
    }

    public var isRunning: Bool { job != nil }

    // MARK: - The line

    /// A line split as the CLI's exec line splits it, and where the word
    /// being typed starts in the line as it was typed.
    public struct Split: Equatable, Sendable {
        /// The words before the one being typed.
        public let words: [String]
        /// The word being typed, as the binary is given it: empty when the
        /// line ends in a blank, or when it is an empty `''` or `""`.
        public let partial: String
        /// How many Unicode scalars of the line come before the word being
        /// typed -- its opening quote or backslash counted -- so that
        /// completion replaces what was typed: the line's whole length when
        /// it ends in a blank, so less than it whenever a last word was
        /// begun, an empty quoted one too.
        public let partialStart: Int
    }

    /// Why a line does not split, in the CLI's own words (`line_tokens.cpp`).
    public enum Unsplittable: Error, Equatable, Sendable {
        /// A `'` with no `'` after it.
        case singleQuoteOpen
        /// A `"` with no unescaped `"` after it.
        case doubleQuoteOpen
        /// A backslash outside quotes as the line's last character.
        case backslashEndsTheLine

        /// The CLI splitter's error, word for word, as the exec line says
        /// it after "not run: ".
        public var reason: String {
            switch self {
            case .singleQuoteOpen: "a single quote is left open"
            case .doubleQuoteOpen: "a double quote is left open"
            case .backslashEndsTheLine: "a backslash ends the line with nothing to escape"
            }
        }
    }

    /// A line split as the CLI's one splitter splits it (`cli/line_tokens`,
    /// which the exec line runs) -- the CLI's blanks between words, quotes
    /// grouping, a backslash escaping as POSIX shells have it, nothing
    /// expanded -- with the word being typed apart: empty when the line ends
    /// in a blank. `nil` for a line that does not split. The quoting table
    /// is `split`'s.
    public static func words(_ line: String) -> (words: [String], partial: String)? {
        split(line).map { ($0.words, $0.partial) }
    }

    /// The CLI's blanks, and only these (`line_tokens.cpp`'s `blank`): a
    /// space, a tab, a line feed and a carriage return. A no-break space
    /// (⌥Space) or any other Unicode space is a character of its word, as
    /// at the exec line.
    static func isBlank(_ scalar: Unicode.Scalar) -> Bool {
        scalar == " " || scalar == "\t" || scalar == "\n" || scalar == "\r"
    }

    /// What a backslash escapes inside double quotes: these four alone, as
    /// in POSIX shells and the CLI's splitter (`line_tokens.cpp`). Before
    /// any other character the backslash is kept.
    static let escapedInDoubleQuotes: Set<Unicode.Scalar> = ["\"", "\\", "$", "`"]

    /// The line split as `words` splits it, with the typed start of the word
    /// being typed. `nil` for an unclosed quote, or a backslash that ends the
    /// line with nothing to escape: `tokens` says which.
    public static func split(_ line: String) -> Split? {
        try? tokens(line).get()
    }

    /// The line split as the CLI's splitter splits it, or why it does not
    /// split, in the CLI's words.
    ///
    /// The quoting table is the CLI's, exactly (GUI 6e): inside double
    /// quotes a backslash escapes only `"`, `\`, `$` and a backtick, and
    /// before anything else -- `\d`, `C:\temp` -- it is an ordinary
    /// character of the word; inside single quotes nothing escapes; outside
    /// quotes a backslash takes the next character as it is; `''` and `""`
    /// are an empty word. The line is read by its Unicode scalars: the CLI
    /// reads its bytes, and every character it treats specially is ASCII,
    /// which in UTF-8 is never part of another character -- where Swift's
    /// characters would merge CR LF, or a combining mark, into the blank,
    /// quote or backslash before it.
    public static func tokens(_ line: String) -> Result<Split, Unsplittable> {
        let scalars = Array(line.unicodeScalars)
        var words: [String] = []
        var word = String.UnicodeScalarView()
        var inWord = false
        var start = 0
        var quote: Unicode.Scalar?
        var at = 0
        while at < scalars.count {
            let scalar = scalars[at]
            if quote == "'" {
                if scalar == "'" { quote = nil } else { word.append(scalar) }
            } else if quote == "\"" {
                if scalar == "\"" {
                    quote = nil
                } else if scalar == "\\", at + 1 < scalars.count, escapedInDoubleQuotes.contains(scalars[at + 1]) {
                    word.append(scalars[at + 1])
                    at += 1
                } else {
                    word.append(scalar)
                }
            } else if scalar == "\\" {
                guard at + 1 < scalars.count else { return .failure(.backslashEndsTheLine) }
                if !inWord { start = at }
                word.append(scalars[at + 1])
                inWord = true
                at += 1
            } else if scalar == "\"" || scalar == "'" {
                if !inWord { start = at }
                quote = scalar
                inWord = true
            } else if isBlank(scalar) {
                if inWord { words.append(String(word)) }
                word = String.UnicodeScalarView()
                inWord = false
            } else {
                if !inWord { start = at }
                word.append(scalar)
                inWord = true
            }
            at += 1
        }
        if let quote { return .failure(quote == "'" ? .singleQuoteOpen : .doubleQuoteOpen) }
        return .success(Split(words: words, partial: inWord ? String(word) : "", partialStart: inWord ? start : scalars.count))
    }

    /// The arguments a line runs: every word the exec line would pass --
    /// the last one too whenever it was begun, an empty `''` or `""` among
    /// them (`say ''` runs `say` with an empty argument) -- without a
    /// leading `apogee`.
    public static func arguments(_ line: String) -> [String]? {
        try? runs(line).get()
    }

    /// `arguments`, or why the line does not split.
    static func runs(_ line: String) -> Result<[String], Unsplittable> {
        tokens(line).map { split in
            let begun = split.partialStart < line.unicodeScalars.count
            var all = split.words + (begun ? [split.partial] : [])
            if all.first == "apogee" { all.removeFirst() }
            return all
        }
    }

    /// The line as the console keeps and shows it: the CLI's blanks trimmed
    /// from both ends -- never one a backslash made part of the last word,
    /// so `say end\ ` is kept, and recalled, as it runs.
    static func trimmed(_ line: String) -> String {
        let runs = arguments(line)
        var scalars = Array(line.unicodeScalars.drop(while: isBlank))
        while let last = scalars.last, isBlank(last),
              arguments(String(String.UnicodeScalarView(scalars.dropLast()))) == runs {
            scalars.removeLast()
        }
        return String(String.UnicodeScalarView(scalars))
    }

    /// The root options that take the word after them as their value
    /// (`apogee --help`): `--config PATH` and `--custom PATH` -- unless
    /// written `--config=PATH`, one word.
    static let valuedRootOptions: Set<String> = ["--config", "--custom"]

    /// The command's own words: `arguments` past the root options, as the
    /// binary reads them. Before its command the binary takes only root
    /// options -- `--release`, `--dev`, `--test`, `-h`, `-V` and the valued
    /// two; after the command it refuses them -- and it still parses a
    /// command that follows `--`, so every word starting with a dash is
    /// passed over, with a valued option's value. The CLI's exec line reads
    /// a line the same way before it refuses (`tui_runner.cpp`'s
    /// `first_verb`).
    public static func commandPath(_ arguments: [String]) -> ArraySlice<String> {
        var at = 0
        while at < arguments.count, arguments[at].hasPrefix("-") {
            at += valuedRootOptions.contains(arguments[at]) ? 2 : 1
        }
        return arguments[min(at, arguments.count)...]
    }

    /// What a bare `apogee` is told: at a terminal it opens the full-screen
    /// shell, and anywhere else it prints its help -- the console asks for a
    /// command instead.
    public static let nothingToRun = "Nothing to run: type a command after apogee -- Tab offers them, and --help lists them all."

    // MARK: - Completion

    /// Asks the binary what may come next: `apogee __complete <words> <partial>`,
    /// one bounded run. One candidate completes the word; several are offered.
    /// An answer applies only to the line it was asked about (GUI 6e): one
    /// that lands after the person typed more, or after a later Tab changed
    /// the line, is dropped -- the line and the offered candidates left as
    /// they stand. Two Tabs on the same line get the same answer, so
    /// whichever lands is right.
    public func complete() async {
        let asked = input
        guard let binary, let split = Self.words(asked) else {
            suggestions = []
            return
        }
        var words = split.words
        if words.first == "apogee" { words.removeFirst() }
        let outcome = await runner.command(binary: binary, ["__complete"] + words + [split.partial])
        guard input == asked else { return }
        guard case .done(let stdout) = outcome else {
            suggestions = []
            return
        }
        let candidates = stdout.split(whereSeparator: \.isNewline).map(String.init).filter { !$0.isEmpty }
        if candidates.count == 1 {
            accept(candidates[0])
        } else {
            suggestions = candidates
        }
    }

    /// Puts a candidate in place of the word being typed -- all of it as it
    /// was typed, its quotes and backslashes too -- quoted as the line splits
    /// it back.
    public func accept(_ candidate: String) {
        guard let split = Self.split(input) else { return }
        let kept = String.UnicodeScalarView(input.unicodeScalars.prefix(split.partialStart))
        input = String(kept) + Self.quoted(candidate) + " "
        suggestions = []
    }

    /// A word as the line splits it back into that word: bare when it holds
    /// none of the CLI's blanks, no quote and no backslash, else in double
    /// quotes with any `"` or `\` in it escaped -- scalar by scalar, as the
    /// line is split.
    static func quoted(_ word: String) -> String {
        let special: (Unicode.Scalar) -> Bool = { isBlank($0) || $0 == "\"" || $0 == "'" || $0 == "\\" }
        if !word.isEmpty && !word.unicodeScalars.contains(where: special) { return word }
        var escaped = String.UnicodeScalarView()
        for scalar in word.unicodeScalars {
            if scalar == "\"" || scalar == "\\" { escaped.append("\\") }
            escaped.append(scalar)
        }
        return "\"" + String(escaped) + "\""
    }

    public func dismissSuggestions() {
        suggestions = []
    }

    // MARK: - Running

    /// Runs the line as typed, split as typed. A line that does not split
    /// says why in the CLI's words; a command the parity table refuses is
    /// refused with its reason, read past any root option; a bare `apogee`
    /// asks for a command; nothing runs while a run is live.
    public func run() {
        let line = Self.trimmed(input)
        guard !line.isEmpty, job == nil else { return }
        let arguments: [String]
        switch Self.runs(input) {
        case .success(let words):
            arguments = words
        case .failure(let unsplittable):
            let reason = unsplittable.reason
            append(.problem, reason.prefix(1).uppercased() + reason.dropFirst() + ".")
            return
        }
        guard !arguments.isEmpty else {
            append(.note, Self.nothingToRun)
            return
        }
        input = ""
        suggestions = []
        history.append(line)
        append(.command, "apogee " + line.replacingOccurrences(of: #"^apogee[ \t\n\r]+"#, with: "", options: .regularExpression))
        if let refusal = ParityTable.refusal(of: Self.commandPath(arguments)) {
            append(.problem, "Not run here: \(refusal.reason).")
            return
        }
        guard let binary else {
            append(.problem, "Apogee isn't installed, so there is nothing to run.")
            return
        }
        let job = starter.startJob(binary: binary, arguments: arguments, streaming: false, folder: nil)
        self.job = job
        running = line
        busy = nil
        Task {
            var end: JobEnd?
            for await update in job.updates {
                switch update {
                case .said(let text): append(.out, text)
                case .busy(let text): busy = text
                case .event: break
                case .ended(let ended): end = ended
                }
            }
            self.job = nil
            running = nil
            busy = nil
            if let end { finish(end) }
        }
    }

    /// Stops the run as Ctrl-C does.
    public func stop() async {
        await job?.stop(grace: .seconds(10))
    }

    public func clear() {
        lines = []
    }

    /// The line run `back` runs ago, for the arrow keys.
    public func recall(_ back: Int) -> String? {
        guard back > 0, back <= history.count else { return nil }
        return history[history.count - back]
    }

    private func finish(_ end: JobEnd) {
        for line in end.stderrTail.text.split(whereSeparator: \.isNewline) where !line.isEmpty {
            append(.err, String(line))
        }
        if let detail = end.detail {
            append(.problem, "Didn't start: \(detail)")
        } else if end.interrupted {
            append(.note, "Stopped.")
        } else if let exit = end.exit {
            append(exit.succeeded ? .note : .problem, Self.ending(exit))
        }
    }

    /// How a run ended, said plainly.
    public static func ending(_ exit: ChildExit) -> String {
        switch exit.reason {
        case .exited: exit.status == 0 ? "Done." : "Exited with status \(exit.status)."
        case .signalled: "Ended by signal \(exit.status)."
        }
    }

    private func append(_ kind: Line.Kind, _ text: String) {
        lines.append(Line(id: next, kind: kind, text: text))
        next += 1
    }
}
