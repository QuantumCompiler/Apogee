import BusinessJobs
import BusinessMedia
import DataProtocol
import Foundation
import Testing

@testable import PresentationCompose

/// A person's brief is never an option (GUI 4g): it runs after `--`, and
/// every flag the place adds -- the plan's `--dry-run`, the stream's
/// `--output-format stream-json`, the preview's `--output-format json` -- goes
/// before it, where the binary reads it.
@Suite("Compose text after --", .timeLimit(.minutes(1)))
@MainActor
struct ComposeTextTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func folder() throws -> URL {
        let folder = FileManager.default.temporaryDirectory.appendingPathComponent("compose-text-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        return folder
    }

    @Test("a dash-leading brief, planned first then rendered: the plan's and the stream's flags before --, the brief after it")
    func dashBrief() async throws {
        let folder = try folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let plan = ScriptedJob()
        let render = ScriptedJob()
        let jobs = ScriptedJobs([plan, render])
        let model = ComposeViewModel(starter: jobs, pollInterval: .milliseconds(20))
        model.folders = { [folder] }
        model.plansAlone = { true }
        model.streams = { true }
        model.connect(binary: Self.binary)
        model.newComposition()
        model.form.brief = "-m cats brief"
        let out = folder.appendingPathComponent("m-cats-brief.mp4").path
        #expect(model.plannedArguments == ["video", "compose", "-o", out, "--", "-m cats brief"])
        model.compose(planFirst: true)
        #expect(jobs.starts[0].arguments == ["video", "compose", "-o", out, "--dry-run", "--output-format", "stream-json", "--", "-m cats brief"])
        plan.end()
        #expect(await eventually { !model.isRunning })
        model.resume(ProjectRecord.planned(out, brief: "-m cats brief"))
        #expect(jobs.starts[1].arguments == ["video", "compose", "-o", out, "--output-format", "stream-json", "--", "-m cats brief"],
                "rendered by the same command, without the dry run")
        render.end()
    }

    @Test("a brief that reads like a flag is no plan: only an option --dry-run plans")
    func briefIsNotAFlag() async throws {
        let folder = try folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let job = ScriptedJob()
        let jobs = ScriptedJobs([job])
        let model = ComposeViewModel(starter: jobs, pollInterval: .milliseconds(20))
        var said: [String] = []
        model.say = { said.append($0) }
        model.folders = { [folder] }
        model.connect(binary: Self.binary)
        model.newComposition()
        model.form.brief = "--dry-run"
        model.compose()
        let out = folder.appendingPathComponent("dry-run.mp4").path
        #expect(jobs.starts[0].arguments == ["video", "compose", "-o", out, "--", "--dry-run"])
        job.end()
        #expect(await eventually { said == ["Composed dry-run.mp4"] }, "\(said)")
    }

    @Test("a project the app did not run resumes by its brief after --")
    func resumeArguments() {
        let project = ProjectRecord.planned("/m/a.mp4", brief: "--voice nobody")
        #expect(ComposeViewModel.resumeArguments(project) == ["video", "compose", "-o", "/m/a.mp4", "--", "--voice nobody"])
    }
}

extension ProjectRecord {
    /// A planned project, as the composer's document says one.
    static func planned(_ output: String, brief: String) -> ProjectRecord {
        let json = #"{"object":"video.project","status":"planned","brief":\#(JSONString(brief)),"title":"T","output":"\#(output)","scenes":[],"created":"2026-10-20T10:00:00Z","edits":[]}"#
        return ProjectRecord(JSONValue.parse(json)!)!
    }
}

/// `text` as a JSON string literal.
private func JSONString(_ text: String) -> String {
    String(data: try! JSONSerialization.data(withJSONObject: text, options: .fragmentsAllowed), encoding: .utf8)!
}
