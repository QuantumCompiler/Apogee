import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Synchronization

/// A runner a test scripts (each bundle keeps its own copy of its support).
final class ScriptedRunner: CommandRunning, @unchecked Sendable {
    private let reads = Mutex<[([String], ReadOutcome)]>([])
    private let log = Mutex<[String]>([])

    var calls: [String] { log.withLock { $0 } }

    func reading(_ prefix: [String], _ json: String) {
        reads.withLock { $0.insert((prefix, .document(ReadDocument(text: json)!)), at: 0) }
    }

    func read(binary: URL, _ arguments: [String]) async -> ReadOutcome {
        log.withLock { $0.append("read " + arguments.joined(separator: " ")) }
        return reads.withLock { rules in rules.first { arguments.starts(with: $0.0) }?.1 }
            ?? .couldNotStart("no scripted read for \(arguments)")
    }

    func command(binary: URL, _ arguments: [String]) async -> CommandOutcome {
        log.withLock { $0.append("run " + arguments.joined(separator: " ")) }
        return .done(stdout: "")
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

    func end(status: Int32 = 0) {
        continuation.yield(.ended(JobEnd(exit: ChildExit(reason: .exited, status: status), stderrTail: BoundedTail(), interrupted: false)))
        continuation.finish()
    }

    func endWith(tail: BoundedTail, status: Int32 = 1) {
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
    struct Start {
        let arguments: [String]
        let streaming: Bool
        let folder: URL?
    }

    private let jobs: Mutex<[ScriptedJob]>
    private let started = Mutex<[Start]>([])

    init(_ jobs: [ScriptedJob]) {
        self.jobs = Mutex(jobs)
    }

    var starts: [Start] { started.withLock { $0 } }

    func startJob(binary: URL, arguments: [String], streaming: Bool, folder: URL?) -> any JobRunning {
        started.withLock { $0.append(Start(arguments: arguments, streaming: streaming, folder: folder)) }
        return jobs.withLock { $0.removeFirst() }
    }
}

@MainActor
func eventually(_ condition: @MainActor () -> Bool) async -> Bool {
    let deadline = ContinuousClock.now + .seconds(5)
    while !condition() {
        if ContinuousClock.now > deadline { return false }
        try? await Task.sleep(for: .milliseconds(10))
    }
    return true
}
