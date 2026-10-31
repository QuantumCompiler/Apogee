import BusinessJobs
import BusinessReads
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationLibrary

/// The binary's refusal recorded beside its exit (every one exits 1):
/// `tests/business/reads/Fixtures/<name>.err`, the home `/Users/someone`.
func refusal(_ name: String) -> (exit: ChildExit, tail: BoundedTail) {
    (ChildExit(reason: .exited, status: 1), tail(fixture(name, "err").trimmingCharacters(in: .newlines)))
}

/// The Library's buttons do what they say (GUI 4h): a save never loses what
/// was typed while it ran, a play's document lands on the symphony it
/// played, a failed read is said, and Register selects only what it made.
@Suite("Library correctness", .timeLimit(.minutes(1)))
@MainActor
struct LibraryCorrectnessTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    /// A throwaway data directory holding the person's own files, as `config
    /// path` would name it.
    func dataHome() throws -> URL {
        let home = FileManager.default.temporaryDirectory.appendingPathComponent("library-\(UUID().uuidString)")
        for folder in ["config", "prompts", "schemas", "symphonies"] {
            try FileManager.default.createDirectory(at: home.appendingPathComponent(folder), withIntermediateDirectories: true)
        }
        try "Sort the issue by area.".write(to: home.appendingPathComponent("prompts/triage.txt"), atomically: true, encoding: .utf8)
        return home
    }

    // MARK: - Saves

    @Test("an agent's save: the editor is read-only while it runs, and nothing typed is put back to the copy taken at Save")
    func agentSaveKeepsTyping() async throws {
        let home = try dataHome()
        defer { try? FileManager.default.removeItem(at: home) }
        let file = home.appendingPathComponent("prompts/triage.txt")
        let runner = ScriptedRunner()
        runner.reading(["agents", "list"], fixture("agents-list"))
        runner.answering(["config", "path"], .done(stdout: home.appendingPathComponent("config/config.json").path + "\n"))
        let model = AgentsViewModel(runner: runner)
        await model.connect(binary: Self.binary)
        model.edit(model.agents.first { $0.name == "triage" }!, file: "prompts/triage.txt")
        model.editor?.text = "Sort by area."
        let gate = runner.holding(["agents", "list"])
        let saving = Task { await model.save() }
        #expect(await eventually { gate.arrived == 1 })
        #expect(model.editor?.saving == true, "read-only while the save runs")
        model.editor?.text = "Sort by area, then urgency."
        gate.open()
        await saving.value
        #expect(try String(contentsOf: file, encoding: .utf8) == "Sort by area.")
        #expect(model.editor?.saving == false)
        #expect(model.editor?.text == "Sort by area, then urgency.", "what was typed is never reverted")
        #expect(model.editor?.verdict?.contains("isn't saved yet") == true, "and never called saved")
    }

    @Test("a symphony's save: read-only while the binary checks it, re-enabled with the saved text, a closed editor left closed")
    func symphonySaveKeepsTyping() async throws {
        let home = try dataHome()
        defer { try? FileManager.default.removeItem(at: home) }
        let path = home.appendingPathComponent("symphonies/notes-digest.yaml")
        try "description: Summarize then check.\n".write(to: path, atomically: true, encoding: .utf8)
        let list = fixture("symphonies-list").replacingOccurrences(of: "/Users/someone/.apogee/symphonies/notes-digest.yaml", with: path.path)
        let runner = ScriptedRunner()
        runner.reading(["symphonies", "list"], list)
        runner.reading(["symphonies", "show"], fixture("symphony-show"))
        let model = SymphoniesViewModel(runner: runner, starter: ScriptedJobs([]))
        await model.connect(binary: Self.binary)
        model.select(model.symphonies.first { $0.name == "notes-digest" }!)
        model.edit(model.current!)
        model.editor?.text = "description: Summarize, then check.\n"
        let gate = runner.holding(["symphonies", "show"])
        let saving = Task { await model.save() }
        #expect(await eventually { gate.arrived == 1 })
        #expect(model.editor?.saving == true, "read-only while the binary reads it")
        model.editor?.text = "description: Summarize, then check twice.\n"
        gate.open()
        await saving.value
        #expect(try String(contentsOf: path, encoding: .utf8) == "description: Summarize, then check.\n")
        #expect(model.editor?.text == "description: Summarize, then check twice.\n", "what was typed is never reverted")
        #expect(model.editor?.saving == false)

        // Saved with nothing typed meanwhile: re-enabled with the saved text.
        await model.save()
        #expect(model.editor?.text == "description: Summarize, then check twice.\n" && model.editor?.saving == false)
        #expect(model.editor?.verdict == "Saved, and apogee reads it.")

        // Closed while the binary reads it: the save doesn't bring it back.
        let held = runner.holding(["symphonies", "show"])
        let again = Task { await model.save() }
        #expect(await eventually { held.arrived == 1 })
        model.closeEditor()
        held.open()
        await again.value
        #expect(model.editor == nil)
    }

    // MARK: - Plays

    @Test("a play's document and its spinner stay on the symphony it played, whichever is in view when it ends")
    func playLandsWhereItStarted() async throws {
        let job = ScriptedJob()
        let runner = ScriptedRunner()
        runner.reading(["symphonies", "list"], fixture("symphonies-list"))
        let model = SymphoniesViewModel(runner: runner, starter: ScriptedJobs([job]))
        await model.connect(binary: Self.binary)
        let critique = try #require(model.symphonies.first { $0.name == "critique-revise" })
        let other = try #require(model.symphonies.first { $0.name != "critique-revise" })
        model.select(critique)
        model.input = "A draft README for the tool."
        model.playIt()
        #expect(model.shownProgress != nil)
        model.select(other)
        #expect(model.shownProgress == nil, "no spinner and no Stop on another symphony's page")
        #expect(model.isPlaying, "one play at a time: still running")
        job.say(.said(fixture("symphony-play")))
        job.end()
        #expect(await eventually { !model.isPlaying })
        #expect(model.shownPlay == nil, "the play's document never lands on the symphony in view")
        model.select(critique)
        #expect(model.shownPlay?.stages.count == 2, "it is the played symphony's")
    }

    // MARK: - Failed reads

    @Test("a failed agents or symphonies list is said where the list is, the rows read before kept")
    func listFailuresSaid() async throws {
        let runner = ScriptedRunner()
        runner.reading(["agents", "list"], fixture("agents-list"))
        runner.reading(["symphonies", "list"], fixture("symphonies-list"))
        let agents = AgentsViewModel(runner: runner)
        let symphonies = SymphoniesViewModel(runner: runner, starter: ScriptedJobs([]))
        await agents.connect(binary: Self.binary)
        await symphonies.connect(binary: Self.binary)
        #expect(agents.emptyText == nil && symphonies.emptyText == nil)
        let bad = refusal("agents-list-bad-config")
        runner.failing(["agents", "list"], .failed(exit: bad.exit, stderrTail: bad.tail))
        let badSymphonies = refusal("symphonies-list-bad-config")
        runner.failing(["symphonies", "list"], .failed(exit: badSymphonies.exit, stderrTail: badSymphonies.tail))
        let agentsBefore = agents.agents.count
        await agents.refresh()
        await symphonies.refresh()
        #expect(agents.agents.count == agentsBefore && !symphonies.symphonies.isEmpty, "the rows already read stay, as Records and Tasks keep theirs")
        #expect(agents.emptyText == "The agents could not be read: apogee agents: /Users/someone/.apogee/config/config.json: not valid JSON: line 4, column 5: expected ',' or '}' after a member (exited with status 1)")
        #expect(symphonies.emptyText?.hasPrefix("The symphonies could not be read: apogee symphonies: /Users/someone/.apogee/config/config.json: not valid JSON") == true)
    }

    @Test("a wedged mcp list is said, not dropped; the presets' failure is said apart")
    func mcpFailuresSaid() async {
        let runner = ScriptedRunner()
        runner.reading(["mcp", "list"], fixture("mcp-list"))
        runner.reading(["mcp", "presets"], fixture("mcp-presets"))
        let model = MCPViewModel(runner: runner)
        model.use(binary: Self.binary)
        await model.refresh()
        #expect(model.serversText == nil && model.presetsText == nil)
        runner.failing(["mcp", "list"], .timedOut(after: .seconds(60), stderrTail: BoundedTail()))
        await model.refresh()
        #expect(model.servers.first?.name == "code", "the servers already read stay")
        #expect(model.serversText?.hasPrefix("The MCP servers could not be read: it did not finish within") == true)
        #expect(model.presetsText == nil)
        let bad = refusal("mcp-list-bad-config")
        runner.failing(["mcp", "presets"], .failed(exit: bad.exit, stderrTail: bad.tail))
        await model.refresh()
        #expect(model.presetsText?.hasPrefix("The presets could not be read: apogee mcp: ") == true)
    }

    @Test("with no apogee the Library's lists say so, as Records and Tasks do")
    func missingBinarySaid() async {
        let runner = ScriptedRunner()
        let agents = AgentsViewModel(runner: runner)
        let symphonies = SymphoniesViewModel(runner: runner, starter: ScriptedJobs([]))
        let mcp = MCPViewModel(runner: runner)
        await agents.connect(binary: nil)
        await symphonies.connect(binary: nil)
        mcp.use(binary: nil)
        await mcp.refresh()
        #expect(agents.emptyText == "Apogee isn't installed, so there are no agents to list.")
        #expect(symphonies.emptyText == "Apogee isn't installed, so there are no symphonies to list.")
        #expect(mcp.serversText == "Apogee isn't installed, so there are no MCP servers to list.")
        #expect(runner.calls.isEmpty && agents.failure == nil && symphonies.failure == nil && mcp.failure == nil)
    }

    // MARK: - Register

    @Test("Register selects the server only when the binary made it; a refusal keeps the selection and is said verbatim")
    func registerSelectsOnlyOnSuccess() async throws {
        let runner = ScriptedRunner()
        runner.reading(["mcp", "list"], fixture("mcp-list"))
        runner.reading(["mcp", "presets"], fixture("mcp-presets"))
        let unseeded = refusal("mcp-create-unseeded")
        runner.answering(["mcp", "create"], .failed(exit: unseeded.exit, stderrTail: unseeded.tail))
        let model = MCPViewModel(runner: runner)
        model.use(binary: Self.binary)
        await model.refresh()
        model.selected = "code"
        let preset = try #require(model.presets.first { $0.name == "code-exec" })
        let made = await model.register(preset, as: "python")
        #expect(!made)
        #expect(model.selected == "code", "the prior selection kept, its page still drawn")
        #expect(model.failure == "apogee mcp: the 'code-exec' preset cannot run here: code_exec_server.py is not seeded under /Users/someone/.apogee/training/scripts -- apogee check --fix (exited with status 1)")
        model.dismissFailure()
        model.selected = nil
        let taken = refusal("mcp-create-taken")
        runner.answering(["mcp", "create"], .failed(exit: taken.exit, stderrTail: taken.tail))
        #expect(!(await model.register(preset, as: "code")))
        #expect(model.selected == nil)
        #expect(model.failure?.hasPrefix("apogee mcp: updating config: MCP server 'code' already exists") == true)
        runner.answering(["mcp", "create"], .done(stdout: "registered MCP server 'python'\n"))
        #expect(await model.register(preset, as: " python "))
        #expect(model.selected == "python")
    }
}
