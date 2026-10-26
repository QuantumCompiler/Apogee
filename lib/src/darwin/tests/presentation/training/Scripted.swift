import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Synchronization

/// A runner a test scripts: each command answered by the first rule its
/// arguments start with, every call recorded. (Each bundle keeps its own
/// support.)
final class ScriptedRunner: CommandRunning, @unchecked Sendable {
    private let answers = Mutex<[([String], CommandOutcome)]>([])
    private let reads = Mutex<[([String], String)]>([])
    private let log = Mutex<[String]>([])
    private let readFailures = Mutex<[([String], ReadOutcome)]>([])

    var calls: [String] { log.withLock { $0 } }

    /// Answers every later read starting with `prefix` with `outcome`.
    func failing(_ prefix: [String], _ outcome: ReadOutcome) {
        readFailures.withLock { $0.insert((prefix, outcome), at: 0) }
    }

    func answering(_ prefix: [String], _ outcome: CommandOutcome) {
        answers.withLock { $0.insert((prefix, outcome), at: 0) }
    }

    func reading(_ prefix: [String], _ json: String) {
        reads.withLock { $0.insert((prefix, json), at: 0) }
    }

    func read(binary: URL, _ arguments: [String]) async -> ReadOutcome {
        log.withLock { $0.append("read " + arguments.joined(separator: "|")) }
        if let failure = readFailures.withLock({ rules in rules.first { arguments.starts(with: $0.0) }?.1 }) { return failure }
        return reads.withLock { rules in rules.first { arguments.starts(with: $0.0) } }.map { .document(ReadDocument(text: $0.1)!) }
            ?? .couldNotStart("no scripted read for \(arguments)")
    }

    func command(binary: URL, _ arguments: [String]) async -> CommandOutcome {
        log.withLock { $0.append("run " + arguments.joined(separator: "|")) }
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

/// Hands out scripted jobs, recording each start and whether it asked for
/// the stream.
final class ScriptedJobs: JobStarting, @unchecked Sendable {
    private let jobs: Mutex<[ScriptedJob]>
    private let started = Mutex<[([String], Bool)]>([])

    init(_ jobs: [ScriptedJob]) {
        self.jobs = Mutex(jobs)
    }

    var starts: [[String]] { started.withLock { $0.map { $0.0 } } }
    /// Each start's streaming flag, in order.
    var streamed: [Bool] { started.withLock { $0.map { $0.1 } } }

    func startJob(binary: URL, arguments: [String], streaming: Bool, folder: URL?) -> any JobRunning {
        started.withLock { $0.append((arguments, streaming)) }
        return jobs.withLock { $0.removeFirst() }
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

func tail(_ text: String) -> BoundedTail {
    var tail = BoundedTail()
    tail.append(Data((text + "\n").utf8))
    return tail
}
