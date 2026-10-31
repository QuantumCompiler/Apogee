#if DEBUG
import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess

/// Preview support (ADR 0004). Each module keeps its own copy, as each test
/// bundle keeps its own scripted support.

/// Answers a preview's reads from the binary's own recorded documents and its
/// commands as done: nothing starts the binary or touches the person's files.
struct PreviewRunner: CommandRunning {
    /// Argument prefixes and what they answer; the longest prefix wins.
    let reads: [([String], String)]
    let commands: [([String], String)]

    init(reads: [([String], String)], commands: [([String], String)] = []) {
        self.reads = reads
        self.commands = commands
    }

    func read(binary: URL, _ arguments: [String]) async -> ReadOutcome {
        guard let text = Self.answer(arguments, reads), let document = ReadDocument(text: text) else {
            return .couldNotStart("no preview document for apogee \(arguments.joined(separator: " "))")
        }
        return .document(document)
    }

    func command(binary: URL, _ arguments: [String]) async -> CommandOutcome {
        .done(stdout: Self.answer(arguments, commands) ?? "")
    }

    static func answer(_ arguments: [String], _ rules: [([String], String)]) -> String? {
        rules.filter { arguments.starts(with: $0.0) }.max { $0.0.count < $1.0.count }?.1
    }
}

/// A runner over a store that changes as a preview's story goes (GUI 4e):
/// it answers as `PreviewRunner` does, and `change` lays what the binary
/// answered later over what it answered before -- `nil` for a read that then
/// failed, as one of a deleted set's path does.
final class PreviewStore: CommandRunning, @unchecked Sendable {
    private let lock = NSLock()
    private var reads: [([String], String?)]
    private let commands: [([String], String)]

    init(reads: [([String], String)], commands: [([String], String)] = []) {
        self.reads = reads.map { ($0.0, Optional($0.1)) }
        self.commands = commands
    }

    /// The store as the binary read it after a change: these answers first.
    func change(_ later: [([String], String?)]) {
        lock.withLock { reads = later + reads }
    }

    func read(binary: URL, _ arguments: [String]) async -> ReadOutcome {
        // The longest prefix wins; of two as long, the later answer.
        let answer = lock.withLock { reads.filter { arguments.starts(with: $0.0) }.max { $0.0.count < $1.0.count } }
        guard let text = answer?.1, let document = ReadDocument(text: text) else {
            return .couldNotStart("no preview document for apogee \(arguments.joined(separator: " "))")
        }
        return .document(document)
    }

    func command(binary: URL, _ arguments: [String]) async -> CommandOutcome {
        .done(stdout: PreviewRunner.answer(arguments, commands) ?? "")
    }
}

/// A job that says what it was given, then keeps running -- a run in
/// progress -- or, when it `finishes`, ends there, as a finished run's
/// stream does. Stop ends it as an interrupted run's stream ends.
final class PreviewJob: JobRunning {
    let updates: AsyncStream<JobUpdate>
    private let continuation: AsyncStream<JobUpdate>.Continuation

    init(_ said: [JobUpdate], finishes: Bool = false) {
        (updates, continuation) = AsyncStream.makeStream(of: JobUpdate.self)
        said.forEach { continuation.yield($0) }
        if finishes { continuation.finish() }
    }

    func stop(grace: Duration) async {
        continuation.yield(previewEnd(status: 130, interrupted: true))
        continuation.finish()
    }
}

/// Starts preview jobs, each saying `said` (and ending there when it `finishes`).
struct PreviewJobs: JobStarting {
    var said: [JobUpdate] = []
    var finishes = false

    func startJob(binary: URL, arguments: [String], streaming: Bool, folder: URL?) -> any JobRunning {
        PreviewJob(said, finishes: finishes)
    }
}

/// Starts preview jobs from a queue, one run a start -- a run that failed,
/// then the same command run again -- each saying its updates and ending
/// there when it `finishes`; past the queue, a run that says nothing.
final class PreviewJobQueue: JobStarting, @unchecked Sendable {
    private let lock = NSLock()
    private var runs: [(said: [JobUpdate], finishes: Bool)]

    init(_ runs: [(said: [JobUpdate], finishes: Bool)]) {
        self.runs = runs
    }

    func startJob(binary: URL, arguments: [String], streaming: Bool, folder: URL?) -> any JobRunning {
        let run = lock.withLock { runs.isEmpty ? (said: [], finishes: false) : runs.removeFirst() }
        return PreviewJob(run.said, finishes: run.finishes)
    }
}

/// How a preview job ends: it exited with `status`, its stderr `stderr` (the
/// binary's own words, recorded), as the supervisor says a run's end. The
/// exit and the tail are InfrastructureProcess's, which this module reaches
/// only through `JobEnd`, so they are made by their initializers alone,
/// never named.
func previewEnd(status: Int32, interrupted: Bool = false, stderr: String = "") -> JobUpdate {
    var tail = BoundedTail()
    tail.append(Data(stderr.utf8))
    return .ended(JobEnd(exit: ChildExit(reason: .exited, status: status), stderrTail: tail, interrupted: interrupted))
}

/// Each line of a run's recorded stdout, as the supervisor says it.
func previewSaid(_ stdout: String) -> [JobUpdate] {
    stdout.split(separator: "\n", omittingEmptySubsequences: false).map { .said(String($0)) }
}

/// Each line of a recorded 52b stream, as the supervisor reads it.
func previewStream(_ jsonl: String) -> [JobUpdate] {
    jsonl.split(whereSeparator: \.isNewline).map { .event(JobEvent.decode(line: String($0))) }
}

/// Where a preview says the binary is; never run.
let previewBinary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
#endif
