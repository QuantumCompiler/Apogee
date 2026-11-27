import BusinessJobs
import BusinessReads
import BusinessSession
import DataDiscovery
import DataProtocol
import Foundation
import InfrastructureProcess
import Synchronization

/// Shared with the session window's tests: each bundle keeps its own copy.
func decoded(_ line: String) -> SessionEvent { .event(EventDecoder.decode(line: line)) }

/// One ordered record a test's sessions and runner share: which act came
/// first, across them -- a stop before a delete.
final class Ledger: @unchecked Sendable {
    private let entries = Mutex<[String]>([])

    var all: [String] { entries.withLock { $0 } }

    func note(_ entry: String) { entries.withLock { $0.append(entry) } }
}

/// A chat session a test drives by hand: it says what the test emits, and
/// records every line it was asked to write, in order.
final class ScriptedSession: ChatSession, @unchecked Sendable {
    /// How its child takes a stop.
    enum Stopping {
        /// It ends at once, as a real session's child does.
        case ends
        /// It ends only once `releaseStop()` lets it: a child slow to go, or
        /// -- never released -- one that won't die.
        case whenReleased
    }

    let events: AsyncStream<SessionEvent>
    private let continuation: AsyncStream<SessionEvent>.Continuation
    private let log = Mutex<[String]>([])
    private let turns = Mutex(0)
    private let name: String
    private let stopping: Stopping
    private let ledger: Ledger?

    private struct Life {
        var ended = false
        var released = false
        var waiting: [CheckedContinuation<Void, Never>] = []
    }

    private let life = Mutex(Life())

    init(_ name: String = "session", stopping: Stopping = .ends, ledger: Ledger? = nil) {
        (events, continuation) = AsyncStream.makeStream(of: SessionEvent.self)
        self.name = name
        self.stopping = stopping
        self.ledger = ledger
    }

    var calls: [String] { log.withLock { $0 } }

    /// Its child has not ended: what the one-live-child law counts.
    var isLive: Bool { life.withLock { !$0.ended } }

    func emit(_ event: SessionEvent) { continuation.yield(event) }

    func emit(_ line: String) { emit(decoded(line)) }

    func end(_ reason: SessionEnd.Reason = .finished) {
        life.withLock { $0.ended = true }
        continuation.yield(.ended(SessionEnd(reason: reason, exit: ChildExit(reason: .exited, status: 0), stderrTail: BoundedTail())))
        continuation.finish()
    }

    /// Lets a held stop end the child, and every later one at once.
    func releaseStop() {
        let waiting = life.withLock { life in
            life.released = true
            defer { life.waiting = [] }
            return life.waiting
        }
        waiting.forEach { $0.resume() }
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
    /// Stopped, it ends -- as a real session's child does -- at once, or
    /// once released.
    func stop(grace: Duration) async {
        log.withLock { $0.append("stop") }
        ledger?.note("\(name): stop")
        if stopping == .whenReleased {
            await withCheckedContinuation { (continuation: CheckedContinuation<Void, Never>) in
                let now = life.withLock { life in
                    if life.released { return true }
                    life.waiting.append(continuation)
                    return false
                }
                if now { continuation.resume() }
            }
        }
        if isLive { end(.finished) }
        ledger?.note("\(name): ended")
    }

    /// The stop's SIGTERM sent now, by a caller giving up: recorded, in the
    /// ledger's order -- a child that ignores it is ended only by its stop.
    func terminateNow() async {
        log.withLock { $0.append("terminate") }
        ledger?.note("\(name): terminate")
    }
}

/// Where a test's sessions come from: a discovery outcome -- the test may
/// change it, a binary installed meanwhile (GUI 6b) -- and the sessions
/// handed out in turn. Held, every locate waits until `releaseLocate()`
/// lets it answer -- a binary still being located.
final class ScriptedStarter: ChatStarting, @unchecked Sendable {
    private let answer: Mutex<DiscoveryOutcome>
    /// What every later locate answers.
    var outcome: DiscoveryOutcome {
        get { answer.withLock { $0 } }
        set { answer.withLock { $0 = newValue } }
    }
    private let sessions: Mutex<[any ChatSession]>
    private let opened = Mutex(0)
    private let handedOut = Mutex<[any ChatSession]>([])

    private struct Locating {
        var held: Bool
        var calls = 0
        var waiting: [CheckedContinuation<Void, Never>] = []
    }

    private let locating: Mutex<Locating>

    init(
        outcome: DiscoveryOutcome = .found(ScriptedStarter.binary, skipped: []), sessions: [any ChatSession],
        holdsLocate: Bool = false
    ) {
        answer = Mutex(outcome)
        self.sessions = Mutex(sessions)
        locating = Mutex(Locating(held: holdsLocate))
    }

    static let binary = FoundBinary(
        url: URL(fileURLWithPath: "/Users/someone/.local/bin/apogee"), source: .release, version: "0.1.9",
        versionLine: "apogee 0.1.9 (7072bdf, 2026-10-19, macos-arm64)")

    var openedCount: Int { opened.withLock { $0 } }
    private let launched = Mutex<[ChatLaunch]>([])
    /// How each session was started, in order.
    var launches: [ChatLaunch] { launched.withLock { $0 } }

    /// The sessions handed out whose child has not ended: the one-live-child
    /// law holds while this is at most one.
    var live: Int {
        handedOut.withLock { $0.filter { ($0 as? ScriptedSession)?.isLive ?? true }.count }
    }

    /// The scripted sessions handed out, in order.
    var handed: [ScriptedSession] { handedOut.withLock { $0.compactMap { $0 as? ScriptedSession } } }

    /// How many times the binary was asked for.
    var locateCalls: Int { locating.withLock { $0.calls } }

    /// Lets every held locate answer, and every later one at once.
    func releaseLocate() {
        let waiting = locating.withLock { locating in
            locating.held = false
            defer { locating.waiting = [] }
            return locating.waiting
        }
        waiting.forEach { $0.resume() }
    }

    func locateBinary() async -> DiscoveryOutcome {
        await withCheckedContinuation { (continuation: CheckedContinuation<Void, Never>) in
            let now = locating.withLock { locating in
                locating.calls += 1
                if !locating.held { return true }
                locating.waiting.append(continuation)
                return false
            }
            if now { continuation.resume() }
        }
        return outcome
    }

    func openChat(binary: URL, launch: ChatLaunch) -> any ChatSession {
        opened.withLock { $0 += 1 }
        launched.withLock { $0.append(launch) }
        let session = sessions.withLock { $0.removeFirst() }
        handedOut.withLock { $0.append(session) }
        return session
    }
}

/// A clock a test moves by hand: a sleep wakes only once `advance(by:)` has
/// passed its deadline, or when its task is cancelled.
final class ScriptedClock: Clock, @unchecked Sendable {
    struct Instant: InstantProtocol {
        var offset: Duration

        func advanced(by duration: Duration) -> Instant { Instant(offset: offset + duration) }
        func duration(to other: Instant) -> Duration { other.offset - offset }
        static func < (left: Instant, right: Instant) -> Bool { left.offset < right.offset }
    }

    private struct Sleeper {
        let id: Int
        let deadline: Instant
        let continuation: CheckedContinuation<Void, any Error>
    }

    private struct State {
        var now = Instant(offset: .zero)
        var sleepers: [Sleeper] = []
        var cancelled: Set<Int> = []
        var next = 0
    }

    private let state = Mutex(State())

    var now: Instant { state.withLock { $0.now } }
    var minimumResolution: Duration { .zero }

    /// The sleeps waiting on the clock: what a test watches before it moves it.
    var sleeping: Int { state.withLock { $0.sleepers.count } }

    func sleep(until deadline: Instant, tolerance: Duration?) async throws {
        let id = state.withLock { state in
            state.next += 1
            return state.next
        }
        try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, any Error>) in
                enum Now { case wait, wake, cancel }
                let now = state.withLock { state -> Now in
                    if state.cancelled.remove(id) != nil { return .cancel }
                    if deadline <= state.now { return .wake }
                    state.sleepers.append(Sleeper(id: id, deadline: deadline, continuation: continuation))
                    return .wait
                }
                switch now {
                case .wait: break
                case .wake: continuation.resume()
                case .cancel: continuation.resume(throwing: CancellationError())
                }
            }
        } onCancel: {
            let sleeper = state.withLock { state -> Sleeper? in
                guard let index = state.sleepers.firstIndex(where: { $0.id == id }) else {
                    state.cancelled.insert(id)
                    return nil
                }
                return state.sleepers.remove(at: index)
            }
            sleeper?.continuation.resume(throwing: CancellationError())
        }
    }

    /// Moves the clock on, waking every sleep whose deadline it passes.
    func advance(by duration: Duration) {
        let woken = state.withLock { state -> [Sleeper] in
            state.now = state.now.advanced(by: duration)
            let now = state.now
            let woken = state.sleepers.filter { $0.deadline <= now }
            state.sleepers.removeAll { $0.deadline <= now }
            return woken
        }
        woken.forEach { $0.continuation.resume() }
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
    /// Where its commands are noted beside the sessions' acts, when given.
    private let ledger: Ledger?

    private struct Holding {
        var prefix: [String]?
        var waiting: [CheckedContinuation<Void, Never>] = []
    }

    /// Commands held open until `release()`: a binary slow to answer.
    private let holding = Mutex(Holding())

    init(ledger: Ledger? = nil) {
        self.ledger = ledger
    }

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

    /// Every later command starting with `prefix` waits, once called, until
    /// `release()` lets it answer.
    func hold(_ prefix: [String]) {
        holding.withLock { $0.prefix = prefix }
    }

    /// Lets every held command answer, and every later one at once.
    func release() {
        let waiting = holding.withLock { holding in
            holding.prefix = nil
            defer { holding.waiting = [] }
            return holding.waiting
        }
        waiting.forEach { $0.resume() }
    }

    func read(binary: URL, _ arguments: [String]) async -> ReadOutcome {
        log.withLock { $0.append("read " + arguments.joined(separator: " ")) }
        return reads.withLock { rules in
            rules.first { arguments.starts(with: $0.0) }?.1
        } ?? .couldNotStart("no scripted read for \(arguments)")
    }

    func command(binary: URL, _ arguments: [String]) async -> CommandOutcome {
        log.withLock { $0.append("run " + arguments.joined(separator: " ")) }
        // The vocabulary asked at launch (GUI 4k) is a read, not an act the
        // ledger orders.
        if arguments != ["__machine-schema"] { ledger?.note("run " + arguments.joined(separator: " ")) }
        await withCheckedContinuation { (continuation: CheckedContinuation<Void, Never>) in
            let now = holding.withLock { holding in
                guard let prefix = holding.prefix, arguments.starts(with: prefix) else { return true }
                holding.waiting.append(continuation)
                return false
            }
            if now { continuation.resume() }
        }
        return commands.withLock { rules in
            rules.first { arguments.starts(with: $0.0) }?.1
        } ?? .done(stdout: "")
    }
}

/// A run a test drives by hand: what it said, and how it ended -- done, its
/// failure in the CLI's words, or stopped by its Stop, as a supervised job
/// ends when interrupted.
final class ScriptedJob: JobRunning, @unchecked Sendable {
    let updates: AsyncStream<JobUpdate>
    private let continuation: AsyncStream<JobUpdate>.Continuation
    private let stops = Mutex(0)

    init() {
        (updates, continuation) = AsyncStream.makeStream(of: JobUpdate.self)
    }

    /// How many times it was asked to stop.
    var stopped: Int { stops.withLock { $0 } }

    func say(_ line: String) { continuation.yield(.said(line)) }

    /// It ended as `status` says: 0 is done; anything else a failure, the
    /// stderr `said` its tail.
    func end(status: Int32 = 0, stderr said: String = "", interrupted: Bool = false) {
        var tail = BoundedTail()
        tail.append(Data(said.utf8))
        continuation.yield(.ended(JobEnd(exit: ChildExit(reason: .exited, status: status), stderrTail: tail, interrupted: interrupted)))
        continuation.finish()
    }

    /// Stopped, it ends interrupted, as a child given SIGINT does.
    func stop(grace: Duration) async {
        stops.withLock { $0 += 1 }
        end(status: 130, interrupted: true)
    }
}

/// Where a test's long runs come from: each start recorded -- its argv, and
/// whether it streams -- and handed a `ScriptedJob` the test drives.
final class ScriptedJobs: JobStarting, @unchecked Sendable {
    struct Start: Equatable {
        let binary: URL
        let arguments: [String]
        let streaming: Bool
    }

    private let started = Mutex<[(Start, ScriptedJob)]>([])

    var starts: [Start] { started.withLock { $0.map(\.0) } }
    var jobs: [ScriptedJob] { started.withLock { $0.map(\.1) } }

    func startJob(binary: URL, arguments: [String], streaming: Bool, folder: URL?) -> any JobRunning {
        let job = ScriptedJob()
        started.withLock { $0.append((Start(binary: binary, arguments: arguments, streaming: streaming), job)) }
        return job
    }
}
