import BusinessSession
import DataDiscovery
import Foundation
import PresentationSession
import Testing

@Suite("SessionViewModel", .timeLimit(.minutes(1)))
@MainActor
struct SessionViewModelTests {
    let session = #"{"type":"session","protocol_version":1,"model":"fake","next_turn":1}"#

    @Test("no binary where the app looks: the typed condition, the install hint and every place looked")
    func missingBinary() async {
        let place = SkippedCandidate(
            candidate: BinaryCandidate(url: URL(fileURLWithPath: "/Users/someone/.local/bin/apogee"), source: .release),
            reason: "not installed")
        let model = SessionViewModel(starter: ScriptedStarter(outcome: .notFound(skipped: [place]), sessions: []))
        model.start()
        #expect(await eventually { if case .missing = model.connection { true } else { false } })
        #expect(model.connection == .missing(
            hint: DiscoveryOutcome.installHint, places: ["/Users/someone/.local/bin/apogee: not installed"]))
        #expect(model.statusText == "Apogee not found")
    }

    @Test("a turn is sent as typed, the draft cleared, and the input held until the turn ends")
    func submit() async {
        let scripted = ScriptedSession()
        let model = SessionViewModel(starter: ScriptedStarter(sessions: [scripted]))
        model.start()
        #expect(await eventually { model.connection == .connected(binary: "/Users/someone/.local/bin/apogee", version: "0.1.9") })
        model.draft = "anything"
        #expect(!model.canSend)  // the session has not spoken
        scripted.emit(session)
        #expect(await eventually { model.transcript.status == .ready })
        model.draft = "  what is 2+2?  "
        #expect(model.canSend)
        model.submit()
        #expect(model.draft == "")
        #expect(!model.transcript.acceptsInput)
        #expect(await eventually { scripted.calls == ["user: what is 2+2?"] })
        model.draft = "too soon"
        model.submit()
        scripted.emit(#"{"type":"result","text":"4","turn":1}"#)
        #expect(await eventually { model.transcript.status == .ready })
        #expect(scripted.calls == ["user: what is 2+2?"])
    }

    @Test("answers leave in the order given, one per question, after the turn that asked")
    func answers() async throws {
        let scripted = ScriptedSession()
        let model = SessionViewModel(starter: ScriptedStarter(sessions: [scripted]))
        model.start()
        scripted.emit(session)
        #expect(await eventually { model.transcript.status == .ready })
        model.draft = "ask me"
        model.submit()
        scripted.emit(#"{"type":"question","turn":1,"questions":[{"question":"a?"},{"question":"b?"}]}"#)
        #expect(await eventually { model.transcript.status == .waiting(turn: 1) })
        #expect(model.inputPrompt == "Answer the question above")
        let card = try #require(model.transcript.items.last)
        model.answer(card.id, with: ["first", "second"])
        #expect(await eventually { scripted.calls.count == 3 })
        #expect(scripted.calls == ["user: ask me", "answer: first", "answer: second"])
        #expect(model.transcript.status == .working(turn: 1))
    }

    @Test("Stop and Esc send the protocol's cancel, only while a turn is in flight")
    func cancel() async {
        let scripted = ScriptedSession()
        let model = SessionViewModel(starter: ScriptedStarter(sessions: [scripted]))
        model.start()
        scripted.emit(session)
        #expect(await eventually { model.transcript.status == .ready })
        model.cancel()  // nothing in flight: nothing sent
        model.draft = "go"
        model.submit()
        scripted.emit(#"{"type":"answer_start","turn":1}"#)
        #expect(await eventually { model.canCancel })
        model.cancel()
        #expect(model.transcript.status == .stopping(turn: 1))
        #expect(!model.canCancel)
        #expect(await eventually { scripted.calls == ["user: go", "cancel"] })
        scripted.emit(#"{"type":"result","text":"","finish_reason":"cancelled","turn":1}"#)
        #expect(await eventually { model.transcript.status == .ready })
    }

    @Test("an ended session takes no input, and starting again opens a new one")
    func endedAndRestart() async {
        let first = ScriptedSession()
        let second = ScriptedSession()
        let starter = ScriptedStarter(sessions: [first, second])
        let model = SessionViewModel(starter: starter)
        model.start()
        first.emit(session)
        #expect(await eventually { model.transcript.status == .ready })
        first.end(.failed)
        #expect(await eventually { model.transcript.status == .ended })
        model.draft = "hello"
        #expect(!model.canSend)
        #expect(model.statusText == "Session ended")
        model.start()
        second.emit(session)
        #expect(await eventually { model.transcript.status == .ready })
        #expect(starter.openedCount == 2)
        #expect(model.transcript.items.isEmpty)
    }

    @Test("paused, a saved session keeps its conversation with no ending card, and resumes it with --resume")
    func pauseAndResume() async throws {
        let first = ScriptedSession()
        let second = ScriptedSession()
        let starter = ScriptedStarter(sessions: [first, second])
        let model = SessionViewModel(starter: starter, launch: ChatLaunch(resume: "c1"))
        var seen: [String] = []
        model.observer = { event in if case .event(let e) = event { seen.append(e.type) } else { seen.append("ended") } }
        model.start()
        first.emit(session)
        #expect(await eventually { model.transcript.status == .ready })
        model.draft = "hello"
        model.submit()
        first.emit(#"{"type":"result","text":"hi","turn":1}"#)
        #expect(await eventually { model.transcript.status == .ready })
        let kept = model.transcript.items
        await model.pause()
        #expect(model.isPaused)
        #expect(model.statusText == "Paused")
        #expect(first.calls.last == "stop")
        #expect(model.transcript.items == kept, "no ending card: the session was freed, not finished")
        model.resume()
        #expect(await eventually { starter.openedCount == 2 })
        #expect(starter.launches.map(\.resume) == ["c1", "c1"])
        #expect(model.transcript.status == .connecting)
        second.emit(#"{"type":"session","protocol_version":1,"model":"fake","next_turn":2}"#)
        #expect(await eventually { model.transcript.status == .ready })
        #expect(model.transcript.items == kept)
        #expect(seen == ["session", "result", "ended", "session"], "the observer is told every event, the pause's end too")
    }

    @Test("a session the library identified resumes by that id; one never saved starts afresh")
    func identifiedOrFresh() async {
        let first = ScriptedSession()
        let second = ScriptedSession()
        let starter = ScriptedStarter(sessions: [first, second])
        let model = SessionViewModel(starter: starter)
        model.start()
        first.emit(session)
        #expect(await eventually { model.transcript.status == .ready })
        model.draft = "hello"
        model.submit()
        first.emit(#"{"type":"result","text":"hi","turn":1}"#)
        #expect(await eventually { model.transcript.status == .ready })
        await model.pause()
        model.resume()
        #expect(await eventually { starter.openedCount == 2 })
        #expect(starter.launches.map(\.resume) == [nil, nil])
        #expect(model.transcript.items.isEmpty, "nothing of an unsaved session could be resumed")

        let third = ScriptedSession()
        let fourth = ScriptedSession()
        let again = ScriptedStarter(sessions: [third, fourth])
        let named = SessionViewModel(starter: again)
        named.start()
        third.emit(session)
        #expect(await eventually { named.transcript.status == .ready })
        named.identify("c9")
        named.identify("c10")
        #expect(named.chatID == "c9", "an id, once known, stays")
        await named.pause()
        named.resume()
        #expect(await eventually { again.openedCount == 2 })
        #expect(again.launches.map(\.resume) == [nil, "c9"])
    }

    @Test("the window over a real session: the recorded mock-actor conversation, answered, every line byte-exact")
    func mockActorEndToEnd() async throws {
        let child = ReplayChild(replaying: mockActorLines)
        let model = SessionViewModel(starter: ReplayStarter(child: child))
        model.start()
        #expect(await eventually { model.transcript.status == .ready })
        model.draft = "Write me a note."
        model.submit()

        func waitingCard() -> TranscriptItem? {
            model.transcript.items.last { item in
                switch item.content {
                case .question(let card): card.answers == nil
                case .permission(let card): card.answer == nil
                default: false
                }
            }
        }
        #expect(await eventually { waitingCard() != nil })
        model.answer(try #require(waitingCard()).id, with: ["todo.md"])
        #expect(await eventually {
            if case .permission = waitingCard()?.content { true } else { false }
        })
        model.answer(try #require(waitingCard()).id, with: ["yes"])
        #expect(await eventually { model.transcript.status == .ready && model.transcript.items.count == 7 })

        guard case .answer(let answer) = model.transcript.items.last?.content else {
            Issue.record("the turn did not end in its answer")
            return
        }
        #expect(answer.text == "I wrote todo.md with one item: buy milk.")
        #expect(answer.finished)
        #expect(child.written == [
            #"{"type":"hello","client":{"name":"Apogee","version":"0.2.0"},"wants":["tools"]}"#,
            #"{"type":"user","text":"Write me a note."}"#,
            #"{"type":"answer","text":"todo.md"}"#,
            #"{"type":"answer","text":"yes"}"#,
        ])
        await model.shutDown()
        #expect(await eventually { model.transcript.status == .ended })
        guard case .ended(let ending) = model.transcript.items.last?.content else { return }
        #expect(ending.reason == .finished)
    }
}
