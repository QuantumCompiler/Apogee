import BusinessReads
import BusinessSession
import DataDiscovery
import DataProtocol
import Foundation
import InfrastructureProcess
import PresentationSession
import Synchronization

/// The recorded mock-actor stream: the real binary's stdout for one turn that
/// asks, writes behind the permission gate and answers
/// (tests/data/protocol/Fixtures/mock-actor.jsonl, the client core's fixture).
let mockActorLines: [String] = {
    let fixture = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
        .appendingPathComponent("data/protocol/Fixtures/mock-actor.jsonl")
    return ((try? String(contentsOf: fixture, encoding: .utf8)) ?? "").split(separator: "\n").map(String.init)
}()

func decoded(_ line: String) -> SessionEvent { .event(EventDecoder.decode(line: line)) }

/// A recorded fixture, by its path under `tests/`.
func recorded(_ path: String) -> String {
    let tests = URL(fileURLWithPath: #filePath)
        .deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
    return (try? String(contentsOf: tests.appendingPathComponent(path), encoding: .utf8)) ?? ""
}

/// A saved conversation resumed by the binary that ships the CLI's M27: its
/// `session` event (`next_turn` 6), then one live turn, sent as
/// `resumedTurn` (tests/data/protocol/Fixtures/resumed-chat.jsonl).
let resumedChatLines: [String] = recorded("data/protocol/Fixtures/resumed-chat.jsonl")
    .split(separator: "\n").map(String.init)
let resumedTurn = "Thanks -- anything else?"

/// That conversation as `chats show` read it back before the resume
/// (tests/business/reads/Fixtures/chats-show.json), and as a binary before
/// M27 refuses the read (`chats-show-refused.err`, exit 106).
let savedChatDocument = ReadDocument(text: recorded("business/reads/Fixtures/chats-show.json"))!
let savedChat = SavedChat(document: savedChatDocument)!
var chatsShowRefused: ReadOutcome {
    var tail = BoundedTail()
    tail.append(Data(recorded("business/reads/Fixtures/chats-show-refused.err").utf8))
    return .failed(exit: ChildExit(reason: .exited, status: 106), stderrTail: tail)
}

/// What each item is, in a word or two, for comparing whole transcripts.
func shape(_ transcript: Transcript) -> [String] {
    transcript.items.map { item in
        switch item.content {
        case .user(let text): "user: \(text)"
        case .activity(let a): "activity\(a.live ? " live" : ""): \(a.reasoning)|\(a.lines.joined(separator: ","))"
        case .answer(let a): "answer\(a.finished ? "" : " streaming"): \(a.text)"
        case .notice(let text): "notice: \(text)"
        case .recalled(let recall): "recalled: \(recall.chats.map(\.name).joined(separator: ","))"
        case .sources(let sources): "sources: \(sources.map(\.file).joined(separator: ","))"
        case .question(let card): "question: \(card.answers?.joined(separator: ",") ?? "waiting")"
        case .permission(let card): "permission: \(card.answer ?? "waiting")"
        case .error(let message): "error: \(message)"
        case .stopped: "stopped"
        case .unanswered: "unanswered"
        case .divider(let text): "divider: \(text)"
        case .todo(let items): "todo: \(items.map(\.text).joined(separator: ","))"
        case .unknown(let type): "unknown: \(type)"
        case .ended(let ending): "ended: \(ending.message)"
        }
    }
}

/// What reads a resumed session's earlier turns back in a test: every read
/// answered with `outcome` -- at once, or, `held`, once `release()` lets it
/// -- and recorded.
final class ScriptedReader: CommandRunning, @unchecked Sendable {
    private struct State {
        var calls: [String] = []
        var released: Bool
        var waiting: [CheckedContinuation<Void, Never>] = []
    }

    private let outcome: ReadOutcome
    private let state: Mutex<State>

    init(_ outcome: ReadOutcome, held: Bool = false) {
        self.outcome = outcome
        state = Mutex(State(released: !held))
    }

    /// Answers with the recorded `chats show` document.
    convenience init(held: Bool = false) {
        self.init(.document(savedChatDocument), held: held)
    }

    var calls: [String] { state.withLock { $0.calls } }

    /// Lets every held read answer, and every later one at once.
    func release() {
        let waiting = state.withLock { state in
            state.released = true
            defer { state.waiting = [] }
            return state.waiting
        }
        waiting.forEach { $0.resume() }
    }

    func read(binary: URL, _ arguments: [String]) async -> ReadOutcome {
        state.withLock { $0.calls.append("read " + arguments.joined(separator: " ")) }
        await withCheckedContinuation { (continuation: CheckedContinuation<Void, Never>) in
            let now = state.withLock { state in
                if state.released { return true }
                state.waiting.append(continuation)
                return false
            }
            if now { continuation.resume() }
        }
        return outcome
    }

    func command(binary: URL, _ arguments: [String]) async -> CommandOutcome {
        state.withLock { $0.calls.append("run " + arguments.joined(separator: " ")) }
        return .done(stdout: "")
    }
}

/// A chat session a test drives by hand: it says what the test emits, and
/// records every line it was asked to write, in order.
final class ScriptedSession: ChatSession, @unchecked Sendable {
    let events: AsyncStream<SessionEvent>
    private let continuation: AsyncStream<SessionEvent>.Continuation
    private let log = Mutex<[String]>([])
    private let turns = Mutex(0)

    init() {
        (events, continuation) = AsyncStream.makeStream(of: SessionEvent.self)
    }

    var calls: [String] { log.withLock { $0 } }

    func emit(_ event: SessionEvent) { continuation.yield(event) }

    func emit(_ line: String) { emit(decoded(line)) }

    func end(_ reason: SessionEnd.Reason = .finished) {
        continuation.yield(.ended(SessionEnd(reason: reason, exit: ChildExit(reason: .exited, status: 0), stderrTail: BoundedTail())))
        continuation.finish()
    }

    func send(user text: String) async throws -> Int {
        log.withLock { $0.append("user: \(text)") }
        return turns.withLock { $0 += 1; return $0 }
    }

    func answer(_ text: String) async throws { log.withLock { $0.append("answer: \(text)") } }
    func attach(path: String, graph: String?) async throws { log.withLock { $0.append("attach: \(path)") } }
    func detach(path: String) async throws { log.withLock { $0.append("detach: \(path)") } }
    func set(_ settings: RunSettings) async throws { log.withLock { $0.append("set: \(DriverLine.set(settings).encoded)") } }
    func compact() async throws { log.withLock { $0.append("compact") } }
    func cancel() async throws { log.withLock { $0.append("cancel") } }
    func close() async { log.withLock { $0.append("close") } }
    /// Stopped, it ends -- as a real session's child does.
    func stop(grace: Duration) async {
        log.withLock { $0.append("stop") }
        end(.finished)
    }
}

/// Where a test's sessions come from: a fixed discovery outcome, and the
/// sessions handed out in turn.
final class ScriptedStarter: ChatStarting, @unchecked Sendable {
    let outcome: DiscoveryOutcome
    private let sessions: Mutex<[any ChatSession]>
    private let opened = Mutex(0)

    init(outcome: DiscoveryOutcome = .found(ScriptedStarter.binary, skipped: []), sessions: [any ChatSession]) {
        self.outcome = outcome
        self.sessions = Mutex(sessions)
    }

    static let binary = FoundBinary(
        url: URL(fileURLWithPath: "/Users/someone/.local/bin/apogee"), source: .release, version: "0.1.9",
        versionLine: "apogee 0.1.9 (7072bdf, 2026-10-19, macos-arm64)")

    var openedCount: Int { opened.withLock { $0 } }
    private let launched = Mutex<[ChatLaunch]>([])
    /// How each session was started, in order.
    var launches: [ChatLaunch] { launched.withLock { $0 } }

    func locateBinary() async -> DiscoveryOutcome { outcome }

    func openChat(binary: URL, launch: ChatLaunch) -> any ChatSession {
        opened.withLock { $0 += 1 }
        launched.withLock { $0.append(launch) }
        return sessions.withLock { $0.removeFirst() }
    }
}

/// A child with no process: it replays recorded stdout lines, pausing after
/// the `session` line until a `user` line is written, and after each
/// `question` until an `answer` is -- and keeps every byte written to it.
final class ReplayChild: RunningChild, @unchecked Sendable {
    enum Step {
        case emit(String)
        case await(type: String)
    }

    let lines: AsyncStream<String>
    private let continuation: AsyncStream<String>.Continuation

    private struct State {
        var steps: [Step]
        var next = 0
        var written: [String] = []
        var consumed = 0
        var inputClosed = false
        var finished = false
    }

    private let state: Mutex<State>

    /// `recorded` paced the way the real binary paces it.
    init(replaying recorded: [String]) {
        var steps: [Step] = []
        for line in recorded {
            steps.append(.emit(line))
            let type = JSONValue.parse(line)?["type"]?.stringValue
            if type == "session" { steps.append(.await(type: "user")) }
            if type == "question" { steps.append(.await(type: "answer")) }
        }
        state = Mutex(State(steps: steps))
        (lines, continuation) = AsyncStream.makeStream(of: String.self)
        advance()
    }

    /// Every line written to the child, as written.
    var written: [String] { state.withLock { $0.written } }

    private func advance() {
        let (out, finish) = state.withLock { state -> ([String], Bool) in
            var out: [String] = []
            while state.next < state.steps.count {
                switch state.steps[state.next] {
                case .emit(let line):
                    out.append(line)
                case .await(let type):
                    let pending = state.written[state.consumed...].firstIndex {
                        JSONValue.parse($0)?["type"]?.stringValue == type
                    }
                    guard let pending else { return (out, false) }
                    state.consumed = pending + 1
                }
                state.next += 1
            }
            let finish = state.inputClosed && !state.finished
            if finish { state.finished = true }
            return (out, finish)
        }
        out.forEach { continuation.yield($0) }
        if finish { continuation.finish() }
    }

    var stderrTail: BoundedTail { BoundedTail() }
    var processIdentifier: Int32 { 0 }

    func write(_ data: Data) throws {
        let closed = state.withLock { state -> Bool in
            guard !state.inputClosed else { return true }
            state.written += String(decoding: data, as: UTF8.self).split(separator: "\n").map(String.init)
            return false
        }
        if closed { throw ChildError.inputClosed }
        advance()
    }

    func closeInput() {
        state.withLock { $0.inputClosed = true }
        advance()
    }

    func interrupt() { terminate() }
    func terminate() { closeInput() }
    func kill() { closeInput() }
    func waitForExit() async -> ChildExit { ChildExit(reason: .exited, status: 0) }
}

/// Hands out one child.
struct ReplaySpawner: ChildSpawner {
    let child: ReplayChild
    func spawn(_ command: ChildCommand) throws -> any RunningChild { child }
}

/// Real `MachineSession`s over a replayed child.
struct ReplayStarter: ChatStarting {
    let child: ReplayChild

    func locateBinary() async -> DiscoveryOutcome { .found(ScriptedStarter.binary, skipped: []) }

    func openChat(binary: URL, launch: ChatLaunch) -> any ChatSession {
        MachineSession.start(
            spawner: ReplaySpawner(child: child), command: launch.command(binary: binary),
            hello: .hello(clientName: "Apogee", clientVersion: "0.2.0", wants: ["tools"]))
    }
}

/// Waits, on the main actor, until `condition` holds -- five seconds at most.
@MainActor
func eventually(_ condition: @MainActor () -> Bool) async -> Bool {
    let deadline = ContinuousClock.now + .seconds(5)
    while !condition() {
        if ContinuousClock.now > deadline { return false }
        try? await Task.sleep(for: .milliseconds(10))
    }
    return true
}
