#if DEBUG
import BusinessJobs
import Foundation
import InfrastructureProcess

/// Preview support (ADR 0004). Each module keeps its own copy, as each test
/// bundle keeps its own scripted support.

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
