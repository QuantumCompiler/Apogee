#if DEBUG
import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Synchronization

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

    /// `stderr` is what the run left there when it `finishes`: its tail.
    init(_ said: [JobUpdate], finishes: Bool = false, status: Int32 = 0, stderr: String = "") {
        (updates, continuation) = AsyncStream.makeStream(of: JobUpdate.self)
        said.forEach { continuation.yield($0) }
        if finishes {
            var tail = BoundedTail()
            tail.append(Data(stderr.utf8))
            continuation.yield(.ended(JobEnd(exit: ChildExit(reason: .exited, status: status), stderrTail: tail, interrupted: false)))
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

/// One recorded run a preview replays: its stdout's lines -- the 52b
/// stream's events when `streaming`, else lines said -- then, when it
/// `finishes`, its end with `status` and the stderr it left.
struct PreviewRun: Sendable {
    var stdout = ""
    var streaming = false
    var finishes = true
    var status: Int32 = 0
    var stderr = ""

    /// What a supervised job says of it, its end last when it finishes.
    var said: [JobUpdate] {
        stdout.split(separator: "\n").map { streaming ? .event(JobEvent.decode(line: String($0))) : .said(String($0)) }
    }

    /// The same, ended as it ended: what its progress folds.
    var updates: [JobUpdate] {
        var tail = BoundedTail()
        tail.append(Data(stderr.utf8))
        return said + (finishes ? [.ended(JobEnd(exit: ChildExit(reason: .exited, status: status), stderrTail: tail, interrupted: false))] : [])
    }
}

/// Starts preview jobs from recorded runs, one run a start, in order; past
/// the last, a run that keeps going and says nothing.
final class PreviewRuns: JobStarting {
    private let runs: Mutex<[PreviewRun]>

    init(_ runs: [PreviewRun]) {
        self.runs = Mutex(runs)
    }

    func startJob(binary: URL, arguments: [String], streaming: Bool, folder: URL?) -> any JobRunning {
        let run = runs.withLock { $0.isEmpty ? PreviewRun(finishes: false) : $0.removeFirst() }
        return PreviewJob(run.said, finishes: run.finishes, status: run.status, stderr: run.stderr)
    }
}

/// Where a preview says the binary is; never run.
let previewBinary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
#endif
