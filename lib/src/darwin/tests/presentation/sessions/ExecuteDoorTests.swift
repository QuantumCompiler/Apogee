import BusinessReads
import BusinessSession
import DataPreferences
import DataProtocol
import Foundation
import InfrastructureProcess
import PresentationSession
import Testing

@testable import PresentationSessions

/// The execute door: `apogee execute` opened as its sheet says -- the suite or
/// the config's, a model, the folder, orchestrate -- through the one live
/// child, with the symphony catalog handed to `/play`.
@Suite("ExecuteDoor", .timeLimit(.minutes(1)))
@MainActor
struct ExecuteDoorTests {
    static let list = #"{"object":"list","data":[]}"#
    static let symphonies = #"{"object":"list","data":[{"name":"echo2","description":"Two stages.","source":"config","stages":[]},{"name":"digest","description":"Summarize a long text, then verify the summary against it.","source":"shipped","stages":[]}],"problems":[]}"#

    func library(_ runner: ScriptedRunner, _ sessions: [ScriptedSession]) -> (SessionLibraryViewModel, ScriptedStarter) {
        let starter = ScriptedStarter(sessions: sessions)
        let library = SessionLibraryViewModel(starter: starter, runner: runner, preferences: Preferences(store: MemoryStore()))
        return (library, starter)
    }

    func runner(defaultSuite: String) -> ScriptedRunner {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], Self.list)
        runner.reading(["symphonies", "list"], Self.symphonies)
        runner.commanding(["config", "get", "suites"], .done(stdout: "duo\nfast\n"))
        runner.commanding(["config", "get", "models.default_suite"], .done(stdout: defaultSuite + "\n"))
        return runner
    }

    @Test("the door reads the suites and the default, and opens execute under a named suite with orchestrate, in its folder")
    func suiteOrchestrateFolder() async {
        let runner = runner(defaultSuite: "")
        let (library, starter) = library(runner, [ScriptedSession(), ScriptedSession()])
        await library.start(inView: true)
        await library.openExecuteDoor()
        #expect(library.executeDoorOpen)
        #expect(library.suites == ["duo", "fast"])
        #expect(library.defaultSuite == "")
        #expect(!library.canOrchestrate, "no suite named and none the default: bare, where the CLI refuses --orchestrate")
        let project = URL(fileURLWithPath: "/Users/someone/code/ingest")
        library.executeForm.suite = "duo"
        library.executeForm.orchestrate = true
        library.executeForm.folder = project
        #expect(library.canOrchestrate)
        await library.openExecute()
        #expect(!library.executeDoorOpen)
        #expect(await eventually { starter.openedCount == 2 })
        let launch = starter.launches.last!
        #expect(launch == ChatLaunch(execute: ChatLaunch.Execute(suite: "duo", orchestrate: true), folder: project))
        #expect(launch.arguments == [
            "execute", "--output-format", "stream-json", "--input-format", "stream-json", "--tools",
            "--suite", "duo", "--orchestrate",
        ])
        #expect(library.current.isExecute)
        #expect(library.current.folder == project, "@ completes from where the session works")
        #expect(library.current.plays.map(\.name) == ["echo2", "digest"], "the catalog /play offers, read before it opened")
        #expect(library.current.executeBanner == "execute · suite duo · orchestrating · in ingest")
        #expect(runner.calls.contains("read symphonies list"))
    }

    @Test("bare on one model: no --suite, -m as chosen, and orchestrate never sent without a suite to offer")
    func bare() async {
        let runner = runner(defaultSuite: "")
        let (library, starter) = library(runner, [ScriptedSession(), ScriptedSession()])
        await library.start(inView: true)
        await library.openExecuteDoor()
        library.executeForm.model = "helper"
        library.executeForm.orchestrate = true
        await library.openExecute()
        #expect(await eventually { starter.openedCount == 2 })
        #expect(starter.launches.last!.arguments == [
            "execute", "--output-format", "stream-json", "--input-format", "stream-json", "--tools", "-m", "helper",
        ])
        #expect(library.current.executeBanner == "execute · no suite")
    }

    @Test("left to the config with a default suite: no --suite on the line, the banner names the default, orchestrate allowed")
    func defaultSuite() async {
        let runner = runner(defaultSuite: "duo")
        let (library, starter) = library(runner, [ScriptedSession(), ScriptedSession()])
        await library.start(inView: true)
        await library.openExecuteDoor()
        #expect(library.canOrchestrate)
        library.executeForm.orchestrate = true
        await library.openExecute()
        #expect(await eventually { starter.openedCount == 2 })
        #expect(starter.launches.last!.arguments.suffix(2) == ["--tools", "--orchestrate"])
        #expect(library.current.executeBanner == "execute · suite duo, the default · orchestrating")
    }

    @Test("a chat is no execute session: no banner, no /play, nothing offered after it")
    func chatHasNoPlay() async {
        let runner = runner(defaultSuite: "")
        let (library, _) = library(runner, [ScriptedSession()])
        await library.start(inView: true)
        #expect(!library.current.isExecute)
        #expect(library.current.executeBanner == nil)
        library.current.draft = "/pl"
        #expect(library.current.slashSuggestions.isEmpty)
        library.current.draft = "/play e"
        #expect(library.current.playSuggestions.isEmpty)
    }

    @Test("/play in an execute session: offered, then the symphonies by name, the line completed for its input and sent as typed")
    func playLine() async {
        let runner = runner(defaultSuite: "duo")
        let first = ScriptedSession()
        let second = ScriptedSession()
        let (library, _) = library(runner, [first, second])
        await library.start(inView: true)
        await library.openExecuteDoor()
        await library.openExecute()
        second.emit(#"{"type":"session","protocol_version":1,"model":"root","next_turn":1}"#)
        let session = library.current
        #expect(await eventually { session.transcript.status == .ready })
        session.draft = "/pl"
        #expect(session.slashSuggestions.map(\.name) == ["/play"])
        session.run(session.slashSuggestions[0])
        #expect(session.draft == "/play ")
        #expect(session.playSuggestions.map(\.name) == ["echo2", "digest"])
        session.draft = "/play ec"
        #expect(session.playSuggestions.map(\.name) == ["echo2"])
        session.complete(play: "echo2")
        #expect(session.draft == "/play echo2 ")
        #expect(session.playSuggestions.isEmpty, "the name is in: what follows is the input")
        session.draft += "hello"
        session.submit()
        #expect(await eventually { second.calls == ["user: /play echo2 hello"] })
    }
}
