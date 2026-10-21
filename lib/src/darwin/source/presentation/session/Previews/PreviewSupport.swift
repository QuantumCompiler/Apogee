#if DEBUG
import BusinessSession
import DataDiscovery
import DataProtocol
import Foundation
import InfrastructureProcess

/// Preview support (ADR 0004). Each module keeps its own copy, as each test
/// bundle keeps its own scripted support.

/// A chat that says the binary's own recorded stdout, paced as the binary
/// paces it: up to its `session` event, then -- once a turn is sent -- up to
/// each `question`, the rest once it is answered. Given `silentFrom`, it
/// stops short of the first line of that type, a turn left in flight as a
/// person sees one mid-turn. Given `ending`, the child ends that way once
/// every line is said. Stopped or closed, it ends as a child that went when
/// asked. What is written to it goes nowhere.
actor PreviewChat: ChatSession {
    nonisolated let events: AsyncStream<SessionEvent>
    private let continuation: AsyncStream<SessionEvent>.Continuation
    /// The recorded events not said yet.
    private var unsaid: ArraySlice<ProtocolEvent>
    private let ending: SessionEnd?
    private var turns = 0

    /// How a child that went when asked ends: its input closed, exit 0.
    static let wentWhenAsked = SessionEnd(reason: .finished, exit: ChildExit(reason: .exited, status: 0), stderrTail: BoundedTail())

    init(saying lines: [String], silentFrom type: String? = nil, ending: SessionEnd? = nil) {
        let (events, continuation) = AsyncStream.makeStream(of: SessionEvent.self)
        let said = type.map { type in lines.prefix { JSONValue.parse($0)?["type"]?.stringValue != type } } ?? lines[...]
        var unsaid = said.map(EventDecoder.decode(line:))[...]
        Self.say(&unsaid, to: continuation, ending: ending)
        self.events = events
        self.continuation = continuation
        self.unsaid = unsaid
        self.ending = ending
    }

    /// Says the events up to the next the binary waits after: its `session`,
    /// a `question` -- or, every line said, the `ending` it was given.
    private static func say(_ unsaid: inout ArraySlice<ProtocolEvent>, to continuation: AsyncStream<SessionEvent>.Continuation, ending: SessionEnd?) {
        while let event = unsaid.popFirst() {
            continuation.yield(.event(event))
            switch event.kind {
            case .session, .question: return
            default: continue
            }
        }
        if let ending {
            continuation.yield(.ended(ending))
            continuation.finish()
        }
    }

    func send(user text: String) async throws -> Int {
        turns += 1
        Self.say(&unsaid, to: continuation, ending: ending)
        return turns
    }

    func answer(_ text: String) async throws {
        Self.say(&unsaid, to: continuation, ending: ending)
    }

    func attach(path: String, graph: String?) async throws {}
    func detach(path: String) async throws {}
    func set(_ settings: RunSettings) async throws {}
    func compact() async throws {}
    func cancel() async throws {}

    func close() async {
        continuation.yield(.ended(Self.wentWhenAsked))
        continuation.finish()
    }

    func stop(grace: Duration) async {
        continuation.yield(.ended(Self.wentWhenAsked))
        continuation.finish()
    }
}

/// Where a preview's chats come from: the binary found where the release
/// installs it -- or, given `missing`, none, every place looked passed over --
/// and each chat opened on it saying `lines`, then ending as `ending` says.
struct PreviewChats: ChatStarting {
    var lines: [String] = []
    var silentFrom: String?
    var ending: SessionEnd?
    var missing: [SkippedCandidate]?

    /// The binary as the tests' scripted starter finds it
    /// (tests/presentation/session/Scripted.swift).
    static let found = FoundBinary(
        url: previewBinary, source: .release, version: "0.1.9",
        versionLine: "apogee 0.1.9 (7072bdf, 2026-10-19, macos-arm64)")

    func locateBinary() async -> DiscoveryOutcome {
        missing.map { .notFound(skipped: $0) } ?? .found(Self.found, skipped: [])
    }

    func openChat(binary: URL, launch: ChatLaunch) -> any ChatSession {
        PreviewChat(saying: lines, silentFrom: silentFrom, ending: ending)
    }
}

/// Where a preview says the binary is; never run.
let previewBinary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
#endif
