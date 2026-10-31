import BusinessSession
import DataProtocol
import Foundation
import PresentationSession
import Testing

/// GUI M13: a model named with `-m` that can't start is refused at launch (the
/// CLI's M29): nothing on stdout -- no `session`, no answer under its label --
/// exit 1, and one line on stderr saying why (machine-mode.md -> A launch
/// refused). Over the branch binary's own recordings: `chat -m mlxgone` (an
/// MLX backend with no Python environment), `chat -m nope` (configured
/// nowhere), `execute -m mlxgone`, and a saved chat resumed with `-m mlxgone`
/// as a relaunch after the settings sheet's `set` makes one.
@Suite("A launch refused (M13)", .timeLimit(.minutes(1)))
@MainActor
struct LaunchRefusedTests {
    /// A refusal's recorded stderr and exit.
    static func refusal(_ name: String) -> (words: String, status: Int32) {
        let words = recorded("data/protocol/Fixtures/\(name).err").trimmingCharacters(in: .whitespacesAndNewlines)
        let status = Int32(recorded("data/protocol/Fixtures/\(name).exit").trimmingCharacters(in: .whitespacesAndNewlines)) ?? -1
        return (words, status)
    }

    /// The session's last row, when it is the ending.
    func ending(_ model: SessionViewModel) -> Ending? {
        if case .ended(let ending)? = model.transcript.items.last?.content { ending } else { nil }
    }

    /// A session launched as `launch`, its child refused as recorded in `name`.
    func refused(_ name: String, launch: ChatLaunch, more: [ScriptedSession] = []) async -> (SessionViewModel, ScriptedStarter) {
        let child = ScriptedSession()
        let starter = ScriptedStarter(sessions: [child] + more)
        let model = SessionViewModel(starter: starter, launch: launch)
        model.start()
        #expect(await eventually { starter.openedCount == 1 })
        let refusal = Self.refusal(name)
        child.end(failing: refusal.words, status: refusal.status)
        _ = await eventually { model.transcript.status == .ended }
        return (model, starter)
    }

    @Test("chat -m mlxgone, refused: drawn in the CLI's words -- its one line, as written -- nothing under the model's label")
    func refusedInItsWords() async throws {
        let (model, starter) = await refused("launch-refused-model", launch: ChatLaunch(settings: RunSettings(model: "mlxgone")))
        #expect(starter.launches[0].arguments.contains("mlxgone"))
        let ending = try #require(ending(model))
        #expect(ending.message == Self.refusal("launch-refused-model").words)
        #expect(model.transcript.model == nil, "no session spoke: nothing answers under mlxgone")
        #expect(!model.transcript.items.contains { if case .answer = $0.content { true } else { false } })
        #expect(model.statusText == "Refused at launch")
    }

    @Test("a name configured nowhere, and execute's -m, refused the same way, each in its own words")
    func everyRefusal() async throws {
        for (name, launch) in [
            ("launch-refused-unknown", ChatLaunch(settings: RunSettings(model: "nope"))),
            ("launch-refused-execute", ChatLaunch(settings: RunSettings(model: "mlxgone"), execute: ChatLaunch.Execute())),
        ] {
            let (model, _) = await refused(name, launch: launch)
            #expect(ending(model)?.message == Self.refusal(name).words, "\(name)")
            #expect(model.statusText == "Refused at launch", "\(name)")
        }
    }

    @Test("Try again launches the same session again -- -m and all, never quietly without it")
    func tryAgain() async throws {
        let again = ScriptedSession()
        let (model, starter) = await refused(
            "launch-refused-model", launch: ChatLaunch(settings: RunSettings(model: "mlxgone")), more: [again])
        model.start()
        #expect(await eventually { starter.openedCount == 2 })
        #expect(starter.launches[1].settings.model == "mlxgone")
    }

    @Test("another model chosen after the refusal -- the settings sheet's: the session starts again on it, said, -m the one chosen")
    func anotherModelChosen() async throws {
        let again = ScriptedSession()
        let (model, starter) = await refused(
            "launch-refused-model", launch: ChatLaunch(settings: RunSettings(model: "mlxgone")), more: [again])
        #expect(model.launchModel == "mlxgone", "what the card offers to choose again")
        await model.apply(RunSettings(model: "good"))
        #expect(await eventually { starter.openedCount == 2 })
        #expect(starter.launches[1].settings.model == "good")
        fixtureStream("chat-on-good").prefix(1).forEach(again.emit)
        #expect(await eventually { model.transcript.status == .ready })
        #expect(model.transcript.items.contains { $0.content == .notice("starting again with model good") })
        #expect(model.transcript.launchRefusal == nil && model.statusText == "Ready")
    }

    @Test("a saved chat relaunched with -m the CLI refuses: its turns stay, the refusal drawn after them in the CLI's words")
    func relaunchRefused() async throws {
        let first = ScriptedSession()
        let second = ScriptedSession()
        let starter = ScriptedStarter(sessions: [first, second])
        let saved = JSONValue.parse(fixtureStream("chat-on-good")[0])?["chat_id"]?.stringValue ?? ""
        let model = SessionViewModel(starter: starter, launch: ChatLaunch(resume: saved))
        model.start()
        // Its first child, as the recording answered on good.
        fixtureStream("chat-on-good").prefix(1).forEach(first.emit)
        #expect(await eventually { model.transcript.status == .ready })
        model.draft = "Which suite answers?"
        model.submit()
        fixtureStream("chat-on-good").dropFirst().forEach(first.emit)
        #expect(await eventually { model.transcript.acceptsInput })
        // The settings sheet's model, sent as the set line between turns --
        // which the CLI takes mid-session (M26's routing, untouched by M29).
        await model.apply(RunSettings(model: "mlxgone"))
        #expect(await eventually { first.calls.contains { $0.hasPrefix("set: ") && $0.contains("mlxgone") } })
        await model.pause()
        model.resume()
        #expect(await eventually { starter.openedCount == 2 })
        #expect(starter.launches[1].resume == saved && starter.launches[1].settings.model == "mlxgone")
        let refusal = Self.refusal("launch-refused-resume")
        second.end(failing: refusal.words, status: refusal.status)
        #expect(await eventually { model.transcript.status == .ended })
        #expect(ending(model)?.message == refusal.words)
        #expect(model.transcript.items.contains { if case .answer(let a) = $0.content { a.text == "GOOD-SAYS hello" } else { false } })
    }

    @Test("a session that spoke and then failed is no launch refusal: it ended, as before")
    func endedAfterSpeaking() async throws {
        let child = ScriptedSession()
        let model = SessionViewModel(starter: ScriptedStarter(sessions: [child]), launch: ChatLaunch())
        model.start()
        fixtureStream("chat-on-good").prefix(1).forEach(child.emit)
        #expect(await eventually { model.transcript.status == .ready })
        child.end(failing: "apogee chat: something went wrong")
        #expect(await eventually { model.transcript.status == .ended })
        #expect(ending(model)?.message == "apogee exited with status 1: apogee chat: something went wrong")
        #expect(model.statusText == "Session ended")
    }
}
