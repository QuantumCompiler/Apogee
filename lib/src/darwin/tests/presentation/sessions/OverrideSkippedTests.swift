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

/// GUI 6b, the first finding: a binary that Try again finds -- nothing found
/// at launch, apogee installed meanwhile -- was never the library's, so the
/// chat answered while the list, its acts and a private session's promised
/// delete went on as if there were no binary.
@Suite("Try again adopts the binary (GUI 6b)", .timeLimit(.minutes(1)))
@MainActor
struct TryAgainAdoptsTests {
    static let found = DiscoveryOutcome.found(ScriptedStarter.binary, skipped: [])

    /// The library launched with no binary found: the list says so, and the
    /// session window shows how to install.
    func launchedMissing(_ runner: ScriptedRunner, _ sessions: [ScriptedSession]) async -> (SessionLibraryViewModel, ScriptedStarter) {
        let starter = ScriptedStarter(outcome: .notFound(skipped: []), sessions: sessions)
        let library = SessionLibraryViewModel(starter: starter, runner: runner, preferences: Preferences(store: MemoryStore()))
        await library.start(inView: true)
        #expect(library.listing == .missing)
        #expect(await eventually { if case .missing = library.current.connection { true } else { false } })
        return (library, starter)
    }

    /// What the scripted binary answers once it is installed: the list, the
    /// models, the roles plain and under rich, its vocabulary and recall.
    func installedRunner() -> ScriptedRunner {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], SavedChatActsTests.listed)
        runner.reading(["models", "list"], SessionLibraryPrivateTests.models)
        runner.reading(["models", "status"], printed("roles-status-duo.json"))
        runner.reading(["models", "status", "--suite", "rich"], printed("roles-status-rich.json"))
        runner.commanding(["__machine-schema"], .done(stdout: schema("machine-schema")))
        runner.commanding(["chats", "recall"], .done(stdout: printed("chats-recall.txt")))
        return runner
    }

    static let recallHolds = "recall holds 2 chat summaries in /Users/someone/.apogee/memory/chats.db"

    func roleValues(_ library: SessionLibraryViewModel) -> [String: String] {
        Dictionary(library.roles.map { ($0.label, $0.value) }, uniquingKeysWith: { first, _ in first })
    }

    @Test("Try again's binary is the library's: its capabilities asked, the list, the models and the open inspector's reads made, handed up -- and Rename, Delete and the roles run on it")
    func tryAgainAdopts() async throws {
        let runner = installedRunner()
        let child = ScriptedSession()
        let (library, starter) = await launchedMissing(runner, [child])
        var handed: [URL] = []
        library.binaryAdopted = { handed.append($0) }
        // The inspector, open while nothing was installed: its one read as
        // it appeared had nothing to read with.
        await library.refreshInspector()
        #expect(runner.calls.isEmpty, "nothing to read with")
        #expect(library.inspector.roles.isEmpty && library.inspector.recall == nil)
        starter.outcome = Self.found
        // The window's Try again: the session locates for itself.
        library.current.start()
        #expect(await eventually { handed == [ScriptedStarter.binary.url] }, "handed up once, for every place")
        #expect(library.binaryURL == ScriptedStarter.binary.url && library.binaryVersion == "0.1.9")
        #expect(runner.calls.prefix(3) == ["run __machine-schema", "read chats list", "read models list"], "asked, then read, as a launch does")
        #expect(
            runner.calls.filter { $0.hasPrefix("read models status") || $0 == "run chats recall" } == ["read models status", "run chats recall"],
            "the open inspector's reads, made once each before every place was handed it")
        #expect(library.capabilitiesKnown && library.hasTrack52)
        #expect(library.listing == .read && library.chats.count == 2)
        #expect(library.emptyText == nil, "never 'Apogee isn't installed' again")
        #expect(library.current.modelChoices == ["local", "claude"])
        #expect(starter.openedCount == 1, "the chat itself connected on it")
        // Neither read was asked for again: the inspector on screen has both.
        #expect(roleValues(library)["utility"] == "m2   (via suite duo)", "the roles, the session's suite unsaid: plain")
        #expect(library.inspector.recall == Self.recallHolds, "the Recall section, and its Forget, drawn")
        let chat = try #require(library.chats.first)
        await library.rename(chat, to: "Ports")
        #expect(runner.calls.contains("run chats title \(chat.id) -- Ports"))
        library.askToDelete(chat)
        await library.confirm()
        #expect(runner.calls.contains("run chats delete \(chat.id)"))
        // The session says its own suite: the roles follow it, read once by
        // the adoption.
        child.emit(#"{"type":"session","protocol_version":1,"model":"m3","next_turn":1,"suite":"rich"}"#)
        #expect(await eventually { runner.calls.contains("read models status --suite rich") }, "read again under the session's suite")
        #expect(await eventually { roleValues(library)["utility"] == "m4   (via suite rich)" })
        #expect(library.inspector.suite == "rich")
        await library.shutDown()
    }

    @Test("the inspector that appeared before the launch's discovery answered: its reads are made once the binary is found, never left with neither")
    func inspectorBeforeTheLaunchsBinary() async {
        let runner = installedRunner()
        let library = SessionLibraryViewModel(
            starter: ScriptedStarter(sessions: [ScriptedSession()]), runner: runner, preferences: Preferences(store: MemoryStore()))
        // The window drew the inspector -- its one read -- before discovery answered.
        await library.refreshInspector()
        #expect(runner.calls.isEmpty, "nothing to read with")
        await library.start(inView: true)
        #expect(runner.calls.filter { $0.hasPrefix("read models status") } == ["read models status"], "the roles, once")
        #expect(runner.calls.filter { $0 == "run chats recall" }.count == 1, "recall, once")
        #expect(roleValues(library)["utility"] == "m2   (via suite duo)")
        #expect(library.inspector.recall == Self.recallHolds)
        await library.shutDown()
    }

    @Test("an inspector that has read once nothing is owed: the window opened again reads nothing more of it")
    func nothingOwedOnceRead() async {
        let runner = installedRunner()
        let library = SessionLibraryViewModel(
            starter: ScriptedStarter(sessions: [ScriptedSession(), ScriptedSession()]), runner: runner,
            preferences: Preferences(store: MemoryStore()))
        await library.start(inView: true)
        await library.refreshInspector()
        #expect(runner.calls.filter { $0.hasPrefix("read models status") }.count == 1)
        await library.shutDown()
        await library.start(inView: true)
        #expect(runner.calls.filter { $0.hasPrefix("read models status") }.count == 1, "owed nothing")
        #expect(runner.calls.filter { $0 == "run chats recall" }.count == 1)
        await library.shutDown()
    }

    @Test("an older binary's private fallback, opened before the binary was found: its promised delete runs once Try again's binary is the library's")
    func privateFallbackDeletedOnTheAdoptedBinary() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], SessionLibraryPrivateTests.list)
        let secret = ScriptedSession("secret")
        let next = ScriptedSession("next")
        let (library, starter) = await launchedMissing(runner, [secret, next])
        starter.outcome = Self.found
        // ⇧⌘N: a private session, on a build that has not said it has
        // --private -- the fallback, deleted when it closes.
        await library.newPrivateSession()
        #expect(await eventually { starter.openedCount == 1 })
        #expect(starter.launches.first == ChatLaunch(noRecall: true))
        #expect(await eventually { runner.calls.contains("read models list") }, "the binary taken up")
        secret.emit(SessionLibraryPrivateTests.old)
        _ = await eventually { library.current.transcript.status == .ready }
        runner.reading(["chats", "list"], #"{"object":"list","data":[{"id":"p9","name":"secret","turns":1,"updated":"2026-10-20T17:00:00Z"},{"id":"c1","name":"Recipes","turns":1,"updated":"2026-10-12T09:00:00Z"}]}"#)
        library.current.draft = "something private"
        library.current.submit()
        secret.emit(#"{"type":"result","text":"ok","turn":1}"#)
        #expect(await eventually { library.current.chatID == "p9" }, "its own chat, found against the list the adoption read")
        await library.newSession()
        #expect(runner.calls.contains("run chats delete p9"), "deleted when it closed, as it promised")
        await library.shutDown()
    }

    @Test("a session that finds the binary the library already has adopts nothing: nothing is asked or read again")
    func sameBinaryAdoptsNothing() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], SavedChatActsTests.listed)
        runner.commanding(["__machine-schema"], .done(stdout: schema("machine-schema")))
        let child = ScriptedSession()
        let library = SessionLibraryViewModel(
            starter: ScriptedStarter(sessions: [child]), runner: runner, preferences: Preferences(store: MemoryStore()))
        var handed = 0
        library.binaryAdopted = { _ in handed += 1 }
        await library.start(inView: true)
        child.emit(SuiteLibraryTests.session)
        #expect(await eventually { library.current.transcript.status == .ready })
        #expect(handed == 0)
        #expect(runner.calls.filter { $0 == "run __machine-schema" }.count == 1)
        await library.shutDown()
    }
}
