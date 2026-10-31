#if DEBUG
import BusinessJobs
import BusinessMedia
import DataPreferences
import Foundation

/// The Voice place's view-model for its previews (ADR 0004): a home of its
/// own under the temporary folder -- its `Music/Apogee` holding the record
/// the module's tests write -- the store's voice model from the recorded
/// documents, and preferences held in memory. Never the person's speeches or
/// defaults, and nothing starts the binary.
@MainActor
enum VoicePreview {
    /// The store, as tests/presentation/voice reads it: its voice model by its
    /// handle, and that model's voices.
    static let runner = PreviewRunner(reads: [
        (["models", "list"], VoiceDocuments.modelsList),
        (["models", "info", "Kokoro-82M/voice/5f3a2c91d0e4"], VoiceDocuments.voiceInfo),
    ])

    /// A store with no voice model: models-stored-list.json, its voice row
    /// taken out.
    static let noVoiceModel = PreviewRunner(reads: [(["models", "list"], withoutVoice(VoiceDocuments.modelsList))])

    static func withoutVoice(_ list: String) -> String {
        guard var document = (try? JSONSerialization.jsonObject(with: Data(list.utf8))) as? [String: Any],
            let rows = document["data"] as? [[String: Any]]
        else { return list }
        document["data"] = rows.filter { $0["format"] as? String != "voice" }
        return (try? JSONSerialization.data(withJSONObject: document)).map { String(decoding: $0, as: UTF8.self) } ?? list
    }

    /// tests/presentation/voice/VoiceViewModelTests.swift's `speech`: two
    /// sentences on the file's own clock, `OUT` its WAV.
    static let speech = #"{"object":"voice.speech","status":"done","output":"OUT","sidecar":"OUT.json","model":{"name":"kokoro"},"voice":"af_heart","language":"en-us","speed":1,"text":"Hello there. How are you?","segments":[{"text":"Hello there. How are you?","seconds":2.4,"offset":0,"sentences":[{"text":"Hello there.","seconds":1.0,"offset":0},{"text":"How are you?","seconds":1.4,"offset":1.0}]}],"seconds":2.4,"created_at":"2026-10-20T10:00:00Z"}"#

    // MARK: - The runtime's install (GUI 4f)

    /// `voice say` refused for its runtime as the binary refused it over a
    /// sandbox with no torch (tests/business/media/Fixtures/
    /// voice-refused-runtime.*): its 52b stream's one `job` line, the refusal
    /// on stderr alone.
    static let refusedRun = PreviewRun(
        stdout: VoiceDocuments.voiceRefused, streaming: true, status: 1, stderr: VoiceDocuments.voiceRefusedErr)

    /// The same speech refused by the branch binary since the CLI's M29
    /// (tests/business/media/Fixtures/voice-refused-runtime-m29.*): its
    /// stream ends on the refusal's `error`, stderr saying it too.
    static let refusedRunSaid = PreviewRun(
        stdout: VoiceDocuments.voiceRefusedM29, streaming: true, status: 1, stderr: VoiceDocuments.voiceRefusedM29Err)

    /// `train setup --with voice` as the binary ran it over that sandbox's
    /// stub pip (tests/business/media/Fixtures/install-voice-*): still going,
    /// its first two lines said; failed; and done.
    static let installRunning = PreviewRun(
        stdout: VoiceDocuments.installVoiceDone.split(separator: "\n").prefix(2).joined(separator: "\n"), finishes: false)
    static let installFailed = PreviewRun(
        stdout: VoiceDocuments.installVoiceFailed, status: 1, stderr: VoiceDocuments.installVoiceFailedErr)
    static let installDone = PreviewRun(stdout: VoiceDocuments.installVoiceDone)

    /// A starter replaying `runs`, one a start.
    static func runs(_ runs: [PreviewRun]) -> PreviewRuns { PreviewRuns(runs) }

    /// Said, refused for its runtime, then the person's yes: the install on
    /// the starter's next run.
    static func install(_ model: VoiceViewModel) async {
        await say(model)
        await until { model.consent != nil }
        model.install()
    }

    /// A 52b run's first segment spoken, as tests/business/jobs/JobsTests.swift
    /// reads the stream's line.
    static let segment: [JobUpdate] = [.event(.segment(index: 1, of: 2, seconds: 2.4, scene: nil))]

    /// One preview's home, `ApogeePreviews/voice/<name>`, made fresh -- with
    /// the test's speech spoken into its `Music/Apogee` as `hello.wav`, a
    /// stand-in WAV, as there.
    static func home(_ name: String, spoken: Bool = true) -> URL {
        let home = FileManager.default.temporaryDirectory.appendingPathComponent("ApogeePreviews/voice/\(name)")
        let folder = home.appendingPathComponent("Music/Apogee")
        try? FileManager.default.removeItem(at: home)
        try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        if spoken {
            let out = folder.appendingPathComponent("hello.wav").path
            try? Data("RIFF".utf8).write(to: URL(fileURLWithPath: out))
            try? speech.replacingOccurrences(of: "OUT", with: out).write(toFile: out + ".json", atomically: true, encoding: .utf8)
        }
        return home
    }

    /// The view-model over `home`, not yet connected (`connect` reads the store).
    static func speeches(
        _ name: String, spoken: Bool = true, runner: PreviewRunner = runner, starter: any JobStarting = PreviewJobs()
    ) -> VoiceViewModel {
        let model = VoiceViewModel(runner: runner, starter: starter, preferences: Preferences(store: MemoryStore()), home: home(name, spoken: spoken))
        model.streams = { true }
        return model
    }

    /// The speech the module's tests read, as the library scans it.
    static func spoken(by model: VoiceViewModel) -> SpeechRecord? {
        MediaLibrary.speeches(in: model.folders).records.first
    }

    /// Connected, the speech picked, and the player's clock at `position`.
    static func playing(_ model: VoiceViewModel, at position: Double) async {
        await model.connect(binary: previewBinary)
        if let speech = model.speeches.first { model.select(speech) }
        model.position = position
    }

    /// Connected, then the test's text said in `bf_emma` at 1.2× -- the
    /// say test's own sheet.
    static func say(_ model: VoiceViewModel) async {
        await model.connect(binary: previewBinary)
        model.text = "Hello there. How are you?"
        model.voice = "bf_emma"
        model.speed = 1.2
        model.speak()
    }

    // MARK: - Two voice models, two engines (the CLI's 53m)

    static let voiceTorch = "Kokoro-82M/voice/5f3a2c91d0e4"
    static let voiceMLX = "Kokoro-82M/mlx/6f708192a3b4"

    /// The store recorded from the binary built with the CLI's 53l and 53m
    /// (tests/business/reads/Fixtures/media-*): Kokoro as hexgrad publishes
    /// it beside its MLX build, in one model folder, each with its voices.
    static let engines = PreviewRunner(reads: [
        (["models", "list"], VoiceDocuments.mediaList),
        (["models", "info", voiceTorch], VoiceDocuments.voiceInfoTorch),
        (["models", "info", voiceMLX], VoiceDocuments.voiceInfoMLX),
    ])

    /// The records the binary wrote on each engine (tests/business/media/
    /// Fixtures/speech-<engine>.json), written into the preview home's
    /// `Music/Apogee` as it left them there, beside stand-in WAVs.
    static func spokenOnEach(_ name: String) -> VoiceViewModel {
        let model = speeches(name, spoken: false, runner: engines)
        let folder = model.outputFolder
        for document in [VoiceDocuments.speechTorch, VoiceDocuments.speechMLX] {
            let text = document.replacingOccurrences(of: "/Users/someone/Music/Apogee", with: folder.path)
            guard let output = (try? JSONSerialization.jsonObject(with: Data(text.utf8)) as? [String: Any])?["output"] as? String else {
                continue
            }
            try? Data("RIFF".utf8).write(to: URL(fileURLWithPath: output))
            try? text.write(toFile: output + ".json", atomically: true, encoding: .utf8)
        }
        return model
    }

    /// The speech with this file name, as the library read it.
    static func speech(_ name: String, of model: VoiceViewModel) -> SpeechRecord? {
        MediaLibrary.speeches(in: model.folders).records.first { $0.name == name }
    }

    /// The library beside a record whose speech is gone -- the test's speech
    /// recorded as `gone.wav`, the WAV itself not there -- which the scan
    /// passes over and says why.
    static func speechGone(_ name: String) -> VoiceViewModel {
        let model = speeches(name)
        let out = model.outputFolder.appendingPathComponent("gone.wav").path
        try? speech.replacingOccurrences(of: "OUT", with: out).write(toFile: out + ".json", atomically: true, encoding: .utf8)
        return model
    }

    /// Waits, a few seconds at most, for what a run's end sets.
    static func until(_ condition: @MainActor () -> Bool) async {
        for _ in 0..<250 {
            if condition() { return }
            try? await Task.sleep(for: .milliseconds(20))
        }
    }
}
#endif
