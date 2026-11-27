import BusinessJobs
import BusinessReads
import BusinessSession
import DataDiscovery
import DataPreferences
import DataProtocol
import Foundation
import InfrastructureProcess
import Synchronization
import Testing

@testable import PresentationApp

/// A vocabulary recorded from a binary's `__machine-schema`
/// (tests/data/protocol/Fixtures).
private func vocabulary(_ name: String) throws -> String {
    let tests = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
    return try String(contentsOf: tests.appendingPathComponent("data/protocol/Fixtures/\(name).json"), encoding: .utf8)
}

/// A chat that speaks at once, as a real child says its `session` event --
/// announcing what the branch's binary announces, as its vocabulary says --
/// and ends when it is stopped.
private final class SpeakingChat: ChatSession, @unchecked Sendable {
    let events: AsyncStream<SessionEvent>
    private let continuation: AsyncStream<SessionEvent>.Continuation
    private let ended = Mutex(false)

    static let session = #"{"type":"session","protocol_version":1,"model":"local","next_turn":1,"capabilities":{"events":["session","job"],"accepts":["user","set"],"tools":true,"ask":true,"schema":"2026-10-25"}}"#

    init() {
        (events, continuation) = AsyncStream.makeStream(of: SessionEvent.self)
        continuation.yield(.event(EventDecoder.decode(line: Self.session)))
    }

    var isLive: Bool { !ended.withLock { $0 } }

    func send(user text: String) async throws -> Int { 1 }
    func answer(_ text: String) async throws {}
    func attach(path: String, graph: String?) async throws {}
    func detach(path: String) async throws {}
    func set(_ settings: RunSettings) async throws {}
    func compact() async throws {}
    func cancel() async throws {}
    func close() async {}

    func stop(grace: Duration) async {
        ended.withLock { $0 = true }
        continuation.yield(.ended(SessionEnd(reason: .finished, exit: ChildExit(reason: .exited, status: 0), stderrTail: BoundedTail())))
        continuation.finish()
    }

    func terminateNow() async {}
}

/// Discovery that answers what the test says -- nothing, until apogee is
/// installed -- and a new speaking chat for each opening, counted.
private final class InstalledLater: ChatStarting, @unchecked Sendable {
    static let binary = FoundBinary(
        url: URL(fileURLWithPath: "/Users/someone/.local/bin/apogee"), source: .release, version: "0.2.0",
        versionLine: "apogee 0.2.0 (0bec6e7c, 2026-10-26, macos-arm64)")

    private let installed: Mutex<Bool>
    private let opened = Mutex<[SpeakingChat]>([])

    init(installed: Bool) {
        self.installed = Mutex(installed)
    }

    func install() { installed.withLock { $0 = true } }

    var openedCount: Int { opened.withLock { $0.count } }
    /// The chats opened whose child has not ended.
    var live: Int { opened.withLock { $0.filter(\.isLive).count } }

    func locateBinary() async -> DiscoveryOutcome {
        installed.withLock { $0 } ? .found(Self.binary, skipped: []) : .notFound(skipped: [])
    }

    func openChat(binary: URL, launch: ChatLaunch) -> any ChatSession {
        let chat = SpeakingChat()
        opened.withLock { $0.append(chat) }
        return chat
    }
}

/// Answers `__machine-schema` with a recorded vocabulary -- held open, once
/// asked to be, until released, a binary slow to answer -- and every read
/// with an empty list, noting each call and each answer of the vocabulary in
/// order.
private final class SlowVocabulary: CommandRunning, @unchecked Sendable {
    let vocabulary: String
    private struct State {
        var log: [String] = []
        var held = false
        var waiting: [CheckedContinuation<Void, Never>] = []
    }

    private let state = Mutex(State())

    init(_ vocabulary: String) { self.vocabulary = vocabulary }

    var log: [String] { state.withLock { $0.log } }

    func hold() { state.withLock { $0.held = true } }

    func release() {
        let waiting = state.withLock { state in
            state.held = false
            defer { state.waiting = [] }
            return state.waiting
        }
        waiting.forEach { $0.resume() }
    }

    func read(binary: URL, _ arguments: [String]) async -> ReadOutcome {
        state.withLock { $0.log.append("read " + arguments.joined(separator: " ")) }
        return .document(ReadDocument(text: #"{"object":"list","data":[]}"#)!)
    }

    func command(binary: URL, _ arguments: [String]) async -> CommandOutcome {
        state.withLock { $0.log.append("run " + arguments.joined(separator: " ")) }
        guard arguments == ["__machine-schema"] else { return .done(stdout: "") }
        await withCheckedContinuation { (continuation: CheckedContinuation<Void, Never>) in
            let now = state.withLock { state in
                guard state.held else { return true }
                state.waiting.append(continuation)
                return false
            }
            if now { continuation.resume() }
        }
        state.withLock { $0.log.append("answered __machine-schema") }
        return .done(stdout: vocabulary)
    }
}

/// Records each run's binary and argv; every run ends at once.
private final class RecordedJobs: JobStarting, @unchecked Sendable {
    private let started = Mutex<[(URL, [String])]>([])
    var runs: [(binary: URL, arguments: [String])] { started.withLock { $0.map { ($0.0, $0.1) } } }

    func startJob(binary: URL, arguments: [String], streaming: Bool, folder: URL?) -> any JobRunning {
        started.withLock { $0.append((binary, arguments)) }
        return Finished()
    }

    private final class Finished: JobRunning, @unchecked Sendable {
        let updates = AsyncStream<JobUpdate> { $0.finish() }
        func stop(grace: Duration) async {}
    }
}

/// A binary's file as a test moves it: another build installed in its place.
private final class File: @unchecked Sendable {
    private let print = Mutex(BinaryFingerprint(fileNumber: 7, size: 100, modified: Date(timeIntervalSince1970: 1)))
    var fingerprint: BinaryFingerprint { print.withLock { $0 } }
    func replace() {
        print.withLock { $0 = BinaryFingerprint(fileNumber: $0.fileNumber + 1, size: $0.size + 1, modified: $0.modified + 1) }
    }
}

/// GUI 6b, the fourth finding: each place change was its own unordered task,
/// so one made while the change before it waited on a changed binary's
/// re-ask ran first -- Sessions then Tasks left a session's child running
/// off screen, and Tasks read before the binary had said what it can do.
@Suite("Place changes wait for the binary and apply in order (GUI 6b)", .timeLimit(.minutes(1)))
@MainActor
struct PlaceChangesTests {
    @Test("Sessions then Tasks while a changed binary is asked again: applied in order, the session left paused, Tasks read once the binary has said")
    func changesWaitForTheReAsk() async throws {
        let chats = InstalledLater(installed: true)
        let runner = SlowVocabulary(try vocabulary("machine-schema"))
        let file = File()
        let app = AppViewModel(
            info: nil, preferences: Preferences(store: MemoryStore(["route": "sessions"])), starter: chats, runner: runner,
            jobs: RecordedJobs(), children: ChildRegistry())
        app.sessions.fingerprint = { _ in file.fingerprint }
        await app.start()
        #expect(await eventually { app.sessions.current.transcript.status == .ready })
        app.shell.go(.tasks)
        await app.placeChanges.settled()
        #expect(app.sessions.current.isPaused && chats.live == 0, "out of view: paused, its child gone")
        // Another build installed under the app, slow to say what it can do.
        file.replace()
        runner.hold()
        app.shell.go(.sessions)
        #expect(await eventually { runner.log.filter { $0 == "run __machine-schema" }.count == 2 })
        #expect(!app.sessions.capabilitiesKnown, "asked again: the earlier build's flags gone")
        app.shell.go(.tasks)
        runner.release()
        await app.placeChanges.settled()
        #expect(app.shell.route == .tasks)
        #expect(app.sessions.current.isPaused, "Sessions came and went, in that order: the session ends paused")
        #expect(chats.live == 0, "no child left running off screen")
        let log = runner.log
        let answered = try #require(log.lastIndex(of: "answered __machine-schema"))
        let read = try #require(log.lastIndex(of: "read task list"))
        #expect(answered < read, "Tasks read once the binary had said: \(log)")
        #expect(app.sessions.streamsJobs && app.downloads.streams(), "known before Tasks reads: a pull streams")
    }

    @Test("with no change of binary, quick changes still apply in order and the last one is in force")
    func lastChangeInForce() async throws {
        let chats = InstalledLater(installed: true)
        let app = AppViewModel(
            info: nil, preferences: Preferences(store: MemoryStore(["route": "models"])), starter: chats,
            runner: SlowVocabulary(try vocabulary("machine-schema")), jobs: RecordedJobs(), children: ChildRegistry())
        await app.start()
        #expect(chats.openedCount == 0)
        app.shell.go(.sessions)
        app.shell.go(.tasks)
        app.shell.go(.sessions)
        await app.placeChanges.settled()
        #expect(await eventually { app.sessions.current.transcript.status == .ready }, "Sessions, chosen last, is in force")
        #expect(chats.live == 1, "one live child")
        await app.sessions.shutDown()
    }
}

/// GUI 6b, the first finding: the places other than Sessions kept the nil
/// binary the launch handed them, though Try again had found one.
@Suite("A binary Try again finds reaches every place (GUI 6b)", .timeLimit(.minutes(1)))
@MainActor
struct BinaryAdoptedTests {
    @Test("launched with no apogee, then Try again finds it: every place is handed it, as the launch hands one")
    func everyPlaceHasIt() async throws {
        let chats = InstalledLater(installed: false)
        let runner = SlowVocabulary(try vocabulary("machine-schema"))
        let jobs = RecordedJobs()
        let app = AppViewModel(
            info: nil, preferences: Preferences(store: MemoryStore(["route": "sessions"])), starter: chats, runner: runner, jobs: jobs,
            children: ChildRegistry())
        await app.start()
        #expect(app.sessions.binaryURL == nil && app.settings.binary == nil)
        #expect(app.tasks.listing == .missing && app.models.listing == .missing && app.records.listing == .missing)
        #expect(await eventually { if case .missing = app.sessions.current.connection { true } else { false } })
        chats.install()
        // The window's Try again.
        app.sessions.current.start()
        let path = InstalledLater.binary.url.path
        #expect(await eventually { app.settings.binary == path }, "Settings says the binary in use")
        #expect(app.sessions.binaryURL == InstalledLater.binary.url && app.sessions.capabilitiesKnown)
        #expect(await eventually { app.tasks.listing == .read }, "Tasks read")
        #expect(app.models.listing != .missing && app.records.listing != .missing && app.training.runsListing != .missing)
        #expect(app.review.targetsListing != .missing && app.agents.listing != .missing && app.mcp.serversListing != .missing)
        app.downloads.reference = "org/model"
        app.downloads.start()
        app.console.input = "version"
        app.console.run()
        #expect(jobs.runs.count == 2 && jobs.runs.allSatisfy { $0.binary == InstalledLater.binary.url }, "a pull and the console run on it")
        await app.sessions.shutDown()
    }
}
