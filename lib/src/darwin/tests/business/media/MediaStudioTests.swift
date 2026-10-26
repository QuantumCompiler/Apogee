import BusinessMedia
import DataProtocol
import Foundation
import Testing

/// A fixture recorded from the branch's binary over a sandbox, by its path
/// under tests/ (GUI 4f).
private func recorded(_ path: String) -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("../../\(path)")
    return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
}

@Suite("InstallConsent")
struct InstallConsentTests {
    @Test("the binary's refusals name their install on stderr alone: each read from there, the naming line its words")
    func fromStderr() throws {
        for (verb, set) in [("compose-refused-runtime", "video"), ("generate-refused-runtime", "video"), ("voice-refused-runtime", "voice"),
            ("compose-refused-voice-runtime", "voice")]
        {
            let stderr = recorded("business/media/Fixtures/\(verb).err")
            // The run's last word is stderr's last line: a stream with no error event.
            let failure = stderr.split(whereSeparator: \.isNewline).last.map(String.init) ?? ""
            let consent = try #require(InstallConsent(failure: failure, stderr: stderr), "\(verb)")
            #expect(consent.arguments == ["train", "setup", "--with", set])
            #expect(consent.command == "apogee train setup --with \(set)")
            #expect(consent.set == set)
            #expect(consent.said == stderr.trimmingCharacters(in: .whitespacesAndNewlines))
        }
    }

    @Test("the naming line is found under others, and a refusal naming no set asks nothing")
    func otherwise() {
        let consent = InstallConsent(
            failure: "a later line", stderr: "apogee video compose: torch and diffusers are not installed in Apogee's Python environment -- apogee train setup --with video\na later line\n")
        #expect(consent?.said.hasPrefix("apogee video compose: torch and diffusers") == true)
        let noEnvironment = recorded("business/media/Fixtures/compose-refused-no-env.err")
        #expect(InstallConsent(failure: noEnvironment, stderr: noEnvironment) == nil, "plain train setup names no set")
        let twoVoices = recorded("business/media/Fixtures/compose-refused-two.err")
        #expect(InstallConsent(failure: twoVoices, stderr: twoVoices) == nil)
    }
}

@Suite("CaptionsAbility")
struct CaptionsAbilityTests {
    func check(_ name: String) throws -> JSONValue {
        try #require(JSONValue.parse(recorded("business/reads/Fixtures/\(name)")))
    }

    @Test("check's ffmpeg captions row: skipped without libass, with its remedy; ok with it")
    func fromCheck() throws {
        let kept = try #require(CaptionsAbility(check: check("check-captions-skipped.json")))
        #expect(!kept.burns)
        #expect(kept.reason == "no subtitles filter (this ffmpeg was built without libass)")
        #expect(kept.remedy == "brew install ffmpeg-full && brew unlink ffmpeg; brew link --force ffmpeg-full")
        let burned = try #require(CaptionsAbility(check: check("check-captions-ok.json")))
        #expect(burned.burns)
        #expect(burned.remedy == nil)
        #expect(CaptionsAbility(check: JSONValue.parse(#"{"rows":[],"ok":true,"failures":0,"warnings":0}"#)!) == nil, "before 52e: no row")
    }
}

@Suite("StoreMedia")
struct StoreMediaTests {
    @Test("the store's voice models, as the recorded models list states them, PyTorch first; and its video models")
    func fromTheList() throws {
        let two = try #require(ReadDocument(text: recorded("business/reads/Fixtures/compose-voices-list.json")))
        #expect(MediaModel.voiceModels(in: two) == [
            MediaModel(handle: "hexgrad--Kokoro-82M/voice/cccccccccccc", engine: .torch),
            MediaModel(handle: "hexgrad--Kokoro-82M/mlx/ffffffffffff", engine: .mlx),
        ])
        #expect(MediaModel.videoModels(in: two) == [MediaModel(handle: "org--clip/pipeline/aaaaaaaaaaaa", engine: .torch)])
        let one = try #require(ReadDocument(text: recorded("business/reads/Fixtures/compose-voice-list.json")))
        #expect(MediaModel.voiceModels(in: one).map(\.handle) == ["hexgrad--Kokoro-82M/voice/cccccccccccc"])
    }
}

@Suite("ComposeFreeNames")
struct ComposeFreeNamesTests {
    @Test("a composition's free name passes over a taken .srt beside it, and its voice model is passed as typed")
    func besidesAndVoiceModel() throws {
        let folder = FileManager.default.temporaryDirectory.appendingPathComponent("free-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: folder) }
        try Data("1\n".utf8).write(to: folder.appendingPathComponent("tides.srt"))
        #expect(MediaLibrary.freePath(in: folder, base: "tides", ext: "mp4").lastPathComponent == "tides.mp4")
        #expect(MediaLibrary.freePath(in: folder, base: "tides", ext: "mp4", besides: ["srt"]).lastPathComponent == "tides-2.mp4")
        var form = ComposeForm()
        form.brief = "the tides"
        form.voiceModel = "hexgrad--Kokoro-82M/mlx/ffffffffffff"
        form.voice = "af_heart"
        #expect(form.arguments(output: "/m/t.mp4") == ["video", "compose", "-o", "/m/t.mp4", "--voice-model", "hexgrad--Kokoro-82M/mlx/ffffffffffff", "--voice", "af_heart", "--", "the tides"])
        form.from = "/m/t.mp4"
        form.instruction = "brighter"
        #expect(form.arguments(output: "/m/e.mp4")?.contains("--voice-model") == false, "an edit keeps its parent's voice model")
    }
}
