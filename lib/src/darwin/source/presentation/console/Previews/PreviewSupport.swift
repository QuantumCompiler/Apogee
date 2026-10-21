#if DEBUG
import BusinessJobs
import BusinessReads
import Foundation
import InfrastructureProcess

/// Preview support (ADR 0004). Each module keeps its own copy, as each test
/// bundle keeps its own scripted support.

/// Answers a preview's commands with what they print: nothing starts the
/// binary or touches the person's files. The console reads no documents.
struct PreviewRunner: CommandRunning {
    /// Argument prefixes and what they print; the longest prefix wins.
    let commands: [([String], String)]

    func read(binary: URL, _ arguments: [String]) async -> ReadOutcome {
        .couldNotStart("no preview document for apogee \(arguments.joined(separator: " "))")
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

/// How a preview job ends: it exited with `status`, `stderr` its tail, as the
/// supervisor says a run's end.
func previewEnd(status: Int32, stderr: String = "", interrupted: Bool = false) -> JobUpdate {
    var tail = BoundedTail()
    if !stderr.isEmpty { tail.append(Data((stderr + "\n").utf8)) }
    return .ended(JobEnd(exit: ChildExit(reason: .exited, status: status), stderrTail: tail, interrupted: interrupted))
}

/// Where a preview says the binary is; never run.
let previewBinary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
#endif
