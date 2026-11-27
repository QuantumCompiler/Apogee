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
    /// Commands the binary refused in its recorded words: answered as a
    /// refusal, its exit unrecorded, so a run that could not start.
    let refusals: [([String], String)]
    /// Reads the binary refused in its recorded words, answered the same way.
    let readRefusals: [([String], String)]

    init(
        reads: [([String], String)], commands: [([String], String)] = [], refusals: [([String], String)] = [],
        readRefusals: [([String], String)] = []
    ) {
        self.reads = reads
        self.commands = commands
        self.refusals = refusals
        self.readRefusals = readRefusals
    }

    func read(binary: URL, _ arguments: [String]) async -> ReadOutcome {
        if let refusal = Self.answer(arguments, readRefusals) {
            return .couldNotStart(refusal.trimmingCharacters(in: .newlines))
        }
        guard let text = Self.answer(arguments, reads), let document = ReadDocument(text: text) else {
            return .couldNotStart("no preview document for apogee \(arguments.joined(separator: " "))")
        }
        return .document(document)
    }

    func command(binary: URL, _ arguments: [String]) async -> CommandOutcome {
        if let refusal = Self.answer(arguments, refusals) {
            return .couldNotStart(refusal.trimmingCharacters(in: .newlines))
        }
        return .done(stdout: Self.answer(arguments, commands) ?? "")
    }

    static func answer(_ arguments: [String], _ rules: [([String], String)]) -> String? {
        rules.filter { arguments.starts(with: $0.0) }.max { $0.0.count < $1.0.count }?.1
    }
}

/// A job that says what it was given, then keeps running: a run in
/// progress -- or, given its `ending`, a run that ended so (GUI M22).
final class PreviewJob: JobRunning {
    let updates: AsyncStream<JobUpdate>
    private let continuation: AsyncStream<JobUpdate>.Continuation

    init(_ said: [JobUpdate], ending: JobEnd? = nil) {
        (updates, continuation) = AsyncStream.makeStream(of: JobUpdate.self)
        said.forEach { continuation.yield($0) }
        if let ending {
            continuation.yield(.ended(ending))
            continuation.finish()
        }
    }

    func stop(grace: Duration) async {
        continuation.finish()
    }
}

/// Starts preview jobs, each saying `said` -- and ending with `ending`,
/// when given.
struct PreviewJobs: JobStarting {
    var said: [JobUpdate] = []
    var ending: JobEnd? = nil

    func startJob(binary: URL, arguments: [String], streaming: Bool, folder: URL?) -> any JobRunning {
        PreviewJob(said, ending: ending)
    }
}

/// A recorded run as a preview job plays it: each line of `stdout` said,
/// then its exit 0, `stderr` the tail it left (GUI M22).
func recordedRun(stdout: String, stderr: String) -> PreviewJobs {
    var tail = BoundedTail()
    tail.append(Data(stderr.utf8))
    return PreviewJobs(
        said: stdout.split(separator: "\n").map { .said(String($0)) },
        ending: JobEnd(exit: ChildExit(reason: .exited, status: 0), stderrTail: tail, interrupted: false))
}

/// Where a preview says the binary is; never run.
let previewBinary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
#endif
