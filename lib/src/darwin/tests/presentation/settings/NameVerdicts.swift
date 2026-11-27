import BusinessReads
import Foundation

/// The binary's verdicts on the app's lines with a dash-led name (GUI M23),
/// recorded from the built CLI over a sandbox by
/// `scripts/refresh-argv-name-verdicts.sh` into
/// `tests/presentation/shell/Fixtures/argv-name-verdicts.txt`: each row a
/// verdict -- `help`, or `exit <status>: <first line>` -- a tab, and the
/// line's words joined by a space, an empty word written `""`. (Each bundle
/// keeps its own support.)
enum NameVerdicts {
    /// The names every place's lines are swept with: each one the CLI's
    /// parser reads as an option before `--`.
    static let names = ["-x", "-h", "--graph"]

    /// Each recorded line, by its row's words, and its verdict.
    static let recorded: [String: String] = {
        let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            .appendingPathComponent("../shell/Fixtures/argv-name-verdicts.txt")
        var rows: [String: String] = [:]
        for row in ((try? String(contentsOf: url, encoding: .utf8)) ?? "").split(separator: "\n") {
            let parts = row.split(separator: "\t", maxSplits: 1).map(String.init)
            if parts.count == 2 { rows[parts[1]] = parts[0] }
        }
        return rows
    }()

    /// `line` as a row writes it.
    static func row(_ line: [String]) -> String {
        line.map { $0.isEmpty ? "\"\"" : $0 }.joined(separator: " ")
    }

    /// The binary's verdict on `line`; `nil` when it was never recorded.
    static func verdict(_ line: [String]) -> String? {
        recorded[row(line)]
    }

    /// Whether a verdict is the parser's, before the verb ran: the verb's
    /// help, or one of CLI11's exits (100-115: 106 `… is required`, 109 `…
    /// not expected`, 114 a value it would not take among them) -- the verbs'
    /// own exits are 0, 1 and 2.
    static func refusedByTheParse(_ verdict: String) -> Bool {
        if verdict == "help" { return true }
        guard verdict.hasPrefix("exit "), let status = Int(verdict.dropFirst(5).prefix { $0.isNumber }) else { return false }
        return (100...115).contains(status)
    }

    /// What a scripted runner's log holds -- each call's words joined by
    /// `separator` -- as the lines the binary runs: a read with the
    /// `--output-format json` `ReadRunner` adds before its `--`, a command as
    /// it is.
    static func lines(ofCalls calls: [String], separator: Character = "|") -> [[String]] {
        calls.compactMap { call in
            for (verb, reads) in [("read ", true), ("run ", false)] where call.hasPrefix(verb) {
                let words = call.dropFirst(verb.count).split(separator: separator, omittingEmptySubsequences: false).map(String.init)
                return reads ? Argv.adding(["--output-format", "json"], to: words) : words
            }
            return nil
        }
    }

    /// Each line holding one of `names`, judged: those never recorded, as the
    /// rows to add, and those the parser refuses.
    static func judged(_ lines: [[String]]) -> (missing: [String], refused: [String]) {
        var missing: [String] = []
        var refused: [String] = []
        for line in Set(lines.filter { $0.contains(where: names.contains) }).sorted(by: { row($0) < row($1) }) {
            if line.contains(where: { $0.contains(where: \.isWhitespace) }) {
                refused.append(row(line) + " -- a swept line's words hold no space, so the refresh script reads it back")
                continue
            }
            switch verdict(line) {
            case nil: missing.append("?\t" + row(line))
            case let verdict? where refusedByTheParse(verdict): refused.append(row(line) + " -- " + verdict)
            default: continue
            }
        }
        return (missing, refused)
    }
}
