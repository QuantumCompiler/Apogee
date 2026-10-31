import BusinessJobs
import BusinessReads
import Foundation
import Testing

@testable import PresentationLibrary

func fixture(_ name: String, _ ext: String = "json") -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
        .appendingPathComponent("../../business/reads/Fixtures/\(name).\(ext)")
    return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
}

@Suite("Library view models", .timeLimit(.minutes(1)))
@MainActor
struct LibraryViewModelTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    /// A throwaway data directory holding the agent's files, as `config path`
    /// would name it.
    func dataHome() throws -> URL {
        let home = FileManager.default.temporaryDirectory.appendingPathComponent("library-\(UUID().uuidString)")
        for folder in ["config", "prompts", "schemas", "symphonies"] {
            try FileManager.default.createDirectory(at: home.appendingPathComponent(folder), withIntermediateDirectories: true)
        }
        try "Sort the issue by area.".write(to: home.appendingPathComponent("prompts/triage.txt"), atomically: true, encoding: .utf8)
        try "{}".write(to: home.appendingPathComponent("schemas/triage-output.json"), atomically: true, encoding: .utf8)
        return home
    }

    @Test("agents: create as the scaffold takes it; delete with or without the files; a shipped agent's files read only")
    func agentsVerbs() async throws {
        var form = AgentsViewModel.CreateForm()
        #expect(form.arguments == nil)
        form.name = "triage"
        form.description = "Sort incoming issues"
        form.tools = "none"
        form.noSchema = true
        #expect(form.arguments == ["agents", "create", "triage", "--description", "Sort incoming issues", "--tools", "none", "--no-schema"])
        let home = try dataHome()
        defer { try? FileManager.default.removeItem(at: home) }
        let runner = ScriptedRunner()
        runner.reading(["agents", "list"], fixture("agents-list"))
        runner.answering(["agents"], .done(stdout: "done\n"))
        runner.answering(["config", "path"], .done(stdout: home.appendingPathComponent("config/config.json").path + "\n"))
        let model = AgentsViewModel(runner: runner)
        await model.connect(binary: Self.binary)
        let triage = try #require(model.agents.first { $0.name == "triage" })
        model.pendingDelete = triage
        await model.confirmDelete(purge: false)
        #expect(runner.calls.contains("run agents|delete|triage|--keep-files"))
        model.pendingDelete = triage
        await model.confirmDelete(purge: true)
        #expect(runner.calls.contains("run agents|delete|triage|--purge"))
        model.edit(model.agents.first { $0.bundled }!, file: "prompts/security-review.txt")
        #expect(model.editor == nil, "a shipped agent's files are Apogee's")
    }

    @Test("an agent's own file edits in place: written where the CLI reads it, read back, its check said honestly")
    func agentEdit() async throws {
        let home = try dataHome()
        defer { try? FileManager.default.removeItem(at: home) }
        let runner = ScriptedRunner()
        runner.reading(["agents", "list"], fixture("agents-list"))
        runner.answering(["config", "path"], .done(stdout: home.appendingPathComponent("config/config.json").path + "\n"))
        let model = AgentsViewModel(runner: runner)
        await model.connect(binary: Self.binary)
        model.edit(model.agents.first { $0.name == "triage" }!, file: "prompts/triage.txt")
        #expect(model.editor?.text == "Sort the issue by area.")
        model.editor?.text = "Sort the issue by area, then by urgency."
        await model.save()
        #expect(try String(contentsOf: home.appendingPathComponent("prompts/triage.txt"), encoding: .utf8) == "Sort the issue by area, then by urgency.")
        #expect(model.editor?.verdict?.hasPrefix("Saved. Apogee checks an agent's prompt and schema when it runs") == true)
        #expect(runner.calls.filter { $0 == "read agents|list" }.count == 2)
    }

    @Test("a play is symphonies play as typed, supervised; its stages and output read from its document")
    func play() async {
        let job = ScriptedJob()
        let runner = ScriptedRunner()
        runner.reading(["symphonies", "list"], fixture("symphonies-list"))
        let starter = ScriptedJobs([job])
        let model = SymphoniesViewModel(runner: runner, starter: starter)
        await model.connect(binary: Self.binary)
        model.select(model.symphonies.first { $0.name == "critique-revise" }!)
        model.input = "A draft README for the tool."
        model.playIt()
        #expect(starter.starts == [["symphonies", "play", "critique-revise", "--input", "A draft README for the tool.", "--output-format", "json"]])
        job.say(.busy("stage 1/2 critique — asking utility (writer): Below is a draft. Work out what it is meant to do -- from… · 0.0 s"))
        job.say(.said(fixture("symphony-play")))
        job.end()
        #expect(await eventually { !model.isPlaying })
        #expect(model.play?.stages.count == 2 && model.play?.output.hasPrefix("A tighter draft") == true)
    }

    @Test("an own spec file saves then asks the binary; its refusal said verbatim and the file put back on asking")
    func symphonyEdit() async throws {
        let home = try dataHome()
        defer { try? FileManager.default.removeItem(at: home) }
        let path = home.appendingPathComponent("symphonies/notes-digest.yaml")
        try "description: Summarize then check.\n".write(to: path, atomically: true, encoding: .utf8)
        let list = fixture("symphonies-list").replacingOccurrences(of: "/Users/someone/.apogee/symphonies/notes-digest.yaml", with: path.path)
        let runner = ScriptedRunner()
        runner.reading(["symphonies", "list"], list)
        runner.failing(["symphonies", "show"], .failed(exit: .init(reason: .exited, status: 1), stderrTail: tail("apogee symphonies: \(path.path): not valid YAML: yaml-cpp: error at line 3, column 1: end of sequence flow not found")))
        let model = SymphoniesViewModel(runner: runner, starter: ScriptedJobs([]))
        await model.connect(binary: Self.binary)
        model.select(model.symphonies.first { $0.name == "notes-digest" }!)
        model.edit(model.symphonies.first { $0.name == "critique-revise" }!)
        #expect(model.editor == nil, "a shipped starter is edited at a prompt")
        model.edit(model.current!)
        model.editor?.text = "stages: [\n"
        await model.save()
        #expect(try String(contentsOf: path, encoding: .utf8) == "stages: [\n")
        #expect(model.editor?.refused == true && model.editor?.verdict?.contains("not valid YAML") == true)
        await model.restore()
        #expect(try String(contentsOf: path, encoding: .utf8) == "description: Summarize then check.\n")
    }

    @Test("MCP: listing waits for the place; a preset registers byte-identically; enable, disable and a tool tried as typed")
    func mcp() async {
        let runner = ScriptedRunner()
        runner.reading(["mcp", "list"], fixture("mcp-list"))
        runner.reading(["mcp", "presets"], fixture("mcp-presets"))
        runner.answering(["mcp"], .done(stdout: "done\n"))
        runner.answering(["mcp", "test"], .done(stdout: "42\n"))
        let model = MCPViewModel(runner: runner)
        model.use(binary: Self.binary)
        #expect(runner.calls.isEmpty, "nothing connects at launch")
        await model.refresh()
        #expect(model.servers.first?.state == "connected")
        await model.register(model.presets.first { $0.name == "code-exec" }!, as: "code")
        #expect(runner.calls.contains("run mcp|create|code|--from|code-exec"))
        await model.setEnabled(model.servers[0], false)
        #expect(runner.calls.contains("run mcp|disable|code"))
        model.selected = "code"
        model.tool = "run_python"
        model.arguments = #"{"code": "print(6 * 7)"}"#
        await model.tryTool()
        #expect(runner.calls.last == #"run mcp|test|code|run_python|{"code": "print(6 * 7)"}"#)
        #expect(model.tried == "42")
    }
}
