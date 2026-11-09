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

/// A fixture recorded from the build tree's binary over a scratch home, by
/// its path under tests/ (GUI 6j: an edit's dry runs on a mock planner).
private func recorded(_ path: String) -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("../../\(path)")
    return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
}

/// GUI 6j: the CLI's edit dry run writes nothing (its 52c), so Render plans
/// the edit again. Recorded over a scratch home on a mock planner: the edit
/// previewed (`compose-edit-preview.*`), the same command planned again with
/// the planner answering otherwise (`compose-edit-replanned.json`), and a
/// re-roll alone, twice (`compose-edit-redo*`), which calls no planner.
@Suite("EditPlansAgain")
struct EditPlansAgainTests {
    func fixture(_ name: String) throws -> ProjectRecord {
        try #require(JSONValue.parse(recorded("business/media/Fixtures/\(name)")).flatMap(ProjectRecord.init))
    }

    /// `name`'s document changed by `change`, as a later run would write it.
    func derived(_ name: String, _ change: (inout [String: Any]) -> Void) throws -> ProjectRecord {
        var document = try #require(
            JSONSerialization.jsonObject(with: Data(recorded("business/media/Fixtures/\(name)").utf8)) as? [String: Any])
        change(&document)
        let text = String(decoding: try JSONSerialization.data(withJSONObject: document), as: UTF8.self)
        return try #require(JSONValue.parse(text).flatMap(ProjectRecord.init))
    }

    /// The preview's document as its render saves it: done, every scene spoken and rendered.
    func rendered(_ name: String) throws -> ProjectRecord {
        try derived(name) { document in
            document["status"] = "done"
            document["scenes"] = (document["scenes"] as? [[String: Any]] ?? []).map { scene in
                var scene = scene
                let index = scene["index"] as? Int ?? 0
                scene["voice"] = ["file": "/Users/someone/Movies/Apogee/tides-edit.scenes/\(index).wav"]
                scene["clip"] = ["file": "/Users/someone/Movies/Apogee/tides-edit.scenes/\(index).mp4"]
                return scene
            }
        }
    }

    @Test("the planner is read from plan.backend -- the one the dry run called -- and absent, none")
    func plannerRead() throws {
        let preview = try fixture("compose-edit-preview.json")
        #expect(preview.planner == "planner")
        #expect(recorded("business/media/Fixtures/compose-edit-preview.err").contains("with planner: \"make the second scene the moon\""),
            "the dry run said it planned with that backend")
        #expect(project(original).planner == nil, "a document with no plan names none")
        #expect(ProjectRecord(JSONValue.parse(#"{"object":"video.project","output":"/m/x.mp4","plan":{"backend":""}}"#)!)?.planner == nil)
    }

    @Test("a re-roll alone calls no planner: its document still names the parent's, its dry run never says it edited with one")
    func redoAloneCallsNoPlanner() throws {
        #expect(try fixture("compose-edit-redo.json").planner == "planner", "inherited from the parent")
        #expect(!recorded("business/media/Fixtures/compose-edit-redo.err").contains("editing"))
        #expect(recorded("business/media/Fixtures/compose-edit-redo.err").contains("scene 2; a new seed"))
    }

    @Test("the same changes: the preview and its render, which only made the scenes")
    func same() throws {
        let preview = try fixture("compose-edit-preview.json")
        #expect(MediaLibrary.sameChanges(try rendered("compose-edit-preview.json"), as: preview))
    }

    @Test("planned again, another scene changed: they differ")
    func changedSet() throws {
        let preview = try fixture("compose-edit-preview.json")
        let replanned = try fixture("compose-edit-replanned.json")
        #expect(preview.scenes.map(\.changes) == [[], ["prompt"]])
        #expect(replanned.scenes.map(\.changes) == [["narration"], []], "the same command, the planner answering otherwise")
        #expect(!MediaLibrary.sameChanges(replanned, as: preview))
    }

    @Test("planned again, the same scenes in another order: they differ")
    func reorder() throws {
        let preview = try fixture("compose-edit-preview.json")
        let reordered = try derived("compose-edit-preview.json") { document in
            let scenes = (document["scenes"] as? [[String: Any]] ?? []).reversed().enumerated().map { at, scene in
                var scene = scene
                scene["index"] = at + 1
                scene["moved"] = at == 0
                return scene
            }
            document["scenes"] = scenes
        }
        #expect(reordered.scenes.map(\.from) == ["2", "1"])
        #expect(!MediaLibrary.sameChanges(reordered, as: preview))
    }

    @Test("planned again, a scene removed: they differ")
    func removal() throws {
        let preview = try fixture("compose-edit-preview.json")
        let removed = try derived("compose-edit-preview.json") { document in
            document["scenes"] = Array((document["scenes"] as? [[String: Any]] ?? []).prefix(1))
            document["removed"] = [2]
        }
        #expect(removed.removed == [2])
        #expect(!MediaLibrary.sameChanges(removed, as: preview))
        let removedOnly = try derived("compose-edit-preview.json") { $0["removed"] = [2] }
        #expect(!MediaLibrary.sameChanges(removedOnly, as: preview), "the scenes left out are compared too")
    }

    @Test("a re-roll run twice: the seeds differ, the words match -- the same changes")
    func seedOnly() throws {
        let first = try fixture("compose-edit-redo.json")
        let again = try fixture("compose-edit-redo-again.json")
        #expect(first.scenes.map(\.seed) != again.scenes.map(\.seed), "a fresh seed each run")
        #expect(MediaLibrary.sameChanges(again, as: first))
    }
}
