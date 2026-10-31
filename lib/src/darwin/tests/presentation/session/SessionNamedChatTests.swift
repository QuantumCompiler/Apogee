import BusinessSession
import DataProtocol
import Foundation
import PresentationSession
import Testing

/// GUI M10: the chat a session's `session` event names (`chat_id`, the
/// CLI's M28) is the one it is, at once -- but the CLI writes a new chat only
/// once a turn of it is answered, so `--resume`, Rename and Delete wait until
/// it is known written (resumed, or found listed by the library). Over the
/// branch binary's recordings: a session left unsent (`new-unsent`) and one
/// that answered (`switched-to-local`).
@Suite("A session's named chat (M10)", .timeLimit(.minutes(1)))
@MainActor
struct SessionNamedChatTests {
    static let unsent = fixtureStream("new-unsent")
    static let switched = fixtureStream("switched-to-local")
    static let unsentID = "20261026-172749-5dec"
    static let switchedID = "20261026-172749-d4cb"

    static let rename = SlashCommand.all.first { $0.act == .library(.rename) }!

    @Test("named at once, written only once identified: /rename waits, and a pause carries an unwritten chat on afresh, named anew")
    func namedThenWritten() async {
        let children = [ScriptedSession(), ScriptedSession(), ScriptedSession()]
        let starter = ScriptedStarter(sessions: children)
        let model = SessionViewModel(starter: starter)
        model.start()
        children[0].emit(Self.unsent[0])
        #expect(await eventually { model.transcript.status == .ready })
        #expect(model.chatID == Self.unsentID && !model.isSaved && model.savedChatID == nil)
        #expect(!model.available(Self.rename), "nothing of it is written for chats title to rename")
        await model.pause()
        model.resume()
        #expect(await eventually { starter.openedCount == 2 })
        #expect(starter.launches[1].resume == nil, "--resume of a chat never written would be refused")
        #expect(model.chatID == nil)
        children[1].emit(Self.switched[0])
        #expect(await eventually { model.chatID == Self.switchedID }, "the new child's chat")
        model.identify(Self.switchedID)
        #expect(model.savedChatID == Self.switchedID && model.available(Self.rename))
        await model.pause()
        model.resume()
        #expect(await eventually { starter.openedCount == 3 })
        #expect(starter.launches[2].resume == Self.switchedID)
    }

    @Test("a resumed chat is written from the start; its session event naming it changes nothing")
    func resumedIsWritten() async {
        let child = ScriptedSession()
        let starter = ScriptedStarter(sessions: [child])
        let model = SessionViewModel(starter: starter, launch: ChatLaunch(resume: Self.switchedID))
        #expect(model.savedChatID == Self.switchedID)
        model.start()
        child.emit(Self.switched[0])
        #expect(await eventually { model.transcript.status == .ready })
        #expect(model.savedChatID == Self.switchedID)
        #expect(starter.launches.map(\.resume) == [Self.switchedID])
    }

    @Test("the library's guess, from a binary before M28, can't take a chat over one the session named")
    func namedWinsOverAGuess() async {
        let child = ScriptedSession()
        let model = SessionViewModel(starter: ScriptedStarter(sessions: [child]))
        model.start()
        child.emit(Self.switched[0])
        #expect(await eventually { model.chatID == Self.switchedID })
        model.identify("20261025-050011-73de")
        #expect(model.chatID == Self.switchedID && !model.isSaved)
    }
}
