import AppKit
import os

/// The application's delegate: it holds the app's state, which the scenes
/// share, and holds ⌘Q until the sessions have ended. AppKit would otherwise
/// exit with the window's shutdown still in flight -- a session's child
/// orphaned, a private session's promised delete never run.
@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    /// What the quit did, in the system log (`log stream --predicate
    /// 'subsystem == "com.quantumcompiler.apogee"'`).
    private static let log = Logger(subsystem: "com.quantumcompiler.apogee", category: "quit")

    /// The app's state, made once, on first use.
    private(set) lazy var model: AppViewModel = {
        let model = AppViewModel(bundle: .main)
        model.termination.log = { line in Self.log.notice("\(line, privacy: .public)") }
        return model
    }()

    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        switch model.termination.ask(reply: { sender.reply(toApplicationShouldTerminate: $0) }) {
        case .now:
            return .terminateNow
        case .later:
            // The backstop: a run-loop timer fires in the modal-panel mode
            // AppKit waits in, even when the main queue -- and so the main
            // actor the shutdown runs on -- can't be drained.
            let backstop = Termination.backstop.components
            let timer = Timer(timeInterval: TimeInterval(backstop.seconds), repeats: false) { [weak self] _ in
                MainActor.assumeIsolated { self?.model.termination.giveUp() }
            }
            RunLoop.main.add(timer, forMode: .common)
            return .terminateLater
        }
    }
}
