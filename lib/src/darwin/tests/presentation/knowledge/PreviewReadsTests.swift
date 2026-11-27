import Foundation
import Testing

@testable import PresentationKnowledge

/// The previews' recorded reads answer the lines the view-models build (GUI
/// M23, after M21): a collection's `embed info` and `embed query` are keyed
/// by prefixes of their lines, so a name that moves would otherwise leave
/// the preview's page empty, and fail nothing.
@Suite("Knowledge preview reads", .timeLimit(.minutes(1)))
@MainActor
struct PreviewReadsTests {
    @Test("every read the Knowledge previews ask is answered, and every prefix holding a name after -- is one a line reaches")
    func previewReads() async {
        let log = PreviewReadLog()
        await PreviewReadLog.$current.withValue(log) {
            for (collections, name) in [(KnowledgePreview.collections(), "notes"), (KnowledgePreview.ingestedWithANote(), "my notes")] {
                await collections.connect(binary: previewBinary)
                for row in collections.collections where row.name == name { await collections.select(row) }
                collections.question = "heat pump"
                await collections.search()
            }
        }
        #expect(log.unanswered.isEmpty, "reads no recorded document answers: \(log.unanswered)")
        #expect(log.unreached.isEmpty, "prefixes no line reaches: \(log.unreached)")
    }
}
