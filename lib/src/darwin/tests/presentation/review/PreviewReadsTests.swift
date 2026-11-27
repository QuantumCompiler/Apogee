import Foundation
import Testing

@testable import PresentationReview

/// The previews' recorded reads answer the lines the view-model builds (GUI
/// M23): a skill's `review skills show` is keyed by a prefix of its line, so
/// a name that moves after `--` would otherwise leave the preview's detail
/// empty, and fail nothing.
@Suite("Review preview reads", .timeLimit(.minutes(1)))
@MainActor
struct PreviewReadsTests {
    @Test("every read the Review previews ask is answered, and every prefix holding a name after -- is one a line reaches")
    func previewReads() async {
        let log = PreviewReadLog()
        await PreviewReadLog.$current.withValue(log) {
            let review = ReviewPreview.review()
            await review.connect(binary: previewBinary)
            await review.show(skill: "security")
        }
        #expect(log.unanswered.isEmpty, "reads no recorded document answers: \(log.unanswered)")
        #expect(log.unreached.isEmpty, "prefixes no line reaches: \(log.unreached)")
    }
}
