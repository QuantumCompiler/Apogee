import Foundation
import Synchronization

/// Starts children with Foundation's `Process`: three pipes, the stdout
/// framed into lines, the stderr kept as a bounded tail.
public struct ProcessSpawner: ChildSpawner {
    /// How many bytes of stderr each child keeps.
    public let stderrLimit: Int

    public init(stderrLimit: Int = 8 * 1024) {
        self.stderrLimit = stderrLimit
    }

    public func spawn(_ command: ChildCommand) throws -> any RunningChild {
        try FoundationChild(command, stderrLimit: stderrLimit)
    }
}

/// A `Process` and its pipes. Its state is behind one lock, so the pipes'
/// handlers, the termination handler and the caller never race.
final class FoundationChild: RunningChild, @unchecked Sendable {
    private let process = Process()
    private let input = Pipe()
    private let output = Pipe()
    private let errors = Pipe()
    let lines: AsyncStream<String>

    private struct State {
        var framer = LineFramer()
        var tail: BoundedTail
        var exit: ChildExit?
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

    init(_ command: ChildCommand, stderrLimit: Int) throws {
        state = Mutex(State(tail: BoundedTail(limit: stderrLimit)))
        let (lines, continuation) = AsyncStream.makeStream(of: String.self)
        self.lines = lines

        process.executableURL = command.executable
        process.arguments = command.arguments
        process.environment = command.environment(over: ProcessInfo.processInfo.environment)
        if let directory = command.workingDirectory {
            process.currentDirectoryURL = directory
        }
        process.standardInput = input
        process.standardOutput = output
        process.standardError = errors

        // A write to a child that has stopped reading must fail as an error,
        // never as the SIGPIPE that would end the app.
        _ = fcntl(input.fileHandleForWriting.fileDescriptor, F_SETNOSIGPIPE, 1)

        output.fileHandleForReading.readabilityHandler = { [self] handle in
            let chunk = handle.availableData
            if chunk.isEmpty {
                handle.readabilityHandler = nil
                if let last = state.withLock({ $0.framer.finish() }) {
                    continuation.yield(last)
                }
                continuation.finish()
                return
            }
            for line in state.withLock({ $0.framer.push(chunk) }) {
                continuation.yield(line)
            }
        }
        errors.fileHandleForReading.readabilityHandler = { [self] handle in
            let chunk = handle.availableData
            if chunk.isEmpty {
                handle.readabilityHandler = nil
                stderrClosed()
                return
            }
            state.withLock { $0.tail.append(chunk) }
        }
        process.terminationHandler = { [self] process in
            process.terminationHandler = nil
            let exit = ChildExit(
                reason: process.terminationReason == .uncaughtSignal ? .signalled : .exited,
                status: process.terminationStatus)
            let ready = state.withLock { state -> [CheckedContinuation<ChildExit, Never>] in
                state.exit = exit
                return state.stderrClosed ? state.take() : []
            }
            ready.forEach { $0.resume(returning: exit) }
            // A grandchild that inherited stderr could hold it open past the
            // child's own exit; the exit is not held hostage to it.
            DispatchQueue.global().asyncAfter(deadline: .now() + 2) { [self] in stderrClosed() }
        }

        do {
            try process.run()
        } catch {
            output.fileHandleForReading.readabilityHandler = nil
            errors.fileHandleForReading.readabilityHandler = nil
            continuation.finish()
            throw ChildError.couldNotStart(
                executable: command.executable.path, reason: error.localizedDescription)
        }
        // The child holds its own ends now; ours would keep each pipe open
        // past the child's exit.
        try? input.fileHandleForReading.close()
        try? output.fileHandleForWriting.close()
        try? errors.fileHandleForWriting.close()
    }

    private func stderrClosed() {
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

    func interrupt() {
        if process.isRunning {
            process.interrupt()
        }
    }

    func terminate() {
        if process.isRunning {
            process.terminate()
        }
    }

    func kill() {
        if process.isRunning {
            Darwin.kill(process.processIdentifier, SIGKILL)
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
