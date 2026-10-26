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
    private let gates = Mutex<[([String], Gate)]>([])

    var calls: [String] { log.withLock { $0 } }

    /// Holds every later call starting with `prefix` at a gate the test opens.
    func holding(_ prefix: [String]) -> Gate {
        let gate = Gate()
        gates.withLock { $0.insert((prefix, gate), at: 0) }
        return gate
    }

    private func gate(_ arguments: [String]) -> Gate? {
        gates.withLock { rules in rules.first { arguments.starts(with: $0.0) }?.1 }
    }

    func answering(_ prefix: [String], _ outcome: CommandOutcome) {
        answers.withLock { $0.insert((prefix, outcome), at: 0) }
    }

    func reading(_ prefix: [String], _ json: String) {
        reads.withLock { $0.insert((prefix, json), at: 0) }
    }

    private let readFailures = Mutex<[([String], ReadOutcome)]>([])

    func failing(_ prefix: [String], _ outcome: ReadOutcome) {
        readFailures.withLock { $0.insert((prefix, outcome), at: 0) }
    }

    func read(binary: URL, _ arguments: [String]) async -> ReadOutcome {
        log.withLock { $0.append("read " + arguments.joined(separator: "|")) }
        await gate(arguments)?.pass()
        if let failure = readFailures.withLock({ rules in rules.first { arguments.starts(with: $0.0) }?.1 }) { return failure }
        return reads.withLock { rules in rules.first { arguments.starts(with: $0.0) } }.map { .document(ReadDocument(text: $0.1)!) }
            ?? .couldNotStart("no scripted read for \(arguments)")
    }

    func command(binary: URL, _ arguments: [String]) async -> CommandOutcome {
        log.withLock { $0.append("run " + arguments.joined(separator: "|")) }
        await gate(arguments)?.pass()
        return answers.withLock { rules in rules.first { arguments.starts(with: $0.0) }?.1 } ?? .done(stdout: "")
    }
}

/// A read or command held until the test lets it go: a slow binary, caught
/// mid-act. `arrived` counts the calls that reached it.
final class Gate: @unchecked Sendable {
    private struct State {
        var arrived = 0
        var open = false
        var waiting: [CheckedContinuation<Void, Never>] = []
    }

    private let state = Mutex(State())

    var arrived: Int { state.withLock { $0.arrived } }

    func pass() async {
        await withCheckedContinuation { (continuation: CheckedContinuation<Void, Never>) in
            let go = state.withLock { state -> Bool in
                state.arrived += 1
                if state.open { return true }
                state.waiting.append(continuation)
                return false
            }
            if go { continuation.resume() }
        }
    }

    func open() {
        let waiting = state.withLock { state -> [CheckedContinuation<Void, Never>] in
            state.open = true
            let waiting = state.waiting
            state.waiting = []
            return waiting
        }
        waiting.forEach { $0.resume() }
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

    func startJob(binary: URL, arguments: [String], streaming: Bool, folder: URL?) -> any JobRunning {
        started.withLock { $0.append(arguments) }
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
