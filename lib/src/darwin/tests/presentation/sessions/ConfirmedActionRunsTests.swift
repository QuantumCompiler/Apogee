import BusinessReads
import BusinessSession
import DataPreferences
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationSessions

/// GUI M20: on a press SwiftUI runs a confirmation dialog's button, then
/// closes the dialog -- its binding's `set(false)`, which is
/// `dismissPending()`, clearing the question -- and only then runs the `Task`
/// the button started. Every test here confirms in that order: asked,
/// cleared, then the answer the dialog showed handed in.
@Suite("Sessions: a confirmed action runs, in the dialog's order (GUI M20)", .timeLimit(.minutes(1)))
@MainActor
struct ConfirmedActionRunsTests {
    static let session = #"{"type":"session","protocol_version":1,"model":"actor","next_turn":1}"#
    static let twoChats = #"{"object":"list","data":[{"id":"c2","name":"Plans","turns":3,"updated":"2026-10-20T15:00:00Z"},{"id":"c1","name":"Recipes","turns":1,"updated":"2026-10-12T09:00:00Z"}]}"#
    static let plansAlone = #"{"object":"list","data":[{"id":"c2","name":"Plans","turns":3,"updated":"2026-10-20T15:00:00Z"}]}"#
    static let recipesAlone = #"{"object":"list","data":[{"id":"c1","name":"Recipes","turns":1,"updated":"2026-10-12T09:00:00Z"}]}"#

    /// The library in view, its list read and its first session -- `open`,
    /// or a new one -- speaking.
    func library(
        _ runner: ScriptedRunner, _ starter: ScriptedStarter, _ first: ScriptedSession, open: String? = nil
    ) async -> SessionLibraryViewModel {
        runner.reading(["chats", "list"], Self.twoChats)
        let library = SessionLibraryViewModel(
            starter: starter, runner: runner, preferences: Preferences(store: MemoryStore(open.map { ["openChat": $0] } ?? [:])))
        await library.start(inView: true)
        first.emit(Self.session)
        _ = await eventually { library.current.transcript.status == .ready }
        return library
    }

    @Test("Delete: asked, the question cleared as the dialog closes, then its answer handed in -- chats delete runs, the list is read again without it, and Deleted is said")
    func deleteASavedChat() async throws {
        let runner = ScriptedRunner()
        let first = ScriptedSession()
        let starter = ScriptedStarter(sessions: [first])
        let library = await library(runner, starter, first)
        var said: [String] = []
        library.say = { said.append($0) }
        let recipes = try #require(library.chats.first { $0.id == "c1" })
        library.askToDelete(recipes)
        let answer = try #require(library.pending, "the dialog is built from the question")
        library.dismissPending()
        #expect(library.pending == nil, "the dialog's closing cleared it")
        runner.reading(["chats", "list"], Self.plansAlone)
        await library.confirm(answer)
        #expect(runner.calls.contains("run chats delete -- c1"))
        #expect(library.chats.map(\.id) == ["c2"], "read back without it")
        #expect(said.last == "Deleted Recipes")
        #expect(!first.calls.contains("stop"), "another chat's delete leaves the open session alone")
    }

    @Test("the open session's Delete, in the dialog's order: it ends and a new session takes its place, then chats delete runs")
    func deleteTheOpenSession() async throws {
        let runner = ScriptedRunner()
        let first = ScriptedSession("first")
        let second = ScriptedSession("second")
        let starter = ScriptedStarter(sessions: [first, second])
        let library = await library(runner, starter, first, open: "c2")
        var said: [String] = []
        library.say = { said.append($0) }
        let open = library.current
        let plans = try #require(library.currentChat)
        #expect(plans.id == "c2")
        library.askToDelete(plans)
        let answer = try #require(library.pending)
        library.dismissPending()
        runner.reading(["chats", "list"], Self.recipesAlone)
        runner.hold(["chats", "delete"])
        let confirmed = Task { await library.confirm(answer) }
        #expect(await eventually { runner.calls.contains("run chats delete -- c2") })
        #expect(first.calls == ["stop"], "the open session ended first")
        #expect(library.current !== open && library.current.chatID != "c2", "a new session in its place before the delete")
        runner.release()
        await confirmed.value
        #expect(await eventually { starter.openedCount == 2 })
        #expect(starter.launches.last?.resume == nil, "a new session, never the deleted one")
        #expect(library.chats.map(\.id) == ["c1"])
        #expect(said.last == "Deleted Plans")
    }

    @Test("a question replaced before the press: the answer the dialog showed is the one acted on, and the newer question stays to be answered")
    func aNewerQuestion() async throws {
        let runner = ScriptedRunner()
        let first = ScriptedSession()
        let starter = ScriptedStarter(sessions: [first])
        let library = await library(runner, starter, first)
        let recipes = try #require(library.chats.first { $0.id == "c1" })
        let plans = try #require(library.chats.first { $0.id == "c2" })
        library.askToDelete(recipes)
        let shown = try #require(library.pending)
        // Another row's Delete… chosen while the dialog was up.
        library.askToDelete(plans)
        await library.confirm(shown)
        #expect(runner.calls.contains("run chats delete -- c1"), "the answer the dialog showed")
        #expect(!runner.calls.contains("run chats delete -- c2"), "never a question the person did not see")
        #expect(library.pending == .delete(plans), "the newer question stays")
        let newer = try #require(library.pending)
        library.dismissPending()
        await library.confirm(newer)
        #expect(runner.calls.contains("run chats delete -- c2"))
    }

    @Test("Keep it, or Esc, stays a no: the question cleared and nothing run")
    func keepItStaysANo() async throws {
        let runner = ScriptedRunner()
        let first = ScriptedSession()
        let starter = ScriptedStarter(sessions: [first])
        let library = await library(runner, starter, first)
        library.askToDelete(try #require(library.chats.first))
        library.dismissPending()
        #expect(library.pending == nil)
        try? await Task.sleep(for: .milliseconds(60))
        #expect(!runner.calls.contains { $0.hasPrefix("run chats delete") })
        #expect(starter.openedCount == 1 && !first.calls.contains("stop"))
    }

    @Test("Stop the turn and switch, in the dialog's order: the turn stopped and the chat opened")
    func stopTheTurnAndSwitch() async throws {
        let runner = ScriptedRunner()
        let first = ScriptedSession()
        let second = ScriptedSession()
        let starter = ScriptedStarter(sessions: [first, second])
        let library = await library(runner, starter, first)
        library.current.draft = "a long one"
        library.current.submit()
        await library.open(.chat("c1"))
        let answer = try #require(library.pending)
        #expect(answer == .stopTurn(for: .chat("c1")))
        library.dismissPending()
        await library.confirm(answer)
        #expect(await eventually { starter.openedCount == 2 })
        #expect(first.calls.contains("stop"))
        #expect(starter.launches.last?.resume == "c1")
    }

    @Test("a new chat under a suite, asked over a running turn: its yes in the dialog's order still opens it under that suite; Keep it drops the suite")
    func aSuiteGoesWithItsQuestion() async throws {
        let runner = ScriptedRunner()
        let first = ScriptedSession()
        let second = ScriptedSession()
        let third = ScriptedSession()
        let starter = ScriptedStarter(sessions: [first, second, third])
        let library = await library(runner, starter, first)
        library.current.draft = "a long one"
        library.current.submit()
        await library.newSession(under: "pair")
        let kept = try #require(library.pending)
        #expect(kept == .stopTurn(for: .new))
        library.dismissPending()
        await library.newSession()
        #expect(library.pending == .stopTurn(for: .new), "a plain new session asks too")
        let plain = try #require(library.pending)
        library.dismissPending()
        await library.confirm(plain)
        #expect(await eventually { starter.openedCount == 2 })
        let plainLaunch = try #require(starter.launches.dropFirst().first)
        #expect(!plainLaunch.arguments.contains("--suite"), "the suite went with the question kept")
        second.emit(Self.session)
        #expect(await eventually { library.current.transcript.status == .ready })
        library.current.draft = "another long one"
        library.current.submit()
        await library.newSession(under: "pair")
        let underPair = try #require(library.pending)
        library.dismissPending()
        await library.confirm(underPair)
        #expect(await eventually { starter.openedCount == 3 })
        let suiteLaunch = try #require(starter.launches.dropFirst(2).first)
        #expect(suiteLaunch.arguments.suffix(2) == ["--suite", "pair"], "the yes opens it under the suite it was asked with")
    }

    @Test("a newer question for another target, asked over a suite's: its yes leaves the suite behind, so a later plain new chat opens under none")
    func aNewerQuestionLeavesTheSuite() async throws {
        let runner = ScriptedRunner()
        let first = ScriptedSession("first")
        let secret = ScriptedSession("private")
        let third = ScriptedSession("third")
        let starter = ScriptedStarter(sessions: [first, secret, third])
        let library = await library(runner, starter, first)
        library.current.draft = "a long one"
        library.current.submit()
        await library.newSession(under: "pair")
        #expect(library.pending == .stopTurn(for: .new))
        // A private chat chosen while the dialog was up: the newer question.
        await library.newPrivateSession()
        #expect(library.pending == .stopTurn(for: .privately))
        library.dismissPending()
        await library.confirm(.stopTurn(for: .privately))
        #expect(await eventually { starter.openedCount == 2 })
        let privateLaunch = try #require(starter.launches.dropFirst().first)
        #expect(!privateLaunch.arguments.contains("--suite"))
        secret.emit(Self.session)
        #expect(await eventually { library.current.transcript.status == .ready })
        await library.newSession()
        #expect(library.pending == nil, "nothing to ask: no turn runs and nothing is lost")
        #expect(await eventually { starter.openedCount == 3 })
        let plainLaunch = try #require(starter.launches.dropFirst(2).first)
        #expect(!plainLaunch.arguments.contains("--suite"), "a suite the person never chose for this chat")
    }

    @Test("the suite's own question answered after a newer one was asked: its yes still opens the chat under that suite")
    func theSuitesAnswerAfterANewerQuestion() async throws {
        let runner = ScriptedRunner()
        let first = ScriptedSession()
        let second = ScriptedSession()
        let starter = ScriptedStarter(sessions: [first, second])
        let library = await library(runner, starter, first)
        library.current.draft = "a long one"
        library.current.submit()
        await library.newSession(under: "pair")
        let shown = try #require(library.pending)
        #expect(shown == .stopTurn(for: .new))
        await library.newPrivateSession()
        library.dismissPending()
        await library.confirm(shown)
        #expect(await eventually { starter.openedCount == 2 })
        let suiteLaunch = try #require(starter.launches.dropFirst().first)
        #expect(suiteLaunch.arguments.suffix(2) == ["--suite", "pair"], "the answer the dialog showed, under its suite")
    }

    @Test("End it and switch, in the dialog's order: the private conversation ends and the chat opens")
    func endItAndSwitch() async throws {
        let runner = ScriptedRunner()
        let next = ScriptedSession("next")
        let (library, starter, secret) = await Lifecycle.privateConversation(runner, children: [next])
        await library.open(.chat("c1"))
        let answer = try #require(library.pending)
        #expect(answer == .endPrivate(for: .chat("c1")))
        library.dismissPending()
        await library.confirm(answer)
        #expect(secret.calls.contains("stop"))
        #expect(await eventually { starter.openedCount == 3 })
        #expect(starter.launches.last?.resume == "c1")
    }
}
