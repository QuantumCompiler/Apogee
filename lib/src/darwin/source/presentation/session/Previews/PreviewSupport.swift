#if DEBUG
import BusinessReads
import BusinessSession
import DataDiscovery
import DataProtocol
import Foundation
import InfrastructureProcess

/// Preview support (ADR 0004). Each module keeps its own copy, as each test
/// bundle keeps its own scripted support.

/// A chat that says the binary's own recorded stdout, paced as the binary
/// paces it: up to its `session` event and the notices it says at launch,
/// then -- once a turn is sent -- up to each `question`, the rest once it is
/// answered. Given `silentFrom`, it stops short of the first line of that
/// type, a turn left in flight as a person sees one mid-turn. Given
/// `ending`, the child ends that way once every line is said. `byTurn`, each
/// turn is said only up to its `result`, and what the binary said between
/// turns to a `set` line -- its notices -- only once a `set` is written (GUI
/// M10's switch). Stopped or closed, it ends as a child that went when
/// asked. What is written to it goes nowhere.
actor PreviewChat: ChatSession {
    nonisolated let events: AsyncStream<SessionEvent>
    private let continuation: AsyncStream<SessionEvent>.Continuation
    /// The recorded events not said yet.
    private var unsaid: ArraySlice<ProtocolEvent>
    private let ending: SessionEnd?
    private let byTurn: Bool
    private var turns = 0

    /// How a child that went when asked ends: its input closed, exit 0.
    static let wentWhenAsked = SessionEnd(reason: .finished, exit: ChildExit(reason: .exited, status: 0), stderrTail: BoundedTail())

    init(saying lines: [String], silentFrom type: String? = nil, ending: SessionEnd? = nil, byTurn: Bool = false) {
        let (events, continuation) = AsyncStream.makeStream(of: SessionEvent.self)
        let said = type.map { type in lines.prefix { JSONValue.parse($0)?["type"]?.stringValue != type } } ?? lines[...]
        var unsaid = said.map(EventDecoder.decode(line:))[...]
        Self.say(&unsaid, to: continuation, ending: ending, byTurn: byTurn)
        self.events = events
        self.continuation = continuation
        self.unsaid = unsaid
        self.ending = ending
        self.byTurn = byTurn
    }

    /// Says the events up to the next the binary waits after: its `session`,
    /// a `question` -- `byTurn`, a `result` -- or, every line said, the
    /// `ending` it was given.
    private static func say(
        _ unsaid: inout ArraySlice<ProtocolEvent>, to continuation: AsyncStream<SessionEvent>.Continuation, ending: SessionEnd?,
        byTurn: Bool
    ) {
        while let event = unsaid.popFirst() {
            continuation.yield(.event(event))
            switch event.kind {
            case .session:
                // What the binary says at launch -- a resume's notes, a
                // suite's admission line (the CLI's M29) -- follows its
                // `session` at once, before anything it reads.
                while let next = unsaid.first, next.turn == nil, case .notice = next.kind {
                    continuation.yield(.event(next))
                    unsaid.removeFirst()
                }
                return
            case .question: return
            case .result where byTurn && !unsaid.isEmpty: return
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
        Self.say(&unsaid, to: continuation, ending: ending, byTurn: byTurn)
        return turns
    }

    func answer(_ text: String) async throws {
        Self.say(&unsaid, to: continuation, ending: ending, byTurn: byTurn)
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
    /// Each turn said up to its `result`, and a `set` answered (`PreviewChat`).
    var byTurn = false

    /// The binary as the tests' scripted starter finds it
    /// (tests/presentation/session/Scripted.swift).
    static let found = FoundBinary(
        url: previewBinary, source: .release, version: "0.1.9",
        versionLine: "apogee 0.1.9 (7072bdf, 2026-10-19, macos-arm64)")

    func locateBinary() async -> DiscoveryOutcome {
        missing.map { .notFound(skipped: $0) } ?? .found(Self.found, skipped: [])
    }

    func openChat(binary: URL, launch: ChatLaunch) -> any ChatSession {
        PreviewChat(saying: lines, silentFrom: silentFrom, ending: ending, byTurn: byTurn)
    }
}

/// Reads a resumed session's earlier turns back as the binary does: `chats
/// show`'s recorded document, with what that binary said on stderr beside
/// it -- or, with none, the recorded refusal of a binary before the CLI's
/// M27, which has no such read (exit 106). Every other read and command
/// answers as nothing ran. Nothing starts the binary.
struct PreviewReader: CommandRunning {
    var document: String?
    var stderr = ""

    /// The binary that recorded the saved conversation.
    static let saved = PreviewReader(document: SessionDocuments.chatsShow)
    /// A binary before `chats show`.
    static let older = PreviewReader(document: nil)
    /// The branch binary's read of a chat holding an attachment.
    static let attached = PreviewReader(document: SessionDocuments.chatsShowAttached)
    /// The branch binary's read of a chat with a message it could not read:
    /// the document's `warnings` say so (the CLI's 55b).
    static let warned = PreviewReader(document: SessionDocuments.chatsShowWarnings)
    /// A binary before 55b reading the same: no `warnings`, the gap said on
    /// stderr alone.
    static let warnedOnStderr = PreviewReader(
        document: SessionDocuments.chatsShowWarningsOlder, stderr: SessionDocuments.chatsShowWarningsOlderErr)

    func read(binary: URL, _ arguments: [String]) async -> ReadOutcome {
        guard arguments.starts(with: ["chats", "show"]) else {
            return .couldNotStart("no preview document for apogee \(arguments.joined(separator: " "))")
        }
        if let document, let read = ReadDocument(text: document) { return .document(read) }
        var tail = BoundedTail()
        tail.append(Data((SessionDocuments.chatsShowRefused + "\n").utf8))
        return .failed(exit: ChildExit(reason: .exited, status: 106), stderrTail: tail)
    }

    func command(binary: URL, _ arguments: [String]) async -> CommandOutcome {
        .done(stdout: "")
    }

    func readKeepingStderr(binary: URL, _ arguments: [String]) async -> ReadWithStderr {
        var tail = BoundedTail()
        tail.append(Data(stderr.utf8))
        return ReadWithStderr(outcome: await read(binary: binary, arguments), stderrTail: tail)
    }
}

/// Where a preview says the binary is; never run.
let previewBinary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
/// Hands each launch the stream the branch binary wrote under its suite
/// flag (GUI 4i): `--suite pair` the chat opened under it, `--suite fast` the
/// same chat resumed under that one, `--suite off` resumed under none -- and
/// a suite the CLI refused at launch, its child ending on its words alone.
struct PreviewSuiteChats: ChatStarting {
    /// The recorded stream by the launch's `--suite`.
    var streams: [String: [String]]
    /// The recorded refusal by the launch's `--suite`: no stdout, exit 1.
    var refusals: [String: String] = [:]

    func locateBinary() async -> DiscoveryOutcome {
        .found(PreviewChats.found, skipped: [])
    }

    func openChat(binary: URL, launch: ChatLaunch) -> any ChatSession {
        let suite = launch.namedSuite ?? ""
        if let refusal = refusals[suite] {
            var tail = BoundedTail()
            tail.append(Data(refusal.utf8))
            return PreviewChat(
                saying: [], ending: SessionEnd(reason: .failed, exit: ChildExit(reason: .exited, status: 1), stderrTail: tail))
        }
        return PreviewChat(saying: streams[suite] ?? [])
    }
}
#endif
