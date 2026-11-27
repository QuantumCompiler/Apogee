import BusinessReads
import DataProtocol
import Foundation
import Testing

/// What `apogee-dev` printed over a sandbox home on the mock backend, the
/// home written `/Users/someone` (beside the Tasks tests',
/// `tests/presentation/tasks/Fixtures`).
private func printed(_ name: String) -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
        .appendingPathComponent("../../presentation/tasks/Fixtures/\(name)")
    return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
}

/// GUI 6g: a turn's `allowed` and `denied` are objects, `{tool,target,by}`
/// (the CLI's `view.cpp`, `uses_json`), decoded field by field -- never a
/// sorted-key JSON string, which is what `JSONValue.serialized` made of
/// each, and what the Rounds row drew. Over `task-status-uses.json`: a task
/// with tools granted `run_command`, whose round 1 ran `ls` on its grant
/// and called `write_file report.txt` with nobody present to allow it.
@Suite("A task's uses, decoded (GUI 6g)")
struct TaskReadsTests {
    func view(_ text: String = printed("task-status-uses.json")) throws -> TaskView {
        let document = try #require(ReadDocument(text: text), "one JSON document")
        return try #require(TaskView(document: document))
    }

    @Test("each denied and allowed entry decodes to its tool, target and authority")
    func recordedUses() throws {
        let view = try view()
        #expect(view.rounds.map(\.round) == [0, 1])
        #expect(view.rounds[0].allowed.isEmpty && view.rounds[0].denied.isEmpty)
        #expect(view.rounds[1].allowed == [TaskView.Use(tool: "run_command", target: "ls", by: "grant")])
        #expect(view.rounds[1].denied == [TaskView.Use(tool: "write_file", target: "report.txt", by: "nobody")])
    }

    @Test("the grants come from policy.grants; a task without tools has none and no policy")
    func grants() throws {
        let view = try view()
        #expect(view.tools && view.grants == ["run_command"])
        let none = try self.view(printed("task-status-stalled.json"))
        #expect(!none.tools && none.grants.isEmpty)
    }

    @Test("a bare-string entry is kept as the tool's name, never dropped and never serialized")
    func bareString() throws {
        let text = printed("task-status-uses.json")
            .replacingOccurrences(
                of: #""denied":[{"by":"nobody","target":"report.txt","tool":"write_file"}]"#,
                with: #""denied":["write_file",{"by":"nobody","target":"report.txt","tool":"write_file"}]"#)
        let round = try #require(try view(text).rounds.last)
        #expect(round.denied == [TaskView.Use(tool: "write_file"), TaskView.Use(tool: "write_file", target: "report.txt", by: "nobody")])
        #expect(!round.denied.contains { $0.tool.contains("{") })
    }

    @Test("a field it does not know is passed over; an entry of no shape it reads names no tool and is left out")
    func tolerant() throws {
        let text = printed("task-status-uses.json")
            .replacingOccurrences(
                of: #""allowed":[{"by":"grant","target":"ls","tool":"run_command"}]"#,
                with: #""allowed":[{"by":"grant","at":"2026-11-09T02:12:11Z","target":"ls","tool":"run_command"},7,null]"#)
        let round = try #require(try view(text).rounds.last)
        #expect(round.allowed == [TaskView.Use(tool: "run_command", target: "ls", by: "grant")])
    }

    @Test("an entry with no by or target keeps what it names")
    func partial() {
        let use = TaskView.Use(JSONValue.parse(#"{"tool":"fetch_url"}"#)!)
        #expect(use == TaskView.Use(tool: "fetch_url", target: "", by: ""))
    }
}
