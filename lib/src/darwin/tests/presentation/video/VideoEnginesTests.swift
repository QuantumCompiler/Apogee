import BusinessJobs
import BusinessMedia
import DataPreferences
import DataProtocol
import Foundation
import Testing

@testable import PresentationVideo

/// A recorded fixture, by its path under tests/.
private func fixture(_ path: String) -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("../../\(path)")
    return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
}

/// The binary's verdict on each command the sheets build with an engine
/// (`tests/business/media/Fixtures/engine-command-verdicts.txt`), recorded
/// over the sandbox store the media documents come from: `taken` where every
/// rule of the command passed -- the run stopped only at the runtime ladder,
/// no Python environment there -- else the binary's first line.
func engineVerdicts() -> [String: String] {
    var out: [String: String] = [:]
    for line in fixture("business/media/Fixtures/engine-command-verdicts.txt").split(separator: "\n") {
        let parts = line.split(separator: "\t", maxSplits: 1).map(String.init)
        if parts.count == 2 { out[parts[1]] = parts[0] }
    }
    return out
}

/// The clip sheet over the store recorded from the binary built with the
/// CLI's 53l (`tests/business/reads/Fixtures/media-*`): a model folder holding
/// a diffusers pipeline beside its MLX video model, a second pipeline, the
/// Wan set the MLX model was converted from (folded), and the MLX one made the
/// default.
@Suite("VideoEngines", .timeLimit(.minutes(1)))
@MainActor
struct VideoEnginesTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let pipeline = "Wan2.2-TI2V-5B/pipeline/9b1e47c0a2f6"
    static let mlx = "Wan2.2-TI2V-5B/mlx/5e6f708192a3"
    static let small = "Wan2.1-T2V-1.3B/pipeline/7c21e0d4b9a3"

    func home() throws -> URL {
        let home = FileManager.default.temporaryDirectory.appendingPathComponent("video-engines-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: home.appendingPathComponent("Movies/Apogee"), withIntermediateDirectories: true)
        return home
    }

    func store(defaultVideo: String = "media-default-video") -> ScriptedRunner {
        let runner = ScriptedRunner()
        runner.reading(["models", "list"], fixture("business/reads/Fixtures/media-list.json"))
        runner.commanding(["config", "get", "models.default_video"], .done(stdout: fixture("business/reads/Fixtures/\(defaultVideo).txt")))
        return runner
    }

    func sheet(_ home: URL, runner: ScriptedRunner) async -> VideoViewModel {
        let model = VideoViewModel(starter: ScriptedJobs([]), preferences: Preferences(store: MemoryStore()), home: home)
        model.reader = runner
        model.connect(binary: Self.binary)
        await model.readModels()
        model.newClip()
        model.form.prompt = "A lighthouse at dusk"
        return model
    }

    /// The sheet's command as the verdicts record it: its output `OUT.mp4`.
    func recorded(_ arguments: [String]?) -> String {
        var words = arguments ?? []
        if let o = words.firstIndex(of: "-o"), o + 1 < words.count { words[o + 1] = "OUT.mp4" }
        return words.joined(separator: " ")
    }

    @Test("the store's video models are read with the default: each pipeline on PyTorch, the MLX video model on MLX, PyTorch first in a folder")
    func modelsRead() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let model = await sheet(home, runner: store())
        #expect(model.models.map(\.handle) == [Self.pipeline, Self.mlx, Self.small])
        #expect(model.models.map { model.label($0) } == ["Wan2.2-TI2V-5B · PyTorch", "Wan2.2-TI2V-5B · MLX", "Wan2.1-T2V-1.3B · PyTorch"])
        #expect(model.defaultModel == Self.mlx, "config get models.default_video, as printed")
        #expect(!model.models.contains { $0.handle.contains("/safetensors/") }, "a Wan set is no video model generate runs")
    }

    @Test("a model alone in its folder: its engine said, never passed; the default: its own engine said, nothing named")
    func engineShown() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let model = await sheet(home, runner: store())
        #expect(model.choice == .byDefault)
        #expect(model.engineLine == "The default, \(Self.mlx), runs on MLX, through mlx-video: an MLX video model.")
        #expect(!(model.plannedArguments ?? []).contains("-m") && !(model.plannedArguments ?? []).contains("--engine"))
        model.choose(.model(Self.small))
        #expect(model.form.model == Self.small && model.form.engine == nil)
        #expect(model.engineLine == "Runs on PyTorch, through diffusers: a diffusers pipeline.")
        #expect(Array((model.plannedArguments ?? []).suffix(2)) == ["-m", Self.small], "the engine is the directory's: no --engine")
        model.choose(.another)
        #expect(model.form.model.isEmpty && model.form.engine == nil)
        #expect(model.engineLine == "Runs on what its directory is: PyTorch for a diffusers pipeline, MLX for an MLX video model.")
    }

    @Test("a pipeline beside its MLX conversion: the engine is the person's, PyTorch first, passed as --engine with the set's handle")
    func engineChosen() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let model = await sheet(home, runner: store())
        model.choose(.model(Self.pipeline))
        #expect(model.form.engine == .torch)
        #expect(Array((model.plannedArguments ?? []).suffix(4)) == ["-m", Self.pipeline, "--engine", "torch"])
        model.choose(.model(Self.mlx))
        #expect(model.form.engine == .mlx)
        #expect(Array((model.plannedArguments ?? []).suffix(4)) == ["-m", Self.mlx, "--engine", "mlx"])
        #expect(model.engineLine == "Runs on MLX, through mlx-video: an MLX video model. Wan2.2-TI2V-5B holds a pipeline and its MLX conversion, so the engine is yours: --engine mlx.")
        model.newClip()
        #expect(model.choice == .byDefault && model.form.engine == nil, "a new sheet starts from the default again")
    }

    @Test("every command the clip sheet builds over the recorded store is one the binary takes; naming the folder, or the other engine, is refused")
    func commandsTaken() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let verdicts = engineVerdicts()
        #expect(!verdicts.isEmpty, "no recorded verdicts")
        let model = await sheet(home, runner: store())
        let choices: [VideoViewModel.ModelChoice] = [.byDefault] + model.models.map { .model($0.handle) }
        var built: Set<String> = []
        for choice in choices {
            model.choose(choice)
            let command = recorded(model.plannedArguments)
            built.insert(command)
            #expect(verdicts[command] == "taken", "\(command): \(verdicts[command] ?? "never recorded")")
        }
        #expect(built.count == 4)
        let folder = "video generate A lighthouse at dusk -o OUT.mp4 -m Wan2.2-TI2V-5B --engine mlx"
        #expect(verdicts[folder]?.hasPrefix("refused: apogee video: 'Wan2.2-TI2V-5B' holds 2 video models -- name one") == true,
                "why the sheet names the set's handle: \(verdicts[folder] ?? "not recorded")")
        let other = "video generate A lighthouse at dusk -o OUT.mp4 -m \(Self.mlx) --engine torch"
        #expect(verdicts[other]?.hasPrefix("refused: apogee video: --engine torch: '\(Self.mlx)' is an MLX video model") == true,
                "--engine only names the model's own: \(verdicts[other] ?? "not recorded")")
        #expect(!built.contains(folder) && !built.contains(other))
    }

    @Test("no default set: the sheet says the CLI refuses a clip that names none")
    func noDefault() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let model = await sheet(home, runner: store(defaultVideo: "media-default-video-unset"))
        #expect(model.defaultModel == nil)
        #expect(model.engineLine == "No default video model is set, so the CLI refuses a clip that names none: pick one.")
    }

    @Test("against a binary before the CLI's 53l nothing changes: a pipeline's sheet keeps its command, and nothing read says nothing")
    func olderBinary() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let older = ScriptedRunner()
        older.reading(["models", "list"], fixture("business/reads/Fixtures/models-stored-list.json"))
        let model = await sheet(home, runner: older)
        #expect(model.models == [MediaModel(handle: "Wan2.2-TI2V-5B/pipeline/9b1e47c0a2f6", engine: .torch)], "an MLX set of no stated kind is no video model")
        #expect(model.defaultModel == nil)
        let unread = VideoViewModel(starter: ScriptedJobs([]), preferences: Preferences(store: MemoryStore()), home: home)
        unread.newClip()
        unread.form.prompt = "A lighthouse at dusk"
        unread.form.model = "wan-1.3b"
        #expect(unread.models.isEmpty && unread.engineLine == nil, "no store read: the typed field, as before")
        #expect(Array((unread.plannedArguments ?? []).suffix(2)) == ["-m", "wan-1.3b"])
    }

    @Test("run again from a clip made on MLX says its engine; a record that states none says nothing")
    func runAgain() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let folder = home.appendingPathComponent("Movies/Apogee")
        for name in ["clip-mlx", "clip-torch"] {
            let text = fixture("business/media/Fixtures/\(name).json").replacingOccurrences(of: "/Users/someone/Movies/Apogee", with: folder.path)
            let output = try #require(JSONValue.parse(text)?["output"]?.stringValue)
            try Data("mp4".utf8).write(to: URL(fileURLWithPath: output))
            try text.write(toFile: output + ".json", atomically: true, encoding: .utf8)
        }
        let model = VideoViewModel(starter: ScriptedJobs([]), preferences: Preferences(store: MemoryStore()), home: home)
        model.connect(binary: Self.binary)
        let mlx = try #require(model.clips.first { $0.name == "lighthouse-mlx.mp4" })
        model.newClip(from: mlx)
        #expect(model.engineLine == "Runs on MLX, as its record did: its own model, \(Self.mlx).")
        #expect(!(model.plannedArguments ?? []).contains("--engine"), "--from replays it")
        model.newClip(from: mlx, extend: true)
        #expect(model.engineLine?.hasPrefix("Runs on MLX") == true)
        let older = try RecordFolderInHome(home).older()
        model.refresh()
        let olderClip = try #require(model.clips.first { $0.output == older })
        model.newClip(from: olderClip)
        #expect(model.engineLine == nil, "a record from before 53l: drawn as before")
    }

    @Test("a clip's page says the engine its record ran on and what ran; a record from before 53l is drawn as before")
    func pageSaysEngine() throws {
        func clip(_ text: String) -> ClipRecord? { JSONValue.parse(text).flatMap(ClipRecord.init) }
        let mlx = try #require(clip(fixture("business/media/Fixtures/clip-mlx.json")))
        let facts = ClipPage.facts(mlx)
        #expect(facts.first?.0 == "Model" && facts.first?.1 == Self.mlx, "an MLX video model is no pipeline")
        #expect(facts.contains { $0.0 == "Engine" && $0.1 == "MLX · mlx 0.31.2, mlx-video 0.0-video-stub" })
        let torch = try #require(clip(fixture("business/media/Fixtures/clip-torch.json")))
        #expect(ClipPage.facts(torch).contains { $0.0 == "Engine" && $0.1 == "PyTorch · diffusers 0.0-stub, torch 0.0-stub" })
        #expect(ClipPage.facts(torch).first?.0 == "Pipeline")
        guard case .object(var fields)? = JSONValue.parse(fixture("business/media/Fixtures/clip-torch.json")) else { return }
        fields["engine"] = nil
        let older = try #require(ClipRecord(.object(fields)))
        #expect(!ClipPage.facts(older).contains { $0.0 == "Engine" } && ClipPage.facts(older).first?.0 == "Pipeline")
    }
}

/// A record from before the CLI's 53l in a home's `Movies/Apogee`: the
/// recorded PyTorch clip's, its `engine` taken out.
struct RecordFolderInHome {
    let folder: URL

    init(_ home: URL) {
        folder = home.appendingPathComponent("Movies/Apogee")
    }

    func older() throws -> String {
        let text = fixture("business/media/Fixtures/clip-torch.json").replacingOccurrences(of: "/Users/someone/Movies/Apogee", with: folder.path)
            .replacingOccurrences(of: "lighthouse-pytorch", with: "lighthouse-older")
        guard case .object(var fields)? = JSONValue.parse(text), let output = fields["output"]?.stringValue else {
            throw CocoaError(.fileReadCorruptFile)
        }
        fields["engine"] = nil
        fields["created_at"] = .string("2026-10-19T09:00:00Z")
        try Data("mp4".utf8).write(to: URL(fileURLWithPath: output))
        try JSONValue.object(fields).serialized.write(toFile: output + ".json", atomically: true, encoding: .utf8)
        return output
    }
}
