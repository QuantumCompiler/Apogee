import BusinessSession
import DataProtocol
import Testing

private func event(_ line: String) -> ProtocolEvent { EventDecoder.decode(line: line) }

@Suite("TurnTracker")
struct TurnTrackerTests {
    @Test("a turn opens with its user line, waits on its questions, and closes with its result")
    func oneTurn() {
        var tracker = TurnTracker()
        #expect(tracker.phase == .opening)
        tracker.apply(event(#"{"type":"session","protocol_version":1,"model":"m","next_turn":1}"#))
        #expect(tracker.phase == .idle)
        #expect(!tracker.turnInFlight)

        #expect(tracker.sent() == 1)
        #expect(tracker.phase == .running(turn: 1))
        tracker.apply(event(#"{"type":"answer_delta","text":"x","turn":1}"#))
        #expect(tracker.phase == .running(turn: 1))
        tracker.apply(event(#"{"type":"question","turn":1,"questions":[{"question":"a"},{"question":"b"}]}"#))
        #expect(tracker.phase == .asking(turn: 1, unanswered: 2))
        #expect(tracker.turnInFlight)
        tracker.answered()
        #expect(tracker.phase == .asking(turn: 1, unanswered: 1))
        tracker.answered()
        #expect(tracker.phase == .running(turn: 1))
        tracker.apply(event(#"{"type":"result","text":"done","turn":1}"#))
        #expect(tracker.phase == .idle)
        #expect(tracker.nextTurn == 2)
    }

    @Test("a resumed conversation numbers its turns past the ones it holds")
    func resumed() {
        var tracker = TurnTracker()
        tracker.apply(event(#"{"type":"session","protocol_version":1,"model":"m","next_turn":7}"#))
        #expect(tracker.sent() == 7)
        #expect(tracker.phase == .running(turn: 7))
    }

    @Test("a line sent before the session spoke is renumbered from next_turn")
    func earlyLine() {
        var tracker = TurnTracker()
        #expect(tracker.sent() == 1)
        #expect(tracker.phase == .opening)
        tracker.apply(event(#"{"type":"session","protocol_version":1,"model":"m","next_turn":4}"#))
        #expect(tracker.open == [4])
        #expect(tracker.phase == .running(turn: 4))
        #expect(tracker.nextTurn == 5)
    }

    @Test("an error ends its turn as a result does, and a second line queued behind it runs next")
    func errorAndQueue() {
        var tracker = TurnTracker()
        tracker.apply(event(#"{"type":"session","protocol_version":1,"model":"m"}"#))
        _ = tracker.sent()
        _ = tracker.sent()
        #expect(tracker.open == [1, 2])
        tracker.apply(event(#"{"type":"error","message":"refused","turn":1}"#))
        #expect(tracker.phase == .running(turn: 2))
        tracker.apply(event(#"{"type":"result","text":"","finish_reason":"cancelled","turn":2}"#))
        #expect(tracker.phase == .idle)
    }

    @Test("the session's end ends every turn")
    func end() {
        var tracker = TurnTracker()
        _ = tracker.sent()
        tracker.end()
        #expect(tracker.phase == .over)
        #expect(tracker.open.isEmpty)
        tracker.apply(event(#"{"type":"result","text":"","turn":1}"#))
        #expect(tracker.phase == .over)
    }
}
