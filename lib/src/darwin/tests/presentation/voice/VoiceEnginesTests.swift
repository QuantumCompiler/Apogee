import BusinessJobs
import BusinessMedia
import DataPreferences
import DataProtocol
import Foundation
import Testing

@testable import PresentationVoice

/// A recorded fixture, by its path under tests/.
private func fixture(_ path: String) -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("../../\(path)")
    return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
}

/// The binary's verdict on each command the sheets build with an engine
/// (`tests/business/media/Fixtures/engine-command-verdicts.txt`): `taken`
/// where every rule of the command passed -- the run stopped only at the
/// runtime ladder, no Python environment there -- else the binary's first line.
private func verdicts() -> [String: String] {
    var out: [String: String] = [:]
    for line in fixture("business/media/Fixtures/engine-command-verdicts.txt").split(separator: "\n") {
        let parts = line.split(separator: "\t", maxSplits: 1).map(String.init)
        if parts.count == 2 { out[parts[1]] = parts[0] }
    }
    return out
}

/// The say sheet over the store recorded from the binary built with the
/// CLI's 53m (`tests/business/reads/Fixtures/media-*`): Kokoro as hexgrad
/// publishes it beside its MLX build, in one model folder -- the user's store
/// once their Kokoro is converted, where a bare `voice say` is refused.
@Suite("VoiceEngines", .timeLimit(.minutes(1)))
@MainActor
struct VoiceEnginesTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let torch = "Kokoro-82M/voice/5f3a2c91d0e4"
    static let mlx = "Kokoro-82M/mlx/6f708192a3b4"

    func home() throws -> URL {
        let home = FileManager.default.temporaryDirectory.appendingPathComponent("voice-engines-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: home.appendingPathComponent("Music/Apogee"), withIntermediateDirectories: true)
        return home
    }

    func store() -> ScriptedRunner {
        let runner = ScriptedRunner()
        runner.reading(["models", "list"], fixture("business/reads/Fixtures/media-list.json"))
        runner.reading(["models", "info", Self.torch], fixture("business/reads/Fixtures/media-info-voice-torch.json"))
        runner.reading(["models", "info", Self.mlx], fixture("business/reads/Fixtures/media-info-voice-mlx.json"))
        return runner
    }

    /// The sheet's command as the verdicts record it: its output `OUT.wav`.
    func recorded(_ arguments: [String]?) -> String {
        var words = arguments ?? []
        if let o = words.firstIndex(of: "-o"), o + 1 < words.count { words[o + 1] = "OUT.wav" }
        return words.joined(separator: " ")
    }

    @Test("Kokoro beside its MLX build: both offered, PyTorch first and picked, its engine passed with its handle -- never the bare command the CLI refuses")
    func twoModels() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let model = VoiceViewModel(runner: store(), starter: ScriptedJobs([]), preferences: Preferences(store: MemoryStore()), home: home)
        await model.connect(binary: Self.binary)
        #expect(model.models.map(\.handle) == [Self.torch, Self.mlx])
        #expect(model.models.map { model.label($0) } == ["Kokoro-82M · PyTorch", "Kokoro-82M · MLX"])
        #expect(model.voiceModel == Self.torch, "PyTorch first, as the CLI's auto")
        #expect(model.voices.flatMap(\.voices) == ["af_heart", "am_adam", "bf_emma"])
        model.text = "Hello there. How are you?"
        #expect(Array((model.plannedArguments ?? []).suffix(6)) == ["-m", Self.torch, "--engine", "torch", "--", "Hello there. How are you?"])
        #expect(model.engineLine == "Speaks on PyTorch, through Kokoro: the voice model as Kokoro publishes it. Kokoro-82M holds it and its MLX build, so the engine is yours: --engine torch.")
        model.voice = "bf_emma"
        await model.choose(model: Self.mlx)
        #expect(model.voiceModel == Self.mlx && model.chosenEngine == .mlx)
        #expect(model.voice == "bf_emma", "the MLX build carries it too")
        #expect(model.engineLine?.hasPrefix("Speaks on MLX, through mlx-audio: an MLX voice model.") == true)
        model.speed = 1.2
        let out = home.appendingPathComponent("Music/Apogee/hello-there-how-are.wav").path
        #expect(model.plannedArguments == [
            "voice", "say", "-o", out, "-m", Self.mlx, "--engine", "mlx", "--voice", "bf_emma", "--speed", "1.2", "--", "Hello there. How are you?",
        ])
    }

    @Test("every command the say sheet builds over the recorded store is one the binary takes; the bare one is refused, naming both")
    func commandsTaken() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let verdicts = verdicts()
        #expect(!verdicts.isEmpty, "no recorded verdicts")
        let model = VoiceViewModel(runner: store(), starter: ScriptedJobs([]), preferences: Preferences(store: MemoryStore()), home: home)
        await model.connect(binary: Self.binary)
        model.text = "Hello there. How are you?"
        var built: Set<String> = []
        for handle in [Self.torch, Self.mlx] {
            await model.choose(model: handle)
            for (voice, speed) in [("", 1.0), ("bf_emma", 1.2)] {
                model.voice = voice
                model.speed = speed
                let command = recorded(model.plannedArguments)
                built.insert(command)
                #expect(verdicts[command] == "taken", "\(command): \(verdicts[command] ?? "never recorded")")
            }
        }
        #expect(built.count == 4)
        let bare = "voice say -o OUT.wav -- Hello there. How are you?"
        #expect(verdicts[bare] == "refused: apogee voice: the store holds 2 voice models -- name one with --model:", "the user's case, recorded")
        #expect(!built.contains(bare))
        let folder = "voice say -o OUT.wav -m Kokoro-82M --engine mlx -- Hello there. How are you?"
        #expect(verdicts[folder]?.hasPrefix("refused: apogee voice: 'Kokoro-82M' holds 2 voice models") == true, "why the sheet names the handle")
        let other = "voice say -o OUT.wav -m \(Self.mlx) --engine torch -- Hello there. How are you?"
        #expect(verdicts[other]?.hasPrefix("refused: apogee voice: --engine torch: '\(Self.mlx)' is an MLX voice model") == true)
    }

    @Test("one voice model in the store: nothing named, as before -- the bare command the binary takes -- its engine said")
    func oneModel() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let runner = ScriptedRunner()
        runner.reading(["models", "list"], fixture("business/reads/Fixtures/models-stored-list.json"))
        runner.reading(["models", "info", Self.torch], fixture("business/reads/Fixtures/models-stored-info-voice.json"))
        let model = VoiceViewModel(runner: runner, starter: ScriptedJobs([]), preferences: Preferences(store: MemoryStore()), home: home)
        await model.connect(binary: Self.binary)
        #expect(model.models == [MediaModel(handle: Self.torch, engine: .torch)] && model.chosenEngine == nil)
        model.text = "Hello there. How are you?"
        let out = home.appendingPathComponent("Music/Apogee/hello-there-how-are.wav").path
        #expect(model.plannedArguments == ["voice", "say", "-o", out, "--", "Hello there. How are you?"], "an older binary's command, unchanged but for the text after --")
        #expect(verdicts()["voice say -o OUT.wav -- Hello there. How are you? (one voice model in the store)"] == "taken")
        #expect(model.engineLine == "Speaks on PyTorch, through Kokoro: the voice model as Kokoro publishes it.")
    }

    @Test("a speech's page says the engine its record spoke on and what ran; a record from before 53m is drawn as before")
    func pageSaysEngine() throws {
        func speech(_ text: String) -> SpeechRecord? { JSONValue.parse(text).flatMap(SpeechRecord.init) }
        let mlx = try #require(speech(fixture("business/media/Fixtures/speech-mlx.json")))
        #expect(SpeechPage.engine(mlx) == "Spoken on MLX · mlx 0.31.2, mlx-audio 0.0-audio-stub")
        let torch = try #require(speech(fixture("business/media/Fixtures/speech-torch.json")))
        #expect(SpeechPage.engine(torch) == "Spoken on PyTorch · kokoro 0.0-stub, torch 0.0-stub")
        let older = try #require(speech(VoiceViewModelTests.speech.replacingOccurrences(of: "OUT", with: "/o.wav")))
        #expect(older.recordedEngine == nil && SpeechPage.engine(older) == nil)
    }
}
