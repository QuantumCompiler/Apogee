import DataPreferences
import Foundation
import Testing

@testable import PresentationVoice

/// The previews' recorded reads answer the lines the view-model builds (GUI
/// M23): a voice model's `models info` is keyed by a prefix of its line, so a
/// handle that moves after `--` would otherwise leave the preview with no
/// voices, and fail nothing.
@Suite("Voice preview reads", .timeLimit(.minutes(1)))
@MainActor
struct PreviewReadsTests {
    @Test("every read the Voice previews ask is answered, and every prefix holding a name after -- is one a line reaches")
    func previewReads() async {
        let log = PreviewReadLog()
        await PreviewReadLog.$current.withValue(log) {
            for runner in [VoicePreview.runner, VoicePreview.engines] {
                let voice = VoiceViewModel(runner: runner, starter: PreviewJobs(), preferences: Preferences(store: MemoryStore()), home: VoicePreview.home("preview-reads", spoken: false))
                await voice.connect(binary: previewBinary)
                for model in voice.models { await voice.choose(model: model.handle) }
            }
        }
        #expect(log.unanswered.isEmpty, "reads no recorded document answers: \(log.unanswered)")
        #expect(log.unreached.isEmpty, "prefixes no line reaches: \(log.unreached)")
    }
}
