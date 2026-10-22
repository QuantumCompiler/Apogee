import BusinessReads
import BusinessSession
import DataProtocol
import Foundation
import InfrastructureProcess
import PresentationSession
import Testing

/// M7: a saved conversation opened again shows its earlier turns -- read
/// back by `chats show` beside the child starting, over the binary's own
/// recordings: the read (`chats show`, and an older binary's refusal of it)
/// and the resumed session's stream.
@Suite("A resumed session", .timeLimit(.minutes(1)))
@MainActor
struct ResumedSessionTests {
    /// What the reducer folds the read and `lines` of the stream to: what
    /// the window must show, whichever arrived first.
    func folded(_ lines: [String], sending typed: String? = nil) -> Transcript {
        var transcript = Transcript()
        transcript.earlier(savedChat)
        for line in lines {
            transcript.apply(decoded(line))
            if let typed, JSONValue.parse(line)?["type"]?.stringValue == "session" { transcript.userSent(typed) }
        }
        return transcript
    }

    /// A resumed session over `reader`, its child saying what the test emits.
    func resumed(_ reader: ScriptedReader, sessions: [ScriptedSession]) -> (SessionViewModel, ScriptedStarter) {
        let starter = ScriptedStarter(sessions: sessions)
        let model = SessionViewModel(starter: starter, launch: ChatLaunch(resume: savedChat.id), reader: reader)
        return (model, starter)
    }

    @Test("reopened: chats show is read beside the child, its turns stand above the live one, numbered on, with no notice")
    func readFirst() async {
        let reader = ScriptedReader()
        let child = ScriptedSession()
        let (model, starter) = resumed(reader, sessions: [child])
        model.start()
        #expect(await eventually { starter.openedCount == 1 && reader.calls.count == 1 })
        #expect(reader.calls == ["read chats show 20261022-182803-f9f6"])
        #expect(starter.launches.map(\.resume) == ["20261022-182803-f9f6"])
        child.emit(resumedChatLines[0])
        #expect(await eventually { model.transcript.status == .ready })
        #expect(shape(model.transcript) == TranscriptTests.earlierShape)
        model.draft = resumedTurn
        model.submit()
        #expect(await eventually { child.calls == ["user: \(resumedTurn)"] })
        for line in resumedChatLines.dropFirst() { child.emit(line) }
        #expect(await eventually { model.transcript.status == .ready && model.transcript.items.count == 17 })
        #expect(model.transcript.items == folded(resumedChatLines, sending: resumedTurn).items)
        #expect(model.transcript.items.last?.turn == 6)
        #expect(!shape(model.transcript).contains { $0.contains("aren't shown") })
    }

    @Test("the session speaks before the read answers: its events wait for it, the turns still above, the notice never drawn")
    func sessionFirst() async throws {
        let reader = ScriptedReader(held: true)
        let child = ScriptedSession()
        let (model, _) = resumed(reader, sessions: [child])
        var seen: [String] = []
        model.observer = { event in if case .event(let e) = event { seen.append(e.type) } }
        model.start()
        #expect(await eventually { reader.calls.count == 1 })
        child.emit(resumedChatLines[0])
        try await Task.sleep(for: .milliseconds(100))
        #expect(model.transcript.status == .connecting, "the session event waits for the read")
        #expect(model.transcript.items.isEmpty, "no notice in the meantime")
        #expect(seen.isEmpty)
        reader.release()
        #expect(await eventually { model.transcript.status == .ready })
        #expect(shape(model.transcript) == TranscriptTests.earlierShape)
        #expect(seen == ["session"])
        #expect(model.transcript.items == folded([resumedChatLines[0]]).items)
    }

    @Test("a binary without chats show refuses the read: the notice reads as today")
    func olderBinary() async {
        let reader = ScriptedReader(chatsShowRefused)
        let child = ScriptedSession()
        let (model, _) = resumed(reader, sessions: [child])
        model.start()
        #expect(await eventually { reader.calls.count == 1 })
        child.emit(resumedChatLines[0])
        #expect(await eventually { model.transcript.status == .ready })
        #expect(shape(model.transcript) == [
            "notice: resumed: 5 earlier turns of this conversation aren't shown here -- apogee keeps them, and the model still has them"
        ])
    }

    @Test("a document that is no chat reads as none: the notice as today")
    func unreadable() async {
        let reader = ScriptedReader(.undecodable(stdout: "not json", stderrTail: .init()))
        let child = ScriptedSession()
        let (model, _) = resumed(reader, sessions: [child])
        model.start()
        child.emit(resumedChatLines[0])
        #expect(await eventually { model.transcript.status == .ready })
        #expect(shape(model.transcript).count == 1 && shape(model.transcript)[0].hasPrefix("notice: resumed: 5 earlier turns"))
    }

    @Test("a conversation the app paused and carried on itself keeps its own rows, and reads nothing again")
    func pausedAndCarriedOn() async {
        let reader = ScriptedReader()
        let first = ScriptedSession()
        let second = ScriptedSession()
        let (model, starter) = resumed(reader, sessions: [first, second])
        model.start()
        first.emit(resumedChatLines[0])
        #expect(await eventually { model.transcript.status == .ready })
        let kept = model.transcript.items
        await model.pause()
        model.resume()
        #expect(await eventually { starter.openedCount == 2 })
        second.emit(#"{"type":"session","protocol_version":1,"model":"chatty","next_turn":6}"#)
        #expect(await eventually { model.transcript.status == .ready })
        #expect(model.transcript.items == kept, "its own rows, and no second copy of the earlier turns")
        #expect(reader.calls == ["read chats show 20261022-182803-f9f6"], "read once, when it opened")
    }

    @Test("a new chat reads nothing; started again once ended, the conversation it was saved as is read back")
    func newThenStartedAgain() async {
        let reader = ScriptedReader()
        let first = ScriptedSession()
        let second = ScriptedSession()
        let starter = ScriptedStarter(sessions: [first, second])
        let model = SessionViewModel(starter: starter, launch: ChatLaunch(), reader: reader)
        model.start()
        first.emit(#"{"type":"session","protocol_version":1,"model":"chatty","next_turn":1}"#)
        #expect(await eventually { model.transcript.status == .ready })
        #expect(reader.calls.isEmpty, "nothing saved to read")
        model.identify(savedChat.id)
        first.end(.failed)
        #expect(await eventually { model.transcript.status == .ended })
        model.start()
        #expect(await eventually { starter.openedCount == 2 && reader.calls.count == 1 })
        second.emit(resumedChatLines[0])
        #expect(await eventually { model.transcript.status == .ready })
        #expect(starter.launches.map(\.resume) == [nil, savedChat.id])
        #expect(shape(model.transcript) == TranscriptTests.earlierShape)
    }
}
