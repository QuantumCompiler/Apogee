import BusinessReads
import DataProtocol
import Foundation
import Testing

private let here = URL(fileURLWithPath: #filePath).deletingLastPathComponent()

private func fixture(_ name: String) -> String {
    try! String(contentsOf: here.appendingPathComponent("Fixtures/\(name)"), encoding: .utf8)
}

private func document(_ name: String) -> ReadDocument {
    ReadDocument(text: fixture(name))!
}

/// Recorded from the binary that ships the CLI's M27, over a scratch home
/// written `/Users/someone` (`Fixtures/chats-show.json`): a conversation
/// driven on the scripted mock backend -- two turns compacted, a plain turn,
/// a tool call, a turn that failed on a backend that was down, and a turn
/// with an attachment -- and its text form (`chats-show.txt`).
/// `Fixtures/chats-show-refused.err` is a binary before M27 refusing the read.
@Suite("SavedChat")
struct SavedChatTests {
    @Test("chats show's recorded document: a compaction where it happened, then each turn as the history holds it")
    func recorded() throws {
        let chat = try #require(SavedChat(document: document("chats-show.json")))
        #expect(chat.id == "20261022-182803-f9f6")
        #expect(chat.name == "Planning A Note")
        #expect(chat.title == "Planning A Note")
        #expect(chat.backend == "chatty")
        #expect(chat.model == nil, "an empty roster model is none")
        let report = SavedAttachment(name: "/Users/someone/Documents/report.md", files: 1, bytes: 71)
        #expect(chat.attachments == [report])
        #expect(chat.turns.map(\.turn) == [2, 3, 4, 4, 5], "an unanswered turn carries the number before it")
        #expect(chat.turns == [
            SavedTurn(
                turn: 2, text: "", attachments: [], answer: "Then write the date first, as 2026-10-22.", tools: [],
                answered: true, compaction: "They asked how to write a note; a short list, the date first."),
            SavedTurn(
                turn: 3, text: "Which port does the dev server use?", attachments: [],
                answer: "The dev server listens on port 8080.", tools: [], answered: true, compaction: nil),
            SavedTurn(
                turn: 4, text: "What is in notes.md?", attachments: [],
                answer: "notes.md holds two items: **buy milk** and *call the plumber*.",
                tools: [SavedToolCall(name: "read_file", summary: #"{"path":"Documents/notes.md"}"#, resultBytes: 30)],
                answered: true, compaction: nil),
            SavedTurn(
                turn: 4, text: "Is the launch still on?", attachments: [], answer: "", tools: [], answered: false,
                compaction: nil),
            SavedTurn(
                turn: 5, text: "What does the report I attached say?", attachments: [report],
                answer: "The report says the launch moved to **November**:\n\n- the beta ends on 2026-10-31\n- the launch is on 2026-11-12",
                tools: [], answered: true, compaction: nil),
        ])
        #expect(chat.turns[0].isCompaction && !chat.turns[1].isCompaction)
    }

    @Test("its last turn is one before the next_turn the resumed session announced, as the binary numbers them")
    func numberedAsTheSessionNumbers() throws {
        let chat = try #require(SavedChat(document: document("chats-show.json")))
        let stream = try String(
            contentsOf: here.deletingLastPathComponent().deletingLastPathComponent()
                .appendingPathComponent("data/protocol/Fixtures/resumed-chat.jsonl"), encoding: .utf8)
        let opening = try #require(stream.split(separator: "\n").first.map { EventDecoder.decode(line: String($0)) })
        guard case .session(let info) = opening.kind else {
            Issue.record("the resumed stream opens with its session event")
            return
        }
        #expect(chat.turns.last?.turn == info.nextTurn - 1)
    }

    @Test("the text a person reads (Fixtures/chats-show.txt) heads the same entries, numbered the same, in the same order")
    func textFormAgrees() throws {
        let chat = try #require(SavedChat(document: document("chats-show.json")))
        let headed = fixture("chats-show.txt").split(separator: "\n").filter { $0.hasPrefix("turn ") }
            .compactMap { Int($0.dropFirst("turn ".count).prefix { $0.isNumber }) }
        #expect(headed == chat.turns.compactMap(\.turn))
        #expect(fixture("chats-show.txt").hasPrefix("Planning A Note -- 20261022-182803-f9f6, on chatty\n"))
    }

    @Test("the read is chats show by the chat's id; a binary before it refuses it, and prints nothing")
    func theRead() {
        #expect(SavedChat.arguments(id: "20261022-182803-f9f6") == ["chats", "show", "20261022-182803-f9f6"])
        #expect(fixture("chats-show-refused.err") == "A subcommand is required\nRun with --help for more information.\n")
        #expect(ReadDocument(text: fixture("chats-show-refused.err")) == nil)
    }

    @Test("a field a binary leaves out reads as empty, one it adds is ignored, and an entry that is no object is skipped")
    func tolerant() throws {
        let document = try #require(ReadDocument(text: #"{"object":"chat","id":"c1","later":{"x":1},"turns":[{"user":{"text":"hi"},"answer":{"text":"hello","tools":[{"name":"grep"},{"summary":"no name"}]},"mood":"new"},{"turn":2,"answer":{}},7,{"turn":3,"compacted":{},"answered":false}]}"#))
        let chat = try #require(SavedChat(document: document))
        #expect(chat.id == "c1" && chat.name == "c1", "no name: the id names it")
        #expect(chat.title == nil && chat.backend == nil && chat.model == nil && chat.attachments.isEmpty)
        #expect(chat.turns == [
            SavedTurn(
                turn: nil, text: "hi", attachments: [], answer: "hello",
                tools: [SavedToolCall(name: "grep", summary: "", resultBytes: nil)], answered: true, compaction: nil),
            SavedTurn(turn: 2, text: "", attachments: [], answer: "", tools: [], answered: false, compaction: nil),
            SavedTurn(turn: 3, text: "", attachments: [], answer: "", tools: [], answered: false, compaction: ""),
        ])
    }

    @Test("a document that is no chat reads as none: another read's, or one with no turns")
    func notAChat() throws {
        #expect(SavedChat(document: document("chats-list.json")) == nil)
        #expect(SavedChat(document: try #require(ReadDocument(text: #"{"object":"chat","id":"c1"}"#))) == nil)
        #expect(SavedChat(document: try #require(ReadDocument(text: #"{"id":"c1","turns":[]}"#)))?.turns == [])
    }
}
