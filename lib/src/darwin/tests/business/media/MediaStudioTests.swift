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

/// GUI M13: since the CLI's M29 a runtime refusal ends the job's stream on an
/// `error` event, its words still on stderr -- read there first, stderr the
/// fallback where no `error` arrived (a binary before M29: 4f's recordings).
@Suite("InstallConsent from the stream's error (M13)")
struct InstallConsentErrorTests {
    /// The first `error` event of a recorded stream: its message.
    static func error(in name: String) -> String? {
        recorded("business/media/Fixtures/\(name).jsonl").split(separator: "\n").lazy.compactMap { line -> String? in
            if case .error(let message) = EventDecoder.decode(line: String(line)).kind { message } else { nil }
        }.first
    }

    @Test("each recorded runtime refusal's error names its install: the consent is read there, its words the error's")
    func fromTheError() throws {
        for (verb, set) in [
            ("generate-refused-runtime-m29", "video"), ("compose-refused-runtime-m29", "video"), ("voice-refused-runtime-m29", "voice"),
        ] {
            let error = try #require(Self.error(in: verb), "\(verb) ends on its error")
            let stderr = recorded("business/media/Fixtures/\(verb).err")
            #expect(stderr.contains(error), "\(verb): stderr says the same words, prefixed")
            let consent = try #require(InstallConsent(error: error, failure: error, stderr: stderr), "\(verb)")
            #expect(consent.arguments == ["train", "setup", "--with", set])
            #expect(consent.said == error, "\(verb): the stream's words, not stderr's")
        }
    }

    @Test("the error first: one naming no install asks nothing, whatever stderr held; no error, stderr read as before")
    func errorFirst() throws {
        let two = try #require(Self.error(in: "compose-refused-two-m29"))
        // A recorded refusal's stderr beside another refusal's error: the
        // stream's word decides, never a line stderr happened to hold.
        let runtimeErr = recorded("business/media/Fixtures/generate-refused-runtime.err")
        #expect(InstallConsent(error: two, failure: two, stderr: runtimeErr) == nil)
        let line = runtimeErr.split(whereSeparator: \.isNewline).last.map(String.init) ?? ""
        let older = try #require(InstallConsent(error: nil, failure: line, stderr: runtimeErr))
        #expect(older.arguments == ["train", "setup", "--with", "video"])
        #expect(older.said == runtimeErr.trimmingCharacters(in: .whitespacesAndNewlines))
    }

    @Test("a failed install naming its rebuild asks for the rebuild next; one naming itself, or nothing, asks nothing")
    func afterFailedInstall() throws {
        // The CLI's M19 refusal, as `train setup` says it on an environment
        // whose Python the set does not run on (`PythonEnv::unfit_reason`).
        let install = ["train", "setup", "--with", "voice"]
        let unfit = "apogee train: the Python environment at /h/venv is on Python 3.14.5, and the 'voice' set runs on >=3.10,<3.13 -- rebuild it on a Python that fits: apogee train setup --rebuild --with voice"
        let next = try #require(InstallConsent(afterFailed: install, failure: unfit, stderr: unfit + "\n"))
        #expect(next.arguments == ["train", "setup", "--rebuild", "--yes", "--with", "voice"])
        #expect(next.rebuilds)
        #expect(next.set == "voice")
        #expect(next.said == unfit)
        #expect(!InstallConsent(arguments: install, said: "").rebuilds)
        // A failure naming the install that just failed, or none, asks nothing again.
        let again = "apogee voice: kokoro is not installed -- apogee train setup --with voice"
        #expect(InstallConsent(afterFailed: install, failure: again, stderr: again) == nil)
        let pip = "apogee train: pip install for the 'voice' set failed (exit code 1):\nERROR: No matching distribution found for kokoro"
        #expect(InstallConsent(afterFailed: install, failure: pip, stderr: pip) == nil)
        #expect(InstallConsent(afterFailed: install, failure: nil, stderr: "") == nil)
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
