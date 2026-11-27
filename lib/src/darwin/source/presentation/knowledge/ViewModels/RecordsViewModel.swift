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
            var options: [Argv.Option] = []
            if let status { options.append(.value("--status", status)) }
            if let discipline { options.append(.value("--discipline", discipline)) }
            let link = link.trimmingCharacters(in: .whitespaces)
            if !link.isEmpty { options.append(.value("--link", link)) }
            return Argv.line(["knowledge", "capture"], options: options, names: [text])
        }
    }

    /// Where a record's raw conversation stands, as `knowledge info --raw`
    /// gave it (GUI 4k, M15).
    public enum Raw: Equatable, Sendable {
        case reading
        case shown(RawConversation)
        /// The CLI's words, when it could not be read.
        case failed(String)
    }

    /// A record's raw conversation, and whose.
    public struct RawView: Equatable, Sendable {
        public let record: String
        public let state: Raw
    }

    /// What the CLI said of a finished capture or reindex on stderr, in its
    /// house voice, word for word -- a verifier's verdict on the record, a
    /// collection it could not register -- shown on Records until Dismiss or
    /// the next run (GUI M22). The toast stays the outcome line.
    public struct DoneNotes: Equatable, Sendable {
        /// The card's title: which run said them.
        public let title: String
        /// What the run said, as `JobNotes` kept it.
        public let notes: JobNotes

        /// The notes `notes` of a run, under `title`.
        public init(title: String, notes: JobNotes) {
            self.title = title
            self.notes = notes
        }
    }

    /// A question as it was asked: its text and the branch it was asked
    /// under (nil for every branch) -- what the matches shown answer, and
    /// what a Branch change asks again (GUI 6h).
    public struct Asked: Equatable, Sendable {
        public let question: String
        public let branch: String?
    }

    public private(set) var records: [KnowledgeRecord] = []
    public private(set) var listing: Listing = .notRead
    /// The branch the list shows; nil for every branch.
    public var branch: String?
    public var question = ""
    public private(set) var query: KnowledgeQuery?
    /// The question standing -- answered, or being asked -- and its branch;
    /// nil with no question asked, or once cleared.
    public private(set) var asked: Asked?
    public private(set) var selected: String?
    public var pendingDelete: KnowledgeRecord?
    public var captureForm = CaptureForm()
    public var capturing = false
    /// A capture or a reindex running now.
    public private(set) var run: JobProgress?
    /// The last capture's or reindex's notes (GUI M22).
    public private(set) var doneNotes: DoneNotes?
    /// A command running now, by what it is doing.
    public private(set) var working: String?
    public private(set) var failure: String?
    /// The open record's raw conversation, once asked for.
    public private(set) var raw: RawView?
    public var say: (@MainActor (String) -> Void)?
    /// The binary's vocabulary date, as the app read it at launch
    /// (`__machine-schema`, the session library's): from the CLI's M31 a
    /// record's raw conversation is asked as its document first (GUI M15).
    public var vocabulary: @MainActor () -> String? = { nil }

    private let runner: any CommandRunning
    private let starter: any JobStarting
    private var binary: URL?
    private var job: (any JobRunning)?
    /// Counts the questions asked: a reply lands only if no question was
    /// asked, and no search cleared, since its own.
    private var asks = 0

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
        if selected != record.id { raw = nil }
        selected = record.id
    }

    /// A record's raw conversation -- `knowledge info <id> --raw` -- never
    /// read from the archive's path (GUI 4k; ADR 0002): its document's `raw`
    /// on a binary that carries it (the CLI's M31; GUI M15), `null` an
    /// archive it can't read; else, or where the document has no `raw`, the
    /// text it prints, run as a command and shown after the CLI's marker.
    public func showRaw(_ record: KnowledgeRecord) async {
        guard let binary else { return }
        raw = RawView(record: record.id, state: .reading)
        let asks = ProseReads.asksDocuments(vocabulary: vocabulary())
        let answer = await runner.documentOrText(
            binary: binary, Argv.line(["knowledge", "info"], options: [.flag("--raw")], names: [record.id]), asksDocument: asks, RawConversation.init(document:))
        guard raw?.record == record.id else { return }
        switch answer {
        case .document(let conversation):
            raw = RawView(record: record.id, state: .shown(conversation))
        case .text(let text):
            raw = RawView(record: record.id, state: RawConversation(printed: text).map(Raw.shown) ?? .failed("apogee printed no raw conversation"))
        case .failed(let why):
            raw = RawView(record: record.id, state: .failed(why))
        }
    }

    public func hideRaw() {
        raw = nil
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
        guard binary != nil, !question.isEmpty else { return }
        await ask(Asked(question: question, branch: branch))
    }

    /// The Branch picker moved (GUI 6h): a question standing is asked again
    /// under the new branch -- its old matches cleared first, so they never
    /// stand under the new label -- and with none, the list just filters.
    public func branchChanged() async {
        guard let asked, asked.branch != branch else { return }
        query = nil
        await ask(Asked(question: asked.question, branch: branch))
    }

    /// Asks `asked` and lands its reply -- or its failure -- only while it
    /// is still the question standing and its branch the one picked: a reply
    /// held past a later question, a Branch change or a Clear is dropped, as
    /// the collections' search drops one asked of another collection (4h).
    private func ask(_ asked: Asked) async {
        guard let binary else { return }
        asks += 1
        let mine = asks
        self.asked = asked
        let outcome = await runner.command(
            binary: binary, Argv.line(["knowledge", "query"], options: [.value("--status", asked.branch ?? ""), .flag("--json")], names: [asked.question]))
        guard asks == mine, self.asked == asked, branch == asked.branch else { return }
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
        asked = nil
        asks += 1
    }

    /// A score as the CLI's retriever scores it.
    public static func score(_ match: KnowledgeQuery.Match) -> String {
        match.score.map { String(format: "%.3f", $0) } ?? "—"
    }

    // MARK: - Changing

    /// `knowledge link -- <id> <ref>`, the id and the typed ref after one
    /// `--` (`Argv`, GUI M23), read back.
    public func link(_ record: KnowledgeRecord, to ref: String) async {
        let ref = ref.trimmingCharacters(in: .whitespaces)
        guard !ref.isEmpty else { return }
        await change(Argv.line(["knowledge", "link"], names: [record.id, ref]), doing: "Linking")
    }

    /// `knowledge status -- <id> <branch>`, read back.
    public func move(_ record: KnowledgeRecord, to status: String) async {
        guard status != record.status else { return }
        await change(Argv.line(["knowledge", "status"], names: [record.id, status]), doing: "Moving")
    }

    /// `knowledge delete <id>` -- the record and its raw archive -- after the
    /// person said yes here (the CLI asks nothing on a pipe) to `record`, the
    /// one the dialog showed. Its closing clears `pendingDelete` before its
    /// button's action runs, so the record is handed in (GUI M20);
    /// `pendingDelete` is cleared only while it is still `record`.
    public func confirmDelete(_ record: KnowledgeRecord) async {
        if pendingDelete == record { pendingDelete = nil }
        await change(Self.deleteArguments(record), doing: "Deleting")
    }

    /// `knowledge delete -- <id>`: the line `confirmDelete` runs, which its
    /// dialog shows as typed (GUI M21), the id after `--` (GUI M23).
    public static func deleteArguments(_ record: KnowledgeRecord) -> [String] {
        Argv.line(["knowledge", "delete"], names: [record.id])
    }

    /// `knowledge export --format json|markdown [--anonymize] -o <file>`.
    public func export(to file: URL, markdown: Bool, anonymize: Bool) async {
        await change(Self.exportArguments(file: file.path, markdown: markdown, anonymize: anonymize), doing: "Exporting")
    }

    /// The export's line: its options alone, the file `-o`'s value.
    public static func exportArguments(file: String, markdown: Bool, anonymize: Bool) -> [String] {
        Argv.line(["knowledge", "export"], options: [.value("--format", markdown ? "markdown" : "json")] + (anonymize ? [.flag("--anonymize")] : []) + [.value("-o", file)])
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
        start(arguments, saying: "What the capture said")
    }

    /// `knowledge reindex`: the vectors made again after an embedding change.
    public func reindex() {
        start(["knowledge", "reindex"], saying: "What the reindex said")
    }

    public var isRunning: Bool { job != nil }

    public func stop() async {
        await job?.stop(grace: .seconds(10))
    }

    /// Runs `arguments` supervised; what a run that succeeded said on stderr
    /// is kept under `title`, and starting drops the last run's (GUI M22).
    private func start(_ arguments: [String], saying title: String) {
        guard let binary, job == nil else { return }
        doneNotes = nil
        run = JobProgress()
        let job = starter.startJob(binary: binary, arguments: arguments, streaming: false, folder: nil)
        self.job = job
        Task {
            for await update in job.updates { run?.apply(update) }
            self.job = nil
            let progress = run
            run = nil
            if let progress, progress.status == .done, !progress.notes.isEmpty {
                doneNotes = DoneNotes(title: title, notes: progress.notes)
            }
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

    /// The notes' card dismissed.
    public func dismissNotes() {
        doneNotes = nil
    }
}
