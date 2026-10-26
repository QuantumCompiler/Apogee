import BusinessSession
import DataDiscovery
import DataPreferences
import Foundation
import InfrastructureProcess
import Synchronization
import Testing

@testable import PresentationSessions

/// A home of a test's own with fake binaries in it: real scripts, run by the
/// real discovery -- never an installed `apogee`.
struct FakeInstall {
    let home: URL

    init() throws {
        home = FileManager.default.temporaryDirectory
            .appendingPathComponent("apogee-install-\(UUID().uuidString)").resolvingSymlinksInPath()
        try FileManager.default.createDirectory(at: home, withIntermediateDirectories: true)
    }

    /// A script at `relative`, run by `/bin/sh`.
    @discardableResult
    func binary(_ relative: String, _ body: String) throws -> URL {
        let url = home.appendingPathComponent(relative)
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try ("#!/bin/sh\n" + body + "\n").write(to: url, atomically: true, encoding: .utf8)
        try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: url.path)
        return url
    }

    /// An `apogee` that answers `version` as a release does.
    static let release = #"[ "$1" = version ] && { echo "apogee 0.2.0 (4c0ffee, 2026-10-25, macos-arm64)"; exit 0; }; exec /bin/cat >/dev/null"#
    /// One a person pointed the app at, broken: `version` fails.
    static let broken = #"echo "error: broken install" >&2; exit 1"#

    func remove() {
        try? FileManager.default.removeItem(at: home)
    }
}

/// Discovery answering in turn: nothing at launch, then -- the release
/// installed meanwhile -- the release, the person's override passed over.
final class LaterStarter: ChatStarting, @unchecked Sendable {
    let mine = BinaryCandidate(url: URL(fileURLWithPath: "/Users/someone/tools/apogee"), source: .override)
    private let calls = Mutex(0)
    let session = ScriptedSession()

    func locateBinary() async -> DiscoveryOutcome {
        let call = calls.withLock { calls in
            calls += 1
            return calls
        }
        let skip = SkippedCandidate(candidate: mine, reason: "not executable")
        return call == 1 ? .notFound(skipped: [skip]) : .found(ScriptedStarter.binary, skipped: [skip])
    }

    func openChat(binary: URL, launch: ChatLaunch) -> any ChatSession { session }
}

/// 4c, the third defect: the app ran another binary than the one the person
/// pointed it at, and said nothing.
@Suite("A failing override", .timeLimit(.minutes(1)))
@MainActor
struct OverrideSkippedTests {
    @Test("an override that fails while a release install answers is said once at launch, with its reason")
    func saidOnceAtLaunch() async throws {
        let install = try FakeInstall()
        defer { install.remove() }
        let release = try install.binary(".local/bin/apogee", FakeInstall.release)
        let mine = try install.binary("tools/apogee", FakeInstall.broken)
        let store = MemoryStore()
        let preferences = Preferences(store: store)
        preferences.binaryOverride = mine
        let client = ApogeeClient(
            identity: ClientIdentity(version: "test"), spawner: ProcessSpawner(), preferences: preferences, home: install.home)
        let library = SessionLibraryViewModel(starter: client, runner: ScriptedRunner(), preferences: preferences)
        var said: [String] = []
        library.say = { said.append($0) }
        await library.start(inView: false)
        #expect(library.binaryURL == release, "the release answered")
        #expect(said.count == 1)
        #expect(said.first?.contains(mine.path) == true, "the override named: \(said)")
        #expect(said.first?.contains("`version` exited with status 1: error: broken install") == true, "its reason said: \(said)")
        #expect(said.first?.contains(release.path) == true, "the binary in use named: \(said)")
        #expect(library.skippedOverride?.path == mine.path)
        #expect(library.skippedOverride?.reason == "`version` exited with status 1: error: broken install")
        // The window opened again: the launch's word is not said twice.
        await library.shutDown()
        await library.start(inView: false)
        #expect(said.count == 1)
    }

    @Test("an override that answers is the one run, and nothing is said")
    func overrideInUse() async throws {
        let install = try FakeInstall()
        defer { install.remove() }
        try install.binary(".local/bin/apogee", FakeInstall.release)
        let mine = try install.binary("tools/apogee", FakeInstall.release)
        let preferences = Preferences(store: MemoryStore())
        preferences.binaryOverride = mine
        let client = ApogeeClient(
            identity: ClientIdentity(version: "test"), spawner: ProcessSpawner(), preferences: preferences, home: install.home)
        let library = SessionLibraryViewModel(starter: client, runner: ScriptedRunner(), preferences: preferences)
        var said: [String] = []
        library.say = { said.append($0) }
        await library.start(inView: false)
        #expect(library.binaryURL == mine)
        #expect(library.skippedOverride == nil)
        #expect(said.isEmpty)
    }

    @Test("a session that locates for itself -- none found at launch -- says a passed-over override too, once")
    func saidBySessionsOwnLocate() async throws {
        let starter = LaterStarter()
        let library = SessionLibraryViewModel(starter: starter, runner: ScriptedRunner(), preferences: Preferences(store: MemoryStore()))
        var said: [String] = []
        library.say = { said.append($0) }
        await library.start(inView: true)
        #expect(library.listing == .missing)
        #expect(await eventually { library.skippedOverride != nil })
        #expect(library.skippedOverride?.path == "/Users/someone/tools/apogee")
        #expect(library.skippedOverride?.reason == "not executable")
        #expect(said == ["Not using /Users/someone/tools/apogee: not executable -- running /Users/someone/.local/bin/apogee instead"])
        await library.shutDown()
    }
}
