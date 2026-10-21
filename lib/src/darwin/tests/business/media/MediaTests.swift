import BusinessMedia
import DataProtocol
import Foundation
import Testing

/// A folder of records, as the CLI leaves them: each output and its sidecar.
final class RecordFolder {
    let url: URL

    init() throws {
        url = FileManager.default.temporaryDirectory.appendingPathComponent("records-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
    }

    deinit { try? FileManager.default.removeItem(at: url) }

    @discardableResult
    func clip(_ name: String, prompt: String, created: String, parent: String? = nil, preset: String? = nil, output: Bool = true) throws -> String {
        let clip = url.appendingPathComponent(name).path
        if output { try Data("mp4".utf8).write(to: URL(fileURLWithPath: clip)) }
        let parentJSON = parent.map { #"{"move":"from","document":"\#($0).json","clip":"\#($0)","continued":null}"# } ?? "null"
        let presetJSON = preset.map { "\"\($0)\"" } ?? "null"
        let record = #"{"object":"video.generation","status":"done","output":"\#(clip)","sidecar":"\#(clip).json","model":{"name":"wan-1.3b"},"prompt":"\#(prompt)","image":null,"mode":"t2v","seed":4242,"seed_source":"generated","settings":{"width":1280,"height":704,"duration":5,"fps":24,"frames":121,"steps":16},"codec":"h264","seconds":312.5,"created_at":"\#(created)","parent":\#(parentJSON),"preset":\#(presetJSON),"loras":[{"path":"/loras/film.safetensors","sha256":"ab","scale":0.8}]}"#
        try record.write(toFile: clip + ".json", atomically: true, encoding: .utf8)
        return clip
    }
}

@Suite("Media")
struct MediaTests {
    @Test("the library is the folders' records, newest first; a record whose clip is gone, or that is not JSON, is skipped and said")
    func scan() throws {
        let folder = try RecordFolder()
        let draft = try folder.clip("lighthouse-draft.mp4", prompt: "a lighthouse at dusk", created: "2026-10-20T10:00:00Z", preset: "draft")
        try folder.clip("lighthouse.mp4", prompt: "a lighthouse at dusk", created: "2026-10-20T11:00:00Z", parent: draft, preset: "final")
        try folder.clip("gone.mp4", prompt: "x", created: "2026-10-20T09:00:00Z", output: false)
        try "not json".write(to: folder.url.appendingPathComponent("broken.json"), atomically: true, encoding: .utf8)
        try #"{"object":"voice.speech","output":"/x.wav"}"#.write(to: folder.url.appendingPathComponent("speech.json"), atomically: true, encoding: .utf8)
        let scan = MediaLibrary.clips(in: [folder.url, folder.url.appendingPathComponent("missing")])
        #expect(scan.records.map(\.name) == ["lighthouse.mp4", "lighthouse-draft.mp4"])
        #expect(scan.skipped.map { ($0.file as NSString).lastPathComponent }.sorted() == ["broken.json", "gone.mp4.json"])
        let final = scan.records[0]
        #expect(final.preset == "final" && final.parentMove == "from" && final.parentClip == draft)
        #expect(final.width == 1280 && final.duration == 5 && final.steps == 16 && final.seed == 4242)
        #expect(final.loras == [ClipRecord.LoRA(path: "/loras/film.safetensors", scale: 0.8)])
        #expect(MediaLibrary.chain(of: final, in: scan.records).map(\.name) == ["lighthouse-draft.mp4"])
    }

    @Test("nothing is written over: a taken name gets a number, its record's name counted too")
    func freePath() throws {
        let folder = try RecordFolder()
        #expect(MediaLibrary.freePath(in: folder.url, base: "a-lighthouse", ext: "mp4").lastPathComponent == "a-lighthouse.mp4")
        try Data().write(to: folder.url.appendingPathComponent("a-lighthouse.mp4"))
        #expect(MediaLibrary.freePath(in: folder.url, base: "a-lighthouse", ext: "mp4").lastPathComponent == "a-lighthouse-2.mp4")
        try Data().write(to: folder.url.appendingPathComponent("a-lighthouse-2.mp4.json"))
        #expect(MediaLibrary.freePath(in: folder.url, base: "a-lighthouse", ext: "mp4").lastPathComponent == "a-lighthouse-3.mp4")
        #expect(MediaLibrary.freePath(in: folder.url, base: "  ", ext: "wav").lastPathComponent == "untitled.wav")
        #expect(MediaLibrary.stem("A lighthouse, at dusk -- waves crashing on rocks!") == "a-lighthouse-at-dusk-waves")
    }

    @Test("every documented flag combination the sheet makes is the command a person types")
    func argv() {
        var form = ClipForm()
        #expect(form.arguments(output: "/m/a.mp4") == nil, "a new clip needs a prompt or a seed image")
        form.prompt = "a lighthouse at dusk"
        #expect(form.arguments(output: "/m/a.mp4") == ["video", "generate", "a lighthouse at dusk", "-o", "/m/a.mp4"])
        form.image = "/p/seed.png"
        form.model = "wan-5b"
        form.seed = 7
        form.duration = 2.5
        form.resolution = "704x1280"
        form.steps = 30
        form.fps = 16
        form.loras = ["/loras/film.safetensors:0.8", ""]
        #expect(form.arguments(output: "/m/a.mp4") == [
            "video", "generate", "a lighthouse at dusk", "-o", "/m/a.mp4", "--image", "/p/seed.png", "-m", "wan-5b",
            "--seed", "7", "--duration", "2.5", "--resolution", "704x1280", "--steps", "30", "--fps", "16",
            "--lora", "/loras/film.safetensors:0.8",
        ])
        form.preset = .draft
        #expect(form.arguments(output: "/m/a.mp4")?.contains("--steps") == false, "a preset sets the steps")
        #expect(form.arguments(output: "/m/a.mp4")?.contains("--draft") == true)
        form.noLoRAs = true
        #expect(form.arguments(output: "/m/a.mp4")?.contains("--lora") == false)
        #expect(form.arguments(output: "/m/a.mp4")?.last == "--no-lora")

        var again = ClipForm()
        again.start = .again("/m/draft.mp4.json")
        again.preset = .final
        #expect(again.arguments(output: "/m/final.mp4") == ["video", "generate", "--from", "/m/draft.mp4.json", "-o", "/m/final.mp4", "--final"])
        var extend = ClipForm()
        extend.start = .extend("/m/a.mp4")
        extend.prompt = "the light comes on"
        #expect(extend.arguments(output: "/m/b.mp4") == ["video", "generate", "the light comes on", "--extend", "/m/a.mp4", "-o", "/m/b.mp4"])
    }

    @Test("a speech's record: its sentences laid on the file's own clock, and the one spoken at a time")
    func speech() throws {
        let value = try #require(JSONValue.parse(#"{"object":"voice.speech","status":"done","output":"/m/hello.wav","model":{"name":"kokoro"},"voice":"af_heart","language":"en-us","speed":1,"text":"Hello there. How are you? Fine.","segments":[{"text":"Hello there. How are you?","seconds":2.4,"offset":0,"sentences":[{"text":"Hello there.","seconds":1.0,"offset":0},{"text":"How are you?","seconds":1.4,"offset":1.0}]},{"text":"Fine.","seconds":0.6,"offset":2.4,"sentences":[{"text":"Fine.","seconds":0.6,"offset":0}]}],"seconds":3.0,"created_at":"2026-10-20T10:00:00Z"}"#))
        let speech = try #require(SpeechRecord(value))
        #expect(speech.sentences.map(\.offset) == [0, 1.0, 2.4])
        #expect(speech.sentence(at: 0.2) == 0)
        #expect(speech.sentence(at: 1.5) == 1)
        #expect(speech.sentence(at: 2.9) == 2)
        #expect(speech.sentence(at: 3.5) == nil)
        #expect(speech.voice == "af_heart" && speech.model == "kokoro")
    }
}
