#if DEBUG
import BusinessReads
import BusinessSession
import DataDiscovery
import DataProtocol
import Foundation

/// Preview support (ADR 0004). Each module keeps its own copy, as each test
/// bundle keeps its own scripted support.

/// Answers a preview's reads from the binary's own recorded documents and its
/// commands as done: nothing starts the binary or touches the person's files.
struct PreviewRunner: CommandRunning {
    /// Argument prefixes and what they answer; the longest prefix wins.
    let reads: [([String], String)]
    let commands: [([String], String)]

    init(reads: [([String], String)], commands: [([String], String)] = []) {
        self.reads = reads
        self.commands = commands
    }

    func read(binary: URL, _ arguments: [String]) async -> ReadOutcome {
        guard let text = Self.answer(arguments, reads), let document = ReadDocument(text: text) else {
            return .couldNotStart("no preview document for apogee \(arguments.joined(separator: " "))")
        }
        return .document(document)
    }

    func command(binary: URL, _ arguments: [String]) async -> CommandOutcome {
        .done(stdout: Self.answer(arguments, commands) ?? "")
    }

    static func answer(_ arguments: [String], _ rules: [([String], String)]) -> String? {
        rules.filter { arguments.starts(with: $0.0) }.max { $0.0.count < $1.0.count }?.1
    }
}

/// A chat that says the binary's own recorded stdout, paced as the binary
/// paces it: up to its `session` event, then -- once a turn is sent -- up to
/// each `question`, the rest once it is answered. Given `silentFrom`, it
/// stops short of the first line of that type, a turn left in flight as a
/// person sees one mid-turn. `byTurn`, each turn is said only up to its
/// `result`, and what the binary said between turns to a `set` line -- its
/// notices -- only once a `set` is written (GUI M10's switch). What is
/// written to it goes nowhere.
actor PreviewChat: ChatSession {
    nonisolated let events: AsyncStream<SessionEvent>
    private let continuation: AsyncStream<SessionEvent>.Continuation
    /// The recorded events not said yet.
    private var unsaid: ArraySlice<ProtocolEvent>
    private let byTurn: Bool
    private var turns = 0

    init(saying lines: [String], silentFrom type: String? = nil, byTurn: Bool = false) {
        let (events, continuation) = AsyncStream.makeStream(of: SessionEvent.self)
        let said = type.map { type in lines.prefix { JSONValue.parse($0)?["type"]?.stringValue != type } } ?? lines[...]
        var unsaid = said.map(EventDecoder.decode(line:))[...]
        Self.say(&unsaid, to: continuation, byTurn: byTurn)
        self.events = events
        self.continuation = continuation
        self.unsaid = unsaid
        self.byTurn = byTurn
    }

    /// Says the events up to the next the binary waits after: its `session`,
    /// a `question` -- `byTurn`, a `result`.
    private static func say(_ unsaid: inout ArraySlice<ProtocolEvent>, to continuation: AsyncStream<SessionEvent>.Continuation, byTurn: Bool) {
        while let event = unsaid.popFirst() {
            continuation.yield(.event(event))
            switch event.kind {
            case .session, .question: return
            case .result where byTurn: return
            default: continue
            }
        }
    }

    func send(user text: String) async throws -> Int {
        turns += 1
        Self.say(&unsaid, to: continuation, byTurn: byTurn)
        return turns
    }

    func answer(_ text: String) async throws {
        Self.say(&unsaid, to: continuation, byTurn: byTurn)
    }

    func attach(path: String, graph: String?) async throws {}
    func detach(path: String) async throws {}

    /// `byTurn`, the notices the binary answered the recorded `set` with --
    /// said between turns, so carrying none.
    func set(_ settings: RunSettings) async throws {
        guard byTurn else { return }
        while let next = unsaid.first, next.turn == nil, case .notice = next.kind {
            continuation.yield(.event(next))
            unsaid.removeFirst()
        }
    }

    func compact() async throws {}
    func cancel() async throws {}

    func close() async {
        continuation.finish()
    }

    /// Stopped, its stream ends -- with no `ended` event: a `SessionEnd`
    /// carries InfrastructureProcess's types, which this module does not
    /// link, so no preview session is ever ended or paused.
    func stop(grace: Duration) async {
        continuation.finish()
    }
}

/// Where a preview's chats come from: the binary found where the release
/// installs it -- or, given `missing`, none, every place looked passed over --
/// and each chat opened on it saying `lines`.
struct PreviewChats: ChatStarting {
    var lines: [String] = []
    var silentFrom: String?
    var missing: [SkippedCandidate]?
    /// Each turn said up to its `result`, and a `set` answered (`PreviewChat`).
    var byTurn = false

    /// The binary as the tests' scripted starter finds it
    /// (tests/presentation/sessions/Scripted.swift).
    static let found = FoundBinary(
        url: previewBinary, source: .release, version: "0.1.9",
        versionLine: "apogee 0.1.9 (7072bdf, 2026-10-19, macos-arm64)")

    func locateBinary() async -> DiscoveryOutcome {
        missing.map { .notFound(skipped: $0) } ?? .found(Self.found, skipped: [])
    }

    func openChat(binary: URL, launch: ChatLaunch) -> any ChatSession {
        PreviewChat(saying: lines, silentFrom: silentFrom, byTurn: byTurn)
    }
}

/// Where a preview says the binary is; never run.
let previewBinary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
#endif
