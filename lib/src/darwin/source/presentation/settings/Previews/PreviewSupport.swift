#if DEBUG
import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess

/// Preview support (ADR 0004). Each module keeps its own copy, as each test
/// bundle keeps its own scripted support.

/// Answers a preview's reads from the binary's own recorded documents and its
/// commands as done -- or refused, as `config get` refuses a key the config
/// does not set: nothing starts the binary or touches the person's files.
struct PreviewRunner: CommandRunning {
    /// Argument prefixes and what they answer; the longest prefix wins.
    let reads: [([String], String)]
    let commands: [([String], String)]
    /// Argument prefixes the binary refuses, when no command answers first.
    let refused: [[String]]
    /// Commands the binary refused in its recorded words, before any answer.
    let refusals: [([String], String)]

    init(
        reads: [([String], String)], commands: [([String], String)] = [], refused: [[String]] = [],
        refusals: [([String], String)] = []
    ) {
        self.reads = reads
        self.commands = commands
        self.refused = refused
        self.refusals = refusals
    }

    func read(binary: URL, _ arguments: [String]) async -> ReadOutcome {
        guard let text = Self.answer(arguments, reads) else {
            return .couldNotStart("no preview document for apogee \(arguments.joined(separator: " "))")
        }
        // A recorded fixture, which the tests decode as this does.
        return .document(ReadDocument(text: text)!)
    }

    func command(binary: URL, _ arguments: [String]) async -> CommandOutcome {
        if let stderr = Self.answer(arguments, refusals) {
            var tail = BoundedTail()
            tail.append(Data(stderr.utf8))
            return .failed(exit: ChildExit(reason: .exited, status: 1), stderrTail: tail)
        }
        if let answer = Self.answer(arguments, commands) { return .done(stdout: answer) }
        if refused.contains(where: { arguments.starts(with: $0) }) {
            return .failed(exit: ChildExit(reason: .exited, status: 1), stderrTail: BoundedTail())
        }
        return .done(stdout: "")
    }

    static func answer(_ arguments: [String], _ rules: [([String], String)]) -> String? {
        rules.filter { arguments.starts(with: $0.0) }.max { $0.0.count < $1.0.count }?.1
    }
}

/// A job that keeps running and has said nothing yet: a fix in progress.
final class PreviewJob: JobRunning {
    let updates: AsyncStream<JobUpdate>
    private let continuation: AsyncStream<JobUpdate>.Continuation

    init() {
        (updates, continuation) = AsyncStream.makeStream(of: JobUpdate.self)
    }

    /// Stopped as Ctrl-C stops it.
    func stop(grace: Duration) async {
        continuation.yield(.ended(JobEnd(exit: ChildExit(reason: .exited, status: 130), stderrTail: BoundedTail(), interrupted: true)))
        continuation.finish()
    }
}

/// Starts preview jobs, each still running.
struct PreviewJobs: JobStarting {
    func startJob(binary: URL, arguments: [String], streaming: Bool, folder: URL?) -> any JobRunning {
        PreviewJob()
    }
}

/// Where a preview says the binary is; never run.
let previewBinary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
#endif
