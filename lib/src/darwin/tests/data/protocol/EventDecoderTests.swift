import DataProtocol
import Foundation
import Testing

/// tests/data/protocol/Fixtures/mock-actor.jsonl: the real v0.1.9 binary's
/// stream for the mock-actor conversation, recorded by scripts/capture-fixture.sh.
private let fixture = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
    .appendingPathComponent("Fixtures/mock-actor.jsonl")

@Suite("EventDecoder")
struct EventDecoderTests {
    func recorded() throws -> [ProtocolEvent] {
        try String(contentsOf: fixture, encoding: .utf8).split(separator: "\n").map {
            EventDecoder.decode(line: String($0))
        }
    }

    @Test("the real binary's mock-actor stream decodes whole, every event typed, in order")
    func recordedStream() throws {
        let events = try recorded()
        let types = events.map(\.type)
        #expect(types.first == "session")
        #expect(types.last == "result")
        for event in events {
            switch event.kind {
            case .unknown, .unreadable: Issue.record("the vocabulary did not cover \(event.type)")
            default: break
            }
        }
        guard case .session(let session) = events[0].kind else {
            Issue.record("the first event is not the session")
            return
        }
        #expect(session.protocolVersion == 1)
        #expect(session.model == "actor")
        #expect(session.nextTurn == 1)
        #expect(session.capabilities?.accepts == ["user", "answer", "attach", "hello", "cancel"])
        #expect(session.capabilities?.ask == true)
        #expect(session.capabilities?.tools == true)
        #expect(session.capabilities?.schema == "2026-10-13")
        #expect(events.dropFirst().allSatisfy { $0.turn == 1 })

        let questions = events.compactMap { if case .question(let q) = $0.kind { q } else { nil } }
        #expect(questions.count == 2)
        #expect(questions[0].isPermission == false)
        #expect(questions[0].items.first?.question == "What should the note be called?")
        #expect(questions[0].items.first?.options.map(\.label) == ["todo.md", "ideas.md"])
        #expect(questions[1].isPermission)
        #expect(questions[1].tool == "write_file")
        #expect(questions[1].target == "todo.md")
        #expect(questions[1].items.first?.options.map(\.label) == ["yes", "no", "always", "session"])

        let answer = events.compactMap { if case .answerDelta(let text) = $0.kind { text } else { nil } }.joined()
        guard case .result(let result) = events.last?.kind else {
            Issue.record("no result")
            return
        }
        #expect(answer == result.text)
        #expect(result.text == "I wrote todo.md with one item: buy milk.")
        #expect(result.finishReason == "stop")
        #expect(result.usage == nil)
    }

    @Test("a type this client does not know is kept, its fields with it")
    func unknownType() {
        let event = EventDecoder.decode(line: #"{"type":"hologram","turn":2,"shape":"cube"}"#)
        #expect(event.kind == .unknown(type: "hologram"))
        #expect(event.turn == 2)
        #expect(event.fields["shape"] == .string("cube"))
    }

    @Test("a field this client does not know rides a known event, kept")
    func unknownField() {
        let event = EventDecoder.decode(line: #"{"type":"answer_delta","text":"hi","turn":1,"glow":true}"#)
        #expect(event.kind == .answerDelta("hi"))
        #expect(event.fields["glow"] == .bool(true))
    }

    @Test("a known type missing what its case needs is kept as unknown, never guessed at")
    func knownButIncomplete() {
        #expect(EventDecoder.decode(line: #"{"type":"answer_delta","turn":1}"#).kind == .unknown(type: "answer_delta"))
        #expect(EventDecoder.decode(line: #"{"type":"session","model":"m"}"#).kind == .unknown(type: "session"))
        #expect(EventDecoder.decode(line: #"{"type":"error"}"#).kind == .unknown(type: "error"))
    }

    @Test("a line that is not one JSON object with a type is unreadable, not fatal")
    func unreadable() {
        #expect(EventDecoder.decode(line: "this is not json").kind == .unreadable(line: "this is not json"))
        #expect(EventDecoder.decode(line: #"{"no":"type"}"#).kind == .unreadable(line: #"{"no":"type"}"#))
        #expect(EventDecoder.decode(line: "[1,2]").kind == .unreadable(line: "[1,2]"))
    }

    @Test("a task run's lifecycle events are kept whole")
    func taskEvents() {
        let event = EventDecoder.decode(line: #"{"type":"task_round","task_id":"task-1","round":{"round":1}}"#)
        #expect(event.kind == .task(type: "task_round"))
        #expect(event.fields["task_id"] == .string("task-1"))
    }

    @Test("the rest of the vocabulary reads into its cases")
    func vocabulary() {
        func kind(_ line: String) -> ProtocolEvent.Kind { EventDecoder.decode(line: line).kind }
        #expect(kind(#"{"type":"thinking","budget_reached":true}"#) == .thinking(budgetReached: true))
        #expect(kind(#"{"type":"memory","chats":2,"decisions":1}"#) == .memory(chats: 2, decisions: 1))
        #expect(kind(#"{"type":"notice","text":"context budget: 2 earlier exchanges not sent"}"#)
            == .notice("context budget: 2 earlier exchanges not sent"))
        #expect(kind(#"{"type":"todo","items":[{"text":"plan","state":"done"},{"text":"build","state":"active"}]}"#)
            == .todo([TodoItem(text: "plan", state: "done"), TodoItem(text: "build", state: "active")]))
        #expect(kind(#"{"type":"error","message":"the provider refused"}"#) == .error(message: "the provider refused"))
        #expect(kind(#"{"type":"result","text":"","finish_reason":"cancelled","usage":{"input_tokens":12,"output_tokens":3}}"#)
            == .result(TurnResult(text: "", model: nil, finishReason: "cancelled", usage: Usage(inputTokens: 12, outputTokens: 3))))
        #expect(kind(#"{"type":"question","kind":"permission","tool":"fetch_url","target":"docs.python.org","outbound":true,"detail":"https://docs.python.org/3/","questions":[]}"#)
            == .question(Question(kind: "permission", tool: "fetch_url", target: "docs.python.org", outbound: true, detail: "https://docs.python.org/3/", items: [])))
    }
}
