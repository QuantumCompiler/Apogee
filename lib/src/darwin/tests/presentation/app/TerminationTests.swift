import DataPreferences
import PresentationSession
import PresentationSessions
import Testing

@testable import PresentationApp

/// A shutdown a test holds: asked, it records so, and answers only once the
/// test lets it -- the session library's `quit()`, as the app delegate's
/// termination waits on it.
@MainActor
final class HeldShutdown {
    private(set) var asked = 0
    private var waiting: CheckedContinuation<ShutdownReport, Never>?

    func shutDown() async -> ShutdownReport {
        asked += 1
        return await withCheckedContinuation { waiting = $0 }
    }

    /// Lets the shutdown answer `report`.
    func finish(_ report: ShutdownReport) {
        waiting?.resume(returning: report)
        waiting = nil
    }
}

/// The running jobs' stop a test holds: asked, it records so, and answers
/// only once the test lets it -- the children registry's, as quit asks it.
@MainActor
final class HeldStop {
    private(set) var asked = 0
    private var waiting: CheckedContinuation<Bool, Never>?

    func stop() async -> Bool {
        asked += 1
        return await withCheckedContinuation { waiting = $0 }
    }

    /// Lets the stop answer whether every job ended.
    func finish(_ ended: Bool) {
        waiting?.resume(returning: ended)
        waiting = nil
    }
}

/// 4a, the second defect: ⌘Q is answered "later" while a session must end,
/// and replied to only once it has.
@Suite("Termination", .timeLimit(.minutes(1)))
@MainActor
struct TerminationTests {
    /// What the delegate is told: each reply, and the log's lines.
    final class Heard {
        var replies: [Bool] = []
        var lines: [String] = []
    }

    func termination(pending: Bool, _ held: HeldShutdown, _ heard: Heard) -> Termination {
        let termination = Termination(pending: { pending }, shutDown: { await held.shutDown() })
        termination.log = { heard.lines.append($0) }
        return termination
    }

    @Test("nothing to wait for: the app quits at once, and nothing is shut down")
    func quitsAtOnce() {
        let held = HeldShutdown()
        let heard = Heard()
        let termination = termination(pending: false, held, heard)
        #expect(termination.ask { heard.replies.append($0) } == .now)
        #expect(held.asked == 0)
        #expect(heard.lines.isEmpty)
    }

    @Test("a session to end: answered later, and replied to only once its stop and its delete have run")
    func repliesAfterTheShutdown() async {
        let held = HeldShutdown()
        let heard = Heard()
        let termination = termination(pending: true, held, heard)
        #expect(termination.ask { heard.replies.append($0) } == .later)
        #expect(await eventually { held.asked == 1 })
        try? await Task.sleep(for: .milliseconds(50))
        #expect(heard.replies.isEmpty, "no reply while the session is ending")
        held.finish(ShutdownReport(ended: true, deleteFailure: nil))
        #expect(await eventually { heard.replies == [true] })
        #expect(heard.lines == [
            "quit: ending the open session first -- its child asked to go, and a private session's promised delete -- waiting at most 5 s on the child",
            "quit: the sessions have ended; quitting",
        ])
    }

    @Test("a child given up after the grace, a delete that failed: both said in the log, and the app still quits")
    func givenUpIsSaid() async {
        let held = HeldShutdown()
        let heard = Heard()
        let termination = termination(pending: true, held, heard)
        #expect(termination.ask { heard.replies.append($0) } == .later)
        #expect(await eventually { held.asked == 1 })
        held.finish(ShutdownReport(ended: false, deleteFailure: "apogee chats: no chat named 'p9' (exited with status 1)"))
        #expect(await eventually { heard.replies == [true] })
        #expect(heard.lines.dropFirst() == [
            "quit: the session's child did not end within 5 s; given up so the app can quit -- its stop still escalates",
            "quit: the private session was not deleted: apogee chats: no chat named 'p9' (exited with status 1)",
            "quit: the sessions have ended; quitting",
        ])
    }

    @Test("asked again while it waits: one shutdown, one reply")
    func askedTwice() async {
        let held = HeldShutdown()
        let heard = Heard()
        let termination = termination(pending: true, held, heard)
        #expect(termination.ask { heard.replies.append($0) } == .later)
        #expect(termination.ask { heard.replies.append($0) } == .later)
        #expect(await eventually { held.asked == 1 })
        held.finish(ShutdownReport(ended: true, deleteFailure: nil))
        #expect(await eventually { heard.replies == [true] })
        try? await Task.sleep(for: .milliseconds(50))
        #expect(held.asked == 1)
        #expect(heard.replies == [true])
    }

    @Test("the backstop passed with the sessions still ending: the app quits anyway, said in the log, and is replied to once")
    func backstopGivesUp() async {
        let held = HeldShutdown()
        let heard = Heard()
        let termination = termination(pending: true, held, heard)
        termination.giveUp()
        #expect(heard.replies.isEmpty, "nothing asked: nothing to give up")
        #expect(termination.ask { heard.replies.append($0) } == .later)
        #expect(await eventually { held.asked == 1 })
        termination.giveUp()
        #expect(heard.replies == [true])
        #expect(heard.lines.last == "quit: the sessions had not ended after 15 s; quitting anyway")
        held.finish(ShutdownReport(ended: true, deleteFailure: nil))
        try? await Task.sleep(for: .milliseconds(50))
        termination.giveUp()
        #expect(heard.replies == [true], "replied once")
        #expect(Termination.backstop == SessionViewModel.grace + .seconds(10))
    }

    @Test("the app's own termination asks its session library: a fresh app, nothing launched, quits at once")
    func composedOverTheLibrary() {
        let model = AppViewModel(info: nil, preferences: Preferences(store: MemoryStore()))
        #expect(!model.sessions.needsShutdown)
        #expect(model.termination.ask { _ in } == .now)
    }

    /// 4c: supervised jobs at quit, which 4a left running.
    @Test("running jobs alone hold the quit: answered later, and replied to once their stop has ended them")
    func jobsHoldTheQuit() async {
        let held = HeldShutdown()
        let stop = HeldStop()
        let heard = Heard()
        let termination = Termination(
            pending: { false }, shutDown: { await held.shutDown() }, jobsRunning: { 2 }, stopJobs: { await stop.stop() })
        termination.log = { heard.lines.append($0) }
        #expect(termination.ask { heard.replies.append($0) } == .later)
        #expect(await eventually { stop.asked == 1 })
        try? await Task.sleep(for: .milliseconds(50))
        #expect(heard.replies.isEmpty, "no reply while the jobs stop")
        #expect(held.asked == 0, "no session to end")
        stop.finish(true)
        #expect(await eventually { heard.replies == [true] })
        #expect(heard.lines == [
            "quit: stopping the 2 running jobs first -- interrupted as Ctrl-C does, terminated after 3 s and killed 2 s later",
            "quit: the jobs have stopped; quitting",
        ])
        #expect(Termination.jobGrace + .seconds(2) == SessionViewModel.grace, "every step within the session's bound")
    }

    @Test("a session and a job: both ended side by side, the reply after both, a job left behind said")
    func sessionAndJobsSideBySide() async {
        let held = HeldShutdown()
        let stop = HeldStop()
        let heard = Heard()
        let termination = Termination(
            pending: { true }, shutDown: { await held.shutDown() }, jobsRunning: { 1 }, stopJobs: { await stop.stop() })
        termination.log = { heard.lines.append($0) }
        #expect(termination.ask { heard.replies.append($0) } == .later)
        #expect(await eventually { held.asked == 1 && stop.asked == 1 }, "both begun before either has ended")
        held.finish(ShutdownReport(ended: true, deleteFailure: nil))
        try? await Task.sleep(for: .milliseconds(50))
        #expect(heard.replies.isEmpty, "the job is still stopping")
        stop.finish(false)
        #expect(await eventually { heard.replies == [true] })
        #expect(heard.lines == [
            "quit: ending the open session first -- its child asked to go, and a private session's promised delete -- waiting at most 5 s on the child",
            "quit: stopping the running job first -- interrupted as Ctrl-C does, terminated after 3 s and killed 2 s later",
            "quit: a job did not end after its stop; quitting anyway",
            "quit: the sessions have ended; quitting",
        ])
    }

    @Test("the grace is said as the log says it")
    func graceWords() {
        #expect(Termination.words(SessionViewModel.grace) == "5 s")
        #expect(Termination.words(.milliseconds(1500)) == "1.5 s")
    }
}

/// Waits, on the main actor, until `condition` holds -- five seconds at most.
@MainActor
func eventually(_ condition: @MainActor () -> Bool) async -> Bool {
    let deadline = ContinuousClock.now + .seconds(5)
    while !condition() {
        if ContinuousClock.now > deadline { return false }
        try? await Task.sleep(for: .milliseconds(10))
    }
    return true
}
