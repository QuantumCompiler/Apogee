#if DEBUG
import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess

/// Preview support (ADR 0004). Each module keeps its own copy, as each test
/// bundle keeps its own scripted support.

/// Answers a preview's reads from the binary's own recorded documents and its
/// commands with what the binary printed: nothing starts the binary or
/// touches the person's files.
struct PreviewRunner: CommandRunning {
    /// Argument prefixes and what they answer; the longest prefix wins.
    let reads: [([String], String)]
    let commands: [([String], String)]

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

/// A job that says what it was given, then keeps running -- a run in
/// progress -- or, when it `finishes`, ends there as the child did: `status`
/// 0 is done, any other a failure in what it said.
final class PreviewJob: JobRunning {
    let updates: AsyncStream<JobUpdate>
    private let continuation: AsyncStream<JobUpdate>.Continuation

    init(_ said: [JobUpdate], finishes: Bool = false, status: Int32 = 0) {
        (updates, continuation) = AsyncStream.makeStream(of: JobUpdate.self)
        said.forEach { continuation.yield($0) }
        if finishes {
            continuation.yield(.ended(JobEnd(exit: ChildExit(reason: .exited, status: status), stderrTail: BoundedTail(), interrupted: false)))
            continuation.finish()
        }
    }

    /// Stopped as Ctrl-C stops it.
    func stop(grace: Duration) async {
        continuation.yield(.ended(JobEnd(exit: ChildExit(reason: .exited, status: 130), stderrTail: BoundedTail(), interrupted: true)))
        continuation.finish()
    }
}

/// Starts preview jobs, each saying `said` (and ending there, with `status`,
/// when it `finishes`).
struct PreviewJobs: JobStarting {
    var said: [JobUpdate] = []
    var finishes = false
    var status: Int32 = 0

    func startJob(binary: URL, arguments: [String], streaming: Bool, folder: URL?) -> any JobRunning {
        PreviewJob(said, finishes: finishes, status: status)
    }
}

/// Where a preview says the binary is; never run.
let previewBinary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
#endif
