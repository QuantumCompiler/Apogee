import BusinessJobs
import BusinessReads
import Foundation
import InfrastructureProcess
import Synchronization

/// A runner a test scripts: each command answered by the first rule its
/// arguments start with, every call recorded. (Each bundle keeps its own
/// support.)
final class ScriptedRunner: CommandRunning, @unchecked Sendable {
    private let answers = Mutex<[([String], CommandOutcome)]>([])
    private let log = Mutex<[String]>([])
    /// Commands held open until the test releases them: the next command
    /// each prefix starts waits on its gate.
    private let gates = Mutex<[([String], AsyncStream<Void>)]>([])

    var calls: [String] { log.withLock { $0 } }

    func answering(_ prefix: [String], _ outcome: CommandOutcome) {
        answers.withLock { $0.insert((prefix, outcome), at: 0) }
    }

    /// Holds the next command `prefix` starts -- recorded in `calls` as it
    /// starts -- until the returned closure is called, then answers it by the
    /// rules, as a slow `__complete` would: so an answer can land after the
    /// line changed, and two can land in either order (GUI 6e).
    func holding(_ prefix: [String]) -> @Sendable () -> Void {
        let (gate, open) = AsyncStream.makeStream(of: Void.self)
        gates.withLock { $0.append((prefix, gate)) }
        return { open.finish() }
    }

    func read(binary: URL, _ arguments: [String]) async -> ReadOutcome {
        log.withLock { $0.append("read " + arguments.joined(separator: " ")) }
        return .couldNotStart("no scripted read for \(arguments)")
    }

    func command(binary: URL, _ arguments: [String]) async -> CommandOutcome {
        log.withLock { $0.append("run " + arguments.joined(separator: "|")) }
        let gate = gates.withLock { held -> AsyncStream<Void>? in
            guard let index = held.firstIndex(where: { arguments.starts(with: $0.0) }) else { return nil }
            return held.remove(at: index).1
        }
        if let gate {
            for await _ in gate {}
        }
        return answers.withLock { rules in rules.first { arguments.starts(with: $0.0) }?.1 } ?? .done(stdout: "")
    }
}

/// A job a test drives by hand.
final class ScriptedJob: JobRunning, @unchecked Sendable {
    let updates: AsyncStream<JobUpdate>
    private let continuation: AsyncStream<JobUpdate>.Continuation

    init() {
        (updates, continuation) = AsyncStream.makeStream(of: JobUpdate.self)
    }

    func say(_ update: JobUpdate) { continuation.yield(update) }

    func end(status: Int32 = 0, stderr: String = "") {
        var tail = BoundedTail()
        if !stderr.isEmpty { tail.append(Data((stderr + "\n").utf8)) }
        continuation.yield(.ended(JobEnd(exit: ChildExit(reason: .exited, status: status), stderrTail: tail, interrupted: false)))
        continuation.finish()
    }

    func stop(grace: Duration) async {
        continuation.yield(.ended(JobEnd(exit: ChildExit(reason: .exited, status: 130), stderrTail: BoundedTail(), interrupted: true)))
        continuation.finish()
    }
}

/// Hands out scripted jobs, recording each start.
final class ScriptedJobs: JobStarting, @unchecked Sendable {
    private let jobs: Mutex<[ScriptedJob]>
    private let started = Mutex<[[String]]>([])

    init(_ jobs: [ScriptedJob]) {
        self.jobs = Mutex(jobs)
    }

    var starts: [[String]] { started.withLock { $0 } }

    /// The next scripted job -- or, when the script has run out, one that
    /// never ends: a start nobody scripted is recorded, never a crash.
    func startJob(binary: URL, arguments: [String], streaming: Bool, folder: URL?) -> any JobRunning {
        started.withLock { $0.append(arguments) }
        return jobs.withLock { $0.isEmpty ? ScriptedJob() : $0.removeFirst() }
    }
}

@MainActor
func eventually(_ condition: @MainActor () -> Bool) async -> Bool {
    for _ in 0..<200 {
        if condition() { return true }
        try? await Task.sleep(for: .milliseconds(10))
    }
    return condition()
}
