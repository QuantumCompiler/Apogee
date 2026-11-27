import BusinessReads
import BusinessSession
import DataProtocol
import Foundation
import InfrastructureProcess
import PresentationSession
import Testing

/// GUI 4i: a chat under a suite -- chosen at launch (`--suite`), switched by
/// a resume with `--suite` since machine mode carries no suite, and `/suite`
/// in the input -- over the branch binary's own recorded streams: a chat
/// opened `--suite pair` (its chat member `plain`), resumed `--suite fast`
/// (onto `fastroot`), resumed `--suite off`, a `set` naming a suite refused
/// whole, and `--suite vast` refused at launch as over this machine's memory.
@Suite("A chat under a suite", .timeLimit(.minutes(1)))
@MainActor
struct SuiteSessionTests {
    static func type(_ line: String) -> String? { JSONValue.parse(line)?["type"]?.stringValue }

    static var chatID: String {
        JSONValue.parse(fixtureStream("chat-under-suite")[0])?["chat_id"]?.stringValue ?? ""
    }

    /// A session over scripted children, started, its first child having said
    /// `stream`'s `session` event.
    func ready(
        _ stream: String = "chat-under-suite", launch: ChatLaunch, children: [ScriptedSession]
    ) async -> (SessionViewModel, ScriptedStarter) {
        let starter = ScriptedStarter(sessions: children)
        let model = SessionViewModel(starter: starter, launch: launch)
        model.start()
        children[0].emit(fixtureStream(stream)[0])
        _ = await eventually { model.transcript.status == .ready }
        return (model, starter)
    }

    /// One recorded turn through `child`: sent, then the stream's events after
    /// its `session` line.
    func turn(_ model: SessionViewModel, _ child: ScriptedSession, _ stream: String) async {
        model.draft = "Which suite answers?"
        model.submit()
        for line in fixtureStream(stream).dropFirst() { child.emit(line) }
        _ = await eventually { model.transcript.acceptsInput }
    }

    func notes(_ model: SessionViewModel) -> [String] {
        model.transcript.items.compactMap { if case .notice(let text) = $0.content { text } else { nil } }
    }

    @Test("/suite is among the session's slash commands, offered in a chat")
    func suiteSlash() {
        let model = SessionViewModel(starter: ScriptedStarter(sessions: [ScriptedSession()]))
        model.draft = "/su"
        #expect(model.slashSuggestions.map(\.name) == ["/suite"])
    }

    @Test("/suite asks the library for the chooser; the chooser's act needs a session it can carry on")
    func suiteSlashActs() async {
        let child = ScriptedSession()
        let (model, _) = await ready(launch: ChatLaunch(), children: [child])
        var acts: [SessionAction] = []
        model.onAction = { acts.append($0) }
        model.draft = "/suite"
        model.run(model.slashSuggestions[0])
        #expect(acts == [.chooseSuite] && model.draft.isEmpty)
    }

    @Test("a new chat under a suite: --suite at launch, and the session says which; the recorded chat answers on the suite's chat member")
    func launchUnderSuite() async {
        let child = ScriptedSession()
        let (model, starter) = await ready(launch: ChatLaunch(suite: "pair"), children: [child])
        #expect(starter.launches.first?.arguments.suffix(2) == ["--suite", "pair"])
        #expect(model.suite == "pair" && model.suiteWords == "suite pair")
        #expect(model.transcript.model == "plain", "pair's chat member")
        let none = SessionViewModel(starter: ScriptedStarter(sessions: []), launch: ChatLaunch(suite: SessionViewModel.noSuite))
        #expect(none.suiteWords == "no suite")
        let left = SessionViewModel(starter: ScriptedStarter(sessions: []), launch: ChatLaunch())
        #expect(left.suiteWords == nil, "unread: the CLI's own")
        left.defaultSuite = "fast"
        #expect(left.suiteWords == "suite fast, the default")
        let resumed = SessionViewModel(starter: ScriptedStarter(sessions: []), launch: ChatLaunch(resume: "c1"))
        resumed.defaultSuite = "fast"
        #expect(resumed.suiteWords == nil, "a resumed chat runs under the suite it last had, which no read states")
    }

    @Test("machine mode carries no suite: a set naming one is refused whole, as recorded -- so a switch is a relaunch")
    func protocolHasNoSuite() {
        let lines = fixtureStream("set-suite-refused")
        let refusal = lines.first { Self.type($0) == "notice" }.flatMap { JSONValue.parse($0)?["text"]?.stringValue }
        #expect(refusal == "set: 'suite' is not a setting a session moves, as text -- model, system, think, effort or temperature; nothing was set")
    }

    @Test("the global default, as recorded: a new chat with no --suite runs under models.default_suite (fast, on fastroot), --suite off under none (on plain) -- so none is always --suite off")
    func theGlobalDefault() {
        func model(_ stream: String) -> String? {
            JSONValue.parse(fixtureStream(stream)[0])?["model"]?.stringValue
        }
        #expect(model("chat-default-suite") == "fastroot")
        #expect(model("chat-suite-off") == "plain")
        #expect(model("resumed-suite-off") == "fastroot", "off keeps the conversation on its model, as /suite off does")
    }

    @Test("a switch between turns: the child stopped, the conversation resumed under the suite -- --resume and --suite, no -m -- said in the transcript")
    func switchRelaunches() async {
        let first = ScriptedSession()
        let second = ScriptedSession()
        let (model, starter) = await ready(
            launch: ChatLaunch(resume: Self.chatID, settings: RunSettings(model: "plain"), suite: "pair"), children: [first, second])
        await turn(model, first, "chat-under-suite")
        await model.switchSuite(to: "fast")
        #expect(first.calls.contains("stop"))
        #expect(await eventually { starter.openedCount == 2 })
        let relaunch = starter.launches[1]
        #expect(relaunch.resume == Self.chatID && relaunch.suite == "fast")
        #expect(relaunch.settings.model == nil, "-m would win over the suite's chat member, as /suite moves it")
        #expect(relaunch.arguments.suffix(4) == ["--resume", Self.chatID, "--suite", "fast"])
        #expect(notes(model).last == "switching to suite fast (--suite fast) -- machine mode carries no suite, so the session restarts under it; the conversation carries on on the suite's chat member, and a local model loads again")
        let resumed = fixtureStream("resumed-under-suite")
        second.emit(resumed[0])
        #expect(await eventually { model.transcript.status == .ready })
        #expect(model.transcript.model == "fastroot" && model.suiteWords == "suite fast")
        await turn(model, second, "resumed-under-suite")
        #expect(model.transcript.items.contains { if case .answer(let a) = $0.content { a.text == "FASTROOT-SAYS hello" } else { false } })
    }

    @Test("none: --suite off, the conversation staying on its model")
    func switchToNone() async {
        let first = ScriptedSession()
        let second = ScriptedSession()
        let (model, starter) = await ready(launch: ChatLaunch(resume: Self.chatID, suite: "fast"), children: [first, second])
        await model.switchSuite(to: SessionViewModel.noSuite)
        #expect(await eventually { starter.openedCount == 2 })
        #expect(starter.launches[1].arguments.suffix(2) == ["--suite", "off"])
        second.emit(fixtureStream("resumed-suite-off")[0])
        #expect(await eventually { model.transcript.status == .ready })
        #expect(model.transcript.model == "fastroot" && model.suiteWords == "no suite")
    }

    @Test("made mid-turn, the switch waits for the turn's end -- shown waiting -- then relaunches")
    func switchHeldMidTurn() async {
        let first = ScriptedSession()
        let second = ScriptedSession()
        let (model, starter) = await ready(launch: ChatLaunch(resume: Self.chatID, suite: "pair"), children: [first, second])
        model.draft = "Which suite answers?"
        model.submit()
        await model.switchSuite(to: "fast")
        #expect(model.held == [.suite("fast", force: false)] && model.held[0].words == "switch to suite fast")
        #expect(!first.calls.contains("stop") && starter.openedCount == 1, "the turn runs on")
        for line in fixtureStream("chat-under-suite").dropFirst() { first.emit(line) }
        #expect(await eventually { starter.openedCount == 2 })
        #expect(first.calls.contains("stop") && model.held.isEmpty)
        #expect(starter.launches[1].suite == "fast")
    }

    // MARK: - 6a, finding 2: a switch held over a new chat's first turn

    /// A new chat under `pair` whose library reads the list back after each
    /// turn (`movesAfterReadBack`), its first turn sent and the switch to
    /// `fast` held for its end; returns once the turn's `result` is in.
    func firstTurnWithSwitchHeld(_ first: ScriptedSession, _ second: ScriptedSession) async -> (SessionViewModel, ScriptedStarter) {
        let starter = ScriptedStarter(sessions: [first, second])
        let model = SessionViewModel(starter: starter, launch: ChatLaunch(suite: "pair"))
        model.movesAfterReadBack = true
        model.start()
        first.emit(fixtureStream("chat-under-suite")[0])
        _ = await eventually { model.transcript.status == .ready }
        #expect(model.chatID == Self.chatID && model.savedChatID == nil, "named by its session event, not yet written")
        model.draft = "Which suite answers?"
        model.submit()
        await model.switchSuite(to: "fast")
        #expect(model.held == [.suite("fast", force: false)])
        for line in fixtureStream("chat-under-suite").dropFirst() { first.emit(line) }
        _ = await eventually { model.transcript.acceptsInput }
        return (model, starter)
    }

    static let carried = "switching to suite fast (--suite fast) -- machine mode carries no suite, so the session restarts under it; the conversation carries on on the suite's chat member, and a local model loads again"

    @Test("a new chat's first turn, the switch held for its end: moved only once the list read after it names the chat written -- --resume, the turn kept, never 'can't be carried on'")
    func switchHeldOverAFirstTurn() async {
        let first = ScriptedSession()
        let second = ScriptedSession()
        let (model, starter) = await firstTurnWithSwitchHeld(first, second)
        // Released by the turn's end into the wait for the read -- the branch
        // that makes no move -- so nothing below can be a move still to come.
        #expect(model.moveAwaitsReadBack && model.held.isEmpty, "the move waits for the list's word")
        #expect(!first.calls.contains("stop") && starter.openedCount == 1)
        #expect(!notes(model).contains { $0.contains("can't be carried on") })
        // The library's read after the turn: the chat is listed, so written.
        model.identify(Self.chatID)
        await model.runHeldMove()
        #expect(!model.moveAwaitsReadBack)
        #expect(first.calls.contains("stop"))
        #expect(await eventually { starter.openedCount == 2 })
        #expect(starter.launches[1].resume == Self.chatID && starter.launches[1].suite == "fast")
        #expect(notes(model).last == Self.carried)
        #expect(!notes(model).contains { $0.contains("can't be carried on") })
        #expect(model.transcript.items.contains { $0.content == .user("Which suite answers?") }, "the first turn stays drawn: carried on")
    }

    @Test("the list read answering only after the child has stopped: the relaunch still carries --resume with the chat it names")
    func listReadAfterTheChildStopped() async {
        let first = ScriptedSession()
        let second = ScriptedSession()
        let (model, starter) = await firstTurnWithSwitchHeld(first, second)
        first.end()
        #expect(await eventually { model.transcript.status == .ended })
        model.identify(Self.chatID)
        await model.runHeldMove()
        #expect(await eventually { starter.openedCount == 2 })
        #expect(starter.launches[1].resume == Self.chatID && starter.launches[1].suite == "fast")
        #expect(!notes(model).contains { $0.contains("can't be carried on") })
    }

    @Test("a turn sent before the list read answers: the move is held again for that turn's end, never ending it")
    func moveHeldAgainForTheNextTurn() async {
        let first = ScriptedSession()
        let second = ScriptedSession()
        let (model, starter) = await firstTurnWithSwitchHeld(first, second)
        model.draft = "And another?"
        model.submit()
        model.identify(Self.chatID)
        await model.runHeldMove()
        #expect(!first.calls.contains("stop") && starter.openedCount == 1)
        #expect(model.held == [.suite("fast", force: false)], "said again: sent when this turn ends")
    }

    @Test("a suite the CLI refuses at launch -- over this machine's memory -- is put back, said in its words, and the session carries on under the one before")
    func refusedSuitePutBack() async {
        let first = ScriptedSession()
        let refused = ScriptedSession()
        let third = ScriptedSession()
        let (model, starter) = await ready(launch: ChatLaunch(resume: Self.chatID, suite: "pair"), children: [first, refused, third])
        await turn(model, first, "chat-under-suite")
        await model.switchSuite(to: "vast")
        #expect(await eventually { starter.openedCount == 2 })
        let words = recorded("data/protocol/Fixtures/suite-over-budget.err").trimmingCharacters(in: .newlines)
        refused.end(failing: words)
        #expect(await eventually { starter.openedCount == 3 })
        #expect(starter.launches.map(\.suite) == ["pair", "vast", "pair"])
        #expect(notes(model).last == "not moved to suite vast: \(words) -- the session carries on under suite pair")
        third.emit(fixtureStream("chat-under-suite")[0])
        #expect(await eventually { model.transcript.status == .ready })
        #expect(model.suiteWords == "suite pair")
    }

    @Test("forced, a suite over this machine's memory runs: --force with --suite")
    func forced() async {
        let first = ScriptedSession()
        let second = ScriptedSession()
        let (model, starter) = await ready(launch: ChatLaunch(resume: Self.chatID, suite: "pair"), children: [first, second])
        await model.switchSuite(to: "vast", force: true)
        #expect(await eventually { starter.openedCount == 2 })
        #expect(starter.launches[1].arguments.suffix(3) == ["--suite", "vast", "--force"])
        #expect(model.suiteWords == "suite vast, forced past its memory")
    }

    @Test("a new session with nothing said starts under the suite; a paused one carries it when it resumes")
    func newAndPaused() async {
        let first = ScriptedSession()
        let second = ScriptedSession()
        let (model, starter) = await ready(launch: ChatLaunch(), children: [first, second])
        await model.switchSuite(to: "pair")
        #expect(await eventually { starter.openedCount == 2 })
        #expect(starter.launches[1].suite == "pair" && starter.launches[1].resume == nil)
        #expect(notes(model).last == "starting under suite pair (--suite pair)")

        let one = ScriptedSession()
        let two = ScriptedSession()
        let (paused, pausedStarter) = await ready(launch: ChatLaunch(resume: Self.chatID, suite: "pair"), children: [one, two])
        await paused.pause()
        #expect(paused.isPaused)
        await paused.switchSuite(to: "fast")
        #expect(pausedStarter.openedCount == 1, "paused: no child until it resumes")
        paused.resume()
        #expect(await eventually { pausedStarter.openedCount == 2 })
        #expect(pausedStarter.launches[1].suite == "fast")
    }

    @Test("a private conversation with a turn can't move -- no --resume carries it -- and an execute session takes no none")
    func whereItCannot() async {
        let child = ScriptedSession()
        let (model, starter) = await ready(launch: ChatLaunch(privately: true), children: [child])
        model.draft = "hello"
        model.submit()
        #expect(!model.canSwitchSuite)
        model.draft = "/su"
        #expect(model.slashSuggestions.isEmpty)
        await model.switchSuite(to: "pair")
        #expect(starter.openedCount == 1)

        let door = ScriptedSession()
        let (execute, executeStarter) = await ready(launch: ChatLaunch(execute: ChatLaunch.Execute(suite: "pair")), children: [door, ScriptedSession()])
        await execute.switchSuite(to: SessionViewModel.noSuite)
        #expect(executeStarter.openedCount == 1, "execute refuses --suite off")
        await execute.switchSuite(to: "fast")
        #expect(await eventually { executeStarter.openedCount == 2 })
        #expect(executeStarter.launches[1].execute?.suite == "fast" && executeStarter.launches[1].arguments.first == "execute")
    }
}
