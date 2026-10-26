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
/// SwiftUI-free (ADR 0002). A run is the shipped jobs core's: its stdout
/// streamed as the supervisor reads it, the busy line its stderr's last line,
/// the stderr kept at the end, Stop as Ctrl-C. Completion is the binary's own
/// (`apogee __complete`), never a list the app carries, and it edits the line
/// as it was typed. A command the parity table refuses -- a carve-out, a door
/// the app already is, an `$EDITOR` verb -- is refused with the table's
/// reason and never started, whatever root options precede it: the command
/// is read past them as the binary reads it (GUI 4g).
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

    /// A line split as a shell splits it, and where the word being typed
    /// starts in the line as it was typed.
    public struct Split: Equatable, Sendable {
        /// The words before the one being typed.
        public let words: [String]
        /// The word being typed, as the binary is given it: empty when the
        /// line ends in a space.
        public let partial: String
        /// How many characters of the line come before the word being typed
        /// -- its opening quote or backslash counted -- so that completion
        /// replaces what was typed: the line's whole length when it ends in a
        /// space.
        public let partialStart: Int
    }

    /// A line split as a shell splits it -- whitespace between words, quotes
    /// grouping, a backslash escaping -- with the word being typed apart:
    /// empty when the line ends in a space. `nil` for an unclosed quote.
    public static func words(_ line: String) -> (words: [String], partial: String)? {
        split(line).map { ($0.words, $0.partial) }
    }

    /// The line split as `words` splits it, with the typed start of the word
    /// being typed. `nil` for an unclosed quote.
    public static func split(_ line: String) -> Split? {
        var words: [String] = []
        var word = ""
        var inWord = false
        var start = 0
        var quote: Character?
        var escaped = false
        for (offset, character) in line.enumerated() {
            if escaped {
                word.append(character)
                escaped = false
            } else if character == "\\" && quote != "'" {
                if !inWord { start = offset }
                escaped = true
                inWord = true
            } else if let open = quote {
                if character == open { quote = nil } else { word.append(character) }
            } else if character == "\"" || character == "'" {
                if !inWord { start = offset }
                quote = character
                inWord = true
            } else if character.isWhitespace {
                if inWord { words.append(word) }
                word = ""
                inWord = false
            } else {
                if !inWord { start = offset }
                word.append(character)
                inWord = true
            }
        }
        guard quote == nil, !escaped else { return nil }
        return Split(words: words, partial: inWord ? word : "", partialStart: inWord ? start : line.count)
    }

    /// The arguments a line runs: its words, without a leading `apogee`.
    public static func arguments(_ line: String) -> [String]? {
        guard let split = words(line) else { return nil }
        var all = split.words + (split.partial.isEmpty ? [] : [split.partial])
        if all.first == "apogee" { all.removeFirst() }
        return all
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
    public func complete() async {
        guard let binary, let split = Self.words(input) else {
            suggestions = []
            return
        }
        var words = split.words
        if words.first == "apogee" { words.removeFirst() }
        let outcome = await runner.command(binary: binary, ["__complete"] + words + [split.partial])
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
        input = String(input.prefix(split.partialStart)) + Self.quoted(candidate) + " "
        suggestions = []
    }

    /// A word as the line splits it back into that word: bare when it holds
    /// no blank, quote or backslash, else in double quotes with any `"` or
    /// `\` in it escaped.
    static func quoted(_ word: String) -> String {
        let special: (Character) -> Bool = { $0.isWhitespace || $0 == "\"" || $0 == "'" || $0 == "\\" }
        if !word.isEmpty && !word.contains(where: special) { return word }
        let escaped = word.replacingOccurrences(of: "\\", with: "\\\\").replacingOccurrences(of: "\"", with: "\\\"")
        return "\"" + escaped + "\""
    }

    public func dismissSuggestions() {
        suggestions = []
    }

    // MARK: - Running

    /// Runs the line as typed. A command the parity table refuses is refused
    /// with its reason, read past any root option; a bare `apogee` asks for a
    /// command; nothing runs while a run is live.
    public func run() {
        let line = input.trimmingCharacters(in: .whitespaces)
        guard !line.isEmpty, job == nil else { return }
        guard let arguments = Self.arguments(line) else {
            append(.problem, "A quote is left open.")
            return
        }
        guard !arguments.isEmpty else {
            append(.note, Self.nothingToRun)
            return
        }
        input = ""
        suggestions = []
        history.append(line)
        append(.command, "apogee " + line.replacingOccurrences(of: #"^apogee\s+"#, with: "", options: .regularExpression))
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
