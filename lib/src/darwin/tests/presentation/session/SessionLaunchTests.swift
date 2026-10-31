import BusinessSession
import DataProtocol
import Foundation
import PresentationSession
import Testing

/// 4a: a session still launching -- its binary being located, no child yet
/// -- can be stopped, paused or restarted, and the one-live-child law holds
/// in that window as in every other. The locate is held by the scripted
/// starter until the test lets it answer: the child that would arrive after
/// the stop.
@Suite("A session's launch", .timeLimit(.minutes(1)))
@MainActor
struct SessionLaunchTests {
    static let old = #"{"type":"session","protocol_version":1,"model":"local","next_turn":1,"capabilities":{"events":[],"accepts":["user","answer","attach","hello","cancel"],"tools":true,"ask":true,"schema":"2026-10-13"}}"#

    /// Lets whatever the last act set going run on: nothing more may open.
    func settle() async {
        try? await Task.sleep(for: .milliseconds(60))
    }

    @Test("stopped while its binary is located: the launch is called off, and no child comes once the locate answers")
    func stopWhileLocating() async {
        let starter = ScriptedStarter(sessions: [ScriptedSession(), ScriptedSession()], holdsLocate: true)
        let model = SessionViewModel(starter: starter)
        model.start()
        #expect(await eventually { starter.locateCalls == 1 })
        #expect(model.connection == .locating)
        await model.shutDown()
        #expect(starter.live == 0)
        starter.releaseLocate()
        await settle()
        #expect(starter.openedCount == 0, "the child that would have come after the stop never opens")
        #expect(starter.live == 0)
        model.start()
        await settle()
        #expect(starter.openedCount == 0, "ended for good: nothing starts it again")
    }

    @Test("paused while its binary is located: the launch is called off and the session paused; resumed, it launches once")
    func pauseWhileLocating() async {
        let first = ScriptedSession()
        let starter = ScriptedStarter(sessions: [first, ScriptedSession()], holdsLocate: true)
        let model = SessionViewModel(starter: starter, launch: ChatLaunch(resume: "c1"))
        model.start()
        #expect(await eventually { starter.locateCalls == 1 })
        await model.pause()
        #expect(model.isPaused)
        starter.releaseLocate()
        await settle()
        #expect(starter.openedCount == 0)
        #expect(starter.live == 0)
        model.resume()
        #expect(await eventually { starter.openedCount == 1 })
        #expect(starter.launches.map(\.resume) == ["c1"])
        first.emit(#"{"type":"session","protocol_version":1,"model":"fake","next_turn":2}"#)
        #expect(await eventually { model.transcript.status == .ready })
        await settle()
        #expect(starter.openedCount == 1)
        #expect(starter.live == 1)
    }

    @Test("a settings restart while its binary is located supersedes the launch: one child, not two")
    func restartWhileLocating() async {
        let starter = ScriptedStarter(sessions: [ScriptedSession(), ScriptedSession()], holdsLocate: true)
        let model = SessionViewModel(starter: starter)
        model.start()
        #expect(await eventually { starter.locateCalls == 1 })
        await model.apply(RunSettings(model: "claude"))
        #expect(await eventually { starter.locateCalls == 2 })
        starter.releaseLocate()
        #expect(await eventually { starter.openedCount == 1 })
        await settle()
        #expect(starter.openedCount == 1, "the launch it replaced opens nothing")
        #expect(starter.live == 1)
        #expect(starter.launches.map(\.settings) == [RunSettings(model: "claude")])
    }

    @Test("stopped while a settings restart waits for the old child: the restart opens no child after the stop")
    func stopDuringRestart() async {
        let first = ScriptedSession(stopping: .whenReleased)
        let starter = ScriptedStarter(sessions: [first, ScriptedSession()])
        let model = SessionViewModel(starter: starter, launch: ChatLaunch(resume: "c1"))
        model.start()
        first.emit(Self.old)
        #expect(await eventually { model.transcript.status == .ready })
        let restarting = Task { await model.apply(RunSettings(model: "claude")) }
        #expect(await eventually { first.calls == ["stop"] })
        let stopping = Task { await model.shutDown() }
        first.releaseStop()
        await restarting.value
        await stopping.value
        await settle()
        #expect(starter.openedCount == 1, "nothing is started after the stop")
        #expect(starter.live == 0)
    }
}
