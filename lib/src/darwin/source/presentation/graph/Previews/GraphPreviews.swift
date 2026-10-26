#if DEBUG
import BusinessJobs
import BusinessReads
import DataPreferences
import DataProtocol
import Foundation
import PresentationShell

/// The Code graph place's view-model for its previews, over the recorded
/// documents (ADR 0004): nothing starts the binary.
@MainActor
enum GraphPreview {
    /// A four-file Python tree's graph (`graph build demo --source src`):
    /// the binary's own name for it, then its `graph stats | query | explain`.
    static let code = PreviewRunner(
        reads: [
            (["graph", "stats"], GraphDocuments.stats),
            (["graph", "query"], GraphDocuments.query),
            (["graph", "explain", "--graph", "demo", "--", "ingest.runner.Runner"], GraphDocuments.explain),
        ],
        commands: [(["__complete", "graph", "stats"], "demo\n")])

    /// A collection's prose graph, built: its stats, its report, an entity
    /// explained. Its name as GraphViewModelTests completes it.
    static let prose = PreviewRunner(
        reads: [
            (["graph", "stats"], GraphDocuments.proseStats),
            (["graph", "report"], GraphDocuments.proseReport),
            (["graph", "explain", "--graph", "notes", "--", "Heat pump"], GraphDocuments.proseExplain),
        ],
        commands: [(["__complete", "graph", "stats"], "notes\n")])

    /// The same collection's graph before it is built, as GraphViewModelTests
    /// holds its stats.
    static let unbuilt = PreviewRunner(
        reads: [(["graph", "stats"], #"{"built":false,"graph":"notes","kind":"collection","object":"graph.stats"}"#)],
        commands: [(["__complete", "graph", "stats"], "notes\n")])

    /// No graph built: the completion names none.
    static let none = PreviewRunner(reads: [], commands: [(["__complete", "graph", "stats"], "")])

    /// The question the recorded query answered.
    static let question = "runner fetch"

    /// `graph query --graph demo -- runner fetch`, read.
    static var query: GraphQuery { GraphQuery(document: ReadDocument(text: GraphDocuments.query)!) }

    /// The collection graph's `graph report`, read.
    static var report: GraphReport { GraphReport(document: ReadDocument(text: GraphDocuments.proseReport)!)! }

    static func graph(_ runner: PreviewRunner = code) -> GraphViewModel {
        GraphViewModel(runner: runner, starter: PreviewJobs())
    }

    /// The window around the screen, its preferences held in memory -- never
    /// the person's own defaults.
    static func shell() -> ShellViewModel {
        ShellViewModel(preferences: Preferences(store: MemoryStore()))
    }

    /// The code graph read, asked the recorded question, and `Runner` explained.
    static func searched(_ graph: GraphViewModel) async {
        await graph.connect(binary: previewBinary)
        graph.question = question
        await graph.search()
        await graph.select("ingest.runner.Runner")
    }
}
#endif
