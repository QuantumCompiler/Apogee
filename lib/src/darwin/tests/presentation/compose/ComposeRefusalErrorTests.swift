import BusinessJobs
import BusinessMedia
import DataProtocol
import Foundation
import Testing

@testable import PresentationCompose

/// GUI M13: since the CLI's M29 `video compose` refused for its runtime ends
/// its stream on an `error` event, and its two-voice-models refusal names
/// compose's own flag, `--voice-model` -- over the branch binary's own
/// recordings (`compose-refused-runtime-m29`, `compose-refused-two-m29`); 4f's
/// recordings, from a binary before M29, are kept beside them.
@Suite("ComposeRefusalError (M13)", .timeLimit(.minutes(1)))
@MainActor
struct ComposeRefusalErrorTests {
    @Test("the stream's error names the install: the consent is the CLI's words there, never an alert")
    func fromTheStreamsError() async throws {
        let folder = try Recorded.folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let first = ScriptedJob()
        let model = await Recorded.composing(folder, jobs: ScriptedJobs([first]))
        model.compose()
        first.replay(
            stdout: Recorded.media("compose-refused-runtime-m29.jsonl"), stderr: Recorded.media("compose-refused-runtime-m29.err"),
            status: 1, streaming: true)
        #expect(await eventually { model.consent != nil })
        let error = try #require(Recorded.error(in: Recorded.media("compose-refused-runtime-m29.jsonl")))
        #expect(model.consent?.arguments == ["train", "setup", "--with", "video"])
        #expect(model.consent?.said == error)
        #expect(model.failure == nil)
    }

    @Test("two voice models: the CLI's refusal names --voice-model -- the flag the picker sends; the older binary's said --model")
    func twoVoiceModelsNameTheFlag() async throws {
        let refusal = try #require(Recorded.error(in: Recorded.media("compose-refused-two-m29.jsonl")))
        #expect(refusal.hasPrefix("the store holds 2 voice models -- name one with --voice-model:"))
        let older = try #require(Recorded.error(in: Recorded.media("compose-refused-two.jsonl")))
        #expect(older.hasPrefix("the store holds 2 voice models -- name one with --model:"), "a binary before M29's words, kept")
        let folder = try Recorded.folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let model = await Recorded.composing(folder, jobs: ScriptedJobs([]))
        #expect(model.voiceModels.allSatisfy { refusal.contains($0.handle) }, "the picker offers exactly the two it names")
        #expect(model.plannedArguments?.contains("--voice-model") == true)
    }

    @Test("a stream's error naming no install -- the two voice models' -- stays the alert, in the error's words, nothing asked")
    func errorNamingNoInstall() async throws {
        let folder = try Recorded.folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let first = ScriptedJob()
        let model = await Recorded.composing(folder, jobs: ScriptedJobs([first]))
        model.compose()
        first.replay(
            stdout: Recorded.media("compose-refused-two-m29.jsonl"), stderr: Recorded.media("compose-refused-two-m29.err"),
            status: 1, streaming: true)
        #expect(await eventually { model.failure != nil })
        #expect(model.failure == Recorded.error(in: Recorded.media("compose-refused-two-m29.jsonl")))
        #expect(model.consent == nil)
    }
}
