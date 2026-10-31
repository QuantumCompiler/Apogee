import BusinessReads
import DataPreferences
import Foundation
import Testing

@testable import PresentationSettings

/// GUI M4: Settings › Backends & keys shows backends and offers none to chat
/// with, so it keeps every entry whatever its MLX kind -- over 3a's
/// registered kinds and entries written into the config by hand over every
/// kind that answers no chat, recorded from the binary.
@Suite("SettingsBackendsKeepEveryEntry", .timeLimit(.minutes(1)))
@MainActor
struct SettingsBackendsKeepEveryEntryTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    static func recorded(_ name: String) -> String {
        let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            .appendingPathComponent("../../business/reads/Fixtures/\(name).json")
        return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
    }

    func settings(models: String) async -> SettingsViewModel {
        let runner = RecordingRunner()
        runner.reading(["models", "list"], models)
        let model = SettingsViewModel(runner: runner, starter: NoJobs(), preferences: Preferences(store: MemoryStore()))
        await model.connect(binary: Self.binary, version: "0.2.0")
        return model
    }

    @Test("every configured entry is listed, the embedding entry and those over a drafter, a video, a voice model or an unknown kind among them")
    func everyEntryListed() async {
        let registered = await settings(models: Self.recorded("store-list-kinds-registered"))
        #expect(registered.backends.map(\.backend) == ["EmbeddingGemma-300M-bf16", "Gemma4-E4B-4bit", "Qwen3-8B-4bit"])
        let byHand = await settings(models: Self.recorded("store-list-kinds-by-hand"))
        #expect(byHand.backends.map(\.backend) == ["Gemma4-E4B-Assistant", "Kokoro-82M", "MusicGen-Small", "Qwen3-8B-4bit", "Wan2.2-TI2V-5B"])
    }
}
