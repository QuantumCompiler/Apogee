import BusinessReads
import BusinessSession
import DataProtocol
import Foundation
import InfrastructureProcess
import PresentationSession
import Testing

/// 4b: the reducer keeps a dead card dead and earlier rows apart from live
/// ones, over the binaries' own recorded streams and reads.
@Suite("The transcript stays honest")
struct TranscriptHonestyTests {
    static func stream(_ name: String) -> [String] { fixtureStream(name) }

    static func type(_ line: String) -> String? { JSONValue.parse(line)?["type"]?.stringValue }

    /// `lines` folded into `transcript` as the window folds them: each of
    /// `typed` sent as the turn its first event numbers opens; the turn
    /// recorded as stopped marked stopping first, as the window's Stop does.
    static func fold(_ lines: [String], into transcript: inout Transcript, typed: [String], without: Set<String> = []) {
        var typed = typed[...]
        var sent: Int?
        for line in lines {
            let event = EventDecoder.decode(line: line)
            if without.contains(event.type) { continue }
            if let turn = event.turn, turn != sent, let next = typed.popFirst() {
                transcript.userSent(next)
                sent = turn
            }
            if case .result(let result) = event.kind, result.wasCancelled { transcript.cancelling() }
            transcript.apply(.event(event))
        }
    }

    /// The deltas and their bounds: a result standing alone, as a driver that
    /// dropped every delta sees the same turn.
    static let deltas: Set<String> = ["answer_start", "answer_delta", "answer_end"]

    func answers(_ transcript: Transcript) -> [String] {
        shape(transcript).filter { $0.hasPrefix("answer") }
    }

    // MARK: - Finding 3

    @Test("a question left standing when its turn ends -- stopped, here -- is closed: an answer to it is refused")
    func deadCardCloses() throws {
        var transcript = Transcript()
        Self.fold(Self.stream("question-cancelled"), into: &transcript, typed: ["Write me a note."])
        #expect(transcript.status == .ready)
        let card = try #require(transcript.items.first { if case .question = $0.content { true } else { false } })
        #expect(transcript.standingCard == nil, "nothing waits on the person now")
        let took = transcript.answered(card.id, with: ["todo.md"])
        #expect(!took)
        guard case .question(let after) = transcript.items.first(where: { $0.id == card.id })?.content else {
            Issue.record("the card is gone")
            return
        }
        #expect(after.answers == nil, "a dead question takes no answer")
        #expect(after.closed && !after.isStanding)
        #expect(shape(transcript) == ["user: Write me a note.", "activity: |", "question: waiting", "stopped"])
    }

    @Test("a permission prompt left standing by a stopped turn is closed too; a card still standing takes its answer")
    func deadPermissionCloses() throws {
        var transcript = Transcript()
        var lines = Self.stream("permission-cancelled")[...]
        var cards: [Int] = []
        while let line = lines.popFirst() {
            let event = EventDecoder.decode(line: line)
            if case .session = event.kind {
                transcript.apply(.event(event))
                transcript.userSent("Write me a note.")
                continue
            }
            transcript.apply(.event(event))
            if case .question = event.kind, let standing = transcript.standingCard {
                cards.append(standing)
                if cards.count == 1 {
                    let took = transcript.answered(standing, with: ["todo.md"])
                    #expect(took, "a standing card takes its answer")
                }
            }
        }
        #expect(cards.count == 2)
        guard case .permission(let permission) = transcript.items.first(where: { $0.id == cards[1] })?.content else {
            Issue.record("no permission card")
            return
        }
        #expect(permission.closed && permission.answer == nil)
        let took = transcript.answered(cards[1], with: ["yes"])
        #expect(!took)
        #expect(transcript.standingCard == nil)
        #expect(transcript.status == .ready)
    }

    @Test("a card a child left standing when it ended is closed")
    func endedChildClosesItsCard() {
        var transcript = Transcript()
        let lines = Self.stream("question-cancelled")
        transcript.apply(decoded(lines[0]))
        transcript.userSent("Write me a note.")
        for line in lines.dropFirst() where Self.type(line) != "result" { transcript.apply(decoded(line)) }
        #expect(transcript.standingCard != nil)
        transcript.apply(.ended(SessionEnd(reason: .failed, exit: nil, stderrTail: .init())))
        #expect(transcript.standingCard == nil)
    }

    // MARK: - Finding 5: earlier and live rows apart

    @Test("a binary before 55b resumes a count-less chat at next_turn 1, over earlier rows numbered 1 and 2: the live result-only answer is still drawn")
    func earlierRowsApart() throws {
        let chat = try #require(SavedChat(document: fixtureDocument("chats-show-countless.json")))
        #expect(chat.turns.map(\.turn) == [1, 2], "the read numbers the earlier turns 1 and 2")
        let lines = Self.stream("resumed-countless")
        #expect(JSONValue.parse(lines[0])?["next_turn"]?.intValue == 1, "and the session numbers its first live turn 1 again")
        var transcript = Transcript()
        transcript.earlier(chat)
        Self.fold(lines, into: &transcript, typed: ["Thanks -- anything else?"], without: Self.deltas)
        #expect(answers(transcript) == [
            "answer: The dev server listens on port 8080.",
            "answer: Nothing else: the launch is on 2026-11-12.",
            "answer: The dev server listens on port 8080.",
        ], "the live answer, after the divider, beside the earlier row of the same number")
        #expect(shape(transcript).last == "answer: The dev server listens on port 8080.")
        #expect(Set(transcript.items.prefix { $0.content != .divider(Transcript.earlierDivider) }.map(\.range)) == [Transcript.earlierRange])
        #expect(transcript.items.last?.range == 1, "the first live child's")
    }

    @Test("the live turn's usage lands on the live row, never on the earlier row that shares its number")
    func usageOnTheLiveRow() throws {
        let chat = try #require(SavedChat(document: fixtureDocument("chats-show-countless.json")))
        var transcript = Transcript()
        transcript.earlier(chat)
        transcript.apply(decoded(Self.stream("resumed-countless")[0]))
        transcript.userSent("Thanks -- anything else?")
        // The mock reports no usage; a provider that does adds it to the same
        // result (hand-made from the recorded line).
        transcript.apply(decoded(#"{"finish_reason":"stop","model":"plain","text":"The dev server listens on port 8080.","turn":1,"type":"result","usage":{"input_tokens":40,"output_tokens":9}}"#))
        var usages: [Usage?] = []
        for item in transcript.items {
            if case .answer(let answer) = item.content { usages.append(answer.usage) }
        }
        #expect(usages == [nil, nil, Usage(inputTokens: 40, outputTokens: 9)])
    }

    @Test("a stopped live turn leaves the CLI's count behind the live numbers: paused and resumed, the next child's turn 3 is its own row, never the first child's")
    func childrenApart() throws {
        let chat = try #require(SavedChat(document: fixtureDocument("chats-show-after-stop.json")))
        #expect(chat.turns.map(\.turn) == [1, 2], "the stopped turn is not kept, so the read counts two")
        let resumed = Self.stream("resumed-after-stop")
        #expect(JSONValue.parse(resumed[0])?["next_turn"]?.intValue == 3)
        var transcript = Transcript()
        Self.fold(
            Self.stream("stopped-turn"), into: &transcript,
            typed: ["Which port does the dev server use?", "Is the launch still on?", "What does the report say?"])
        #expect(shape(transcript).contains("stopped"))
        transcript.paused()
        transcript.reopening()
        Self.fold(resumed, into: &transcript, typed: ["Thanks -- anything else?"], without: Self.deltas)
        #expect(answers(transcript) == [
            "answer: The dev server listens on port 8080.",
            "answer: The laun",
            "answer: The report says the launch moved to November.",
            "answer: The dev server listens on port 8080.",
        ])
        #expect(transcript.status == .ready)
        #expect(transcript.items.last?.turn == 3)
        #expect(transcript.items.last?.range == 2, "turn 3 of the second child, apart from the first's turn 3")
    }
}
