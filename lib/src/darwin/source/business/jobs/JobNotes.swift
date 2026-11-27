import Foundation
import InfrastructureProcess

/// A finished run's notes: what the CLI said a person must know about a run
/// that still exited 0, on stderr, in its house voice -- `apogee embed:
/// could not register 'my notes' in config -- …`, `apogee graph: could not
/// set graph.enabled …`, `apogee knowledge: validated by …` -- kept word for
/// word and in order (GUI M22).
///
/// The CLI's never-fatal design says these on stderr and keeps the exit at
/// 0, so neither the exit nor stdout carries them. A note is told from the
/// rest of stderr by its shape alone, never its words: the same note reads
/// differently in two homes. The shape is the CLI's own prefix, a string
/// literal at each site (`embed.cpp`, `graph.cpp`, `knowledge.cpp`),
/// documented nowhere -- the app already decides on it where an install's
/// line is read (`RuntimeInstall`'s `apogee ` lines) and where a record
/// store that never captured is told apart (`RecordsViewModel.neverCaptured`).
/// The verb-less `apogee: …` line is not one: it rides every run in a YAML
/// home, and Settings › Health shows it. Neither is the `[graph] …`
/// narration, which is progress.
public struct JobNotes: Sendable, Equatable {
    /// How many notes are kept; the rest are counted (`more`).
    public static let limit = 20

    /// The notes, the first `limit` of them, in order, as printed.
    public private(set) var lines: [String] = []

    /// How many more notes the run said past the first `limit`.
    public private(set) var more = 0

    /// None.
    public init() {}

    /// The notes among `lines`, in order: every line of a run's stderr as
    /// the spawner framed it.
    public init(lines: some Sequence<String>) {
        for line in lines { take(line) }
    }

    /// The notes in a run's stderr tail -- the route of a child that framed
    /// no lines. A tail that was cut starts wherever the cut fell, so its
    /// first line is skipped: a fragment is never a note.
    public init(in tail: BoundedTail) {
        var lines = tail.text.split(separator: "\n", omittingEmptySubsequences: false)
        if tail.cut, !lines.isEmpty { lines.removeFirst() }
        self.init(lines: lines.map { $0.last == "\r" ? String($0.dropLast()) : String($0) })
    }

    /// `line` kept when it is a note.
    public mutating func take(_ line: String) {
        guard Self.isNote(line) else { return }
        if lines.count < Self.limit {
            lines.append(line)
        } else {
            more += 1
        }
    }

    /// Whether the run said none.
    public var isEmpty: Bool { lines.isEmpty }

    /// What a card shows: every note kept, as printed, and -- past the
    /// limit -- one line counting the rest, in the app's words.
    public var shown: [String] {
        more == 0 ? lines : lines + ["… and \(more) more note\(more == 1 ? "" : "s")"]
    }

    /// Whether `line` is a note: `apogee`, then one or more lowercase words
    /// (a letter first, then letters, digits, `-` or `_`), each after one
    /// space, then `: `. `apogee embed: …`, `apogee mcp serve: …` and `apogee
    /// graph: warning: …` are; `apogee: …` and `[graph] …` are not.
    public static func isNote(_ line: String) -> Bool {
        guard line.hasPrefix("apogee ") else { return false }
        var rest = line.utf8.dropFirst("apogee ".utf8.count)
        while true {
            guard let first = rest.first, first >= UInt8(ascii: "a"), first <= UInt8(ascii: "z") else { return false }
            let word = rest.prefix { byte in
                (UInt8(ascii: "a")...UInt8(ascii: "z")).contains(byte) || (UInt8(ascii: "0")...UInt8(ascii: "9")).contains(byte)
                    || byte == UInt8(ascii: "-") || byte == UInt8(ascii: "_")
            }
            rest = rest.dropFirst(word.count)
            if rest.starts(with: ": ".utf8) { return true }
            guard rest.first == UInt8(ascii: " ") else { return false }
            rest = rest.dropFirst()
        }
    }
}
