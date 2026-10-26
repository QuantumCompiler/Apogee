import BusinessReads
import DataProtocol
import Foundation
import Testing

/// What the branch's binary printed over a sandbox home, the home written
/// `/Users/someone` (`Fixtures/`).
private func printed(_ name: String) -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("Fixtures/\(name)")
    return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
}

/// GUI 4k's reads: text a verb prints alone, read as the CLI prints it.
@Suite("The remaining verbs' reads (GUI 4k)")
struct RemainingVerbsReadsTests {
    @Test("chats info's text is its fields in order, a value's own colons kept, a line with none skipped")
    func chatsInfo() {
        let fields = Field.lines(printed("chats-info-compacted.txt"))
        #expect(fields.map(\.label) == ["id", "title", "backend", "started", "updated", "turns", "messages", "compacted"])
        #expect(fields[0] == Field("id", "20261026-142409-5159"))
        #expect(fields[3] == Field("started", "2026-10-26T14:24:09Z"))
        #expect(fields.last == Field("compacted", "1 time(s)"))
        #expect(Field.lines("no colon here\n: no label\nkey:\n") == [Field("key", "")])
    }

    @Test("server_tools: one tool a line, none for the empty answer; only the three APIs hold it")
    func serverTools() {
        #expect(ServerTools.list(printed("config-get-server-tools.txt")) == ["web_search", "code_execution"])
        #expect(ServerTools.list(printed("config-get-server-tools-none.txt")) == [])
        #expect(ServerTools.key(of: "claude-api") == "backends.claude-api.server_tools")
        #expect(["anthropic", "openai", "google"].allSatisfy(ServerTools.held(by:)))
        #expect(!["claude-cli", "llamacpp", "mlx", "ollama-cli", "mock"].contains(where: ServerTools.held(by:)))
    }

    @Test("a record carries whether it has an archive; its conversation comes only from knowledge info --raw's text")
    func rawConversation() throws {
        let records = KnowledgeRecord.list(from: try #require(ReadDocument(text: printed("knowledge-list-one.json"))))
        #expect(records.first?.rawRef == "/Users/someone/.apogee/knowledge/raw/kr-20261026T142411Z-2e8272.md")
        #expect(RawConversation(printed: printed("knowledge-info-raw.txt")) == .text(
            "Ada: testers keep hitting cancel when they mean back. Grace: then we drop the cancel button from checkout. Ada: agreed, ship it."))
        #expect(RawConversation(printed: printed("knowledge-info-raw-unavailable.txt")) == .unavailable)
        #expect(RawConversation(printed: "ID:          kr-1\nIntent:      x\n") == nil, "neither of the CLI's lines: nothing assumed")
        let bare = try #require(KnowledgeRecord(JSONValue.parse(#"{"id":"kr-2","raw_ref":""}"#)!))
        #expect(bare.rawRef == nil)
    }
}
