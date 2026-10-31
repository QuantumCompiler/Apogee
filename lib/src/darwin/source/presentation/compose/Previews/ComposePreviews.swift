#if DEBUG
import BusinessJobs
import BusinessMedia
import DataProtocol
import Foundation
import Observation

/// Compositions' view-model for its previews (ADR 0004): a folder of its own
/// under the temporary folder, holding the project documents the module's
/// tests write. Never the person's folders, and nothing starts the binary.
@MainActor
enum ComposePreview {
    /// One preview's folder, `ApogeePreviews/compose/<name>`, made fresh.
    static func folder(_ name: String) -> URL {
        let folder = FileManager.default.temporaryDirectory.appendingPathComponent("ApogeePreviews/compose/\(name)")
        try? FileManager.default.removeItem(at: folder)
        try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        return folder
    }

    /// tests/business/media/ProjectsTests.swift's `original`: a brief made
    /// into three scenes, two of them made, its captions kept beside it.
    static let heatPumps = #"{"object":"video.project","version":1,"status":"done","brief":"how a heat pump works","title":"Heat pumps","output":"/m/heat.mp4","settings":{"voice":"af_heart","music":null},"scenes":[{"index":1,"narration":"Heat moves.","visual":"a thermometer","seconds":3,"asset":null,"seed":11,"voice":{"file":"/m/heat.scenes/1.wav"},"clip":{"file":"/m/heat.scenes/1.mp4"}},{"index":2,"narration":"The outdoor unit.","visual":"a box outside","seconds":3,"asset":null,"seed":12,"voice":{"file":"/m/heat.scenes/2.wav"},"clip":{"file":"/m/heat.scenes/2.mp4"}},{"index":3,"narration":"Warm air inside.","visual":"a room","seconds":3,"asset":null,"seed":13,"voice":{"file":"/m/heat.scenes/3.wav"},"clip":null}],"captions":{"file":"/m/heat.srt","burned":false},"created":"2026-10-20T10:00:00Z","parent":null,"edits":[]}"#

    /// ProjectsTests' edit of it, carrying 52c's own comparison: a scene
    /// changed, one new, one removed.
    static let heatPumpsEdited = #"{"object":"video.project","status":"planned","brief":"b","output":"/m/heat-edit.mp4","parent":"/m/heat.mp4.json","scenes":[{"index":1,"narration":"Heat moves.","visual":"a thermometer","from":1,"changes":[]},{"index":2,"narration":"The indoor unit on the wall.","visual":"a wall unit","from":2,"changes":["narration","prompt"],"moved":false},{"index":3,"narration":"A new ending.","visual":"x","from":"new","changes":[]}],"removed":[3],"edits":[{"instruction":"show the indoor unit","redo":[],"from":"/m/heat.mp4.json"}]}"#

    /// ProjectsTests' edit from a build before 52c: no comparison of its own.
    static let heatPumpsCompared = #"{"object":"video.project","status":"planned","brief":"b","output":"/m/e.mp4","scenes":[{"index":1,"narration":"Heat moves.","visual":"a thermometer","seconds":3,"seed":11},{"index":2,"narration":"The indoor unit.","visual":"a box outside","seconds":3,"seed":99}]}"#

    /// tests/presentation/compose/ComposeViewModelTests.swift's `projectJSON`:
    /// three scenes, the first `made` of them spoken and rendered.
    static func tides(_ output: String, status: String, made: Int) -> String {
        let scenes = (1...3).map { i in
            #"{"index":\#(i),"narration":"scene \#(i)","visual":"v\#(i)","seed":\#(i),"voice":\#(i <= made ? #"{"file":"/s/\#(i).wav"}"# : "null"),"clip":\#(i <= made ? #"{"file":"/s/\#(i).mp4"}"# : "null")}"#
        }
        return #"{"object":"video.project","status":"\#(status)","brief":"tides","title":"Tides","output":"\#(output)","scenes":[\#(scenes.joined(separator: ","))],"created":"2026-10-20T10:00:00Z","edits":[]}"#
    }

    /// The edit's dry run, as ComposeViewModelTests' edit test has the CLI
    /// print it: the edited project, its own comparison in it.
    static func tidesEditPlanned(_ output: String) -> [JobUpdate] {
        [
            .said(#"{"object":"video.project","status":"planned","brief":"tides","output":"\#(output)","scenes":["#),
            .said(#"{"index":1,"narration":"scene 1","visual":"v1","from":1,"changes":[]},{"index":2,"narration":"scene 2, brighter","visual":"v2 bright","from":2,"changes":["narration","prompt"]},{"index":3,"narration":"scene 3","visual":"v3","from":3,"changes":[]}]}"#),
        ]
    }

    /// A ProjectsTests document placed in `folder`: its `/m/` paths there, kept
    /// beside its video as `<name>.json` -- and, when `made`, a stand-in for
    /// the video itself.
    static func place(_ document: String, as name: String, in folder: URL, made: Bool = false) {
        let output = folder.appendingPathComponent(name).path
        try? document.replacingOccurrences(of: "/m/", with: folder.path + "/").write(toFile: output + ".json", atomically: true, encoding: .utf8)
        if made { try? Data("mp4".utf8).write(to: URL(fileURLWithPath: output)) }
    }

    /// A tides project placed in `folder` as `tides.mp4.json`; its video's path.
    @discardableResult
    static func placeTides(in folder: URL, status: String, made: Int) -> String {
        let output = folder.appendingPathComponent("tides.mp4").path
        try? tides(output, status: status, made: made).write(toFile: output + ".json", atomically: true, encoding: .utf8)
        return output
    }

    /// The view-model over `folder`, read, with `selecting` (a video's file
    /// name) picked; its store -- voice models and `check` -- answered by
    /// `runner`.
    static func compositions(
        _ folder: URL, selecting: String? = nil, starter: any JobStarting = PreviewJobs(), plansAlone: Bool = false,
        runner: PreviewRunner = store()
    ) -> ComposeViewModel {
        let model = ComposeViewModel(starter: starter)
        model.folders = { [folder] }
        model.plansAlone = { plansAlone }
        model.reader = runner
        model.connect(binary: previewBinary)
        if let picked = selecting.flatMap({ name in model.projects.first { $0.name == name } }) { model.select(picked) }
        return model
    }

    /// The heat-pump composition, made.
    static func made(_ name: String) -> ComposeViewModel {
        let folder = folder(name)
        place(heatPumps, as: "heat.mp4", in: folder, made: true)
        return compositions(folder, selecting: "heat.mp4")
    }

    /// The heat-pump composition and its edit, the edit picked.
    static func edited(_ name: String) -> ComposeViewModel {
        let folder = folder(name)
        place(heatPumps, as: "heat.mp4", in: folder, made: true)
        place(heatPumpsEdited, as: "heat-edit.mp4", in: folder)
        return compositions(folder, selecting: "heat-edit.mp4")
    }

    /// The tides composition, `made` scenes of it, in `status`.
    static func tidesComposition(_ name: String, status: String, made: Int) -> ComposeViewModel {
        let folder = folder(name)
        placeTides(in: folder, status: status, made: made)
        return compositions(folder, selecting: "tides.mp4")
    }

    /// The tides composition, one scene made, resumed on a 52b build: its
    /// second scene rendering, by the stream's scene and step events.
    static func rendering(_ name: String) -> ComposeViewModel {
        let folder = folder(name)
        placeTides(in: folder, status: "rendering", made: 1)
        let said: [JobUpdate] = [.event(.scene(scene: 2, scenes: 3, state: "rendering")), .event(.step(step: 3, steps: 50, scene: 2))]
        let model = compositions(folder, selecting: "tides.mp4", starter: PreviewJobs(said: said))
        model.streams = { true }
        if let project = model.selectedProject { model.resume(project) }
        return model
    }

    /// The tides composition, made, open in the edit sheet with the edit
    /// test's instruction; its dry run, when previewed, prints the edit test's
    /// planned project.
    static func editing(_ name: String) -> ComposeViewModel {
        let folder = folder(name)
        placeTides(in: folder, status: "done", made: 3)
        let planned = folder.appendingPathComponent("tides-edit.mp4").path
        let model = compositions(folder, selecting: "tides.mp4", starter: PreviewJobs(said: tidesEditPlanned(planned), finishes: true))
        if let project = model.selectedProject {
            model.edit(project)
            model.form.instruction = "make scene 2 brighter"
        }
        return model
    }

    /// A project read from one of the documents above, its paths as written.
    static func project(_ document: String) -> ProjectRecord? {
        JSONValue.parse(document).flatMap(ProjectRecord.init)
    }

    // MARK: - Recorded over a sandbox (GUI 4f)

    /// The store the branch's binary composed over (tests/business/reads/
    /// Fixtures/compose-*, check-captions-*): one voice model or two -- Kokoro
    /// as hexgrad publishes it beside its MLX build -- and `check` with its
    /// ffmpeg's captions row skipped (no libass) or passing.
    static func store(twoVoices: Bool = false, burns: Bool = false) -> PreviewRunner {
        PreviewRunner(
            reads: [
                (["models", "list"], twoVoices ? ComposeDocuments.composeVoicesList : ComposeDocuments.composeVoiceList),
                (["check"], burns ? ComposeDocuments.checkCaptionsOk : ComposeDocuments.checkCaptionsSkipped),
            ], commands: [])
    }

    /// The brief every recording composed.
    static let brief = "the tides, twice a day"

    /// The new sheet over that store, its store read, on the recordings' brief.
    static func sheet(_ name: String, twoVoices: Bool = false, burns: Bool = false, starter: any JobStarting = PreviewJobs())
        async -> ComposeViewModel
    {
        let model = compositions(folder(name), starter: starter, plansAlone: true, runner: store(twoVoices: twoVoices, burns: burns))
        await model.readStore()
        model.newComposition()
        model.form.brief = brief
        return model
    }

    /// `video compose` refused for its runtime as the binary refused it over
    /// a sandbox with no torch (tests/business/media/Fixtures/
    /// compose-refused-runtime.*): its 52b stream's one `job` line, the
    /// refusal on stderr alone.
    static let refusedRun = PreviewRun(
        stdout: ComposeDocuments.composeRefused, streaming: true, status: 1, stderr: ComposeDocuments.composeRefusedErr)

    /// The same composition refused by the branch binary since the CLI's M29
    /// (tests/business/media/Fixtures/compose-refused-runtime-m29.*): its
    /// stream ends on the refusal's `error`, stderr saying it too.
    static let refusedRunSaid = PreviewRun(
        stdout: ComposeDocuments.composeRefusedM29, streaming: true, status: 1, stderr: ComposeDocuments.composeRefusedM29Err)

    /// `train setup --with video` still going, its first two lines said
    /// (tests/business/media/Fixtures/install-video-done.txt).
    static let installRunning = PreviewRun(
        stdout: ComposeDocuments.installVideoDone.split(separator: "\n").prefix(2).joined(separator: "\n"), finishes: false)

    /// The recordings' brief composed and refused for its runtime -- as
    /// `run` refused it -- the consent standing, and, when `install`, the
    /// person's yes.
    static func refused(_ name: String, install: Bool = false, run: PreviewRun = refusedRun) async -> ComposeViewModel {
        let model = await sheet(name, starter: PreviewRuns([run, installRunning]))
        model.streams = { true }
        model.compose()
        await until { model.consent != nil }
        if install { model.install() }
        return model
    }

    /// Planned first, as the binary planned it on its mock planner
    /// (tests/business/media/Fixtures/compose-planned.*): the dry run's
    /// stream replayed, its kept project written beside the video -- and what
    /// the place said of it, into `said`.
    static func planned(_ name: String, said: @escaping @MainActor (String) -> Void) async -> ComposeViewModel {
        let model = await sheet(name, starter: PreviewRuns([PreviewRun(stdout: ComposeDocuments.composePlanned, streaming: true)]))
        model.streams = { true }
        model.say = said
        guard let output = model.plannedOutput?.path else { return model }
        model.compose(planFirst: true)
        try? ComposeDocuments.composePlannedProject
            .replacingOccurrences(of: "/Users/someone/Movies/Apogee/tides", with: (output as NSString).deletingPathExtension)
            .write(toFile: output + ".json", atomically: true, encoding: .utf8)
        await until { !model.isRunning }
        return model
    }

    /// The made tides composition in the edit sheet, its preview's dry run
    /// going -- the planner not answered yet.
    static func previewing(_ name: String) -> ComposeViewModel {
        let folder = folder(name)
        placeTides(in: folder, status: "done", made: 3)
        let model = compositions(folder, selecting: "tides.mp4")
        if let project = model.selectedProject {
            model.edit(project)
            model.form.instruction = "make scene 2 brighter"
        }
        Task { await model.previewEdit() }
        return model
    }

    /// A made composition beside two the scan passes over: a document cut
    /// short, and a project moved here from another folder, naming its old place.
    static func skipping(_ name: String) -> ComposeViewModel {
        let folder = folder(name)
        place(heatPumps, as: "heat.mp4", in: folder, made: true)
        try? Data(heatPumps.prefix(40).utf8).write(to: folder.appendingPathComponent("cut-short.mp4.json"))
        try? tides("/Users/someone/Elsewhere/tides.mp4", status: "done", made: 3)
            .write(to: folder.appendingPathComponent("tides.mp4.json"), atomically: true, encoding: .utf8)
        return compositions(folder, selecting: "heat.mp4")
    }

    /// Waits, a few seconds at most, for what a run's end sets.
    static func until(_ condition: @MainActor () -> Bool) async {
        for _ in 0..<250 {
            if condition() { return }
            try? await Task.sleep(for: .milliseconds(20))
        }
    }
}

/// What a place said, kept for a preview to show beside the view: the shell
/// draws the toast, which the module's own previews cannot.
@MainActor
@Observable
final class PreviewSaid {
    var last = ""
}
#endif
