#if DEBUG
import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation

/// The Knowledge place's view-models for its previews, over the recorded
/// documents (ADR 0004): nothing starts the binary.
@MainActor
enum KnowledgePreview {
    static let runner = PreviewRunner(
        reads: [
            (["knowledge", "list"], KnowledgeDocuments.knowledgeList),
            (["embed", "list"], KnowledgeDocuments.collectionsList),
            (["embed", "info", "notes"], KnowledgeDocuments.collectionInfo),
        ],
        commands: [
            (["knowledge", "query"], KnowledgeDocuments.knowledgeQuery),
            (["embed", "query", "notes"], KnowledgeDocuments.collectionQuery),
        ])

    /// A record the branch's binary captured over GUI 4k's sandbox, and its
    /// raw conversation as `knowledge info --raw` printed it -- or, its
    /// archive removed, said unavailable.
    static func rawRunner(unavailable: Bool = false) -> PreviewRunner {
        PreviewRunner(
            reads: [(["knowledge", "list"], KnowledgeDocuments.knowledgeListOne)],
            commands: [
                (["knowledge", "info"], unavailable ? KnowledgeDocuments.knowledgeInfoRawUnavailable : KnowledgeDocuments.knowledgeInfoRaw)
            ])
    }

    /// That record, as `knowledge list` named it.
    static var captured: KnowledgeRecord {
        KnowledgeRecord.list(from: ReadDocument(text: KnowledgeDocuments.knowledgeListOne)!)[0]
    }

    /// The branch binary (schema 2026-10-26, the CLI's M31) over M15's
    /// sandbox: its captured record, and `knowledge info --raw`'s document
    /// carrying the conversation as `raw` -- or, its archive removed, `null`.
    static func rawDocumentRunner(unavailable: Bool = false) -> PreviewRunner {
        PreviewRunner(
            reads: [
                (["knowledge", "list"], KnowledgeDocuments.knowledgeListRaw),
                (["knowledge", "info"], unavailable ? KnowledgeDocuments.knowledgeInfoRawNull : KnowledgeDocuments.knowledgeInfoRawDocument),
            ])
    }

    /// The records on that binary: its vocabulary's date says the raw
    /// conversation is asked as the document.
    static func documentRecords(unavailable: Bool = false) -> RecordsViewModel {
        let records = RecordsViewModel(runner: rawDocumentRunner(unavailable: unavailable), starter: PreviewJobs(said: []))
        records.vocabulary = { "2026-10-26" }
        return records
    }

    /// That record, as its `knowledge list` named it.
    static var capturedM31: KnowledgeRecord {
        KnowledgeRecord.list(from: ReadDocument(text: KnowledgeDocuments.knowledgeListRaw)!)[0]
    }

    /// The records read, `record` open, and its conversation asked for.
    static func openRaw(_ records: RecordsViewModel, _ record: KnowledgeRecord) async {
        await records.connect(binary: previewBinary)
        records.select(record)
        await records.showRaw(record)
    }

    /// The records read, the captured one open, and its conversation asked
    /// for.
    static func openRaw(_ records: RecordsViewModel) async {
        await records.connect(binary: previewBinary)
        records.select(captured)
        await records.showRaw(captured)
    }

    /// The questions the recorded answers were asked, as the view-models'
    /// tests ask them.
    static let recordsQuestion = "why was the cancel button removed"
    static let collectionQuestion = "how does a heat pump work"

    /// The conversation a capture is given, as the tests give it.
    static let conversation = "Ada: drop it? Bob: yes"

    /// An ingest's busy line, as tests/presentation/knowledge holds it.
    static let ingesting: JobUpdate = .busy("[ingest] solar.md (file 2/3)")

    /// The records `knowledge list` names, as rows.
    static var recordRows: [KnowledgeRecord] {
        KnowledgeRecord.list(from: ReadDocument(text: KnowledgeDocuments.knowledgeList)!)
    }

    /// One of them, by id.
    static func record(_ id: String) -> KnowledgeRecord {
        recordRows.first { $0.id == id }!
    }

    /// `embed info notes`, read.
    static var notes: CollectionInfo { CollectionInfo(document: ReadDocument(text: KnowledgeDocuments.collectionInfo)!)! }

    /// A run's progress once it has said `updates`, folded as the
    /// view-models fold a job's.
    static func progress(_ updates: [JobUpdate]) -> JobProgress {
        updates.reduce(into: JobProgress()) { $0.apply($1) }
    }

    /// A fresh install: no record ever captured, so `knowledge list` refuses
    /// the collection in its recorded words, while `embed list` reads empty.
    static let fresh = PreviewRunner(
        reads: [(["embed", "list"], KnowledgeDocuments.collectionsListFresh)],
        refusals: [(["knowledge", "list"], KnowledgeDocuments.knowledgeListFresh)])

    static func records(said: [JobUpdate] = [], runner: PreviewRunner = KnowledgePreview.runner) -> RecordsViewModel {
        RecordsViewModel(runner: runner, starter: PreviewJobs(said: said))
    }

    static func collections(said: [JobUpdate] = [], runner: PreviewRunner = KnowledgePreview.runner) -> CollectionsViewModel {
        CollectionsViewModel(runner: runner, starter: PreviewJobs(said: said))
    }

    /// The collections read, and `notes` -- the recorded one -- chosen.
    static func openNotes(_ collections: CollectionsViewModel) async {
        await collections.connect(binary: previewBinary)
        if let notes = collections.collections.first(where: { $0.name == "notes" }) {
            await collections.select(notes)
        }
    }
}
#endif
