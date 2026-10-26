import DataProtocol
import Foundation
import Testing

/// The CLI's M28, read: the `session` event names the chat the session
/// writes (`chat_id`) -- over the branch binary's own recordings (GUI M10):
/// a new session, the same stream as a binary before M28 wrote it, and a
/// private session, which writes none.
@Suite("The session names its chat (M28)")
struct SessionChatTests {
    static func first(_ name: String) throws -> SessionInfo {
        let fixture = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
            .appendingPathComponent("Fixtures/\(name).jsonl")
        let line = try #require(try String(contentsOf: fixture, encoding: .utf8).split(separator: "\n").first)
        return try #require(session(String(line)), "\(name) opens with its session")
    }

    static func session(_ line: String) -> SessionInfo? {
        if case .session(let info) = EventDecoder.decode(line: line).kind { info } else { nil }
    }

    @Test("a new session names its chat, a session left unsent too; a binary before M28 and a private session name none")
    func recorded() throws {
        #expect(try Self.first("switched-to-local").chatID == "20261026-172749-d4cb")
        #expect(try Self.first("new-unsent").chatID == "20261026-172749-5dec")
        #expect(try Self.first("switched-to-local-older-handmade").chatID == nil)
        let secret = try Self.first("private-unsent")
        #expect(secret.isPrivate && secret.chatID == nil)
    }

    @Test("what is no id is none: absent, empty or not a string -- and the event is a session all the same")
    func tolerant() {
        #expect(Self.session(#"{"type":"session","protocol_version":1,"model":"m"}"#)?.chatID == nil)
        #expect(Self.session(#"{"type":"session","protocol_version":1,"model":"m","chat_id":""}"#)?.chatID == nil)
        #expect(Self.session(#"{"type":"session","protocol_version":1,"model":"m","chat_id":7}"#)?.chatID == nil)
        let grown = EventDecoder.decode(line: #"{"type":"session","protocol_version":1,"model":"m","chat_id":"c1","chat_title":"later"}"#)
        guard case .session(let info) = grown.kind else {
            Issue.record("not a session: \(grown.kind)")
            return
        }
        #expect(info.chatID == "c1")
        #expect(grown.fields["chat_title"]?.stringValue == "later", "a field this client does not know rides along, kept")
    }
}
