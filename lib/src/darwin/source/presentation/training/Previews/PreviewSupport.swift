#if DEBUG
import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess

/// Preview support (ADR 0004). Each module keeps its own copy, as each test
/// bundle keeps its own scripted support.

/// Answers a preview's reads from the binary's own recorded documents and its
/// commands as done -- with what the binary printed, where one is recorded:
/// nothing starts the binary or touches the person's files.
struct PreviewRunner: CommandRunning {
    /// Argument prefixes and what they answer; the longest prefix wins.
    let reads: [([String], String)]
    /// Commands and what each printed -- a completion's candidates; the
    /// longest prefix wins, and any other is done with nothing said.
    var commands: [([String], String)] = []
    /// Reads the binary refused, each its recorded stderr; every one recorded
    /// exited 1.
    var refusals: [([String], String)] = []
    /// Reads that never finish within the reader's limit: the reader's own
    /// outcome, with no word of the binary's.
    var unfinished: [[String]] = []

    func read(binary: URL, _ arguments: [String]) async -> ReadOutcome {
        if let stderr = Self.answer(arguments, refusals) {
            var tail = BoundedTail()
            tail.append(Data(stderr.utf8))
            return .failed(exit: ChildExit(reason: .exited, status: 1), stderrTail: tail)
        }
        if unfinished.contains(where: { arguments.starts(with: $0) }) {
            return .timedOut(after: .seconds(60), stderrTail: BoundedTail())
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

/// A job that says what it was given, then keeps running -- a run in
/// progress -- or, when it `finishes`, ends there, as a finished run's
/// stream does.
final class PreviewJob: JobRunning {
    let updates: AsyncStream<JobUpdate>
    private let continuation: AsyncStream<JobUpdate>.Continuation

    init(_ said: [JobUpdate], finishes: Bool = false) {
        (updates, continuation) = AsyncStream.makeStream(of: JobUpdate.self)
        said.forEach { continuation.yield($0) }
        if finishes { continuation.finish() }
    }

    func stop(grace: Duration) async {
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

/// Where a preview says the binary is; never run.
let previewBinary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
#endif
