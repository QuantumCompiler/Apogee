import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Observation

/// The Knowledge place's records: the decision records `knowledge list`
/// reads, a question answered by `knowledge query --json`, and each change as
/// its verb, read back.
///
/// SwiftUI-free (ADR 0002). Documents in, commands out: the list is the
/// binary's, a score is on the retriever's own scale and said with it, and a
/// capture -- one model call by the clerk -- runs supervised.
@MainActor
@Observable
public final class RecordsViewModel {
    public enum Listing: Equatable, Sendable {
        case notRead
        case read
        case missing
        case failed(String)
    }

    /// What `knowledge capture` is asked, as a person types it.
    public struct CaptureForm: Equatable, Sendable {
        public var text = ""
        /// Overrides of the clerk's own reading, left out unless set.
        public var status: String?
        public var discipline: String?
        public var link = ""

        public init() {}

        /// `knowledge capture [--status s] [--discipline d] [--link l] --
        /// <text>`; nil with nothing to capture. The text after `--`, so the
        /// binary never reads it as a flag (`Argv`, GUI 4g).
        public var arguments: [String]? {
            let text = text.trimmingCharacters(in: .whitespacesAndNewlines)
            guard !text.isEmpty else { return nil }
            var arguments = ["knowledge", "capture"]
            if let status { arguments += ["--status", status] }
            if let discipline { arguments += ["--discipline", discipline] }
            let link = link.trimmingCharacters(in: .whitespaces)
            if !link.isEmpty { arguments += ["--link", link] }
            return arguments + Argv.text(text)
        }
    }

    public private(set) var records: [KnowledgeRecord] = []
    public private(set) var listing: Listing = .notRead
    /// The branch the list shows; nil for every branch.
    public var branch: String?
    public var question = ""
    public private(set) var query: KnowledgeQuery?
    public private(set) var selected: String?
    public var pendingDelete: KnowledgeRecord?
    public var captureForm = CaptureForm()
    public var capturing = false
    /// A capture or a reindex running now.
    public private(set) var run: JobProgress?
    /// A command running now, by what it is doing.
    public private(set) var working: String?
    public private(set) var failure: String?
    public var say: (@MainActor (String) -> Void)?

    private let runner: any CommandRunning
    private let starter: any JobStarting
    private var binary: URL?
    private var job: (any JobRunning)?

    public init(runner: any CommandRunning, starter: any JobStarting) {
        self.runner = runner
        self.starter = starter
    }

    public func connect(binary: URL?) async {
        self.binary = binary
        if binary == nil {
            listing = .missing
            return
        }
        await refresh()
    }

    public func appeared() async {
        guard binary != nil else { return }
        await refresh()
    }

    /// `knowledge list`, read again. A store that never captured is no
    /// failure: the CLI refuses to list it by design, so a typo never makes an
    /// empty database, and that refusal reads as no records yet. Any other
    /// failure is said.
    public func refresh() async {
        guard let binary else { return }
        let outcome = await runner.read(binary: binary, ["knowledge", "list"])
        if let document = outcome.document {
            records = KnowledgeRecord.list(from: document)
            listing = .read
        } else if Self.neverCaptured(outcome) {
            records = []
            listing = .read
        } else {
            listing = .failed(outcome.failure ?? "the records could not be read")
        }
        if let selected, !records.contains(where: { $0.id == selected }) { self.selected = nil }
    }

    /// Whether a failed `knowledge list` is the CLI's refusal of a collection
    /// that does not exist yet -- `knowledge.cpp`'s `open_collection`: exit 1,
    /// its last stderr line `apogee knowledge: no knowledge collection named
    /// '<name>' -- capture a record first with 'apogee knowledge capture'` --
    /// matched on those words whole, never on every failure. A fresh install
    /// is that store.
    static func neverCaptured(_ outcome: ReadOutcome) -> Bool {
        guard case .failed(let exit, let tail) = outcome, exit.reason == .exited, exit.status == 1,
            let line = tail.lastLine?.trimmingCharacters(in: .whitespaces)
        else { return false }
        return line.hasPrefix("apogee knowledge: no knowledge collection named '")
            && line.hasSuffix("' -- capture a record first with 'apogee knowledge capture'")
    }

    /// The records the list shows: the chosen branch, or all.
    public var shown: [KnowledgeRecord] {
        guard let branch else { return records }
        return records.filter { $0.status == branch }
    }

    public var selectedRecord: KnowledgeRecord? {
        selected.flatMap { id in records.first { $0.id == id } ?? query?.matches.first { $0.id == id }?.record }
    }

    public func select(_ record: KnowledgeRecord) {
        selected = record.id
    }

    public var emptyText: String? {
        switch listing {
        case .notRead: return nil
        case .missing: return "Apogee isn't installed, so there are no records to read."
        case .failed(let message): return "The records could not be read: \(message)"
        case .read: break
        }
        if records.isEmpty { return "No decision records yet. Capture one from a conversation: the clerk distils its reasoning and its decision." }
        return shown.isEmpty ? "No \(branch ?? "") records." : nil
    }

    // MARK: - Asking

    /// `knowledge query --status <branch> --json -- <question>`, over the
    /// chosen branch or every branch (`--status ""`) -- run as typed, its
    /// stdout the document: the query takes `--json`, never
    /// `--output-format`. The question after `--` (`Argv`, GUI 4g).
    public func search() async {
        let question = question.trimmingCharacters(in: .whitespaces)
        guard let binary, !question.isEmpty else { return }
        let outcome = await runner.command(binary: binary, ["knowledge", "query", "--status", branch ?? "", "--json"] + Argv.text(question))
        if case .done(let stdout) = outcome, let document = ReadDocument(text: stdout), let query = KnowledgeQuery(document: document) {
            self.query = query
            failure = nil
        } else {
            query = nil
            failure = outcome.failure ?? "the question could not be answered"
        }
    }

    public func clearSearch() {
        query = nil
        question = ""
    }

    /// A score as the CLI's retriever scores it.
    public static func score(_ match: KnowledgeQuery.Match) -> String {
        match.score.map { String(format: "%.3f", $0) } ?? "—"
    }

    // MARK: - Changing

    /// `knowledge link <id> <ref>`, read back.
    public func link(_ record: KnowledgeRecord, to ref: String) async {
        let ref = ref.trimmingCharacters(in: .whitespaces)
        guard !ref.isEmpty else { return }
        await change(["knowledge", "link", record.id, ref], doing: "Linking")
    }

    /// `knowledge status <id> <branch>`, read back.
    public func move(_ record: KnowledgeRecord, to status: String) async {
        guard status != record.status else { return }
        await change(["knowledge", "status", record.id, status], doing: "Moving")
    }

    /// `knowledge delete <id>` -- the record and its raw archive -- after the
    /// person said yes here (the CLI asks nothing on a pipe).
    public func confirmDelete() async {
        guard let record = pendingDelete else { return }
        pendingDelete = nil
        await change(["knowledge", "delete", record.id], doing: "Deleting")
    }

    /// `knowledge export --format json|markdown [--anonymize] -o <file>`.
    public func export(to file: URL, markdown: Bool, anonymize: Bool) async {
        var arguments = ["knowledge", "export", "--format", markdown ? "markdown" : "json"]
        if anonymize { arguments.append("--anonymize") }
        arguments += ["-o", file.path]
        await change(arguments, doing: "Exporting")
    }

    public static func exportArguments(file: String, markdown: Bool, anonymize: Bool) -> [String] {
        ["knowledge", "export", "--format", markdown ? "markdown" : "json"] + (anonymize ? ["--anonymize"] : []) + ["-o", file]
    }

    private func change(_ arguments: [String], doing: String) async {
        guard let binary, working == nil else { return }
        working = doing
        let outcome = await runner.command(binary: binary, arguments)
        working = nil
        await refresh()
        if query != nil { await search() }
        if let failure = outcome.failure {
            self.failure = failure
        } else if case .done(let stdout) = outcome {
            failure = nil
            say?(stdout.split(whereSeparator: \.isNewline).last.map(String.init) ?? "Done")
        }
    }

    // MARK: - Capturing and reindexing

    public func newCapture() {
        captureForm = CaptureForm()
        capturing = true
    }

    /// The capture sheet's command, supervised: the clerk's one model call.
    public func capture() {
        guard let arguments = captureForm.arguments else { return }
        capturing = false
        start(arguments)
    }

    /// `knowledge reindex`: the vectors made again after an embedding change.
    public func reindex() {
        start(["knowledge", "reindex"])
    }

    public var isRunning: Bool { job != nil }

    public func stop() async {
        await job?.stop(grace: .seconds(10))
    }

    private func start(_ arguments: [String]) {
        guard let binary, job == nil else { return }
        run = JobProgress()
        let job = starter.startJob(binary: binary, arguments: arguments, streaming: false, folder: nil)
        self.job = job
        Task {
            for await update in job.updates { run?.apply(update) }
            self.job = nil
            let progress = run
            run = nil
            await refresh()
            switch progress?.status {
            case .done: say?(progress?.notices.first ?? "Done")
            case .failed(let message): failure = message
            case .stopped: say?("Stopped")
            default: break
            }
        }
    }

    public func dismissFailure() {
        failure = nil
    }
}
