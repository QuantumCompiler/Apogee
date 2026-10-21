import BusinessReads
import DataProtocol
import Foundation
import Testing

private func document(_ name: String) -> ReadDocument {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("Fixtures/\(name).json")
    return ReadDocument(text: try! String(contentsOf: url, encoding: .utf8))!
}

/// Recorded from the installed binary over a scratch home: the shipped agents
/// and one scaffolded, the code-exec preset registered and connected, the
/// shipped starters, a spec file and a config entry, and a play on a mock.
@Suite("Library")
struct LibraryTests {
    @Test("agents: the shipped and the person's own, with their files and policies")
    func agents() throws {
        let agents = AgentEntry.list(from: document("agents-list"))
        let triage = try #require(agents.first { $0.name == "triage" })
        #expect(!triage.bundled && triage.tools == "read-only")
        #expect(triage.prompts == ["prompts/triage.txt"] && triage.schemas == ["schemas/triage-output.json"])
        #expect(agents.contains { $0.bundled && $0.name == "security-review" })
    }

    @Test("MCP: a server's state and tools, its environment only as set or not; a preset's register line or refusal")
    func mcp() throws {
        let server = try #require(MCPServer.list(from: document("mcp-list")).first)
        #expect(server.name == "code" && server.state == "connected" && server.enabled && !server.envSet)
        #expect(server.tools == ["run_python", "write_input"])
        let presets = MCPPreset.list(from: document("mcp-presets"))
        #expect(presets.first { $0.name == "code-exec" }?.register == "apogee mcp create code --from code-exec")
        #expect(presets.first { $0.name == "self" }?.refusal?.isEmpty == false)
    }

    @Test("symphonies: the three sources, a play stage beside an asking one, the person's file editable")
    func symphonies() throws {
        let all = Symphony.list(from: document("symphonies-list"))
        #expect(Set(all.map(\.source)) == ["shipped", "config", "file"])
        let digest = try #require(all.first { $0.name == "digest" })
        #expect(digest.stages.first?.plays == "summarize-verify" && digest.stages.last?.role == "extraction")
        #expect(all.filter(\.isOwnFile).map(\.name) == ["notes-digest"])
        #expect(all.first { $0.name == "quick-check" }?.path == nil)
        let shown = try #require(Symphony(document("symphony-show").value))
        #expect(shown.name == "notes-digest" && shown.problems.isEmpty && shown.stages.count == 2)
    }

    @Test("a play's document: each stage's answer and backend, and the output")
    func play() throws {
        let play = try #require(SymphonyPlay(document: document("symphony-play")))
        #expect(play.symphony == "critique-revise" && play.stages.map(\.name) == ["critique", "revise"])
        #expect(play.stages.map(\.role) == ["utility", "chat"] && play.output.hasPrefix("A tighter draft"))
    }
}
