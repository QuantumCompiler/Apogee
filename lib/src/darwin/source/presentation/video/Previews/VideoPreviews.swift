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
        _ name: String, gone: Bool = false, selecting: String? = nil, starter: PreviewJobs = PreviewJobs()
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

    private static func connected(_ home: URL, starter: PreviewJobs) -> VideoViewModel {
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

    /// The refusal of a run whose runtime is not installed, in the CLI's words
    /// (tests/presentation/video/VideoViewModelTests.swift's consent test).
    static let refusal = "apogee video: the video runtime is not installed -- run: apogee train setup --with video"

    /// A 52b run refused for its runtime: the stream's error, then its end.
    static let refused = PreviewJobs(said: [.event(.error(refusal))], finishes: true, status: 1)

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
}
#endif
