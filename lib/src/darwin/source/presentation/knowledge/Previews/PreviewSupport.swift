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
    /// Reads the binary refused, each its recorded stderr; every one recorded
    /// exited 1.
    let refusals: [([String], String)]

    init(reads: [([String], String)], commands: [([String], String)] = [], refusals: [([String], String)] = []) {
        self.reads = reads
        self.commands = commands
        self.refusals = refusals
    }

    func read(binary: URL, _ arguments: [String]) async -> ReadOutcome {
        if let stderr = Self.answer(arguments, refusals) {
            var tail = BoundedTail()
            tail.append(Data(stderr.utf8))
            return .failed(exit: ChildExit(reason: .exited, status: 1), stderrTail: tail)
        }
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

/// A job that says what it was given, then keeps running: a run in
/// progress.
final class PreviewJob: JobRunning {
    let updates: AsyncStream<JobUpdate>
    private let continuation: AsyncStream<JobUpdate>.Continuation

    init(_ said: [JobUpdate]) {
        (updates, continuation) = AsyncStream.makeStream(of: JobUpdate.self)
        said.forEach { continuation.yield($0) }
    }

    func stop(grace: Duration) async {
        continuation.finish()
    }
}

/// Starts preview jobs, each saying `said`.
struct PreviewJobs: JobStarting {
    var said: [JobUpdate] = []

    func startJob(binary: URL, arguments: [String], streaming: Bool, folder: URL?) -> any JobRunning {
        PreviewJob(said)
    }
}

/// Where a preview says the binary is; never run.
let previewBinary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
#endif
