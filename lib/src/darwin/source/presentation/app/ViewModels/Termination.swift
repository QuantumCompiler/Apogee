import Foundation
import PresentationSession
import PresentationSessions

/// ⌘Q, held until the sessions have ended and the running jobs have
/// stopped. With a session's child live or launching, a private session's
/// promised delete still to run, or a supervised job running, the app
/// answers "later", ends them -- the child stopped by its escalation, waited
/// for at most the session's grace, then the delete; beside it each job
/// stopped as its Stop does, within the same bound (4c) -- and only then
/// replies, so the process never exits with a child or a promise left behind.
///
/// AppKit-free (ADR 0002): the app delegate asks it, maps its answer to
/// AppKit's, and writes `log`'s lines to the system log. The reply is given
/// once: by the shutdown's end, or by `giveUp()` at the backstop.
@MainActor
final class Termination {
    enum Answer: Equatable {
        /// Nothing to wait for: quit now.
        case now
        /// Quit once the sessions have ended; the reply says so.
        case later
    }

    /// How long the app waits on the sessions at most, whatever happens --
    /// the child's grace, and time for the delete after it. The shutdown runs
    /// on the main actor, which AppKit services while it waits for the reply
    /// when the quit came from the event loop (⌘Q, the Dock, a logout); asked
    /// from inside a main-queue job, its nested run loop can't drain that
    /// queue, so the delegate's run-loop timer -- which still fires -- gives
    /// up at this backstop instead of letting the quit hang.
    static let backstop: Duration = SessionViewModel.grace + .seconds(10)

    /// The grace each running job's Stop is given at quit: interrupted at
    /// once, terminated after this, killed two seconds later -- all within
    /// the session's grace, the bound the quit waits.
    static let jobGrace: Duration = SessionViewModel.grace - .seconds(2)

    /// Writes one line of what the quit did: the delegate's log.
    var log: @MainActor (String) -> Void = { _ in }

    private let pending: @MainActor () -> Bool
    private let shutDown: @MainActor () async -> ShutdownReport
    private let jobsRunning: @MainActor () -> Int
    private let stopJobs: @MainActor () async -> Bool
    /// The quit in flight: asked again meanwhile, the app waits on this one.
    private var quitting: Task<Void, Never>?
    /// AppKit's reply, until it has been given.
    private var reply: (@MainActor (Bool) -> Void)?

    /// Over `pending` -- whether a session must end first -- and `shutDown`,
    /// which ends them; `jobsRunning` -- how many supervised jobs run -- and
    /// `stopJobs`, which stops them all and says whether all have ended.
    init(
        pending: @escaping @MainActor () -> Bool, shutDown: @escaping @MainActor () async -> ShutdownReport,
        jobsRunning: @escaping @MainActor () -> Int = { 0 }, stopJobs: @escaping @MainActor () async -> Bool = { true }
    ) {
        self.pending = pending
        self.shutDown = shutDown
        self.jobsRunning = jobsRunning
        self.stopJobs = stopJobs
    }

    /// The app asked to quit. `.later` ends the sessions and stops the jobs
    /// first, side by side, and then calls `reply(true)` -- always, a child
    /// given up included, so the quit is never refused.
    func ask(reply: @escaping @MainActor (Bool) -> Void) -> Answer {
        if quitting != nil { return .later }
        let session = pending()
        let jobs = jobsRunning()
        guard session || jobs > 0 else { return .now }
        self.reply = reply
        let grace = Self.words(SessionViewModel.grace)
        if session {
            log("quit: ending the open session first -- its child asked to go, and a private session's promised delete -- waiting at most \(grace) on the child")
        }
        if jobs > 0 {
            log("quit: stopping \(jobs == 1 ? "the running job" : "the \(jobs) running jobs") first -- interrupted as Ctrl-C does, terminated after \(Self.words(Self.jobGrace)) and killed 2 s later")
        }
        quitting = Task {
            let stopping = Task {
                guard jobs > 0 else { return true }
                return await stopJobs()
            }
            if session {
                let report = await shutDown()
                if !report.ended {
                    log("quit: the session's child did not end within \(grace); given up so the app can quit -- its stop still escalates")
                }
                if let failure = report.deleteFailure {
                    log("quit: the private session was not deleted: \(failure)")
                }
            }
            if !(await stopping.value) {
                log("quit: a job did not end after its stop; quitting anyway")
            }
            guard self.reply != nil else { return }
            log(session ? "quit: the sessions have ended; quitting" : "quit: the jobs have stopped; quitting")
            answer()
        }
        return .later
    }

    /// The backstop passed with the sessions still ending: the app quits
    /// anyway, and the log says so. Nothing once the reply has been given.
    func giveUp() {
        guard reply != nil else { return }
        log("quit: the sessions had not ended after \(Self.words(Self.backstop)); quitting anyway")
        answer()
    }

    private func answer() {
        let reply = self.reply
        self.reply = nil
        reply?(true)
    }

    /// A duration as the log says it: `5 s`.
    static func words(_ duration: Duration) -> String {
        let (seconds, attoseconds) = duration.components
        return attoseconds == 0 ? "\(seconds) s" : "\(Double(seconds) + Double(attoseconds) / 1e18) s"
    }
}
