import BusinessJobs
import BusinessMedia
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationCompose

@Suite("ComposeViewModel", .timeLimit(.minutes(1)))
@MainActor
struct ComposeViewModelTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func folder() throws -> URL {
        let folder = FileManager.default.temporaryDirectory.appendingPathComponent("compose-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        return folder
    }

    func projectJSON(_ output: String, status: String, made: Int) -> String {
        let scenes = (1...3).map { i in
            #"{"index":\#(i),"narration":"scene \#(i)","visual":"v\#(i)","seed":\#(i),"voice":\#(i <= made ? #"{"file":"/s/\#(i).wav"}"# : "null"),"clip":\#(i <= made ? #"{"file":"/s/\#(i).mp4"}"# : "null")}"#
        }
        return #"{"object":"video.project","status":"\#(status)","brief":"tides","title":"Tides","output":"\#(output)","scenes":[\#(scenes.joined(separator: ","))],"created":"2026-10-20T10:00:00Z","edits":[]}"#
    }

    @Test("a brief composes; its scenes are read from the project document as the composer saves it")
    func composeReadsTheProject() async throws {
        let folder = try folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let job = ScriptedJob()
        let jobs = ScriptedJobs([job])
        let model = ComposeViewModel(starter: jobs, pollInterval: .milliseconds(20))
        model.folders = { [folder] }
        model.connect(binary: Self.binary)
        model.newComposition()
        model.form.brief = "Tides, explained"
        let out = folder.appendingPathComponent("tides-explained.mp4").path
        #expect(model.plannedArguments == ["video", "compose", "-o", out, "--", "Tides, explained"])
        model.compose(planFirst: true)
        #expect(jobs.starts[0].arguments == ["video", "compose", "-o", out, "--", "Tides, explained"], "plan first needs 52c: one run")
        try projectJSON(out, status: "rendering", made: 1).write(toFile: out + ".json", atomically: true, encoding: .utf8)
        #expect(await eventually { model.selectedProject?.made == 1 })
        try projectJSON(out, status: "rendering", made: 2).write(toFile: out + ".json", atomically: true, encoding: .utf8)
        #expect(await eventually { model.selectedProject?.made == 2 }, "re-read as it moves")
        await model.stop()
        try projectJSON(out, status: "cancelled", made: 2).write(toFile: out + ".json", atomically: true, encoding: .utf8)
        #expect(await eventually { !model.isRunning })
        #expect(model.selectedProject?.status == "cancelled")
        #expect(model.selectedProject?.made == 2, "the finished scenes stand")
    }

    @Test("stopped, it resumes by the same command; planned, it renders by it")
    func resume() async throws {
        let folder = try folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let first = ScriptedJob()
        let second = ScriptedJob()
        let jobs = ScriptedJobs([first, second])
        let model = ComposeViewModel(starter: jobs, pollInterval: .milliseconds(20))
        model.folders = { [folder] }
        model.plansAlone = { true }
        model.connect(binary: Self.binary)
        model.newComposition()
        model.form.brief = "tides"
        model.compose(planFirst: true)
        let out = folder.appendingPathComponent("tides.mp4").path
        #expect(jobs.starts[0].arguments == ["video", "compose", "-o", out, "--dry-run", "--", "tides"])
        try projectJSON(out, status: "planned", made: 0).write(toFile: out + ".json", atomically: true, encoding: .utf8)
        first.end()
        #expect(await eventually { !model.isRunning && model.selectedProject?.status == "planned" })
        model.resume(model.selectedProject!)
        #expect(jobs.starts[1].arguments == ["video", "compose", "-o", out, "--", "tides"], "the same command, without the dry run")
        second.end()
    }

    @Test("an edit is previewed by its dry run, its diff shown, then rendered by the same command")
    func edit() async throws {
        let folder = try folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let parentOut = folder.appendingPathComponent("tides.mp4").path
        try projectJSON(parentOut, status: "done", made: 3).write(toFile: parentOut + ".json", atomically: true, encoding: .utf8)
        let preview = ScriptedJob()
        let render = ScriptedJob()
        let jobs = ScriptedJobs([preview, render])
        let model = ComposeViewModel(starter: jobs, pollInterval: .milliseconds(20))
        model.folders = { [folder] }
        model.connect(binary: Self.binary)
        let parent = try #require(model.projects.first)
        model.edit(parent)
        model.form.instruction = "make scene 2 brighter"
        let out = folder.appendingPathComponent("tides-edit.mp4").path
        let previewing = Task { await model.previewEdit() }
        #expect(await eventually { jobs.starts.count == 1 })
        #expect(jobs.starts[0].arguments == ["video", "compose", "--from", parentOut, "--edit", "make scene 2 brighter", "-o", out, "--dry-run", "--output-format", "json"])
        preview.say(.said(#"{"object":"video.project","status":"planned","brief":"tides","output":"\#(out)","scenes":["#))
        preview.say(.said(#"{"index":1,"narration":"scene 1","visual":"v1","from":1,"changes":[]},{"index":2,"narration":"scene 2, brighter","visual":"v2 bright","from":2,"changes":["narration","prompt"]},{"index":3,"narration":"scene 3","visual":"v3","from":3,"changes":[]}]}"#))
        preview.end()
        await previewing.value
        #expect(model.previewDiff?.compared == false)
        #expect(model.previewDiff?.changes.map(\.kind) == [.unchanged, .changed(["narration", "prompt"]), .unchanged])
        model.renderEdit()
        #expect(jobs.starts[1].arguments == ["video", "compose", "--from", parentOut, "--edit", "make scene 2 brighter", "-o", out])
        render.end()
    }
}
