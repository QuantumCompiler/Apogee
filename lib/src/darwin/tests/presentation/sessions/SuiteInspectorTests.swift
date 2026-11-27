import BusinessReads
import BusinessSession
import DataPreferences
import DataProtocol
import Foundation
import InfrastructureProcess
import PresentationSession
import Testing

@testable import PresentationSessions

/// GUI M13: the inspector says the suite a session runs under -- a resumed
/// chat's own, which the CLI's `session` event names since its M29 -- and
/// whether an execute session orchestrates, over the branch binary's own
/// recordings; a binary before M29 says neither, and the inspector falls back
/// to what the app launched.
@Suite("The inspector says the suite (M13)", .timeLimit(.minutes(1)))
@MainActor
struct SuiteInspectorTests {
    static func stream(_ name: String) -> [String] {
        let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            .appendingPathComponent("../../data/protocol/Fixtures/\(name).jsonl")
        return ((try? String(contentsOf: url, encoding: .utf8)) ?? "").split(separator: "\n").map(String.init)
    }

    /// A stream's lines before its first turn: `session`, and its launch notices.
    static func opening(_ name: String) -> [String] {
        stream(name).prefix { JSONValue.parse($0)?["turn"] == nil }.map { $0 }
    }

    static func chatID(_ name: String) -> String {
        JSONValue.parse(stream(name)[0])?["chat_id"]?.stringValue ?? ""
    }

    /// The library started, its new session's child spoken -- the recorded
    /// chat's opening, as any session's -- before the test opens another.
    func library(_ starter: ScriptedStarter) async -> SessionLibraryViewModel {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], #"{"object":"list","data":[]}"#)
        let library = SessionLibraryViewModel(starter: starter, runner: runner, preferences: Preferences(store: MemoryStore()))
        await library.start(inView: true)
        _ = await eventually { starter.openedCount == 1 }
        starter.handed.first?.emit(Self.stream("chat-on-good")[0])
        _ = await eventually { library.current.transcript.status == .ready }
        return library
    }

    func facts(_ library: SessionLibraryViewModel) -> [String: String] {
        Dictionary(library.inspector.modelFacts.map { ($0.label, $0.value) }, uniquingKeysWith: { first, _ in first })
    }

    @Test("a chat resumed with no --suite: the inspector names the suite the CLI says it runs under")
    func resumedChatsSuite() async {
        let fresh = ScriptedSession()
        let resumed = ScriptedSession()
        let library = await library(ScriptedStarter(sessions: [fresh, resumed]))
        let id = Self.chatID("resumed-under-suite-m29")
        await library.open(.chat(id))
        Self.opening("resumed-under-suite-m29").forEach(resumed.emit)
        #expect(await eventually { library.current.transcript.status == .ready })
        #expect(facts(library)["Suite"] == "pair")
    }

    @Test("an execute session: its suite, and whether it orchestrates -- the CLI's word, where the door passed no flag")
    func executeOrchestrates() async {
        let fresh = ScriptedSession()
        let door = ScriptedSession()
        let library = await library(ScriptedStarter(sessions: [fresh, door]))
        library.executeForm.suite = "orch"
        await library.openExecute()
        Self.opening("execute-orchestrating").forEach(door.emit)
        #expect(await eventually { library.current.transcript.status == .ready })
        #expect(facts(library)["Suite"] == "orch")
        #expect(facts(library)["Orchestrating"] == "yes")
    }

    @Test("a chat the CLI says runs under no suite: the inspector says none")
    func saidNone() async {
        let fresh = ScriptedSession()
        let resumed = ScriptedSession()
        let library = await library(ScriptedStarter(sessions: [fresh, resumed]))
        await library.open(.chat(Self.chatID("resumed-suite-off-m29")))
        Self.opening("resumed-suite-off-m29").forEach(resumed.emit)
        #expect(await eventually { library.current.transcript.status == .ready })
        #expect(facts(library)["Suite"] == "none")
    }

    // MARK: - Roles under the session's own suite (GUI 6b)

    /// `models status` as the branch's binary answered it over a sandbox whose
    /// `models.default_suite` is duo (utility m2): plain -- duo's members --
    /// under `--suite off`, and under `--suite rich`.
    func rolesRunner() -> ScriptedRunner {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], #"{"object":"list","data":[]}"#)
        runner.reading(["models", "status"], printed("roles-status-duo.json"))
        runner.reading(["models", "status", "--suite", "off"], printed("roles-status-off.json"))
        runner.reading(["models", "status", "--suite", "rich"], printed("roles-status-rich.json"))
        return runner
    }

    static let opened = #"{"type":"session","protocol_version":1,"model":"m1","next_turn":1}"#

    func values(_ library: SessionLibraryViewModel) -> [String: String] {
        Dictionary(library.roles.map { ($0.label, $0.value) }, uniquingKeysWith: { first, _ in first })
    }

    @Test("6b: a bare session's roles are read under --suite off -- no suite's members, no suite or footprint row -- and a session under rich reads rich's")
    func rolesFollowTheLaunchsSuite() async throws {
        let runner = rolesRunner()
        let first = ScriptedSession()
        let bare = ScriptedSession()
        let rich = ScriptedSession()
        let starter = ScriptedStarter(sessions: [first, bare, rich])
        let library = SessionLibraryViewModel(starter: starter, runner: runner, preferences: Preferences(store: MemoryStore()))
        await library.start(inView: true)
        first.emit(Self.opened)
        #expect(await eventually { library.current.transcript.status == .ready })
        await library.refreshRoles()
        #expect(runner.calls.filter { $0.hasPrefix("read models status") } == ["read models status"], "the suite unknown: the config's default, as plain models status resolves it")
        #expect(values(library)["utility"] == "m2   (via suite duo)")
        #expect(library.roles.map(\.label) == ["chat", "embedding", "extraction", "vision", "transcription", "utility"], "never the suite or its footprint")

        await library.newSession(under: "off")
        #expect(await eventually { starter.openedCount == 2 })
        bare.emit(Self.opened)
        #expect(await eventually { library.current.transcript.status == .ready })
        #expect(await eventually { runner.calls.contains("read models status --suite off") }, "read again: the session runs bare")
        #expect(await eventually { values(library)["utility"] == "(unset -- the chat's own backend)" })
        #expect(!library.roles.contains { $0.value.contains("via suite") }, "no suite's members")
        #expect(library.inspector.suite == "none")

        await library.newSession(under: "rich")
        #expect(await eventually { starter.openedCount == 3 })
        rich.emit(Self.opened)
        #expect(await eventually { runner.calls.contains("read models status --suite rich") })
        #expect(await eventually { values(library)["utility"] == "m4   (via suite rich)" })
        #expect(!library.roles.contains { ["suite", "footprint"].contains($0.label) })
        #expect(library.inspector.suite == "rich")
        await library.shutDown()
    }

    @Test("6b: /suite moves the session: its roles are read again, once, under the suite it moved to")
    func rolesFollowASwitch() async throws {
        let runner = rolesRunner()
        let first = ScriptedSession()
        let moved = ScriptedSession()
        let starter = ScriptedStarter(sessions: [first, moved])
        let library = SessionLibraryViewModel(starter: starter, runner: runner, preferences: Preferences(store: MemoryStore()))
        await library.start(inView: true)
        first.emit(Self.opened)
        #expect(await eventually { library.current.transcript.status == .ready })
        await library.refreshRoles()
        #expect(values(library)["chat"] == "m1   (via suite duo)")
        await library.switchSuite(to: "rich")
        #expect(await eventually { starter.openedCount == 2 })
        moved.emit(Self.opened)
        #expect(await eventually { runner.calls.contains("read models status --suite rich") }, "the move read Roles again")
        #expect(await eventually { values(library)["chat"] == "m3   (via suite rich)" })
        #expect(runner.calls.filter { $0.hasPrefix("read models status") } == ["read models status", "read models status --suite rich"], "once")
        await library.shutDown()
    }

    @Test("6b: a move to rich whose roles read fails shows the failure alone -- none of duo's rows under rich's Suite line")
    func failedReadAfterAMove() async throws {
        let runner = rolesRunner()
        // rich removed meanwhile: the branch's binary's refusal, recorded.
        var gone = BoundedTail()
        gone.append(Data(printed("roles-status-rich-gone.err").utf8))
        runner.reading(["models", "status", "--suite", "rich"], .failed(exit: ChildExit(reason: .exited, status: 1), stderrTail: gone))
        let first = ScriptedSession()
        let moved = ScriptedSession()
        let starter = ScriptedStarter(sessions: [first, moved])
        let library = SessionLibraryViewModel(starter: starter, runner: runner, preferences: Preferences(store: MemoryStore()))
        await library.start(inView: true)
        first.emit(Self.opened)
        #expect(await eventually { library.current.transcript.status == .ready })
        await library.refreshRoles()
        #expect(values(library)["chat"] == "m1   (via suite duo)")
        await library.switchSuite(to: "rich")
        #expect(await eventually { starter.openedCount == 2 })
        moved.emit(Self.opened)
        #expect(await eventually { library.rolesFailure != nil }, "the move read Roles again, and it failed")
        #expect(library.rolesFailure == "apogee models: --suite: no suite named 'rich' (configured: duo) (exited with status 1)", "in the CLI's words")
        #expect(library.roles.isEmpty && library.inspector.roles.isEmpty, "none of duo's rows")
        #expect(library.inspector.rolesFailure == library.rolesFailure)
        #expect(library.inspector.suite == "rich")
        await library.shutDown()
    }

    @Test("a binary before M29: a resumed chat's suite unsaid -- no Suite line made up -- and a named one the launch's")
    func olderFallback() async {
        let fresh = ScriptedSession()
        let resumed = ScriptedSession()
        let library = await library(ScriptedStarter(sessions: [fresh, resumed]))
        await library.open(.chat(Self.chatID("resumed-under-suite")))
        Self.opening("resumed-under-suite").forEach(resumed.emit)
        #expect(await eventually { library.current.transcript.status == .ready })
        #expect(facts(library)["Suite"] == nil, "unsaid, never none")
        #expect(facts(library)["Orchestrating"] == nil, "a chat says nothing of orchestration")
    }
}
