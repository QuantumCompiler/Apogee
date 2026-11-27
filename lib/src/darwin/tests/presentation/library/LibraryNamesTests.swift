import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationLibrary

private func recorded(_ name: String) -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("Fixtures/\(name)")
    return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
}

private func failed(_ status: Int32, _ stderr: String) -> CommandOutcome {
    .failed(exit: ChildExit(reason: .exited, status: status), stderrTail: tail(stderr.trimmingCharacters(in: .newlines)))
}

/// CLI11's refusal of a dash-led name before `--`: the verb never ran.
private let required = failed(106, "name is required\nRun with --help for more information.")

/// Library's rows reach their verbs by their names (GUI M23). A config keeps
/// an agent, a symphony and an MCP server named `-x`, `-h` and `--graph`,
/// hand-written (`Fixtures/*-dashes.json`, recorded from the built CLI over
/// such a sandbox): before `--` the parser refused every one, and a typed
/// `-h` in the create form printed the verb's help with exit 0, which the app
/// toasted as success. Each name before `--` is scripted to answer CLI11's
/// refusal, and after `--` the binary's recorded answer.
@Suite("Library names", .timeLimit(.minutes(1)))
@MainActor
struct LibraryNamesTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func runner() -> ScriptedRunner {
        let runner = ScriptedRunner()
        runner.answering(["config", "path"], .done(stdout: "/Users/someone/.apogee/config/config.json\n"))
        runner.reading(["agents", "list"], recorded("agents-list-dashes.json"))
        runner.reading(["symphonies", "list"], recorded("symphonies-list-dashes.json"))
        runner.reading(["mcp", "list"], recorded("mcp-list-dashes.json"))
        runner.reading(["mcp", "presets"], recorded("mcp-presets.json"))
        for name in NameVerdicts.names {
            for verb in [["agents", "delete"], ["symphonies", "delete"], ["mcp", "enable"], ["mcp", "disable"], ["config", "delete-mcp-server"], ["mcp", "test"]] {
                runner.answering(verb + [name], required)
            }
            runner.failing(["symphonies", "show", name], .failed(exit: ChildExit(reason: .exited, status: 106), stderrTail: tail("name is required\nRun with --help for more information.")))
        }
        runner.answering(["agents", "create", "-h"], .done(stdout: recorded("agents-create-help.txt")))
        runner.answering(["agents", "create", "--", "-h"], failed(1, recorded("agents-create-dash-h.err")))
        runner.answering(["agents", "delete", "--keep-files", "--", "-x"], failed(1, recorded("agents-delete-dash-x.err")))
        runner.reading(["symphonies", "show", "--", "-x"], recorded("symphonies-show-dash-x.json"))
        runner.answering(["symphonies", "delete", "--", "-x"], failed(1, recorded("symphonies-delete-dash-x.err")))
        runner.answering(["symphonies", "create", "--from", "-x", "--", "copy"], .done(stdout: recorded("symphonies-create-from-dash-x.txt")))
        runner.answering(["mcp", "create", "--from", "code-exec", "--", "-h"], failed(1, recorded("mcp-create-dash-h.err")))
        runner.answering(["mcp", "enable", "--", "-x"], failed(1, recorded("mcp-enable-dash-x.err")))
        runner.answering(["config", "delete-mcp-server", "--", "-x"], failed(1, recorded("config-delete-mcp-server-dash-x.err")))
        return runner
    }

    @Test("a typed -h in New agent runs agents create -- -h: the CLI refuses it by its name rule, and nothing is toasted or selected")
    func createTypedDashH() async {
        let runner = runner()
        let agents = AgentsViewModel(runner: runner)
        var said: [String] = []
        agents.say = { said.append($0) }
        await agents.connect(binary: Self.binary)
        agents.createForm.name = "-h"
        #expect(agents.createForm.arguments == ["agents", "create", "--", "-h"])
        await agents.create()
        #expect(runner.calls.contains("run agents|create|--|-h"))
        #expect(agents.failure?.contains("agent name '-h' is not one both config formats can edit") == true, "failure: \(agents.failure ?? "none")")
        #expect(said.isEmpty, "the verb's help is never a success: \(said)")
        #expect(agents.selected == nil)
    }

    @Test("a listed -x agent's delete reaches the verb, which refuses in its own words, never the parser's")
    func deleteListedAgent() async throws {
        let runner = runner()
        let agents = AgentsViewModel(runner: runner)
        await agents.connect(binary: Self.binary)
        let agent = try #require(agents.agents.first { $0.name == "-x" }, "agents: \(agents.agents.map(\.name))")
        await agents.confirmDelete(agent, purge: false)
        #expect(runner.calls.contains("run agents|delete|--keep-files|--|-x"))
        #expect(agents.failure?.contains("agent '-x' is not one both config formats can edit") == true, "failure: \(agents.failure ?? "none")")
        #expect(AgentsViewModel.deleteArguments("-x", purge: true) == ["agents", "delete", "--purge", "--", "-x"])
    }

    @Test("a listed -x symphony is read, copied and deleted by its name: show and copy reach it, delete says the CLI's words")
    func listedSymphony() async throws {
        let runner = runner()
        let symphonies = SymphoniesViewModel(runner: runner, starter: ScriptedJobs([]))
        await symphonies.connect(binary: Self.binary)
        let symphony = try #require(symphonies.symphonies.first { $0.name == "-x" }, "symphonies: \(symphonies.symphonies.map(\.name))")
        let file = FileManager.default.temporaryDirectory.appendingPathComponent("library-names-\(UUID().uuidString).yaml")
        defer { try? FileManager.default.removeItem(at: file) }
        symphonies.selected = "-x"
        symphonies.editor = DefinitionEditor(title: "-x", path: file.path, original: "stages: []\n")
        await symphonies.save()
        #expect(runner.calls.contains("read symphonies|show|--|-x"))
        #expect(symphonies.editor?.verdict == "Saved, and apogee reads it.", "verdict: \(symphonies.editor?.verdict ?? "none")")
        symphonies.copyName = "copy"
        await symphonies.copy()
        #expect(runner.calls.contains("run symphonies|create|--from|-x|--|copy"))
        #expect(symphonies.selected == "copy" && symphonies.failure == nil)
        await symphonies.confirmDelete(symphony)
        #expect(runner.calls.contains("run symphonies|delete|--|-x"))
        #expect(symphonies.failure?.hasPrefix("apogee symphonies: symphony '-x' not found in config") == true, "failure: \(symphonies.failure ?? "none")")
    }

    @Test("Play on a -x symphony names it after --, the JSON flag before it")
    func playListedSymphony() async throws {
        let runner = runner()
        let job = ScriptedJob()
        let starter = ScriptedJobs([job])
        let symphonies = SymphoniesViewModel(runner: runner, starter: starter)
        await symphonies.connect(binary: Self.binary)
        symphonies.selected = "-x"
        symphonies.input = "hi"
        symphonies.playIt()
        #expect(starter.starts == [["symphonies", "play", "--input", "hi", "--output-format", "json", "--", "-x"]])
        job.end()
    }

    @Test("an MCP server -x is enabled, removed and tried by its name, and a typed -h registration reaches mcp create: each says the CLI's words")
    func listedServer() async throws {
        let runner = runner()
        let servers = MCPViewModel(runner: runner)
        await servers.connect(binary: Self.binary)
        let server = try #require(servers.servers.first { $0.name == "-x" }, "servers: \(servers.servers.map(\.name))")
        await servers.setEnabled(server, true)
        #expect(runner.calls.contains("run mcp|enable|--|-x"))
        #expect(servers.failure?.contains("MCP server '-x' is not one both config formats can edit") == true, "failure: \(servers.failure ?? "none")")
        await servers.confirmRemove(server)
        #expect(runner.calls.contains("run config|delete-mcp-server|--|-x"))
        #expect(servers.failure?.hasPrefix("apogee config: MCP server '-x'") == true, "failure: \(servers.failure ?? "none")")
        #expect(MCPViewModel.removeArguments("-x") == ["config", "delete-mcp-server", "--", "-x"])
        let preset = try #require(servers.presets.first { $0.name == "code-exec" })
        #expect(preset.registration == ["mcp", "create", "--from", "code-exec", "--", "code"], "the preset's own line, its server after --")
        #expect(await !servers.register(preset, as: "-h"))
        #expect(runner.calls.contains("run mcp|create|--from|code-exec|--|-h"))
        #expect(servers.failure?.contains("MCP server name '-h' is not one both config formats can edit") == true)
    }

    @Test("every line Library builds with -x, -h or --graph reaches the verb: each recorded from the built CLI, none the parser's refusal")
    func sweep() async throws {
        var lines: [[String]] = []
        for name in NameVerdicts.names {
            let runner = runner()
            let agents = AgentsViewModel(runner: runner)
            await agents.connect(binary: Self.binary)
            agents.createForm.name = name
            agents.createForm.description = "d"
            await agents.create()
            for agent in agents.agents where agent.name == name {
                await agents.confirmDelete(agent, purge: false)
                await agents.confirmDelete(agent, purge: true)
            }

            let job = ScriptedJob()
            let starter = ScriptedJobs([job])
            let symphonies = SymphoniesViewModel(runner: runner, starter: starter)
            await symphonies.connect(binary: Self.binary)
            let symphony = try #require(symphonies.symphonies.first { $0.name == name })
            let file = FileManager.default.temporaryDirectory.appendingPathComponent("library-sweep-\(UUID().uuidString).yaml")
            defer { try? FileManager.default.removeItem(at: file) }
            symphonies.selected = name
            symphonies.editor = DefinitionEditor(title: name, path: file.path, original: "stages: []\n")
            await symphonies.save()
            symphonies.editor = nil
            symphonies.selected = name
            symphonies.input = "hi"
            symphonies.playIt()
            job.end()
            #expect(await eventually { !symphonies.isPlaying })
            symphonies.selected = name
            symphonies.copyName = "copy"
            await symphonies.copy()
            symphonies.selected = "critique-revise"
            symphonies.copyName = name
            await symphonies.copy()
            await symphonies.confirmDelete(symphony)
            lines += starter.starts

            let servers = MCPViewModel(runner: runner)
            await servers.connect(binary: Self.binary)
            let server = try #require(servers.servers.first { $0.name == name })
            await servers.setEnabled(server, true)
            await servers.setEnabled(server, false)
            await servers.confirmRemove(server)
            servers.selected = name
            servers.tool = "tool"
            await servers.tryTool()
            let preset = try #require(servers.presets.first { $0.name == "code-exec" })
            await servers.register(preset, as: name)
            lines += NameVerdicts.lines(ofCalls: runner.calls)
        }
        let (missing, refused) = NameVerdicts.judged(lines)
        #expect(missing.isEmpty, "never recorded -- add these rows to tests/presentation/shell/Fixtures/argv-name-verdicts.txt and run scripts/refresh-argv-name-verdicts.sh <the built apogee>:\n\(missing.joined(separator: "\n"))")
        #expect(refused.isEmpty, "the parser refuses:\n\(refused.joined(separator: "\n"))")
    }
}
