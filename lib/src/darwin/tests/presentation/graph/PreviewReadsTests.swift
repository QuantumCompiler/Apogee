import Foundation
import Testing

@testable import PresentationGraph

/// The previews' recorded reads answer the lines the view-model builds (GUI
/// M23, after M21): an entity's `graph explain` is keyed by a prefix of its
/// line, so a word that moves would otherwise leave the preview's card
/// empty, and fail nothing.
@Suite("Graph preview reads", .timeLimit(.minutes(1)))
@MainActor
struct PreviewReadsTests {
    @Test("every read the Graph previews ask is answered, and every prefix holding a name after -- is one a line reaches")
    func previewReads() async {
        let log = PreviewReadLog()
        await PreviewReadLog.$current.withValue(log) {
            for (runner, node) in [(GraphPreview.code, "ingest.runner.Runner"), (GraphPreview.prose, "Heat pump")] {
                let graph = GraphPreview.graph(runner)
                await graph.connect(binary: previewBinary)
                await graph.select(node)
            }
        }
        #expect(log.unanswered.isEmpty, "reads no recorded document answers: \(log.unanswered)")
        #expect(log.unreached.isEmpty, "prefixes no line reaches: \(log.unreached)")
    }
}
