import Foundation

/// How a child process ended.
public struct ChildExit: Sendable, Equatable, CustomStringConvertible {
    public enum Reason: Sendable, Equatable {
        /// It returned `status` from its main.
        case exited
        /// Signal `status` ended it.
        case signalled
    }

    public let reason: Reason
    public let status: Int32

    public init(reason: Reason, status: Int32) {
        self.reason = reason
        self.status = status
    }

    public var succeeded: Bool { reason == .exited && status == 0 }

    public var description: String {
        switch reason {
        case .exited: "exited with status \(status)"
        case .signalled: "ended by signal \(status)"
        }
    }
}

/// Why a child could not be started, or written to.
public enum ChildError: Error, Equatable, CustomStringConvertible {
    /// The program would not start: missing, not executable, refused by the system.
    case couldNotStart(executable: String, reason: String)
    /// Its input is closed -- by us, or because it stopped reading.
    case inputClosed

    public var description: String {
        switch self {
        case .couldNotStart(let executable, let reason): "\(executable) could not be started: \(reason)"
        case .inputClosed: "the child's input is closed"
        }
    }
}

/// A child process, held by its three pipes and nothing else.
///
/// Its stdout arrives as lines; its stderr is captured into a bounded tail,
/// never shown; its exit is awaited. Stopping it is the caller's escalation:
/// close its input first, then `terminate`, then `kill` -- each signal to
/// its whole process group, as a terminal's reach its foreground job's
/// (GUI 4c), so what the child started ends with it.
public protocol RunningChild: AnyObject, Sendable {
    /// stdout, one line at a time, in order; finished when the child closes
    /// it -- or, when something it started still holds stdout open, shortly
    /// after the child's exit.
    var lines: AsyncStream<String> { get }

    /// The last of what it wrote to stderr.
    var stderrTail: BoundedTail { get }

    var processIdentifier: Int32 { get }

    /// Writes `data` to its stdin. Throws `ChildError.inputClosed` once the
    /// input is closed or the child stopped reading -- never a signal that
    /// would end the app.
    func write(_ data: Data) throws

    /// Closes its stdin: the child reads end of input.
    func closeInput()

    /// Interrupts it, as Ctrl-C at a terminal does: SIGINT, to its group.
    func interrupt()

    /// Asks it to stop: SIGTERM, to its group.
    func terminate()

    /// Ends it: SIGKILL, to its group.
    func kill()

    /// Whether anything of its process group still runs -- the child, or
    /// what it started, which may outlive it.
    var groupRunning: Bool { get }

    /// How it ended, once it has exited and its stderr has closed -- or,
    /// when something it started holds stderr open, shortly after its exit.
    func waitForExit() async -> ChildExit
}

extension RunningChild {
    /// A child with no group of its own to watch: nothing of it runs beyond
    /// what `waitForExit` says.
    public var groupRunning: Bool { false }
}

/// Starts children. The app's is `ProcessSpawner`; a test may hand in its own.
public protocol ChildSpawner: Sendable {
    func spawn(_ command: ChildCommand) throws -> any RunningChild
}
