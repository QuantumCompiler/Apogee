import BusinessJobs
import BusinessReads
import Foundation
import InfrastructureProcess
import Observation

/// The Knowledge place's collections: the embed store as `embed list` and
/// `embed info` read it, a search answered as `embed query` prints it, ingest
/// supervised, deletes confirmed.
///
/// SwiftUI-free (ADR 0002). A search's results are the CLI's own lines --
/// the retriever that ran and each score on its scale, never recomputed --
/// since `embed query` has no document form.
@MainActor
@Observable
public final class CollectionsViewModel {
    /// What `embed ingest` is asked, as a person types it.
    public struct IngestForm: Equatable, Sendable {
        public var collection = ""
        public var path = ""
        /// `lexical`, `vector` or `auto`; nil leaves the collection's own.
        public var retriever: String?
        /// Build the collection's graph after (`--graph`).
        public var graph = false

        public init() {}

        /// `embed ingest <collection> <path> [--retriever r] [--graph]`.
        public var arguments: [String]? {
            let collection = collection.trimmingCharacters(in: .whitespaces)
            guard !collection.isEmpty, !path.isEmpty else { return nil }
            var arguments = ["embed", "ingest", collection, path]
            if let retriever { arguments += ["--retriever", retriever] }
            if graph { arguments.append("--graph") }
            return arguments
        }
    }

    /// A delete waiting on the person's word: a whole collection, or one
    /// source in it.
    public struct PendingDelete: Equatable, Sendable {
        public let collection: String
        public let source: String?

        /// `embed delete <collection> --yes` -- the CLI only describes a whole
        /// collection's delete without it -- or `--source <s>`, which it
        /// deletes as asked.
        public var arguments: [String] {
            if let source { ["embed", "delete", collection, "--source", source] } else { ["embed", "delete", collection, "--yes"] }
        }
    }

    public private(set) var collections: [CollectionRow] = []
    public private(set) var listed = false
    public private(set) var selected: String?
    public private(set) var info: CollectionInfo?
    public var question = ""
    /// A search's results, as `embed query` printed them.
    public private(set) var results: String?
    public var pendingDelete: PendingDelete?
    public var ingestForm = IngestForm()
    public var ingesting = false
    public private(set) var run: JobProgress?
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
        await refresh()
    }

    public func appeared() async {
        await refresh()
    }

    /// `embed list`, and the chosen collection's `embed info`, read again.
    public func refresh() async {
        guard let binary else { return }
        let outcome = await runner.read(binary: binary, ["embed", "list"])
        if let document = outcome.document {
            collections = CollectionRow.list(from: document)
            listed = true
        } else {
            failure = outcome.failure
        }
        if let selected, !collections.contains(where: { $0.name == selected }) {
            self.selected = nil
            info = nil
            results = nil
        }
        if let selected { await readInfo(selected) }
    }

    public func select(_ collection: CollectionRow) async {
        guard collection.name != selected else { return }
        selected = collection.name
        info = nil
        results = nil
        await readInfo(collection.name)
    }

    private func readInfo(_ name: String) async {
        guard let binary else { return }
        let outcome = await runner.read(binary: binary, ["embed", "info", name])
        guard selected == name else { return }
        info = outcome.document.flatMap(CollectionInfo.init)
        if info == nil { failure = outcome.failure }
    }

    public var emptyText: String? {
        guard binary != nil else { return "Apogee isn't installed, so there are no collections to read." }
        guard listed else { return nil }
        return collections.isEmpty ? "No collections yet. Ingest a folder of documents: it is read, chunked and indexed for retrieval." : nil
    }

    /// The vectors line, in the CLI's terms: how many chunks carry one, or
    /// that the collection is text only.
    public static func vectors(_ info: CollectionInfo) -> String {
        info.vectors.map { "\($0) of \(info.chunks) chunks" } ?? "none -- a text index only"
    }

    // MARK: - Searching

    /// `embed query <collection> <question>`: its lines, as printed.
    public func search() async {
        let question = question.trimmingCharacters(in: .whitespaces)
        guard let binary, let selected, !question.isEmpty else { return }
        switch await runner.command(binary: binary, ["embed", "query", selected, question]) {
        case .done(let stdout):
            results = stdout.trimmingCharacters(in: .newlines)
            failure = nil
        case let other:
            results = nil
            failure = other.failure
        }
    }

    // MARK: - Changing

    public func confirmDelete() async {
        guard let binary, let pending = pendingDelete, working == nil else { return }
        pendingDelete = nil
        working = "Deleting"
        let outcome = await runner.command(binary: binary, pending.arguments)
        working = nil
        await refresh()
        if let failure = outcome.failure {
            self.failure = failure
        } else if case .done(let stdout) = outcome {
            say?(stdout.split(whereSeparator: \.isNewline).last.map(String.init) ?? "Deleted")
        }
    }

    public func newIngest(into collection: String? = nil) {
        ingestForm = IngestForm()
        ingestForm.collection = collection ?? ""
        ingesting = true
    }

    /// The ingest sheet's command, supervised.
    public func ingest() {
        guard let binary, let arguments = ingestForm.arguments, job == nil else { return }
        ingesting = false
        let name = ingestForm.collection.trimmingCharacters(in: .whitespaces)
        run = JobProgress()
        let job = starter.startJob(binary: binary, arguments: arguments, streaming: false, folder: nil)
        self.job = job
        Task {
            for await update in job.updates { run?.apply(update) }
            self.job = nil
            let progress = run
            run = nil
            selected = name
            await refresh()
            switch progress?.status {
            case .done: say?(progress?.notices.first ?? "Ingested")
            case .failed(let message): failure = message
            case .stopped: say?("Stopped: what was read stays")
            default: break
            }
        }
    }

    public var isRunning: Bool { job != nil }

    public func stop() async {
        await job?.stop(grace: .seconds(10))
    }

    public func dismissFailure() {
        failure = nil
    }
}
