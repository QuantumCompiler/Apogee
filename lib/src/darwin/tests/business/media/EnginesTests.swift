import BusinessMedia
import DataProtocol
import Foundation
import Testing

/// Recorded from the binary built with the CLI's 53l and 53m (`Fixtures/`):
/// the records its real seeded drivers wrote on each engine, over the CLI's
/// own stub runtimes in a sandbox home -- no model, no network -- the home
/// written `/Users/someone/.apogee`, the media folders `/Users/someone/Movies/
/// Apogee` and `/Users/someone/Music/Apogee`.
private func recorded(_ name: String) -> JSONValue {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("Fixtures/\(name).json")
    return JSONValue.parse((try? String(contentsOf: url, encoding: .utf8)) ?? "") ?? .null
}

/// The sandbox store's `models list` (tests/business/reads/Fixtures).
private func storeRows() -> [(handle: String, format: String?, kind: String?)] {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
        .appendingPathComponent("../reads/Fixtures/media-list.json")
    let list = JSONValue.parse((try? String(contentsOf: url, encoding: .utf8)) ?? "")
    return (list?["data"]?.arrayValue ?? []).compactMap { row in
        row["model"]?.stringValue.map { ($0, row["format"]?.stringValue, row["kind"]?.stringValue) }
    }
}

@Suite("Engines")
struct EnginesTests {
    @Test("a clip and a speech the binary made on MLX each say their engine and what ran: mlx-video, mlx-audio")
    func recordsOnMLX() throws {
        let clip = try #require(ClipRecord(recorded("clip-mlx")))
        #expect(clip.recordedEngine == .mlx && clip.engine == .mlx)
        #expect(clip.model == "Wan2.2-TI2V-5B/mlx/5e6f708192a3")
        #expect(clip.versions["mlx_video"] == "0.0-video-stub" && clip.versions["torch"] == "", "the record's own versions, as written")
        #expect(clip.engine.said(versions: clip.versions) == "MLX · mlx 0.31.2, mlx-video 0.0-video-stub", "an empty version did not run")
        let speech = try #require(SpeechRecord(recorded("speech-mlx")))
        #expect(speech.recordedEngine == .mlx && speech.engine == .mlx)
        #expect(speech.model == "Kokoro-82M/mlx/6f708192a3b4")
        #expect(speech.engine.said(versions: speech.versions) == "MLX · mlx 0.31.2, mlx-audio 0.0-audio-stub")
        #expect(speech.sentences.map(\.text) == ["Hello there.", "How are you?"], "the timings read as before")
    }

    @Test("a record made on PyTorch states its engine too: diffusers or Kokoro, and torch")
    func recordsOnPyTorch() throws {
        let clip = try #require(ClipRecord(recorded("clip-torch")))
        #expect(clip.recordedEngine == .torch)
        #expect(clip.engine.said(versions: clip.versions) == "PyTorch · diffusers 0.0-stub, torch 0.0-stub")
        let speech = try #require(SpeechRecord(recorded("speech-torch")))
        #expect(speech.recordedEngine == .torch)
        #expect(speech.engine.said(versions: speech.versions) == "PyTorch · kokoro 0.0-stub, torch 0.0-stub")
    }

    @Test("a record from before there were two engines states none, and ran on PyTorch; a word the app does not know is kept")
    func olderRecords() throws {
        for (name, read) in [("clip-torch", { ClipRecord($0)?.recordedEngine }), ("speech-torch", { SpeechRecord($0)?.recordedEngine })] as [(String, (JSONValue) -> MediaEngine??)] {
            guard case .object(var fields) = recorded(name) else {
                Issue.record("\(name) is not a document")
                continue
            }
            fields["engine"] = nil
            #expect(read(.object(fields)) == .some(nil), "\(name) without its engine states none")
            fields["engine"] = .string("tpu")
            #expect(read(.object(fields)) == .some(.other("tpu")), "\(name): a later CLI's word, as written")
        }
        guard case .object(var fields) = recorded("clip-torch") else { return }
        fields["engine"] = nil
        #expect(try #require(ClipRecord(.object(fields))).engine == .torch, "the CLI's rule: a record without it ran on torch")
        #expect(MediaEngine.recorded(nil) == .torch)
    }

    @Test("the store's video and voice models by format and kind -- PyTorch first within a folder -- and the folders that leave the engine to the person")
    func storeModels() {
        let rows = storeRows()
        #expect(rows.count == 5)
        let video = MediaModel.offered(rows.compactMap { row in MediaEngine.video(format: row.format, kind: row.kind).map { MediaModel(handle: row.handle, engine: $0) } })
        #expect(video.map(\.handle) == ["Wan2.2-TI2V-5B/pipeline/9b1e47c0a2f6", "Wan2.2-TI2V-5B/mlx/5e6f708192a3", "Wan2.1-T2V-1.3B/pipeline/7c21e0d4b9a3"],
                "the folder where it is first listed, PyTorch first in it")
        #expect(video.map(\.engine) == [.torch, .mlx, .torch])
        #expect(video.map { MediaModel.choosesEngine($0, among: video) } == [true, true, false], "only a pipeline beside its MLX conversion")
        let voice = MediaModel.offered(rows.compactMap { row in MediaEngine.voice(format: row.format, kind: row.kind).map { MediaModel(handle: row.handle, engine: $0) } })
        #expect(voice == [MediaModel(handle: "Kokoro-82M/voice/5f3a2c91d0e4", engine: .torch), MediaModel(handle: "Kokoro-82M/mlx/6f708192a3b4", engine: .mlx)])
        #expect(voice.allSatisfy { MediaModel.choosesEngine($0, among: voice) })
        #expect(MediaEngine.video(format: "mlx", kind: "voice") == nil && MediaEngine.voice(format: "pipeline", kind: nil) == nil)
        #expect(MediaEngine.video(format: "mlx", kind: nil) == nil, "an MLX set of no stated kind is a chat model's, as before 53e")
    }

    @Test("a name given to -m finds a model as the CLI finds one: its handle, or a folder holding one; a folder holding two is none -- and a picker names each by folder and engine")
    func named() {
        let models = [
            MediaModel(handle: "Wan2.2-TI2V-5B/pipeline/9b1e47c0a2f6", engine: .torch),
            MediaModel(handle: "Wan2.2-TI2V-5B/mlx/5e6f708192a3", engine: .mlx),
            MediaModel(handle: "Wan2.1-T2V-1.3B/pipeline/7c21e0d4b9a3", engine: .torch),
        ]
        #expect(MediaModel.named("Wan2.2-TI2V-5B/mlx/5e6f708192a3", in: models)?.engine == .mlx)
        #expect(MediaModel.named("Wan2.1-T2V-1.3B", in: models)?.handle == "Wan2.1-T2V-1.3B/pipeline/7c21e0d4b9a3")
        #expect(MediaModel.named("Wan2.2-TI2V-5B", in: models) == nil, "the CLI refuses it, naming both")
        #expect(MediaModel.named("elsewhere", in: models) == nil)
        #expect(models.map { $0.label(among: models) } == ["Wan2.2-TI2V-5B · PyTorch", "Wan2.2-TI2V-5B · MLX", "Wan2.1-T2V-1.3B · PyTorch"])
        let twins = models + [MediaModel(handle: "Wan2.1-T2V-1.3B/pipeline/0f1e2d3c4b5a", engine: .torch)]
        #expect(twins[2].label(among: twins) == "Wan2.1-T2V-1.3B/pipeline/7c21e0d4b9a3 · PyTorch", "two on one engine in a folder: named by handle")
    }

    @Test("--engine is passed after -m only where the form was given one; an older form's command is unchanged")
    func engineFlag() throws {
        var form = ClipForm()
        form.prompt = "A lighthouse at dusk"
        #expect(form.arguments(output: "/o.mp4") == ["video", "generate", "A lighthouse at dusk", "-o", "/o.mp4"])
        form.model = "Wan2.2-TI2V-5B/mlx/5e6f708192a3"
        #expect(form.arguments(output: "/o.mp4") == ["video", "generate", "A lighthouse at dusk", "-o", "/o.mp4", "-m", "Wan2.2-TI2V-5B/mlx/5e6f708192a3"])
        form.engine = .mlx
        form.seed = 7
        #expect(form.arguments(output: "/o.mp4") == [
            "video", "generate", "A lighthouse at dusk", "-o", "/o.mp4", "-m", "Wan2.2-TI2V-5B/mlx/5e6f708192a3", "--engine", "mlx", "--seed", "7",
        ])
        let again = ClipForm.from(try #require(ClipRecord(recorded("clip-mlx"))), extend: false)
        #expect(again.engine == nil && !again.arguments(output: "/o.mp4")!.contains("--engine"), "--from replays the record's engine itself")
    }
}
