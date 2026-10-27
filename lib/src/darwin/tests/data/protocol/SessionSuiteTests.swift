import DataProtocol
import Foundation
import Testing

/// The CLI's M29, read: the `session` event names the suite the session runs
/// under (`suite`, absent when none) and, for `execute`, whether it
/// orchestrates (`orchestrating`), under the vocabulary dated `2026-10-26` --
/// over the branch binary's own recordings (GUI M13), beside 4i's from a
/// binary before it, which says neither.
@Suite("The session names its suite (M29)")
struct SessionSuiteTests {
    static func first(_ name: String) throws -> SessionInfo {
        let fixture = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            .appendingPathComponent("Fixtures/\(name).jsonl")
        let line = try #require(try String(contentsOf: fixture, encoding: .utf8).split(separator: "\n").first)
        return try #require(session(String(line)), "\(name) opens with its session")
    }

    static func session(_ line: String) -> SessionInfo? {
        if case .session(let info) = EventDecoder.decode(line: line).kind { info } else { nil }
    }

    @Test("a chat under a suite, the same chat resumed with no --suite, and the default suite: each named; none said by its absence")
    func chats() throws {
        let opened = try Self.first("chat-under-suite-m29")
        #expect(opened.suite == "pair" && opened.namesItsSuite)
        #expect(opened.orchestrating == nil, "a chat says nothing of orchestration")
        #expect(try Self.first("resumed-under-suite-m29").suite == "pair", "the chat's own suite, no flag naming it")
        #expect(try Self.first("chat-default-suite-m29").suite == "fast")
        let off = try Self.first("resumed-suite-off-m29")
        #expect(off.suite == nil && off.namesItsSuite, "none, said by a binary that names its suite")
        #expect(off.capabilities?.schema == MachineProtocol.namesSuite)
    }

    @Test("execute: orchestrating from the suite's orchestrate: true, from --orchestrate, and not at all")
    func execute() throws {
        let suiteSays = try Self.first("execute-orchestrating")
        #expect(suiteSays.suite == "orch" && suiteSays.orchestrating == true)
        #expect(try Self.first("execute-orchestrate-flag").orchestrating == true)
        let plain = try Self.first("execute-not-orchestrating")
        #expect(plain.suite == "pair" && plain.orchestrating == false)
    }

    @Test("a binary before M29 names neither: not said, never none")
    func older() throws {
        for name in ["chat-under-suite", "resumed-under-suite", "resumed-suite-off", "execute-orchestrate"] {
            let info = try Self.first(name)
            #expect(info.suite == nil && info.orchestrating == nil, "\(name)")
            #expect(!info.namesItsSuite, "\(name): 2026-10-25 or before")
        }
    }

    @Test("tolerant: an empty or non-string suite is none named, a non-boolean orchestrating unsaid, and the event a session all the same")
    func tolerant() {
        let empty = Self.session(#"{"type":"session","protocol_version":1,"model":"m","suite":""}"#)
        #expect(empty?.suite == nil && empty?.namesItsSuite == false)
        #expect(Self.session(#"{"type":"session","protocol_version":1,"model":"m","suite":7}"#)?.suite == nil)
        #expect(Self.session(#"{"type":"session","protocol_version":1,"model":"m","orchestrating":"yes"}"#)?.orchestrating == nil)
        let grown = EventDecoder.decode(line: #"{"type":"session","protocol_version":1,"model":"m","suite":"s","suite_members":3}"#)
        guard case .session(let info) = grown.kind else {
            Issue.record("not a session: \(grown.kind)")
            return
        }
        #expect(info.suite == "s" && info.namesItsSuite)
        #expect(grown.fields["suite_members"]?.intValue == 3, "a field this client does not know rides along, kept")
    }
}
