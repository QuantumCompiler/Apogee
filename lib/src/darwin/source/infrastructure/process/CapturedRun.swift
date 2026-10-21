import Foundation
import Synchronization

/// A child run to its end, with everything it said kept: what a one-shot
/// command -- a read, a version probe -- comes back as.
public struct CapturedRun: Sendable, Equatable {
    public let exit: ChildExit
    /// Its stdout, lines joined with `\n`, up to the limit.
    public let stdout: String
    /// Whether stdout ran past the limit, and the child was stopped for it.
    public let stdoutTruncated: Bool
    public let stderrTail: BoundedTail
    /// Whether it outlived its time and was stopped for it.
    public let timedOut: Bool
}

extension ChildSpawner {
    /// Runs `command` to its end with its input closed -- a one-shot command
    /// reads nothing, or reads `input` and then its end -- stopping it once it
    /// runs past `timeout`, or writes more than `stdoutLimit` bytes:
    /// `terminate` first, `kill` two seconds later.
    public func capture(
        _ command: ChildCommand, timeout: Duration, stdoutLimit: Int = 64 << 20, input: Data? = nil
    ) async throws -> CapturedRun {
        let child = try spawn(command)
        if let input {
            try? child.write(input)
        }
        child.closeInput()
        let stopped = StopFlag()
        let timer = Task {
            try await Task.sleep(for: timeout)
            stopped.set()
            child.terminate()
            try await Task.sleep(for: .seconds(2))
            child.kill()
        }
        var kept: [String] = []
        var size = 0
        var truncated = false
        // Read to the end even past the limit, so the child is never left
        // blocked on a full pipe.
        for await line in child.lines where !truncated {
            size += line.utf8.count + 1
            if size > stdoutLimit {
                truncated = true
                child.terminate()
            } else {
                kept.append(line)
            }
        }
        let exit = await child.waitForExit()
        timer.cancel()
        return CapturedRun(
            exit: exit, stdout: kept.joined(separator: "\n"), stdoutTruncated: truncated,
            stderrTail: child.stderrTail,
            timedOut: stopped.isSet)
    }
}

/// Set once, read from anywhere.
private final class StopFlag: Sendable {
    private let value = Mutex(false)
    func set() { value.withLock { $0 = true } }
    var isSet: Bool { value.withLock { $0 } }
}
