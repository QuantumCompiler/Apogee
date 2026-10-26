#if DEBUG
import BusinessReads
import DataDiscovery
import DataPreferences
import DataProtocol
import Foundation
import PresentationSession
import PresentationShell

/// The Sessions place's view-models for its previews, over the binary's
/// recorded documents and the library's tests' own (ADR 0004): nothing
/// starts the binary.
@MainActor
enum SessionsPreview {
    // MARK: - The recorded conversation

    static let mockActor = SessionsDocuments.mockActor.split(separator: "\n").map(String.init)

    /// A saved conversation resumed: its `session` event, then one live turn
    /// -- and the conversation as `chats list` and `chats show` read it before.
    static let resumedChat = SessionsDocuments.resumedChat.split(separator: "\n").map(String.init)
    static var savedChatID: String { ChatSummary.list(from: ReadDocument(text: SessionsDocuments.chatsList)!)[0].id }
    /// What its live turn was sent as, recorded with it.
    static let resumedTurn = "Thanks -- anything else?"

    /// What the person sends and answers in it, as the session window's
    /// end-to-end test does (tests/presentation/session/SessionViewModelTests).
    static let noteTurn = "Write me a note."
    static let noteAnswers = ["todo.md", "yes"]

    // MARK: - Documents the library's tests hold

    /// Two saved conversations, each naming its backend (SessionLibraryTests'
    /// `glyphFallback`).
    static let chatsList = #"{"object":"list","data":[{"id":"c2","name":"Plans","turns":3,"updated":"2026-10-20T15:00:00Z","backend":"claude","model":""},{"id":"c1","name":"Recipes","turns":1,"updated":"2026-10-12T09:00:00Z","backend":"actor","model":""}]}"#

    /// The backends those name, and their types (the same test).
    static let modelsList = #"{"object":"list","data":[{"backend":"claude","backend_type":"anthropic"},{"backend":"actor","backend_type":"mock"}]}"#

    /// A conversation in each date group, and one undated (SessionLibraryTests'
    /// `groupsAndSearch`).
    static let everyGroup = #"{"object":"list","data":[{"id":"a","name":"Today's plan","turns":1,"updated":"2026-10-20T08:00:00Z"},{"id":"b","name":"Yesterday's","turns":1,"updated":"2026-10-19T12:00:00Z"},{"id":"c","name":"Last week","turns":1,"updated":"2026-10-14T12:00:00Z"},{"id":"d","name":"Long ago PLAN","turns":1,"updated":"2026-09-01T12:00:00Z"},{"id":"e","name":"Undated","turns":1}]}"#

    /// The roles, as `models status` reads them (SessionLibraryTests' `inspector`).
    static let modelsStatus = #"{"fields":[{"field":"chat","value":"actor"},{"field":"vision","value":"(unset -- the chat's own backend)"}]}"#

    /// A build with `--private`: its `session` line accepts `set`
    /// (SessionLibraryPrivateTests).
    static let newerBuild = #"{"type":"session","protocol_version":1,"model":"local","next_turn":1,"capabilities":{"events":[],"accepts":["user","answer","attach","hello","cancel","detach","set","compact"],"tools":true,"ask":true,"schema":"2026-10-20"}}"#

    /// A conversation on that build: its `session` line, then the recorded
    /// mock-actor turn after the recording's own `session` line -- a private
    /// session there is `--private`, so the turn is a conversation nothing
    /// of which is written.
    static let privateConversation = [newerBuild] + Array(mockActor.dropFirst())

    /// The configured suites, as `config get suites` prints them, and the
    /// folder a door's session opens in (ExecuteDoorTests).
    static let suites = "duo\nfast\n"
    static let project = URL(fileURLWithPath: "/Users/someone/code/ingest")

    /// 2026-10-20 18:00 UTC: the tests' now, so the date groups hold.
    static let now = Date(timeIntervalSince1970: 1_792_519_200)

    /// The saved conversations `chatsList` names, as rows.
    static var chats: [ChatSummary] { ChatSummary.list(from: ReadDocument(text: chatsList)!) }

    // MARK: - The library

    /// Answers the library's reads and the door's commands; `defaultSuite`
    /// is `models.default_suite`, empty when none is set.
    static func runner(chats: String, defaultSuite: String) -> PreviewRunner {
        PreviewRunner(
            reads: [
                (["chats", "list"], chats),
                (["chats", "show"], SessionsDocuments.chatsShow),
                (["models", "list"], modelsList),
                (["models", "status"], modelsStatus),
                (["symphonies", "list"], SessionsDocuments.symphoniesList),
            ],
            commands: [
                (["config", "get", "suites"], suites),
                (["config", "get", "models.default_suite"], defaultSuite + "\n"),
            ])
    }

    /// The library over `chats`, each session it opens saying `lines` --
    /// or, `missing`, no binary found. `openChat` is the conversation left
    /// open last time. Not started.
    static func library(
        chats: String = chatsList, lines: [String] = mockActor, missing: Bool = false, openChat: String? = nil,
        defaultSuite: String = ""
    ) -> SessionLibraryViewModel {
        let now = Self.now
        return SessionLibraryViewModel(
            starter: PreviewChats(lines: lines, missing: missing ? passedOver : nil),
            runner: runner(chats: chats, defaultSuite: defaultSuite),
            preferences: Preferences(store: MemoryStore(openChat.map { ["openChat": $0] } ?? [:])),
            now: { now })
    }

    /// The window around it, its preferences held in memory.
    static func shell() -> ShellViewModel {
        ShellViewModel(preferences: Preferences(store: MemoryStore()))
    }

    /// Every place the app looks, on a Mac with no apogee installed: each
    /// candidate discovery names for the home, passed over as it says of a
    /// file that is not there (`BinaryDiscovery.candidates`, `locate`).
    static let passedOver = [
        SkippedCandidate(candidate: BinaryCandidate(url: previewBinary, source: .release), reason: "not installed"),
        SkippedCandidate(
            candidate: BinaryCandidate(url: URL(fileURLWithPath: "/Users/someone/.local/bin/apogee-dev"), source: .dev),
            reason: "not installed"),
        SkippedCandidate(
            candidate: BinaryCandidate(url: URL(fileURLWithPath: "/Users/someone/.local/bin/apogee-test"), source: .test),
            reason: "not installed"),
    ]

    // MARK: - Driving a session as the person does

    /// Waits until `condition` holds -- five seconds at most, as the tests'
    /// `eventually` does.
    static func until(_ condition: () -> Bool) async {
        let deadline = ContinuousClock.now + .seconds(5)
        while !condition() && ContinuousClock.now < deadline {
            try? await Task.sleep(for: .milliseconds(10))
        }
    }

    /// Sends `typed` once `session` has spoken, then answers each card as it
    /// stands with the next of `answers`.
    static func converse(_ session: SessionViewModel, typed: String, answers: [String] = []) async {
        session.start()
        await until { session.transcript.status == .ready }
        session.draft = typed
        session.submit()
        for answer in answers {
            await until { waiting(session) != nil }
            guard let card = waiting(session) else { return }
            session.answer(card, with: [answer])
        }
    }

    /// The card that waits on the person, by its id.
    static func waiting(_ session: SessionViewModel) -> Int? {
        session.transcript.items.last { item in
            switch item.content {
            case .question(let card): card.answers == nil
            case .permission(let card): card.answer == nil
            default: false
            }
        }?.id
    }
}
#endif
