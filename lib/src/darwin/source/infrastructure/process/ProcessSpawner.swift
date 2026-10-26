import Foundation
import Synchronization

/// Starts children with Foundation's `Process`: three pipes, the stdout
/// framed into lines, the stderr kept as a bounded tail, the person's
/// shell's `PATH` under the command's own variables, and each child the
/// leader of a process group of its own, which its signals reach.
///
/// Every child the app starts comes through here -- a session, a read, a
/// command, a job, a version probe -- so every one gets that `PATH` (GUI M6).
public struct ProcessSpawner: ChildSpawner {
    /// How many bytes of stderr each child keeps.
    public let stderrLimit: Int

    /// Where each child's `PATH` comes from: the app's resolver unless given
    /// another -- the login shell's once it has answered, the fallback until
    /// then, read at each spawn and never waited for.
    public let path: ShellPath

    public init(stderrLimit: Int = 8 * 1024, path: ShellPath = .app) {
        self.stderrLimit = stderrLimit
        self.path = path
    }

    public func spawn(_ command: ChildCommand) throws -> any RunningChild {
        try FoundationChild(command, path: path.current.path, stderrLimit: stderrLimit)
    }
}

/// A `Process` and its pipes. Its state is behind one lock, so the pipes'
/// handlers, the termination handler and the caller never race.
///
/// - **Its own process group.** Foundation's `Process` starts every child
///   leading a new group whose id is its pid, and what the child starts joins
///   it (pinned by `ProcessGroupTests`). Every signal goes to that group --
///   the child and all it started -- as Ctrl-C at a terminal reaches the
///   foreground group, and still after the child itself has gone, while
///   anything of the group runs (GUI 4c).
/// - **Neither stream is held hostage.** Something the child started that
///   inherited its stdout or stderr can hold either open past the child's
///   exit; `failsafe` after the exit both are ended anyway.
/// - **Nothing holds it but its owner.** The handlers capture it weakly and
///   are cleared on every end -- end of a stream, the failsafe, a start that
///   failed, its release -- so a grandchild holding a pipe never pins it.
final class FoundationChild: RunningChild, @unchecked Sendable {
    /// How long after the child's exit a stream something it started still
    /// holds open is waited for.
    static let failsafe: Duration = .seconds(2)

    private let process = Process()
    private let input = Pipe()
    private let output = Pipe()
    private let errors = Pipe()
    let lines: AsyncStream<String>
    private let linesContinuation: AsyncStream<String>.Continuation

    private struct State {
        var framer = LineFramer()
        var tail: BoundedTail
        var exit: ChildExit?
        var stdoutFinished = false
        var stderrClosed = false
        var inputClosed = false
        var waiters: [CheckedContinuation<ChildExit, Never>] = []

        /// The waiters, handed over once: each is resumed exactly one time.
        mutating func take() -> [CheckedContinuation<ChildExit, Never>] {
            defer { waiters = [] }
            return waiters
        }
    }

    private let state: Mutex<State>

    init(_ command: ChildCommand, path: String, stderrLimit: Int) throws {
        state = Mutex(State(tail: BoundedTail(limit: stderrLimit)))
        (lines, linesContinuation) = AsyncStream.makeStream(of: String.self)

        process.executableURL = command.executable
        process.arguments = command.arguments
        process.environment = command.environment(over: ProcessInfo.processInfo.environment, path: path)
        if let directory = command.workingDirectory {
            process.currentDirectoryURL = directory
        }
        process.standardInput = input
        process.standardOutput = output
        process.standardError = errors

        // A write to a child that has stopped reading must fail as an error,
        // never as the SIGPIPE that would end the app.
        _ = fcntl(input.fileHandleForWriting.fileDescriptor, F_SETNOSIGPIPE, 1)

        output.fileHandleForReading.readabilityHandler = { [weak self] handle in
            guard let self else {
                handle.readabilityHandler = nil
                return
            }
            let chunk = handle.availableData
            if chunk.isEmpty {
                finishOutput()
                return
            }
            let lines = state.withLock { state in state.stdoutFinished ? [] : state.framer.push(chunk) }
            for line in lines {
                linesContinuation.yield(line)
            }
        }
        errors.fileHandleForReading.readabilityHandler = { [weak self] handle in
            guard let self else {
                handle.readabilityHandler = nil
                return
            }
            let chunk = handle.availableData
            if chunk.isEmpty {
                closeErrors()
                return
            }
            state.withLock { $0.tail.append(chunk) }
        }
        process.terminationHandler = { [weak self] process in
            process.terminationHandler = nil
            guard let self else { return }
            let exit = ChildExit(
                reason: process.terminationReason == .uncaughtSignal ? .signalled : .exited,
                status: process.terminationStatus)
            let ready = state.withLock { state -> [CheckedContinuation<ChildExit, Never>] in
                state.exit = exit
                return state.stderrClosed ? state.take() : []
            }
            ready.forEach { $0.resume(returning: exit) }
            // Something the child started that inherited stdout or stderr
            // could hold either open past the child's own exit; neither the
            // lines nor the exit is held hostage to it.
            DispatchQueue.global().asyncAfter(deadline: .now() + Self.failsafe.timeInterval) { [weak self] in
                self?.finishOutput()
                self?.closeErrors()
            }
        }

        do {
            try process.run()
        } catch {
            output.fileHandleForReading.readabilityHandler = nil
            errors.fileHandleForReading.readabilityHandler = nil
            process.terminationHandler = nil
            linesContinuation.finish()
            throw ChildError.couldNotStart(
                executable: command.executable.path, reason: error.localizedDescription)
        }
        // The child holds its own ends now; ours would keep each pipe open
        // past the child's exit.
        try? input.fileHandleForReading.close()
        try? output.fileHandleForWriting.close()
        try? errors.fileHandleForWriting.close()
    }

    deinit {
        output.fileHandleForReading.readabilityHandler = nil
        errors.fileHandleForReading.readabilityHandler = nil
        linesContinuation.finish()
    }

    /// stdout is over -- its end read, or the failsafe passed: its handler
    /// cleared, the last unended line said, the lines finished. Once.
    private func finishOutput() {
        output.fileHandleForReading.readabilityHandler = nil
        let last = state.withLock { state -> String?? in
            guard !state.stdoutFinished else { return nil }
            state.stdoutFinished = true
            return .some(state.framer.finish())
        }
        guard let last else { return }
        if let last {
            linesContinuation.yield(last)
        }
        linesContinuation.finish()
    }

    /// stderr is over -- its end read, or the failsafe passed: its handler
    /// cleared, and the exit's waiters answered once the child has exited.
    private func closeErrors() {
        errors.fileHandleForReading.readabilityHandler = nil
        let (exit, ready) = state.withLock { state -> (ChildExit?, [CheckedContinuation<ChildExit, Never>]) in
            state.stderrClosed = true
            guard let exit = state.exit else { return (nil, []) }
            return (exit, state.take())
        }
        if let exit {
            ready.forEach { $0.resume(returning: exit) }
        }
    }

    var stderrTail: BoundedTail { state.withLock { $0.tail } }

    var processIdentifier: Int32 { process.processIdentifier }

    func write(_ data: Data) throws {
        guard !state.withLock({ $0.inputClosed }) else { throw ChildError.inputClosed }
        do {
            try input.fileHandleForWriting.write(contentsOf: data)
        } catch {
            state.withLock { $0.inputClosed = true }
            throw ChildError.inputClosed
        }
    }

    func closeInput() {
        let alreadyClosed = state.withLock { state -> Bool in
            defer { state.inputClosed = true }
            return state.inputClosed
        }
        if !alreadyClosed {
            try? input.fileHandleForWriting.close()
        }
    }

    func interrupt() { signalGroup(SIGINT) }

    func terminate() { signalGroup(SIGTERM) }

    func kill() { signalGroup(SIGKILL) }

    var groupRunning: Bool {
        let group = process.processIdentifier
        return group > 0 && Darwin.kill(-group, 0) == 0
    }

    /// `signal` to the child's group, while anything of it runs -- the child
    /// gone or not. Were the child ever not a group's leader, the group would
    /// not be there to reach, and the child alone gets it.
    private func signalGroup(_ signal: Int32) {
        let group = process.processIdentifier
        guard group > 0 else { return }
        if Darwin.kill(-group, signal) != 0 && process.isRunning {
            _ = Darwin.kill(group, signal)
        }
    }

    func waitForExit() async -> ChildExit {
        await withCheckedContinuation { continuation in
            let ready = state.withLock { state -> ChildExit? in
                if let exit = state.exit, state.stderrClosed {
                    return exit
                }
                state.waiters.append(continuation)
                return nil
            }
            if let ready {
                continuation.resume(returning: ready)
            }
        }
    }
}

extension Duration {
    /// In seconds, as Dispatch takes a delay.
    fileprivate var timeInterval: TimeInterval {
        let (seconds, attoseconds) = components
        return TimeInterval(seconds) + TimeInterval(attoseconds) / 1e18
    }
}
