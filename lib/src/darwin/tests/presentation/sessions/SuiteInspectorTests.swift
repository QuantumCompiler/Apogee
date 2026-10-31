import BusinessReads
import BusinessSession
import DataPreferences
import DataProtocol
import Foundation
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
