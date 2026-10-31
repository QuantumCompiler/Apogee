import BusinessJobs
import BusinessMedia
import DataPreferences
import Foundation
import Testing

@testable import PresentationVoice

/// A person's text is never an option (GUI 4g): on the branch binary `voice
/// say "--draft"` answered "text is required"; the text goes after `--`, and
/// the stream's flag before it.
@Suite("Voice text after --", .timeLimit(.minutes(1)))
@MainActor
struct VoiceTextTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    @Test("dash-leading text is spoken as the text: every flag before --, the text after it")
    func dashText() async throws {
        let home = FileManager.default.temporaryDirectory.appendingPathComponent("voice-text-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: home.appendingPathComponent("Music/Apogee"), withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: home) }
        let runner = ScriptedRunner()
        runner.reading(["models", "list"], VoiceViewModelTests.recorded("list"))
        runner.reading(["models", "info", "Kokoro-82M/voice/5f3a2c91d0e4"], VoiceViewModelTests.recorded("info-voice"))
        let job = ScriptedJob()
        let jobs = ScriptedJobs([job])
        let model = VoiceViewModel(runner: runner, starter: jobs, preferences: Preferences(store: MemoryStore()), home: home)
        model.streams = { true }
        await model.connect(binary: Self.binary)
        model.text = "--draft"
        model.voice = "bf_emma"
        let out = home.appendingPathComponent("Music/Apogee/draft.wav").path
        #expect(model.plannedArguments == ["voice", "say", "-o", out, "--voice", "bf_emma", "--", "--draft"])
        model.speak()
        #expect(jobs.starts.first?.arguments == ["voice", "say", "-o", out, "--voice", "bf_emma", "--output-format", "stream-json", "--", "--draft"])
        job.end()
        #expect(await eventually { !model.isRunning })
    }
}
