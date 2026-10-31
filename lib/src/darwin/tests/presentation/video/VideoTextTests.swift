import BusinessJobs
import BusinessMedia
import DataPreferences
import Foundation
import Testing

@testable import PresentationVideo

/// A person's prompt is never an option (GUI 4g): the clip runs with it
/// after `--`, and the stream's flag goes before it.
@Suite("Video text after --", .timeLimit(.minutes(1)))
@MainActor
struct VideoTextTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    @Test("a dash-leading prompt streams as the prompt: --output-format stream-json before --, the prompt after it")
    func dashPrompt() async throws {
        let home = FileManager.default.temporaryDirectory.appendingPathComponent("video-text-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: home.appendingPathComponent("Movies/Apogee"), withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: home) }
        let job = ScriptedJob()
        let jobs = ScriptedJobs([job])
        let model = VideoViewModel(starter: jobs, preferences: Preferences(store: MemoryStore()), home: home)
        model.streams = { true }
        model.connect(binary: Self.binary)
        model.newClip()
        model.form.prompt = "-x a fox at dusk"
        let out = home.appendingPathComponent("Movies/Apogee/x-a-fox-at-dusk.mp4").path
        #expect(model.plannedArguments == ["video", "generate", "-o", out, "--", "-x a fox at dusk"])
        model.generate()
        #expect(jobs.starts.first?.arguments == ["video", "generate", "-o", out, "--output-format", "stream-json", "--", "-x a fox at dusk"])
        job.end()
        #expect(await eventually { !model.isRunning })
    }
}
