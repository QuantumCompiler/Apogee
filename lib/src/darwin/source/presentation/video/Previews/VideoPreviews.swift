#if DEBUG
import BusinessJobs
import BusinessMedia
import DataPreferences
import Foundation
import InfrastructureProcess

/// The Video place's view-model for its previews (ADR 0004): a home of its
/// own under the temporary folder -- its `Movies/Apogee` holding the records
/// the module's tests write -- and preferences held in memory. Never the
/// person's clips or defaults, and nothing starts the binary.
@MainActor
enum VideoPreview {
    /// One preview's home, `ApogeePreviews/video/<name>`, made fresh with its
    /// empty `Movies/Apogee`.
    static func home(_ name: String) -> URL {
        let home = FileManager.default.temporaryDirectory.appendingPathComponent("ApogeePreviews/video/\(name)")
        try? FileManager.default.removeItem(at: home)
        try? FileManager.default.createDirectory(at: home.appendingPathComponent("Movies/Apogee"), withIntermediateDirectories: true)
        return home
    }

    /// A clip and its record, as the CLI leaves them: the record as
    /// tests/business/media/MediaTests.swift's `RecordFolder.clip` writes it,
    /// the clip a stand-in file, as there.
    @discardableResult
    static func clip(
        _ name: String, in folder: URL, prompt: String, created: String, parent: String? = nil, preset: String? = nil,
        output: Bool = true
    ) -> String {
        let clip = folder.appendingPathComponent(name).path
        if output { try? Data("mp4".utf8).write(to: URL(fileURLWithPath: clip)) }
        let parentJSON = parent.map { #"{"move":"from","document":"\#($0).json","clip":"\#($0)","continued":null}"# } ?? "null"
        let presetJSON = preset.map { "\"\($0)\"" } ?? "null"
        let record = #"{"object":"video.generation","status":"done","output":"\#(clip)","sidecar":"\#(clip).json","model":{"name":"wan-1.3b"},"prompt":"\#(prompt)","image":null,"mode":"t2v","seed":4242,"seed_source":"generated","settings":{"width":1280,"height":704,"duration":5,"fps":24,"frames":121,"steps":16},"codec":"h264","seconds":312.5,"created_at":"\#(created)","parent":\#(parentJSON),"preset":\#(presetJSON),"loras":[{"path":"/loras/film.safetensors","sha256":"ab","scale":0.8}]}"#
        try? record.write(toFile: clip + ".json", atomically: true, encoding: .utf8)
        return clip
    }

    /// The library MediaTests' scan reads: a draft, and the final run again
    /// from it -- and, when `gone`, a record whose clip is no longer there --
    /// with `selecting` picked.
    static func library(
        _ name: String, gone: Bool = false, selecting: String? = nil, starter: any JobStarting = PreviewJobs()
    ) -> VideoViewModel {
        let home = home(name)
        let folder = home.appendingPathComponent("Movies/Apogee")
        let draft = clip("lighthouse-draft.mp4", in: folder, prompt: "a lighthouse at dusk", created: "2026-10-20T10:00:00Z", preset: "draft")
        clip("lighthouse.mp4", in: folder, prompt: "a lighthouse at dusk", created: "2026-10-20T11:00:00Z", parent: draft, preset: "final")
        if gone { clip("gone.mp4", in: folder, prompt: "x", created: "2026-10-20T09:00:00Z", output: false) }
        let model = connected(home, starter: starter)
        if let picked = selecting.flatMap({ clip($0, of: model) }) { model.select(picked) }
        return model
    }

    /// The library, a clip generated from the sheet's prompt (the module's
    /// tests' own) on a 52b build, its run going on `starter`'s job.
    static func generating(_ name: String, starter: PreviewJobs) -> VideoViewModel {
        let model = library(name, starter: starter)
        model.streams = { true }
        model.newClip()
        model.form.prompt = "A lighthouse at dusk"
        model.generate()
        return model
    }

    /// A library with nothing in it yet.
    static func empty(_ name: String, starter: PreviewJobs = PreviewJobs()) -> VideoViewModel {
        connected(home(name), starter: starter)
    }

    private static func connected(_ home: URL, starter: any JobStarting) -> VideoViewModel {
        let model = VideoViewModel(starter: starter, preferences: Preferences(store: MemoryStore()), home: home)
        model.connect(binary: previewBinary)
        return model
    }

    /// The clip with this file name, as the library read it.
    static func clip(_ name: String, of model: VideoViewModel) -> ClipRecord? {
        model.clips.first { $0.name == name }
    }

    /// A 52b run's step, as tests/presentation/video's streaming test says it.
    static let step: [JobUpdate] = [.event(.step(step: 3, steps: 50, scene: nil))]

    // MARK: - The runtime's install (GUI 4f)

    /// `video generate` refused for its runtime as the binary refused it over
    /// a sandbox with no torch (tests/business/media/Fixtures/
    /// generate-refused-runtime.*): its 52b stream's one `job` line, the
    /// refusal on stderr alone.
    static let refusedRun = PreviewRun(
        stdout: VideoDocuments.generateRefused, streaming: true, status: 1, stderr: VideoDocuments.generateRefusedErr)

    /// `train setup --with video` as the binary ran it over that sandbox's
    /// stub pip (tests/business/media/Fixtures/install-video-*): still going,
    /// its first two lines said; failed; and done.
    static let installRunning = PreviewRun(
        stdout: VideoDocuments.installVideoDone.split(separator: "\n").prefix(2).joined(separator: "\n"), finishes: false)
    static let installFailed = PreviewRun(
        stdout: VideoDocuments.installVideoFailed, status: 1, stderr: VideoDocuments.installVideoFailedErr)
    static let installDone = PreviewRun(stdout: VideoDocuments.installVideoDone)

    /// A starter replaying `runs`, one a start.
    static func runs(_ runs: [PreviewRun]) -> PreviewRuns { PreviewRuns(runs) }

    /// The clip the recording asked for, on a 52b build, refused for its
    /// runtime: the consent standing.
    static func refuse(_ model: VideoViewModel) async {
        model.streams = { true }
        model.newClip()
        model.form.prompt = "a heron lands on a pier at dusk"
        model.generate()
        await until { model.consent != nil }
    }

    /// Refused, then the person's yes: the install on the starter's next run.
    static func install(_ model: VideoViewModel) async {
        await refuse(model)
        model.install()
    }

    /// Waits, a few seconds at most, for what a run's end sets.
    static func until(_ condition: @MainActor () -> Bool) async {
        for _ in 0..<250 {
            if condition() { return }
            try? await Task.sleep(for: .milliseconds(20))
        }
    }

    /// How a run ended: `status` 0 is done, any other a failure.
    static func ended(_ status: Int32) -> JobUpdate {
        .ended(JobEnd(exit: ChildExit(reason: .exited, status: status), stderrTail: BoundedTail(), interrupted: false))
    }

    /// A run's progress, folded from what it said.
    static func progress(_ said: [JobUpdate]) -> JobProgress {
        var progress = JobProgress()
        said.forEach { progress.apply($0) }
        return progress
    }

    // MARK: - The store's video models and their engines (the CLI's 53l)

    static let mlxVideo = "Wan2.2-TI2V-5B/mlx/5e6f708192a3"
    static let pipeline = "Wan2.2-TI2V-5B/pipeline/9b1e47c0a2f6"
    static let smallPipeline = "Wan2.1-T2V-1.3B/pipeline/7c21e0d4b9a3"

    /// The store recorded from the binary built with the CLI's 53l and 53m
    /// (tests/business/reads/Fixtures/media-*): a model folder holding a
    /// diffusers pipeline beside its MLX video model, a second pipeline, and
    /// the MLX one made the default.
    static let store = PreviewRunner(
        reads: [(["models", "list"], VideoDocuments.mediaList)],
        commands: [(["config", "get", "models.default_video"], VideoDocuments.defaultVideo)])

    /// The same store before any model was made the default: `config get
    /// models.default_video` printing nothing.
    static let storeNoDefault = PreviewRunner(
        reads: [(["models", "list"], VideoDocuments.mediaList)],
        commands: [(["config", "get", "models.default_video"], VideoDocuments.defaultVideoUnset)])

    /// The store's video models read into `model`, as coming into view does.
    static func reading(_ model: VideoViewModel, over runner: PreviewRunner = store) async {
        model.reader = runner
        await model.readModels()
    }

    /// The sheet on a new clip of the module's tests' prompt, over the
    /// store, `choice` picked as a person picks it.
    static func newClip(_ model: VideoViewModel, choosing choice: VideoViewModel.ModelChoice, over runner: PreviewRunner = store) async {
        await reading(model, over: runner)
        model.newClip()
        model.form.prompt = "A lighthouse at dusk"
        model.choose(choice)
    }

    /// A record the binary wrote on each engine (tests/business/media/
    /// Fixtures/clip-<engine>.json), written into `folder` as it left it --
    /// its media folder this preview's -- beside a stand-in clip.
    @discardableResult
    static func recorded(_ document: String, in folder: URL) -> String {
        let text = document.replacingOccurrences(of: "/Users/someone/Movies/Apogee", with: folder.path)
        guard let output = (try? JSONSerialization.jsonObject(with: Data(text.utf8)) as? [String: Any])?["output"] as? String else {
            return ""
        }
        try? Data("mp4".utf8).write(to: URL(fileURLWithPath: output))
        try? text.write(toFile: output + ".json", atomically: true, encoding: .utf8)
        return output
    }

    /// The library holding the clips the binary made on PyTorch and on MLX,
    /// with `selecting` picked.
    static func engines(_ name: String, selecting: String? = nil) -> VideoViewModel {
        let home = home(name)
        let folder = home.appendingPathComponent("Movies/Apogee")
        recorded(VideoDocuments.clipTorch, in: folder)
        recorded(VideoDocuments.clipMLX, in: folder)
        let model = connected(home, starter: PreviewJobs())
        if let picked = selecting.flatMap({ clip($0, of: model) }) { model.select(picked) }
        return model
    }
}
#endif
