import BusinessMedia
import DataProtocol
import Foundation
import Testing

func project(_ json: String) -> ProjectRecord { ProjectRecord(JSONValue.parse(json)!)! }

let original = #"{"object":"video.project","version":1,"status":"done","brief":"how a heat pump works","title":"Heat pumps","output":"/m/heat.mp4","settings":{"voice":"af_heart","music":null},"scenes":[{"index":1,"narration":"Heat moves.","visual":"a thermometer","seconds":3,"asset":null,"seed":11,"voice":{"file":"/m/heat.scenes/1.wav"},"clip":{"file":"/m/heat.scenes/1.mp4"}},{"index":2,"narration":"The outdoor unit.","visual":"a box outside","seconds":3,"asset":null,"seed":12,"voice":{"file":"/m/heat.scenes/2.wav"},"clip":{"file":"/m/heat.scenes/2.mp4"}},{"index":3,"narration":"Warm air inside.","visual":"a room","seconds":3,"asset":null,"seed":13,"voice":{"file":"/m/heat.scenes/3.wav"},"clip":null}],"captions":{"file":"/m/heat.srt","burned":false},"created":"2026-10-20T10:00:00Z","parent":null,"edits":[]}"#

@Suite("Projects")
struct ProjectsTests {
    @Test("a project's scenes: spoken and rendered as the document says, its captions, its status")
    func read() {
        let p = project(original)
        #expect(p.status == "done" && p.title == "Heat pumps")
        #expect(p.scenes.map(\.isMade) == [true, true, false])
        #expect(p.made == 2)
        #expect(p.captionsBurned == false)
        #expect(!p.hasDiff)
        #expect(ProjectRecord(JSONValue.parse(#"{"object":"video.generation","output":"/x"}"#)!) == nil)
    }

    @Test("52c's own comparison is the diff: each scene's source and changes, what moved, what was removed")
    func diffFromTheDocument() {
        let edited = project(#"{"object":"video.project","status":"planned","brief":"b","output":"/m/heat-edit.mp4","parent":"/m/heat.mp4.json","scenes":[{"index":1,"narration":"Heat moves.","visual":"a thermometer","from":1,"changes":[]},{"index":2,"narration":"The indoor unit on the wall.","visual":"a wall unit","from":2,"changes":["narration","prompt"],"moved":false},{"index":3,"narration":"A new ending.","visual":"x","from":"new","changes":[]}],"removed":[3],"edits":[{"instruction":"show the indoor unit","redo":[],"from":"/m/heat.mp4.json"}]}"#)
        #expect(edited.hasDiff)
        let diff = MediaLibrary.diff(edited: edited, parent: project(original))
        #expect(!diff.compared)
        #expect(diff.changes.map(\.kind) == [.unchanged, .changed(["narration", "prompt"]), .added, .removed])
        #expect(diff.changes.last?.narration == "Warm air inside.")
    }

    @Test("before 52c the diff is the two documents compared in order, and says so")
    func diffCompared() {
        let edited = project(#"{"object":"video.project","status":"planned","brief":"b","output":"/m/e.mp4","scenes":[{"index":1,"narration":"Heat moves.","visual":"a thermometer","seconds":3,"seed":11},{"index":2,"narration":"The indoor unit.","visual":"a box outside","seconds":3,"seed":99}]}"#)
        let diff = MediaLibrary.diff(edited: edited, parent: project(original))
        #expect(diff.compared)
        #expect(diff.changes.map(\.kind) == [.unchanged, .changed(["narration", "seed"]), .removed])
    }

    @Test("new, edit, redo-scene and resume are the commands a person types")
    func argv() {
        var form = ComposeForm()
        #expect(form.arguments(output: "/m/a.mp4") == nil)
        form.brief = "how a heat pump works"
        #expect(form.arguments(output: "/m/a.mp4") == ["video", "compose", "-o", "/m/a.mp4", "--", "how a heat pump works"])
        form.assets = "/m/stills"
        form.music = "/m/bed.mp3"
        form.captions = false
        form.voice = "bf_emma"
        form.model = "wan-5b"
        #expect(form.arguments(output: "/m/a.mp4", dryRun: true) == [
            "video", "compose", "-o", "/m/a.mp4", "--assets", "/m/stills", "--music", "/m/bed.mp3",
            "--no-captions", "--voice", "bf_emma", "-m", "wan-5b", "--dry-run", "--", "how a heat pump works",
        ])
        var edit = ComposeForm()
        edit.from = "/m/heat.mp4"
        #expect(edit.arguments(output: "/m/e.mp4") == nil, "an edit needs an instruction or a scene to redo")
        edit.instruction = "show the indoor unit"
        edit.redo = [3, 1]
        #expect(edit.arguments(output: "/m/e.mp4") == [
            "video", "compose", "--from", "/m/heat.mp4", "--edit", "show the indoor unit", "--redo-scene", "1", "--redo-scene", "3", "-o", "/m/e.mp4",
        ])
    }

    @Test("a project is read from beside its video, and listed even before its video exists")
    func beside() throws {
        let folder = FileManager.default.temporaryDirectory.appendingPathComponent("projects-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: folder) }
        let video = folder.appendingPathComponent("tide.mp4").path
        try original.replacingOccurrences(of: "/m/heat.mp4", with: video).replacingOccurrences(of: #""status":"done""#, with: #""status":"cancelled""#)
            .write(toFile: video + ".json", atomically: true, encoding: .utf8)
        #expect(ProjectRecord.beside(video)?.status == "cancelled")
        #expect(MediaLibrary.projects(in: [folder]).records.map(\.output) == [video], "a stopped composition with no video yet is listed")
    }
}
