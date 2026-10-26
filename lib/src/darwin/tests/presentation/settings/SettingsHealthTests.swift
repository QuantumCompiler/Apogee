import DataPreferences
import Foundation
import Testing

@testable import PresentationSettings

/// A recorded fixture, by its path under tests/.
private func recorded(_ path: String) -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("../../\(path)")
    return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
}

/// Settings' Health over `check`'s own document (GUI 4f), recorded from the
/// branch's binary over a sandbox whose ffmpeg lacks libass
/// (tests/business/reads/Fixtures/check-captions-skipped.json): a skipped row
/// that carries a remedy is drawn with it -- the captions' `ffmpeg-full`
/// among them -- after what fails and warns.
@Suite("SettingsHealth", .timeLimit(.minutes(1)))
@MainActor
struct SettingsHealthTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func read(_ check: String) async -> SettingsViewModel {
        let runner = RecordingRunner()
        runner.reading(["check"], check)
        let model = SettingsViewModel(runner: runner, starter: NoJobs(), preferences: .init(store: MemoryStore()))
        await model.connect(binary: Self.binary, version: "0.2.0")
        return model
    }

    @Test("skipped rows with a remedy are drawn, with it, after the failing and warning ones; nothing that passes is")
    func skippedWithTheirRemedy() async throws {
        let model = await read(recorded("business/reads/Fixtures/check-captions-skipped.json"))
        let captions = try #require(model.healthRows.first { $0.name == "ffmpeg captions" })
        #expect(captions.status == "skipped")
        #expect(captions.remedy == "brew install ffmpeg-full && brew unlink ffmpeg; brew link --force ffmpeg-full")
        let skipped = model.checks.filter { $0.status == "skipped" && $0.remedy != nil }
        #expect(skipped.count == 9)
        #expect(model.healthRows.map(\.status) == ["warn", "warn"] + Array(repeating: "skipped", count: 9))
        #expect(!model.healthRows.contains { $0.status == "ok" })
    }

    @Test("a skipped row with no remedy says nothing to do, so it is not drawn")
    func skippedWithoutARemedy() async throws {
        let check = #"{"rows":[{"section":"Video","name":"ffmpeg captions","status":"skipped","detail":"could not ask ffmpeg for its filters -- it did not answer"},{"section":"Runtimes","name":"llama-server","status":"fail","detail":"not installed","remedy":"apogee check --fix"}],"ok":false,"failures":1,"warnings":0}"#
        let model = await read(check)
        #expect(model.healthRows.map(\.name) == ["llama-server"])
    }
}
