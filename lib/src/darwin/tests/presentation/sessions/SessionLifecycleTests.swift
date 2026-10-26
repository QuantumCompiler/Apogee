import BusinessReads
import BusinessSession
import DataPreferences
import DataProtocol
import Foundation
import InfrastructureProcess
import PresentationSession
import Testing

@testable import PresentationSessions

/// The documents and steps the lifecycle suites share.
@MainActor
enum Lifecycle {
    /// A build with the CLI's 52d: its `session` line accepts `set`, so a
    /// private session is `--private` and writes nothing.
    static let newer = #"{"type":"session","protocol_version":1,"model":"local","next_turn":1,"capabilities":{"events":[],"accepts":["user","answer","attach","hello","cancel","detach","set","compact"],"tools":true,"ask":true,"schema":"2026-10-20"}}"#
    /// A `--private` session's `session` line on that build: `"private": true`.
    static let privately = #"{"type":"session","protocol_version":1,"model":"local","next_turn":1,"private":true,"capabilities":{"events":[],"accepts":["user","answer","attach","hello","cancel","detach","set","compact"],"tools":true,"ask":true,"schema":"2026-10-20"}}"#
    /// An older build: no `set`, so a private session is the fallback.
    static let older = #"{"type":"session","protocol_version":1,"model":"local","next_turn":1,"capabilities":{"events":[],"accepts":["user","answer","attach","hello","cancel"],"tools":true,"ask":true,"schema":"2026-10-13"}}"#
    static let list = #"{"object":"list","data":[{"id":"c2","name":"Plans","turns":3,"updated":"2026-10-20T15:00:00Z"},{"id":"c1","name":"Recipes","turns":1,"updated":"2026-10-12T09:00:00Z"}]}"#

    static func library(
        _ runner: ScriptedRunner, _ starter: ScriptedStarter, _ store: MemoryStore = MemoryStore(),
        clock: ScriptedClock = ScriptedClock()
    ) -> SessionLibraryViewModel {
        SessionLibraryViewModel(starter: starter, runner: runner, preferences: Preferences(store: store), clock: clock)
    }

    /// Lets whatever the last act set going run on: nothing more may open.
    static func settle() async {
        try? await Task.sleep(for: .milliseconds(60))
    }

    /// The one-live-child law: at most one child lives, now and once what
    /// the last act set going has run.
    static func law(_ starter: ScriptedStarter, _ step: String, sourceLocation: SourceLocation = #_sourceLocation) async {
        #expect(starter.live <= 1, "after \(step): \(starter.live) live children", sourceLocation: sourceLocation)
        await settle()
        #expect(starter.live <= 1, "after \(step), settled: \(starter.live) live children", sourceLocation: sourceLocation)
    }

    /// A turn sent and answered in the open session.
    static func converse(_ library: SessionLibraryViewModel, _ child: ScriptedSession, _ text: String = "keep this between us") async {
        library.current.draft = text
        library.current.submit()
        _ = await eventually { child.calls.contains("user: \(text)") }
        child.emit(#"{"type":"result","text":"I will.","turn":1}"#)
        _ = await eventually { library.current.transcript.status == .ready }
    }

    /// The library on a 52d build with a `--private` session open, a turn
    /// sent and answered in it; `children` are the sessions after it.
    static func privateConversation(_ runner: ScriptedRunner, children: [ScriptedSession]) async -> (SessionLibraryViewModel, ScriptedStarter, ScriptedSession) {
        runner.reading(["chats", "list"], list)
        let first = ScriptedSession("first")
        let secret = ScriptedSession("private")
        let starter = ScriptedStarter(sessions: [first, secret] + children)
        let library = library(runner, starter)
        await library.start(inView: true)
        first.emit(newer)
        _ = await eventually { library.privateIsNothingWritten }
        await library.newPrivateSession()
        _ = await eventually { starter.openedCount == 2 }
        secret.emit(privately)
        _ = await eventually { library.current.transcript.status == .ready }
        await converse(library, secret)
        return (library, starter, secret)
    }
}

/// 4a, the first defect: a conversation nothing of which is written can't be
/// carried on by `--resume`, so leaving the place never pauses it, and a
/// switch away asks first.
@Suite("Private sessions survive navigation", .timeLimit(.minutes(1)))
@MainActor
struct PrivateSessionsSurviveTests {
    @Test("a private conversation left for another place and returned to keeps its transcript and its child")
    func leaveAndReturn() async {
        let runner = ScriptedRunner()
        let (library, starter, secret) = await Lifecycle.privateConversation(runner, children: [ScriptedSession()])
        let kept = library.current.transcript.items
        let session = library.current
        await library.leftView()
        await Lifecycle.law(starter, "leaving")
        #expect(!secret.calls.contains("stop"), "not paused: a pause would lose it")
        #expect(secret.isLive)
        await library.cameIntoView()
        await Lifecycle.law(starter, "coming back")
        #expect(library.current === session)
        #expect(library.current.transcript.items == kept)
        #expect(starter.openedCount == 2, "the same child, not a fresh private one")
        #expect(starter.live == 1)
    }

    @Test("a private conversation left mid-turn keeps its child once the turn ends")
    func leaveMidTurn() async {
        let runner = ScriptedRunner()
        let (library, starter, secret) = await Lifecycle.privateConversation(runner, children: [ScriptedSession()])
        library.current.draft = "and another thing"
        library.current.submit()
        await library.leftView()
        secret.emit(#"{"type":"result","text":"noted","turn":2}"#)
        #expect(await eventually { library.current.transcript.status == .ready })
        await Lifecycle.law(starter, "the turn ending out of view")
        #expect(!secret.calls.contains("stop"))
        #expect(!library.current.isPaused)
    }

    @Test("a switch away from a private conversation asks first: Keep keeps it, End ends it")
    func switchAsks() async {
        let runner = ScriptedRunner()
        let next = ScriptedSession("next")
        let (library, starter, secret) = await Lifecycle.privateConversation(runner, children: [next, ScriptedSession()])
        let session = library.current
        await library.open(.chat("c1"))
        #expect(library.pending == .endPrivate(for: .chat("c1")), "asked first")
        await Lifecycle.law(starter, "asking")
        #expect(!secret.calls.contains("stop"))
        library.dismissPending()
        #expect(library.current === session, "kept")
        #expect(secret.isLive)
        await library.newSession()
        #expect(library.pending == .endPrivate(for: .new), "a new session asks too")
        await library.confirm()
        await Lifecycle.law(starter, "ending it")
        #expect(secret.calls.contains("stop"))
        #expect(await eventually { starter.openedCount == 3 })
        #expect(starter.live == 1)
    }

    @Test("a private session with no turn sent loses nothing: paused on leaving, and a switch away asks nothing")
    func emptyPrivateSession() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], Lifecycle.list)
        let first = ScriptedSession("first")
        let secret = ScriptedSession("private")
        let again = ScriptedSession("private again")
        let starter = ScriptedStarter(sessions: [first, secret, again, ScriptedSession()])
        let library = Lifecycle.library(runner, starter)
        await library.start(inView: true)
        first.emit(Lifecycle.newer)
        #expect(await eventually { library.privateIsNothingWritten })
        await library.newPrivateSession()
        #expect(await eventually { starter.openedCount == 2 })
        secret.emit(Lifecycle.privately)
        #expect(await eventually { library.current.transcript.status == .ready })
        #expect(!library.current.isLostIfEnded)
        await library.leftView()
        #expect(secret.calls == ["stop"], "nothing to lose: freed as any idle session is")
        await library.cameIntoView()
        #expect(await eventually { starter.openedCount == 3 })
        #expect(starter.launches.last == ChatLaunch(privately: true), "a private session again")
        again.emit(Lifecycle.privately)
        #expect(await eventually { library.current.transcript.status == .ready })
        await library.newSession()
        #expect(library.pending == nil)
        await Lifecycle.law(starter, "switching from an empty private session")
        #expect(await eventually { starter.openedCount == 4 })
    }
}

/// 4a, the second defect: ⌘Q ends every session child and keeps the private
/// fallback's promise before the app may go, each child waited for at most
/// the grace -- on a scripted clock here.
@Suite("Quit ends the sessions", .timeLimit(.minutes(1)))
@MainActor
struct QuitTests {
    /// A turn's end that the CLI saved, as the fallback's chat `p9`.
    static let savedAsP9 = #"{"object":"list","data":[{"id":"p9","name":"secret","turns":1,"updated":"2026-10-20T17:00:00Z"},{"id":"c2","name":"Plans","turns":3,"updated":"2026-10-20T15:00:00Z"},{"id":"c1","name":"Recipes","turns":1,"updated":"2026-10-12T09:00:00Z"}]}"#

    @Test("quit with a private fallback session live: its child's stop, then its promised delete, all before the quit returns")
    func stopThenDelete() async {
        let ledger = Ledger()
        let runner = ScriptedRunner(ledger: ledger)
        runner.reading(["chats", "list"], Lifecycle.list)
        let first = ScriptedSession("first", ledger: ledger)
        let secret = ScriptedSession("private", ledger: ledger)
        let starter = ScriptedStarter(sessions: [first, secret])
        let library = Lifecycle.library(runner, starter)
        await library.start(inView: true)
        first.emit(Lifecycle.older)
        #expect(await eventually { library.current.transcript.status == .ready })
        await library.newPrivateSession()
        #expect(await eventually { starter.openedCount == 2 })
        #expect(starter.launches.last == ChatLaunch(noRecall: true))
        secret.emit(Lifecycle.older)
        #expect(await eventually { library.current.transcript.status == .ready })
        runner.reading(["chats", "list"], Self.savedAsP9)
        await Lifecycle.converse(library, secret)
        #expect(await eventually { library.current.chatID == "p9" })
        #expect(library.needsShutdown)
        let before = ledger.all.count
        let report = await library.quit()
        #expect(Array(ledger.all.dropFirst(before)) == ["private: stop", "private: ended", "run chats delete p9"])
        #expect(report == ShutdownReport(ended: true, deleteFailure: nil))
        #expect(!library.needsShutdown)
        #expect(starter.live == 0)
    }

    @Test("the window closing and ⌘Q at once: the child stopped once, its promised delete run once, both waited for")
    func closeAndQuitAtOnce() async {
        let ledger = Ledger()
        let runner = ScriptedRunner(ledger: ledger)
        runner.reading(["chats", "list"], Lifecycle.list)
        let first = ScriptedSession("first", ledger: ledger)
        let secret = ScriptedSession("private", stopping: .whenReleased, ledger: ledger)
        let starter = ScriptedStarter(sessions: [first, secret])
        let library = Lifecycle.library(runner, starter)
        await library.start(inView: true)
        first.emit(Lifecycle.older)
        #expect(await eventually { library.current.transcript.status == .ready })
        await library.newPrivateSession()
        #expect(await eventually { starter.openedCount == 2 })
        secret.emit(Lifecycle.older)
        #expect(await eventually { library.current.transcript.status == .ready })
        runner.reading(["chats", "list"], Self.savedAsP9)
        await Lifecycle.converse(library, secret)
        #expect(await eventually { library.current.chatID == "p9" })
        let before = ledger.all.count
        let closing = Task { await library.shutDown() }
        #expect(await eventually { ledger.all.count == before + 1 })
        let quitting = Task { await library.quit() }
        await Lifecycle.settle()
        secret.releaseStop()
        let report = await quitting.value
        await closing.value
        #expect(report == ShutdownReport(ended: true, deleteFailure: nil))
        #expect(Array(ledger.all.dropFirst(before)) == ["private: stop", "private: ended", "run chats delete p9"])
        #expect(!library.needsShutdown)
        _ = await library.quit()
        #expect(ledger.all.filter { $0.hasPrefix("run chats delete") }.count == 1, "asked again, nothing runs twice")
    }

    @Test("a session that won't die is given up after the grace, and the quit still returns")
    func givenUpAfterTheGrace() async {
        let ledger = Ledger()
        let runner = ScriptedRunner(ledger: ledger)
        runner.reading(["chats", "list"], Lifecycle.list)
        let stubborn = ScriptedSession("stubborn", stopping: .whenReleased, ledger: ledger)
        let starter = ScriptedStarter(sessions: [stubborn])
        let clock = ScriptedClock()
        let library = Lifecycle.library(runner, starter, clock: clock)
        await library.start(inView: true)
        stubborn.emit(Lifecycle.older)
        #expect(await eventually { library.current.transcript.status == .ready })
        let done = Flag()
        let quitting = Task {
            let report = await library.quit()
            done.raised = true
            return report
        }
        #expect(await eventually { clock.sleeping == 1 && ledger.all == ["stubborn: stop"] })
        clock.advance(by: SessionViewModel.grace - .milliseconds(1))
        await Lifecycle.settle()
        #expect(!done.raised, "waiting while the grace lasts")
        clock.advance(by: .milliseconds(1))
        let report = await quitting.value
        #expect(report == ShutdownReport(ended: false, deleteFailure: nil))
        #expect(ledger.all == ["stubborn: stop"], "given up, not ended")
        stubborn.releaseStop()
    }

    @Test("a child that ends in time: the grace's timer is called off, and nothing waits on the clock")
    func endedInTime() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], Lifecycle.list)
        let child = ScriptedSession()
        let starter = ScriptedStarter(sessions: [child])
        let clock = ScriptedClock()
        let library = Lifecycle.library(runner, starter, clock: clock)
        await library.start(inView: true)
        child.emit(Lifecycle.older)
        #expect(await eventually { library.current.transcript.status == .ready })
        let report = await library.quit()
        #expect(report.ended)
        #expect(await eventually { clock.sleeping == 0 })
        #expect(starter.live == 0)
    }

    @Test("what a quit waits for: a live child, a launch, or a promised delete; a paused session or a closed window none")
    func whatAQuitWaitsFor() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], Lifecycle.list)
        let first = ScriptedSession()
        let second = ScriptedSession()
        let starter = ScriptedStarter(sessions: [first, second, ScriptedSession()])
        let library = Lifecycle.library(runner, starter, MemoryStore(["openChat": "c2"]))
        #expect(!library.needsShutdown, "nothing started")
        await library.start(inView: false)
        #expect(!library.needsShutdown, "out of view: nothing launched")
        await library.cameIntoView()
        #expect(library.needsShutdown, "launching")
        first.emit(Lifecycle.older)
        #expect(await eventually { library.current.transcript.status == .ready })
        #expect(library.needsShutdown, "a live child")
        await library.leftView()
        #expect(library.current.isPaused)
        #expect(!library.needsShutdown, "paused: no child")
        await library.newPrivateSession()
        #expect(library.needsShutdown, "the fallback's delete is promised")
        await library.shutDown()
        #expect(!library.needsShutdown, "the window closed: all done")
    }

    @Test("quit while the open session is still locating: the launch is called off, and no child comes")
    func quitWhileLocating() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], Lifecycle.list)
        let starter = ScriptedStarter(sessions: [ScriptedSession(), ScriptedSession()])
        let library = Lifecycle.library(runner, starter)
        await library.start(inView: true)
        #expect(library.current.connection == .locating)
        let report = await library.quit()
        #expect(report.ended)
        await Lifecycle.settle()
        #expect(starter.openedCount == 0)
        #expect(starter.live == 0)
    }

    @Test("quit during a switch joins its close, and the switch opens nothing after it")
    func quitDuringASwitch() async {
        let ledger = Ledger()
        let runner = ScriptedRunner(ledger: ledger)
        runner.reading(["chats", "list"], Lifecycle.list)
        let slow = ScriptedSession("slow", stopping: .whenReleased, ledger: ledger)
        let starter = ScriptedStarter(sessions: [slow, ScriptedSession()])
        let library = Lifecycle.library(runner, starter)
        await library.start(inView: true)
        slow.emit(Lifecycle.older)
        #expect(await eventually { library.current.transcript.status == .ready })
        let switching = Task { await library.open(.chat("c1")) }
        #expect(await eventually { ledger.all == ["slow: stop"] })
        let quitting = Task { await library.quit() }
        await Lifecycle.settle()
        slow.releaseStop()
        let report = await quitting.value
        await switching.value
        await Lifecycle.settle()
        #expect(report.ended)
        #expect(ledger.all == ["slow: stop", "slow: ended"], "stopped once: the quit joined the switch's close")
        #expect(starter.openedCount == 1, "nothing opens once the app is quitting")
        #expect(starter.live == 0)
    }

    @Test("with the window closed, a new session starts no child until the window opens again")
    func closedWindowStartsNothing() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], Lifecycle.list)
        let first = ScriptedSession()
        let starter = ScriptedStarter(sessions: [first, ScriptedSession()])
        let library = Lifecycle.library(runner, starter)
        await library.start(inView: true)
        first.emit(Lifecycle.older)
        #expect(await eventually { library.current.transcript.status == .ready })
        await library.shutDown()
        await library.newSession()
        await library.cameIntoView()
        await Lifecycle.law(starter, "a new session with the window closed")
        #expect(starter.openedCount == 1)
        #expect(starter.live == 0)
        await library.start(inView: true)
        #expect(await eventually { starter.openedCount == 2 })
        await Lifecycle.law(starter, "the window open again")
    }
}

/// Set on the main actor, read by the test.
@MainActor
final class Flag {
    var raised = false
}

/// 4a's guardrail: the one-live-child law after every step of the lifecycle
/// -- launch, a turn, leave and return, a switch mid-launch, a private
/// conversation left, kept and ended, the window closed and opened, and the
/// quit.
@Suite("The one-live-child law", .timeLimit(.minutes(1)))
@MainActor
struct OneLiveChildMatrixTests {
    @Test("after every step of the lifecycle, at most one child lives")
    func everyStep() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], Lifecycle.list)
        let starter = ScriptedStarter(sessions: (0..<12).map { ScriptedSession("child \($0)") })
        let library = Lifecycle.library(runner, starter, MemoryStore(["openChat": "c2"]))
        /// The open session's child, once its launch has opened it, says its
        /// `session` line.
        func speak(_ line: String = Lifecycle.newer) async -> ScriptedSession? {
            _ = await eventually { if case .connected = library.current.connection { library.current.isLive } else { false } }
            let child = starter.handed.last
            child?.emit(line)
            _ = await eventually { library.current.transcript.status == .ready }
            return child
        }

        await library.start(inView: true)
        await Lifecycle.law(starter, "launch")
        let first = await speak()
        await Lifecycle.law(starter, "the session speaking")
        if let first { await Lifecycle.converse(library, first, "first turn") }
        await Lifecycle.law(starter, "a turn")
        await library.leftView()
        await Lifecycle.law(starter, "leaving")
        #expect(starter.live == 0)
        await library.cameIntoView()
        await Lifecycle.law(starter, "coming back")
        await library.open(.chat("c1"))
        await Lifecycle.law(starter, "a switch while the resumed one was still launching")
        #expect(starter.live == 1)
        _ = await speak()
        await library.newPrivateSession()
        await Lifecycle.law(starter, "a private session")
        let secret = await speak(Lifecycle.privately)
        if let secret { await Lifecycle.converse(library, secret) }
        await Lifecycle.law(starter, "a private turn")
        await library.leftView()
        await Lifecycle.law(starter, "leaving the private conversation")
        #expect(starter.live == 1, "kept")
        await library.cameIntoView()
        await Lifecycle.law(starter, "back to it")
        await library.open(.chat("c2"))
        library.dismissPending()
        await Lifecycle.law(starter, "keeping it")
        #expect(secret?.isLive == true)
        await library.newSession()
        await library.confirm()
        await Lifecycle.law(starter, "ending it for a new session")
        #expect(secret?.isLive == false)
        _ = await speak()
        await library.shutDown()
        await Lifecycle.law(starter, "the window closing")
        #expect(starter.live == 0)
        let openedWhileOpen = starter.openedCount
        await library.newSession()
        await library.cameIntoView()
        await Lifecycle.law(starter, "a new session while it is closed")
        #expect(starter.live == 0)
        #expect(starter.openedCount == openedWhileOpen)
        await library.start(inView: true)
        await Lifecycle.law(starter, "the window opening again")
        _ = await speak()
        #expect(starter.live == 1)
        _ = await library.quit()
        await Lifecycle.law(starter, "the quit")
        #expect(starter.live == 0)
    }
}

/// 4a, the third defect: a switch while the open session is still
/// launching calls its launch off, so the one-live-child law holds in the
/// window between a launch and its child.
@Suite("A switch during a launch", .timeLimit(.minutes(1)))
@MainActor
struct SwitchDuringLaunchTests {
    @Test("a switch while the open session is still locating calls its launch off: one child")
    func switchWhileLocating() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], Lifecycle.list)
        let starter = ScriptedStarter(sessions: [ScriptedSession(), ScriptedSession(), ScriptedSession()])
        let library = Lifecycle.library(runner, starter)
        await library.start(inView: true)
        #expect(library.current.connection == .locating, "launched, its child not opened yet")
        await library.open(.chat("c2"))
        await Lifecycle.law(starter, "switching")
        #expect(starter.openedCount == 1)
        #expect(starter.launches.map(\.resume) == ["c2"])
    }

    @Test("coming back and opening a row at once -- a sidebar row clicked from another place -- leaves one child")
    func returnAndOpenAtOnce() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], Lifecycle.list)
        let first = ScriptedSession()
        let starter = ScriptedStarter(sessions: [first, ScriptedSession(), ScriptedSession()])
        let library = Lifecycle.library(runner, starter, MemoryStore(["openChat": "c2"]))
        await library.start(inView: true)
        first.emit(Lifecycle.older)
        #expect(await eventually { library.current.transcript.status == .ready })
        await library.leftView()
        #expect(library.current.isPaused)
        // The shell's route change and the row's own act, as the sidebar sets both going.
        let back = Task { await library.cameIntoView() }
        let opening = Task { await library.open(.chat("c1")) }
        await back.value
        await opening.value
        await Lifecycle.law(starter, "coming back and opening")
        #expect(starter.live == 1)
        #expect(starter.launches.last?.resume == "c1")
    }

    @Test("two rows opened at once: the last one's child alone lives")
    func twoSwitchesAtOnce() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], Lifecycle.list)
        let first = ScriptedSession()
        let starter = ScriptedStarter(sessions: [first, ScriptedSession(), ScriptedSession(), ScriptedSession()])
        let library = Lifecycle.library(runner, starter)
        await library.start(inView: true)
        first.emit(Lifecycle.older)
        #expect(await eventually { library.current.transcript.status == .ready })
        let one = Task { await library.open(.chat("c1")) }
        let two = Task { await library.open(.chat("c2")) }
        await one.value
        await two.value
        await Lifecycle.law(starter, "two opens")
        #expect(starter.live == 1)
    }
}

/// 4a, the second defect: a window closed and opened again within the
/// grace starts nothing until the child before it has gone.
@Suite("Close and reopen", .timeLimit(.minutes(1)))
@MainActor
struct CloseAndReopenTests {
    @Test("the window closed and opened again within the grace starts nothing until the child before it has gone")
    func reopenWaitsForTheShutdown() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], Lifecycle.list)
        let first = ScriptedSession("first", stopping: .whenReleased)
        let second = ScriptedSession("second")
        let starter = ScriptedStarter(sessions: [first, second])
        let library = Lifecycle.library(runner, starter)
        await library.start(inView: true)
        first.emit(Lifecycle.older)
        #expect(await eventually { library.current.transcript.status == .ready })
        let closing = Task { await library.shutDown() }
        #expect(await eventually { first.calls == ["stop"] })
        let reopening = Task { await library.start(inView: true) }
        await Lifecycle.law(starter, "reopening within the grace")
        #expect(starter.openedCount == 1, "nothing opens while the child before is still going")
        first.releaseStop()
        await closing.value
        await reopening.value
        #expect(await eventually { starter.openedCount == 2 })
        await Lifecycle.law(starter, "reopened")
        #expect(starter.live == 1)
    }
}
