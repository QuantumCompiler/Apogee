import BusinessSession
import DataDiscovery
import DataProtocol
import Foundation
import InfrastructureProcess
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
