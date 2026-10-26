import BusinessSession
import DataProtocol
import Foundation
import PresentationSession
import Testing

/// GUI M10, the first defect: the window's label is the model the CLI says
/// answered -- `result.model` -- else the one its `session` event named, never
/// the opening event alone. Over the branch binary's own recording
/// (`switched-to-local.jsonl`): a session on `claude`, a turn, the settings
/// sheet's `set` to the local model, the CLI's notice, a turn the local model
/// answered -- and the same stream as a binary before the CLI's M28 wrote it,
/// with no `chat_id` (`switched-to-local-older-handmade.jsonl`).
@Suite("The label follows the model that answered (M10)", .timeLimit(.minutes(1)))
@MainActor
struct SessionLabelTests {
    static let switched = fixtureStream("switched-to-local")
    static let older = fixtureStream("switched-to-local-older-handmade")
    /// What the recording sent, turn by turn.
    static let turns = ["Which port does the dev server use?", "And on the local model?"]
    static let local = "Gemma4-26B-Instruct"
    static let notice = "switched to Gemma4-26B-Instruct -- from the next turn"

    static func type(_ line: String) -> String? { JSONValue.parse(line)?["type"]?.stringValue }

    /// `lines` folded as the window folds them -- each turn sent as the
    /// recording sent it, before its first event -- and the label after each
    /// line, by the line's type.
    static func labels(_ lines: [String]) -> (transcript: Transcript, labels: [(type: String, model: String?)]) {
        var transcript = Transcript()
        var unsent = turns[...]
        var sent = 0
        var labels: [(type: String, model: String?)] = []
        for line in lines {
            let event = EventDecoder.decode(line: line)
            if let turn = event.turn, turn > sent, let text = unsent.popFirst() {
                transcript.userSent(text)
                sent = turn
            }
            transcript.apply(.event(event))
            labels.append((event.type, transcript.model))
        }
        return (transcript, labels)
    }

    @Test("the recorded switch: claude until the local model answers, then the local model's -- never claude again")
    func labelFollowsTheAnswer() throws {
        let (transcript, labels) = Self.labels(Self.switched)
        let results = labels.enumerated().filter { $0.element.type == "result" }.map(\.offset)
        try #require(results.count == 2)
        #expect(labels[...results[1]].dropLast().allSatisfy { $0.model == "claude" }, "the opening model, and the one that answered turn 1")
        #expect(labels[results[1]].model == Self.local)
        #expect(transcript.model == Self.local, "the label ends as the local model's")
        #expect(transcript.model != "claude")
    }

    @Test("between the switch and the next answer the label keeps the model that last answered; the CLI's own notice stands in the transcript")
    func noticeStandsTheLabelWaits() throws {
        let (_, labels) = Self.labels(Self.switched)
        let notice = try #require(labels.firstIndex { $0.type == "notice" })
        #expect(labels[notice].model == "claude")
        let (transcript, _) = Self.labels(Array(Self.switched.prefix(notice + 1)))
        #expect(transcript.items.last?.content == .notice(Self.notice), "the CLI's words, as written")
        #expect(transcript.model == "claude")
    }

    @Test("a notice is never read for the model's name: only an answer moves the label")
    func noticeNeverParsed() {
        var transcript = Transcript()
        transcript.apply(decoded(Self.switched[0]))
        transcript.apply(decoded(#"{"type":"notice","text":"switched to Gemma4-26B-Instruct -- from the next turn"}"#))
        #expect(transcript.model == "claude")
        transcript.userSent("hi")
        transcript.apply(decoded(#"{"type":"result","text":"ok","turn":1}"#))
        #expect(transcript.model == "claude", "a result that names no model leaves the label as it was")
        transcript.userSent("again")
        transcript.apply(decoded(#"{"type":"result","text":"ok","model":"","turn":2}"#))
        #expect(transcript.model == "claude", "nor does one naming none")
    }

    @Test("a binary before M28 -- no chat_id -- labels the same: the answer's model")
    func olderBinaryLabelsTheSame() {
        #expect(JSONValue.parse(Self.older[0])?["chat_id"] == nil)
        #expect(Self.labels(Self.older).transcript.model == Self.local)
    }

    @Test("the window's header over a live session: the sheet's set line sent, the notice, then the local model's answer -- the header says it")
    func headerFollowsTheSwitch() async {
        let child = ScriptedSession()
        let model = SessionViewModel(starter: ScriptedStarter(sessions: [child]))
        model.start()
        child.emit(Self.switched[0])
        #expect(await eventually { model.transcript.status == .ready })
        let notice = Self.switched.firstIndex { Self.type($0) == "notice" }!
        model.draft = Self.turns[0]
        model.submit()
        Self.switched[1..<notice].forEach(child.emit)
        #expect(await eventually { model.transcript.acceptsInput })
        await model.apply(RunSettings(model: Self.local))
        #expect(await eventually { child.calls.last == #"set: {"type":"set","model":"Gemma4-26B-Instruct"}"# })
        child.emit(Self.switched[notice])
        #expect(await eventually { model.transcript.items.last?.content == .notice(Self.notice) })
        #expect(model.transcript.model == "claude", "said from the next turn: nothing has answered on it yet")
        model.draft = Self.turns[1]
        model.submit()
        Self.switched[(notice + 1)...].forEach(child.emit)
        #expect(await eventually { model.transcript.acceptsInput && model.transcript.model == Self.local })
    }
}
