import BusinessSession
import DataProtocol
import Foundation
import PresentationSession
import Testing

/// GUI M13: the session names its suite and, for execute, whether it
/// orchestrates (the CLI's M29, schema `2026-10-26`) -- over the branch
/// binary's own recordings: a chat opened `--suite pair`, then resumed with no
/// `--suite` (its own suite, which nothing said before M29), resumed `--suite
/// fast` and `--suite off`, a new chat under the default suite, and execute
/// under a suite whose `orchestrate: true` says so, under `--orchestrate`, and
/// under neither. A binary before M29 (4i's recordings, schema `2026-10-25`)
/// says none of it, and the app's own launch stays the fallback.
@Suite("The session says its suite (M13)", .timeLimit(.minutes(1)))
@MainActor
struct SuiteSaidTests {
    static var chatID: String {
        JSONValue.parse(fixtureStream("chat-under-suite-m29")[0])?["chat_id"]?.stringValue ?? ""
    }

    /// The admission line M29 sends after `session` for each suite recorded.
    static func admission(_ suite: String) -> String { "suite \(suite): no member holds memory on this machine" }

    /// A session over scripted children, started, its first child having said
    /// `stream`'s lines up to its first turn -- its `session` event and the
    /// notices M29 sends with it.
    func ready(_ stream: String, launch: ChatLaunch, children: [ScriptedSession]) async -> (SessionViewModel, ScriptedStarter) {
        let starter = ScriptedStarter(sessions: children)
        let model = SessionViewModel(starter: starter, launch: launch)
        model.start()
        Self.opening(stream).forEach(children[0].emit)
        _ = await eventually { model.transcript.status == .ready }
        return (model, starter)
    }

    /// A stream's lines before its first turn: `session`, and what it said at launch.
    static func opening(_ stream: String) -> [String] {
        Array(fixtureStream(stream).prefix { JSONValue.parse($0)?["turn"] == nil })
    }

    /// One recorded turn through `child`: sent, then the stream's turn events.
    func turn(_ model: SessionViewModel, _ child: ScriptedSession, _ stream: String) async {
        model.draft = "Which suite answers?"
        model.submit()
        for line in fixtureStream(stream) where JSONValue.parse(line)?["turn"] != nil { child.emit(line) }
        _ = await eventually { model.transcript.acceptsInput }
    }

    func notes(_ model: SessionViewModel) -> [String] {
        model.transcript.items.compactMap { if case .notice(let text) = $0.content { text } else { nil } }
    }

    @Test("a chat resumed with no --suite: the header says the suite the CLI says it runs under -- its own, which no flag named")
    func resumedSaysItsSuite() async {
        let (model, _) = await ready("resumed-under-suite-m29", launch: ChatLaunch(resume: Self.chatID), children: [ScriptedSession()])
        #expect(model.suite == nil, "no --suite was passed")
        #expect(model.suiteWords == "suite pair", "the CLI's word: the chat's own suite")
    }

    @Test("a new chat under the default suite: the CLI names it, and it is the default the launch left it to")
    func newUnderTheDefault() async {
        let child = ScriptedSession()
        let starter = ScriptedStarter(sessions: [child])
        let model = SessionViewModel(starter: starter, launch: ChatLaunch())
        model.defaultSuite = "fast"
        model.start()
        Self.opening("chat-default-suite-m29").forEach(child.emit)
        #expect(await eventually { model.transcript.status == .ready })
        #expect(model.suiteWords == "suite fast, the default")
    }

    @Test("a binary that says suites and names none: the session runs under no suite, said so -- never left unsaid")
    func saidNone() async {
        let (model, _) = await ready("resumed-suite-off-m29", launch: ChatLaunch(resume: Self.chatID), children: [ScriptedSession()])
        #expect(model.suiteWords == "no suite")
    }

    @Test("the chooser stands on the suite the CLI names: moving to it again restarts nothing")
    func chooserOnTheSaidSuite() async {
        let first = ScriptedSession()
        let (model, starter) = await ready(
            "resumed-under-suite-m29", launch: ChatLaunch(resume: Self.chatID), children: [first, ScriptedSession()])
        #expect(model.runningSuite == "pair", "no flag named it")
        await model.switchSuite(to: "pair")
        #expect(starter.openedCount == 1 && !first.calls.contains("stop"))
        let (off, _) = await ready("resumed-suite-off-m29", launch: ChatLaunch(resume: Self.chatID), children: [ScriptedSession()])
        #expect(off.runningSuite == SessionViewModel.noSuite)
    }

    @Test("a binary before M29 names no suite: read as not said, the launch's flags and the config the fallback, as before")
    func olderNotSaid() async {
        let (resumed, _) = await ready("resumed-under-suite", launch: ChatLaunch(resume: Self.chatID), children: [ScriptedSession()])
        #expect(resumed.suiteWords == nil, "unsaid, never none")
        let (named, _) = await ready("chat-under-suite", launch: ChatLaunch(suite: "pair"), children: [ScriptedSession()])
        #expect(named.suiteWords == "suite pair", "the launch's flag")
        let (off, _) = await ready("resumed-suite-off", launch: ChatLaunch(resume: Self.chatID, suite: "off"), children: [ScriptedSession()])
        #expect(off.suiteWords == "no suite", "--suite off, the launch's")
    }

    @Test("execute under a suite whose orchestrate: true says so, no flag passed: the banner says it orchestrates, the CLI's word")
    func executeOrchestrating() async {
        let (model, _) = await ready(
            "execute-orchestrating", launch: ChatLaunch(execute: ChatLaunch.Execute(suite: "orch")), children: [ScriptedSession()])
        #expect(model.executeBanner == "execute · suite orch · orchestrating")
    }

    @Test("execute not orchestrating, said so; --orchestrate said back; an older binary's banner from the door's flags")
    func executeBanners() async {
        let (plain, _) = await ready(
            "execute-not-orchestrating", launch: ChatLaunch(execute: ChatLaunch.Execute(suite: "pair")), children: [ScriptedSession()])
        #expect(plain.executeBanner == "execute · suite pair")
        let (flagged, _) = await ready(
            "execute-orchestrate-flag", launch: ChatLaunch(execute: ChatLaunch.Execute(suite: "pair", orchestrate: true)),
            children: [ScriptedSession()])
        #expect(flagged.executeBanner == "execute · suite pair · orchestrating")
        let (older, _) = await ready(
            "execute-orchestrate", launch: ChatLaunch(execute: ChatLaunch.Execute(suite: "duo", orchestrate: true)),
            children: [ScriptedSession()])
        #expect(older.executeBanner == "execute · suite duo · orchestrating", "the door's flags, as before")
    }

    @Test("the suite's admission line is drawn as the CLI's notice, once")
    func admissionOnce() async {
        let child = ScriptedSession()
        let (model, _) = await ready("chat-under-suite-m29", launch: ChatLaunch(suite: "pair"), children: [child])
        await turn(model, child, "chat-under-suite-m29")
        #expect(notes(model) == [Self.admission("pair")])
    }

    @Test("a switch on a binary that sends the notice: the CLI's admission line says it, the app's own line dropped -- one line, not two")
    func switchSaidOnce() async {
        let first = ScriptedSession()
        let second = ScriptedSession()
        let (model, starter) = await ready(
            "chat-under-suite-m29", launch: ChatLaunch(resume: Self.chatID, suite: "pair"), children: [first, second])
        await turn(model, first, "chat-under-suite-m29")
        await model.switchSuite(to: "fast")
        #expect(await eventually { starter.openedCount == 2 })
        Self.opening("resumed-switched-suite-m29").forEach(second.emit)
        #expect(await eventually { model.transcript.status == .ready })
        // The admission notice follows the `session` event, so it is folded a
        // moment after the status reads ready: wait for it, not for the status.
        #expect(await eventually { notes(model) == [Self.admission("pair"), Self.admission("fast")] })
        #expect(!notes(model).contains { $0.hasPrefix("switching to suite") }, "the app's own line, said by the CLI")
        #expect(model.suiteWords == "suite fast")
    }

    @Test("left and come back: the resumed child's admission line repeats the one shown, and is not drawn twice")
    func pausedAndResumedOnce() async {
        let first = ScriptedSession()
        let second = ScriptedSession()
        let (model, starter) = await ready(
            "chat-under-suite-m29", launch: ChatLaunch(resume: Self.chatID, suite: "pair"), children: [first, second])
        await turn(model, first, "chat-under-suite-m29")
        await model.pause()
        model.resume()
        #expect(await eventually { starter.openedCount == 2 })
        Self.opening("resumed-under-suite-m29").forEach(second.emit)
        #expect(await eventually { model.transcript.status == .ready })
        #expect(notes(model) == [Self.admission("pair")])
    }

    @Test("--suite off on that binary: the CLI says nothing of none, so the app's own line stands")
    func offStillSaid() async {
        let first = ScriptedSession()
        let second = ScriptedSession()
        let (model, starter) = await ready(
            "chat-under-suite-m29", launch: ChatLaunch(resume: Self.chatID, suite: "pair"), children: [first, second])
        await turn(model, first, "chat-under-suite-m29")
        await model.switchSuite(to: SessionViewModel.noSuite)
        #expect(await eventually { starter.openedCount == 2 })
        Self.opening("resumed-suite-off-m29").forEach(second.emit)
        #expect(await eventually { model.transcript.status == .ready })
        #expect(notes(model).last?.hasPrefix("leaving the suite (--suite off)") == true)
        #expect(model.suiteWords == "no suite")
    }
}
