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
/// (tests/data/protocol/Fixtures): the branch's, the installed one's, or one
/// hand-made to the CLI source's shape before its track 52.
private func schema(_ name: String) throws -> String {
    let tests = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
    return try String(contentsOf: tests.appendingPathComponent("data/protocol/Fixtures/\(name).json"), encoding: .utf8)
}

/// Finds a binary and counts the chats it was asked to open: none opens while
/// another place is in view.
private final class CountingStarter: ChatStarting, @unchecked Sendable {
    private let opened = Mutex(0)
    var openedCount: Int { opened.withLock { $0 } }

    func locateBinary() async -> DiscoveryOutcome {
        .found(
            FoundBinary(
                url: URL(fileURLWithPath: "/Users/someone/.local/bin/apogee"), source: .release, version: "0.2.0",
                versionLine: "apogee 0.2.0 (0bec6e7c, 2026-10-26, macos-arm64)"),
            skipped: [])
    }

    func openChat(binary: URL, launch: ChatLaunch) -> any ChatSession {
        opened.withLock { $0 += 1 }
        return NeverChat()
    }
}

/// A chat that never speaks: only counted, never driven.
private final class NeverChat: ChatSession, @unchecked Sendable {
    let events = AsyncStream<SessionEvent> { _ in }
    func send(user text: String) async throws -> Int { 1 }
    func answer(_ text: String) async throws {}
    func attach(path: String, graph: String?) async throws {}
    func detach(path: String) async throws {}
    func set(_ settings: RunSettings) async throws {}
    func compact() async throws {}
    func cancel() async throws {}
    func close() async {}
    func stop(grace: Duration) async {}
}

/// Answers `__machine-schema` with a recorded vocabulary -- or refuses it, as
/// a binary that has none would -- and every read with nothing.
private final class VocabularyRunner: CommandRunning, @unchecked Sendable {
    let vocabulary: String?
    init(_ vocabulary: String?) { self.vocabulary = vocabulary }

    func read(binary: URL, _ arguments: [String]) async -> ReadOutcome {
        .couldNotStart("no scripted read for \(arguments)")
    }

    func command(binary: URL, _ arguments: [String]) async -> CommandOutcome {
        guard arguments == ["__machine-schema"] else { return .done(stdout: "") }
        guard let vocabulary else { return .failed(exit: ChildExit(reason: .exited, status: 109), stderrTail: BoundedTail()) }
        return .done(stdout: vocabulary)
    }
}

/// Records whether each run was asked to stream.
private final class StreamingJobs: JobStarting, @unchecked Sendable {
    private let asked = Mutex<[Bool]>([])
    var streaming: [Bool] { asked.withLock { $0 } }

    func startJob(binary: URL, arguments: [String], streaming: Bool, folder: URL?) -> any JobRunning {
        asked.withLock { $0.append(streaming) }
        return EndedJob()
    }
}

private final class EndedJob: JobRunning, @unchecked Sendable {
    let updates = AsyncStream<JobUpdate> { $0.finish() }
    func stop(grace: Duration) async {}
}

/// GUI 4k, the seventh gap: a launch that restores onto another place than
/// Sessions starts no session, and still runs no screen on a fallback against
/// a capable binary -- the vocabulary is read at launch.
@Suite("Capabilities from launch, whatever place is restored (GUI 4k)", .timeLimit(.minutes(1)))
@MainActor
struct CapabilitiesFromLaunchTests {
    private func launched(_ vocabulary: String?, route: String = "models") async -> (AppViewModel, CountingStarter, StreamingJobs) {
        let starter = CountingStarter()
        let jobs = StreamingJobs()
        let app = AppViewModel(
            info: nil, preferences: Preferences(store: MemoryStore(["route": route])), starter: starter,
            runner: VocabularyRunner(vocabulary), jobs: jobs, children: ChildRegistry())
        await app.start()
        return (app, starter, jobs)
    }

    @Test("restored onto Models on the branch's binary: pulls stream, Compose plans alone, Settings sets -- and no session started")
    func capableRestoredElsewhere() async throws {
        let (app, starter, jobs) = await launched(try schema("machine-schema"))
        #expect(app.shell.route == .models)
        #expect(starter.openedCount == 0, "Sessions isn't in view: no child")
        #expect(app.downloads.streams(), "stream-json pulls")
        #expect(app.video.streams() && app.voice.streams() && app.compose.streams())
        #expect(app.compose.plansAlone(), "Compose's plan-alone")
        #expect(app.settings.hasTrack52(), "Settings' Set, On/Off and Forget")
        #expect(app.settings.configSetHint == nil, "nothing said it lacks")
        app.downloads.reference = "org/model"
        app.downloads.start()
        #expect(jobs.streaming == [true])
    }

    @Test("restored onto Video on a binary before track 52: every fallback, because it said so")
    func incapableRestoredElsewhere() async throws {
        let (app, starter, _) = await launched(try schema("machine-schema-pre52-handmade"), route: "video")
        #expect(starter.openedCount == 0)
        #expect(!app.downloads.streams() && !app.compose.plansAlone() && !app.settings.hasTrack52())
        #expect(app.settings.configSetHint == "Read here, set in the config file: this apogee has no config set.", "asked, and it said")
    }

    @Test("a binary that doesn't answer: the fallbacks, and Settings never says it has no config set")
    func unknownRestoredElsewhere() async {
        let (app, starter, _) = await launched(nil, route: "training")
        #expect(starter.openedCount == 0)
        #expect(!app.downloads.streams() && !app.settings.hasTrack52())
        #expect(app.settings.configSetHint?.contains("has no config set") == false)
        #expect(app.settings.configSetHint?.contains("hasn't said what it can do") == true)
    }
}
