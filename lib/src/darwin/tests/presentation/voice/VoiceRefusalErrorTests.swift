import BusinessJobs
import BusinessMedia
import DataPreferences
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationVoice

/// GUI M13: since the CLI's M29 `voice say` refused for its runtime ends its
/// stream on an `error` event. The consent reads that `error` first, stderr
/// only where none arrived -- over the branch binary's own recording
/// (`voice-refused-runtime-m29`), and 4f's from a binary before it.
@Suite("VoiceRefusalError (M13)", .timeLimit(.minutes(1)))
@MainActor
struct VoiceRefusalErrorTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func refused(_ recording: String) async throws -> (VoiceViewModel, URL) {
        let home = FileManager.default.temporaryDirectory.appendingPathComponent("voice-error-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: home.appendingPathComponent("Music/Apogee"), withIntermediateDirectories: true)
        let runner = ScriptedRunner()
        runner.reading(["models", "list"], recordedFixture("business/reads/Fixtures/compose-voice-list.json"))
        let first = ScriptedJob()
        let model = VoiceViewModel(runner: runner, starter: ScriptedJobs([first]), preferences: Preferences(store: MemoryStore()), home: home)
        model.streams = { true }
        await model.connect(binary: Self.binary)
        model.text = "Hello there. How are you?"
        model.speak()
        first.replay(
            stdout: recordedFixture("business/media/Fixtures/\(recording).jsonl"),
            stderr: recordedFixture("business/media/Fixtures/\(recording).err"), status: 1, streaming: true)
        _ = await eventually { model.consent != nil }
        return (model, home)
    }

    /// The first line of a recorded stream that is an `error` event: its message.
    static func error(in stream: String) -> String? {
        stream.split(separator: "\n").lazy.compactMap { line -> String? in
            if case .error(let message) = JobEvent.decode(line: String(line)) { message } else { nil }
        }.first
    }

    @Test("the stream's error names the install: the consent is the CLI's words there")
    func fromTheStreamsError() async throws {
        let (model, home) = try await refused("voice-refused-runtime-m29")
        defer { try? FileManager.default.removeItem(at: home) }
        let error = try #require(Self.error(in: recordedFixture("business/media/Fixtures/voice-refused-runtime-m29.jsonl")))
        #expect(model.consent?.arguments == ["train", "setup", "--with", "voice"])
        #expect(model.consent?.said == error)
    }

    @Test("a binary before M29 -- its stream silent -- still asks, from stderr's words")
    func olderFromStderr() async throws {
        let (model, home) = try await refused("voice-refused-runtime")
        defer { try? FileManager.default.removeItem(at: home) }
        #expect(model.consent?.arguments == ["train", "setup", "--with", "voice"])
        #expect(model.consent?.said == "apogee voice say: torch and kokoro are not installed in Apogee's Python environment -- apogee train setup --with voice")
    }
}
