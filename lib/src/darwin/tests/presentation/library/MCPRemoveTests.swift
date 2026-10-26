import BusinessJobs
import BusinessReads
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationLibrary

/// GUI 4k, the fifth gap: an MCP server removed from the Library, as every
/// other Library resource is deleted -- asked first, through the binary's own
/// `config delete-mcp-server`, read back.
@Suite("Library: an MCP server removed (GUI 4k)", .timeLimit(.minutes(1)))
@MainActor
struct MCPRemoveTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func model(_ runner: ScriptedRunner) async throws -> (MCPViewModel, MCPServer) {
        runner.reading(["mcp", "list"], fixture("mcp-list"))
        runner.reading(["mcp", "presets"], fixture("mcp-presets"))
        let model = MCPViewModel(runner: runner)
        model.use(binary: Self.binary)
        await model.refresh()
        let server = try #require(model.servers.first)
        model.selected = server.name
        return (model, server)
    }

    @Test("Remove asks first; on yes it is config delete-mcp-server <name>, read back, the CLI's words said, the selection gone with the server")
    func removeAsked() async throws {
        let runner = ScriptedRunner()
        runner.answering(["config", "delete-mcp-server"], .done(stdout: fixture("mcp-delete", "txt")))
        var said: [String] = []
        let (model, server) = try await model(runner)
        model.say = { said.append($0) }
        model.askToRemove(server)
        #expect(model.pendingRemove == server)
        #expect(!runner.calls.contains { $0.hasPrefix("run config|delete-mcp-server") }, "nothing runs before the person's word")
        let lists = runner.calls.filter { $0 == "read mcp|list" }.count
        runner.reading(["mcp", "list"], #"{"data":[],"object":"list"}"#)
        await model.confirmRemove()
        #expect(model.pendingRemove == nil)
        #expect(runner.calls.contains("run config|delete-mcp-server|\(server.name)"))
        #expect(runner.calls.filter { $0 == "read mcp|list" }.count == lists + 1, "read back")
        #expect(model.servers.isEmpty && model.selected == nil)
        #expect(said == ["removed MCP server 'notes' from /Users/someone/.apogee/config/config.json"], "the CLI's own line")
    }

    @Test("a dismissed question runs nothing; a refusal is the CLI's words, the selection kept")
    func dismissedAndRefused() async throws {
        let runner = ScriptedRunner()
        let missing = refusal("mcp-delete-missing")
        runner.answering(["config", "delete-mcp-server"], .failed(exit: missing.exit, stderrTail: missing.tail))
        let (model, server) = try await model(runner)
        model.askToRemove(server)
        model.pendingRemove = nil
        await model.confirmRemove()
        #expect(!runner.calls.contains { $0.hasPrefix("run config|delete-mcp-server") })
        model.askToRemove(server)
        await model.confirmRemove()
        #expect(model.failure == "apogee config: MCP server 'notes' not found in config (exited with status 1)")
        #expect(model.selected == server.name, "nothing went: the page stays")
    }
}
