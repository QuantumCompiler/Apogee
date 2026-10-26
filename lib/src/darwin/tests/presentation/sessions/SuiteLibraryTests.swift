import BusinessReads
import BusinessSession
import DataPreferences
import DataProtocol
import Foundation
import InfrastructureProcess
import PresentationSession
import Testing

@testable import PresentationSessions

/// GUI 4i: the library's side of a chat under a suite -- the chooser's
/// suites with the CLI's verdict on each (`models status --suite <name>`), a
/// new chat opened under one, and the open session moved -- over the reads
/// recorded from the branch's binary in a sandbox.
@Suite("Sessions under a suite", .timeLimit(.minutes(1)))
@MainActor
struct SuiteLibraryTests {
    static let session = #"{"type":"session","protocol_version":1,"model":"plain","next_turn":1}"#
    static let names = ["cloud", "fast", "pair", "shared", "vast"]

    static func recorded(_ name: String) -> String {
        let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            .appendingPathComponent("../../business/reads/Fixtures/\(name)")
        return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
    }

    func library(_ starter: ScriptedStarter) async -> (SessionLibraryViewModel, ScriptedRunner) {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], #"{"object":"list","data":[]}"#)
        runner.commanding(["config", "get", "suites"], .done(stdout: Self.recorded("suites-config-get.txt")))
        runner.commanding(["config", "get", "models.default_suite"], .done(stdout: Self.recorded("suites-default-suite.txt")))
        for name in Self.names {
            runner.reading(["models", "status", "--suite", name], Self.recorded("suites-models-status-\(name).json"))
        }
        let library = SessionLibraryViewModel(starter: starter, runner: runner, preferences: Preferences(store: MemoryStore()))
        await library.start(inView: true)
        return (library, runner)
    }

    @Test("the chooser: every suite, the default marked, the CLI's verdict on each -- then shown")
    func chooserReadsVerdicts() async throws {
        let child = ScriptedSession()
        let (library, runner) = await library(ScriptedStarter(sessions: [child]))
        child.emit(Self.session)
        #expect(await eventually { library.current.transcript.status == .ready })
        await library.chooseSuite()
        let choices = library.current.suiteChoices
        #expect(choices.map(\.name) == Self.names)
        #expect(choices.filter(\.isDefault).map(\.name) == ["fast"])
        #expect(choices.first { $0.name == "shared" }?.verdict == "fits")
        #expect(choices.first { $0.name == "vast" }?.overBudget == true)
        #expect(choices.first { $0.name == "fast" }?.verdict == "no member holds memory on this machine")
        #expect(library.current.defaultSuite == "fast" && library.current.presenting == .suites)
        #expect(Self.names.allSatisfy { runner.calls.contains("read models status --suite \($0)") })
    }

    @Test("the session's /suite reaches the library's chooser")
    func slashOpensTheChooser() async {
        let child = ScriptedSession()
        let (library, _) = await library(ScriptedStarter(sessions: [child]))
        child.emit(Self.session)
        #expect(await eventually { library.current.transcript.status == .ready })
        library.current.draft = "/suite"
        library.current.run(library.current.slashSuggestions[0])
        #expect(await eventually { library.current.presenting == .suites })
    }

    @Test("Open a chat under it: a new session launched --suite, forced only when asked")
    func newSessionUnderSuite() async {
        let first = ScriptedSession()
        let second = ScriptedSession()
        let third = ScriptedSession()
        let starter = ScriptedStarter(sessions: [first, second, third])
        let (library, _) = await library(starter)
        first.emit(Self.session)
        #expect(await eventually { library.current.transcript.status == .ready })
        await library.newSession(under: "pair")
        #expect(await eventually { starter.openedCount == 2 })
        #expect(starter.launches[1].arguments.suffix(2) == ["--suite", "pair"] && starter.launches[1].resume == nil)
        await library.newSession(under: "vast", force: true)
        #expect(await eventually { starter.openedCount == 3 })
        #expect(starter.launches[2].arguments.suffix(3) == ["--suite", "vast", "--force"])
        #expect(starter.live <= 1, "one live child")
    }

    @Test("Switch the session to it: the open session relaunched under the suite; nothing to switch once shut")
    func switchFromTheLibrary() async {
        let first = ScriptedSession()
        let second = ScriptedSession()
        let starter = ScriptedStarter(sessions: [first, second])
        let (library, _) = await library(starter)
        first.emit(Self.session)
        #expect(await eventually { library.current.transcript.status == .ready })
        #expect(library.canSwitchSuite)
        await library.switchSuite(to: "fast")
        #expect(await eventually { starter.openedCount == 2 })
        #expect(starter.launches[1].suite == "fast")
        await library.shutDown()
        #expect(!library.canSwitchSuite)
    }
}
