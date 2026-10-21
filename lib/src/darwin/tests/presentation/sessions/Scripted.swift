import BusinessReads
import BusinessSession
import DataDiscovery
import DataProtocol
import Foundation
import InfrastructureProcess
import Synchronization

/// Shared with the session window's tests: each bundle keeps its own copy.
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

@MainActor
func eventually(_ condition: @MainActor () -> Bool) async -> Bool {
    let deadline = ContinuousClock.now + .seconds(5)
    while !condition() {
        if ContinuousClock.now > deadline { return false }
        try? await Task.sleep(for: .milliseconds(10))
    }
    return true
}

/// A runner a test scripts: each read or command answered by the first rule
/// its arguments start with, every call recorded.
final class ScriptedRunner: CommandRunning, @unchecked Sendable {
    private let reads = Mutex<[([String], ReadOutcome)]>([])
    private let commands = Mutex<[([String], CommandOutcome)]>([])
    private let log = Mutex<[String]>([])

    var calls: [String] { log.withLock { $0 } }

    func reading(_ prefix: [String], _ json: String) {
        reads.withLock { $0.insert((prefix, .document(ReadDocument(text: json)!)), at: 0) }
    }

    func reading(_ prefix: [String], _ outcome: ReadOutcome) {
        reads.withLock { $0.insert((prefix, outcome), at: 0) }
    }

    func commanding(_ prefix: [String], _ outcome: CommandOutcome) {
        commands.withLock { $0.insert((prefix, outcome), at: 0) }
    }

    func read(binary: URL, _ arguments: [String]) async -> ReadOutcome {
        log.withLock { $0.append("read " + arguments.joined(separator: " ")) }
        return reads.withLock { rules in
            rules.first { arguments.starts(with: $0.0) }?.1
        } ?? .couldNotStart("no scripted read for \(arguments)")
    }

    func command(binary: URL, _ arguments: [String]) async -> CommandOutcome {
        log.withLock { $0.append("run " + arguments.joined(separator: " ")) }
        return commands.withLock { rules in
            rules.first { arguments.starts(with: $0.0) }?.1
        } ?? .done(stdout: "")
    }
}
