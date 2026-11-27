import Foundation
import Testing

@testable import PresentationSuites

/// The previews' recorded reads answer the lines the view-model builds (GUI
/// M23): a member's `models info` is keyed by a prefix of its line, so a
/// backend that moves after `--` would otherwise leave the preview's memory
/// unknown, and fail nothing.
@Suite("Suites preview reads", .timeLimit(.minutes(1)))
@MainActor
struct PreviewReadsTests {
    @Test("every read the Suites previews ask is answered, and every prefix holding a name after -- is one a line reaches")
    func previewReads() async {
        let log = PreviewReadLog()
        await PreviewReadLog.$current.withValue(log) {
            for suites in [SuitesPreview.model(), SuitesPreview.recordedModel(), SuitesPreview.typedModel(), SuitesPreview.describedModel()] {
                await suites.connect(binary: previewBinary)
            }
        }
        #expect(log.unanswered.isEmpty, "reads no recorded document answers: \(log.unanswered)")
        #expect(log.unreached.isEmpty, "prefixes no line reaches: \(log.unreached)")
    }
}
