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
    /// Reads the binary refused in its recorded words: an older binary's
    /// refusal of a verb it lacks, exiting 109 as its parser does for words
    /// it doesn't take.
    let readRefusals: [([String], String)]

    init(
        reads: [([String], String)], commands: [([String], String)] = [], refused: [[String]] = [],
        refusals: [([String], String)] = [], readRefusals: [([String], String)] = []
    ) {
        self.reads = reads
        self.commands = commands
        self.refused = refused
        self.refusals = refusals
        self.readRefusals = readRefusals
    }

    func read(binary: URL, _ arguments: [String]) async -> ReadOutcome {
        PreviewReadLog.asked(arguments, reads.map(\.0) + readRefusals.map(\.0), read: true)
        if let stderr = Self.answer(arguments, readRefusals) {
            var tail = BoundedTail()
            tail.append(Data(stderr.utf8))
            return .failed(exit: ChildExit(reason: .exited, status: 109), stderrTail: tail)
        }
        guard let text = Self.answer(arguments, reads) else {
            return .couldNotStart("no preview document for apogee \(arguments.joined(separator: " "))")
        }
        // A recorded fixture, which the tests decode as this does.
        return .document(ReadDocument(text: text)!)
    }

    func command(binary: URL, _ arguments: [String]) async -> CommandOutcome {
        PreviewReadLog.asked(arguments, commands.map(\.0) + refusals.map(\.0) + refused, read: false)
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

/// A runner over a home that changes as a preview's story goes (GUI M16):
/// it answers as `before` does until the binary is asked `change` -- which
/// `before` answers -- and as `after` does from then on, as the binary read
/// the home back.
final class PreviewChanging: CommandRunning, @unchecked Sendable {
    private let lock = NSLock()
    private let before: PreviewRunner
    private let after: PreviewRunner
    private let change: [String]
    private var changed = false

    init(_ before: PreviewRunner, after: PreviewRunner, once change: [String]) {
        self.before = before
        self.after = after
        self.change = change
    }

    /// The runner answering `arguments`, flipped once the change is asked.
    private func answering(_ arguments: [String]) -> PreviewRunner {
        lock.withLock {
            let runner = changed ? after : before
            if arguments.starts(with: change) { changed = true }
            return runner
        }
    }

    func read(binary: URL, _ arguments: [String]) async -> ReadOutcome {
        await answering(arguments).read(binary: binary, arguments)
    }

    func command(binary: URL, _ arguments: [String]) async -> CommandOutcome {
        await answering(arguments).command(binary: binary, arguments)
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

/// A job that has failed as `check --fix` fails while a row still does
/// (`check.cpp`: it exits as the report passes): exit 1, nothing on stderr
/// -- the report is its stdout -- so the fold says it in its own words.
final class PreviewFailedJob: JobRunning {
    let updates: AsyncStream<JobUpdate>

    init() {
        let (updates, continuation) = AsyncStream.makeStream(of: JobUpdate.self)
        self.updates = updates
        continuation.yield(.ended(JobEnd(exit: ChildExit(reason: .exited, status: 1), stderrTail: BoundedTail(), interrupted: false)))
        continuation.finish()
    }

    func stop(grace: Duration) async {}
}

/// Starts preview jobs, each already failed (GUI 6c's failed fix).
struct PreviewFailingJobs: JobStarting {
    func startJob(binary: URL, arguments: [String], streaming: Bool, folder: URL?) -> any JobRunning {
        PreviewFailedJob()
    }
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
