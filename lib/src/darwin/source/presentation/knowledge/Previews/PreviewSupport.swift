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
        PreviewReadLog.asked(arguments, reads.map(\.0) + refusals.map(\.0), read: true)
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
        PreviewReadLog.asked(arguments, commands.map(\.0), read: false)
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
/// What a preview's runner was asked, for the test that holds the previews'
/// prefixes to the lines their view-models build (GUI M23). Set as a task
/// local around a preview's story, it keeps each read no rule answered and
/// each rule that answered something: a prefix that stops matching would
/// otherwise draw an empty state, or a shorter prefix's document, and fail
/// nothing. Outside a test it is never set, and nothing is kept.
final class PreviewReadLog: @unchecked Sendable {
    @TaskLocal static var current: PreviewReadLog?

    private let lock = NSLock()
    private var held: Set<[String]> = []
    private var hit: Set<[String]> = []
    private var missed: [[String]] = []
    private var shapes: [[String]] = []

    /// `arguments` asked of a runner holding `rules`; for a read, `read`,
    /// since a read no rule answers draws nothing. Only a read holding a
    /// name after `--` is kept unanswered: a home recorded from an older
    /// binary leaves a verb it lacked unanswered on purpose.
    static func asked(_ arguments: [String], _ rules: [[String]], read: Bool) {
        guard let log = current else { return }
        let rule = rules.filter { arguments.starts(with: $0) }.max { $0.count < $1.count }
        log.lock.withLock {
            log.held.formUnion(rules)
            if let rule {
                log.hit.insert(rule)
            } else if read, arguments.contains("--") {
                log.missed.append(arguments)
                let names = Array(arguments.drop { $0 != "--" }.dropFirst())
                if !names.isEmpty, rules.contains(where: { rule in names.allSatisfy(rule.contains) }) { log.shapes.append(arguments) }
            }
        }
    }

    /// Whether a runner asked so far holds `rule`: a test drives only the
    /// reads a preview's home answers.
    func holds(_ rule: [String]) -> Bool { lock.withLock { held.contains(rule) } }

    /// The reads holding a name after `--` that no rule answered.
    var unanswered: [[String]] { lock.withLock { missed } }

    /// The reads holding a name after `--` that no rule answered while a
    /// rule of the runner asked names the same words: a prefix kept in
    /// another shape than the line's.
    var misshapen: [[String]] { lock.withLock { shapes } }

    /// The rules holding a name after `--` that nothing asked reached.
    var unreached: [[String]] {
        lock.withLock { held.subtracting(hit).filter { $0.contains("--") }.sorted { $0.joined(separator: " ") < $1.joined(separator: " ") } }
    }
}

#endif
