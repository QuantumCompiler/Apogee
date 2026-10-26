import BusinessJobs
import BusinessMedia
import DataPreferences
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationVoice

@Suite("VoiceViewModel", .timeLimit(.minutes(1)))
@MainActor
struct VoiceViewModelTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let speech = #"{"object":"voice.speech","status":"done","output":"OUT","sidecar":"OUT.json","model":{"name":"kokoro"},"voice":"af_heart","language":"en-us","speed":1,"text":"Hello there. How are you?","segments":[{"text":"Hello there. How are you?","seconds":2.4,"offset":0,"sentences":[{"text":"Hello there.","seconds":1.0,"offset":0},{"text":"How are you?","seconds":1.4,"offset":1.0}]}],"seconds":2.4,"created_at":"2026-10-20T10:00:00Z"}"#

    func home() throws -> URL {
        let home = FileManager.default.temporaryDirectory.appendingPathComponent("voice-home-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: home.appendingPathComponent("Music/Apogee"), withIntermediateDirectories: true)
        return home
    }

    /// Recorded from the installed binary over a scratch store
    /// (tests/business/reads/Fixtures): a voice model is stored weights, named
    /// by its handle.
    static func recorded(_ name: String) -> String {
        let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            .appendingPathComponent("../../business/reads/Fixtures/models-stored-\(name).json")
        return try! String(contentsOf: url, encoding: .utf8)
    }

    func runner() -> ScriptedRunner {
        let runner = ScriptedRunner()
        runner.reading(["models", "list"], Self.recorded("list"))
        runner.reading(["models", "info", "Kokoro-82M/voice/5f3a2c91d0e4"], Self.recorded("info-voice"))
        return runner
    }

    @Test("the picker's voices are the model's own, as its models info lists them by language")
    func voices() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let model = VoiceViewModel(runner: runner(), starter: ScriptedJobs([]), preferences: Preferences(store: MemoryStore()), home: home)
        await model.connect(binary: Self.binary)
        #expect(model.voiceModel == "Kokoro-82M/voice/5f3a2c91d0e4", "the store's voice model, by its handle")
        #expect(model.voices == [
            VoiceViewModel.VoiceGroup(language: "American English", voices: ["af_heart", "am_adam"]),
            VoiceViewModel.VoiceGroup(language: "British English", voices: ["bf_emma"]),
        ])
    }

    @Test("say is the real command into a free name in ~/Music/Apogee; the library reads the speech back")
    func speak() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let job = ScriptedJob()
        let jobs = ScriptedJobs([job])
        let model = VoiceViewModel(runner: runner(), starter: jobs, preferences: Preferences(store: MemoryStore()), home: home)
        await model.connect(binary: Self.binary)
        model.text = "Hello there. How are you?"
        model.voice = "bf_emma"
        model.speed = 1.2
        let out = home.appendingPathComponent("Music/Apogee/hello-there-how-are.wav").path
        #expect(model.plannedArguments == ["voice", "say", "-o", out, "--voice", "bf_emma", "--speed", "1.2", "--", "Hello there. How are you?"])
        model.speak()
        #expect(jobs.starts.first?.arguments == ["voice", "say", "-o", out, "--voice", "bf_emma", "--speed", "1.2", "--", "Hello there. How are you?"])
        try Data("RIFF".utf8).write(to: URL(fileURLWithPath: out))
        try Self.speech.replacingOccurrences(of: "OUT", with: out).write(toFile: out + ".json", atomically: true, encoding: .utf8)
        job.end()
        #expect(await eventually { model.selected == out })
        #expect(model.speeches.count == 1)
    }

    @Test("playback lights the sentence the record's timings say, on a scripted clock; a click seeks to its start")
    func timings() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let out = home.appendingPathComponent("Music/Apogee/hello.wav").path
        try Data("RIFF".utf8).write(to: URL(fileURLWithPath: out))
        try Self.speech.replacingOccurrences(of: "OUT", with: out).write(toFile: out + ".json", atomically: true, encoding: .utf8)
        let model = VoiceViewModel(runner: runner(), starter: ScriptedJobs([]), preferences: Preferences(store: MemoryStore()), home: home)
        await model.connect(binary: Self.binary)
        model.select(model.speeches[0])
        #expect(model.currentSentence == 0)
        model.position = 0.99
        #expect(model.currentSentence == 0)
        model.position = 1.2
        #expect(model.currentSentence == 1)
        model.position = 2.5
        #expect(model.currentSentence == nil, "past the end, nothing is lit")
        #expect(model.start(of: 1) == 1.0)
        #expect(model.start(of: 5) == nil)
    }

    @Test("a run refused for its runtime asks; yes installs the voice set and speaks again")
    func consent() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let refused = ScriptedJob()
        let install = ScriptedJob()
        let again = ScriptedJob()
        let jobs = ScriptedJobs([refused, install, again])
        let model = VoiceViewModel(runner: runner(), starter: jobs, preferences: Preferences(store: MemoryStore()), home: home)
        await model.connect(binary: Self.binary)
        model.text = "hi"
        model.speak()
        var tail = BoundedTail()
        tail.append(Data("apogee voice: the voice runtime is not installed -- run 'apogee train setup --with voice'\n".utf8))
        refused.endWith(tail: tail)
        #expect(await eventually { model.consent?.arguments == ["train", "setup", "--with", "voice"] })
        model.install()
        install.end()
        #expect(await eventually { jobs.starts.count == 3 })
        #expect(jobs.starts[2].arguments == jobs.starts[0].arguments)
        again.end(status: 1)
    }
}
