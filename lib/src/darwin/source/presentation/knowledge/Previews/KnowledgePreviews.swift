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

    static func records(said: [JobUpdate] = []) -> RecordsViewModel {
        RecordsViewModel(runner: runner, starter: PreviewJobs(said: said))
    }

    static func collections(said: [JobUpdate] = []) -> CollectionsViewModel {
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
