import Foundation
import PresentationSession
import PresentationSessions

/// ⌘Q, held until the sessions have ended. With a session's child live or
/// launching, or a private session's promised delete still to run, the app
/// answers "later", ends them -- the child stopped by its escalation, waited
/// for at most the session's grace, then the delete -- and only then replies,
/// so the process never exits with a child or a promise left behind.
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

    /// Writes one line of what the quit did: the delegate's log.
    var log: @MainActor (String) -> Void = { _ in }

    private let pending: @MainActor () -> Bool
    private let shutDown: @MainActor () async -> ShutdownReport
    /// The quit in flight: asked again meanwhile, the app waits on this one.
    private var quitting: Task<Void, Never>?
    /// AppKit's reply, until it has been given.
    private var reply: (@MainActor (Bool) -> Void)?

    /// Over `pending` -- whether a session must end first -- and `shutDown`,
    /// which ends them.
    init(pending: @escaping @MainActor () -> Bool, shutDown: @escaping @MainActor () async -> ShutdownReport) {
        self.pending = pending
        self.shutDown = shutDown
    }

    /// The app asked to quit. `.later` ends the sessions first and then
    /// calls `reply(true)` -- always, a child given up included, so the quit
    /// is never refused.
    func ask(reply: @escaping @MainActor (Bool) -> Void) -> Answer {
        if quitting != nil { return .later }
        guard pending() else { return .now }
        self.reply = reply
        let grace = Self.words(SessionViewModel.grace)
        log("quit: ending the open session first -- its child asked to go, and a private session's promised delete -- waiting at most \(grace) on the child")
        quitting = Task {
            let report = await shutDown()
            if !report.ended {
                log("quit: the session's child did not end within \(grace); given up so the app can quit -- its stop still escalates")
            }
            if let failure = report.deleteFailure {
                log("quit: the private session was not deleted: \(failure)")
            }
            guard self.reply != nil else { return }
            log("quit: the sessions have ended; quitting")
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
