import Foundation

/// A symphony `/play` can name, as `symphonies list` says it.
public struct PlayChoice: Equatable, Sendable, Identifiable {
    public let name: String
    public let description: String
    public var id: String { name }

    public init(name: String, description: String) {
        self.name = name
        self.description = description
    }
}

/// A symphony played in a turn, as the session's own lines say it
/// (machine-mode.md -> Playing a symphony): a `/play`'s stages, or the
/// model's own choice and then its stages. Nothing new crosses the wire --
/// these are `tool_status` lines, each kept as the CLI wrote it and read here
/// only to say which stage the play is on.
public struct PlayNarration: Equatable, Sendable {
    /// One stage as its line said it: `stage 1/2 first — asking utility
    /// (helper): One: hello`, or a chain's `digest → summarize-verify, stage
    /// 1/2 summarize — …`.
    public struct Stage: Equatable, Sendable {
        public let index: Int
        public let count: Int
        public let name: String
        public let role: String
        public let backend: String
        /// Where a stage of a symphony another plays sits: `digest →
        /// summarize-verify`.
        public let chain: String?
        /// The line, whole.
        public let line: String
    }

    /// The symphony, as the model's choice or the typed `/play` named it.
    public let symphony: String?
    /// The model chose it (`--orchestrate`): `play — the model chose echo2:
    /// 2 member calls, utility → chat`, whole.
    public let chosen: String?
    public let stages: [Stage]

    /// The play `lines` narrate, for a turn typed as `typed`; `nil` when they
    /// narrate none.
    public init?(lines: [String], typed: String?) {
        var chosen: String?
        var symphony: String?
        var stages: [Stage] = []
        for line in lines {
            if let match = line.firstMatch(of: /^play — the model chose (\S+?):/) {
                chosen = line
                symphony = String(match.1)
            } else if let stage = Self.stage(line) {
                stages.append(stage)
            }
        }
        guard chosen != nil || !stages.isEmpty else { return nil }
        if symphony == nil, let typed, let match = typed.firstMatch(of: /^\/play\s+(\S+)/) {
            symphony = String(match.1)
        }
        self.symphony = symphony
        self.chosen = chosen
        self.stages = stages
    }

    /// The stage playing now, or played last.
    public var current: Stage? { stages.last }

    /// What the play is doing, in a few words: `playing echo2 · stage 2 of 2`.
    public func summary(live: Bool) -> String {
        let name = symphony.map { " \($0)" } ?? ""
        guard let current else { return (live ? "playing" : "played") + name }
        if live { return "playing\(name) · stage \(current.index) of \(current.count)" }
        let count = stages.count
        return "played\(name) · \(count) \(count == 1 ? "stage" : "stages")"
    }

    static func stage(_ line: String) -> Stage? {
        guard let match = line.firstMatch(of: /^(?:(.+), )?stage (\d+)\/(\d+) (\S+) — asking (\S+) \(([^)]*)\)/),
            let index = Int(match.2), let count = Int(match.3)
        else { return nil }
        return Stage(
            index: index, count: count, name: String(match.4), role: String(match.5), backend: String(match.6),
            chain: match.1.map(String.init), line: line)
    }
}
