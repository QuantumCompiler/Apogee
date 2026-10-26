#if DEBUG
import BusinessReads
import BusinessSession
import DataDiscovery
import DataProtocol
import Foundation
import InfrastructureProcess

/// The session window's view-models and transcripts for its previews, over
/// the binary's recorded streams (ADR 0004): nothing starts the binary.
@MainActor
enum SessionPreview {
    // MARK: - The recorded streams

    static let mockActor = lines(SessionDocuments.mockActor)
    static let executePlay = lines(SessionDocuments.executePlay)
    static let executeOrchestrate = lines(SessionDocuments.executeOrchestrate)
    static let executePlayRefused = lines(SessionDocuments.executePlayRefused)
    static let executeProject = lines(SessionDocuments.executeProject)
    /// A saved conversation resumed: its `session` event, then one live turn.
    static let resumedChat = lines(SessionDocuments.resumedChat)
    /// A turn the scripted mock answered in markdown, every shape the app draws.
    static let markdownAnswer = lines(SessionDocuments.markdownAnswer)
    /// A turn the scripted mock answered with fenced code in several
    /// languages, a diff, an unnamed fence and an Elixir one.
    static let codeAnswer = lines(SessionDocuments.codeAnswer)
    /// A turn that asks, answered, then a `set` between turns and its notice.
    static let setBetweenTurns = lines(SessionDocuments.setBetweenTurns)
    /// A turn stopped while its question waited, and while its permission
    /// prompt did.
    static let questionCancelled = lines(SessionDocuments.questionCancelled)
    static let permissionCancelled = lines(SessionDocuments.permissionCancelled)
    /// A chat holding an attachment resumed, and the attachment detached.
    static let resumedDetach = lines(SessionDocuments.resumedDetach)
    /// A chat with a message the loader dropped, resumed: the branch binary
    /// says so in a notice, a binary before 55b on stderr alone.
    static let resumedWarnings = lines(SessionDocuments.resumedWarnings)
    static let resumedWarningsOlder = lines(SessionDocuments.resumedWarningsOlder)

    /// What the person sends and answers in the mock-actor conversation, as
    /// the window's end-to-end test does (SessionViewModelTests'
    /// `mockActorEndToEnd`).
    static let noteTurn = "Write me a note."
    static let noteAnswers = ["todo.md", "yes"]

    /// What the resumed conversation's live turn was sent as, recorded with
    /// it (TranscriptTests' `earlierThenLive`).
    static let resumedTurn = "Thanks -- anything else?"

    /// The saved conversation that stream resumes, as `chats show` read it
    /// back before the resume.
    static var savedChat: SavedChat { SavedChat(document: ReadDocument(text: SessionDocuments.chatsShow)!)! }

    /// Its launch: `--resume` with its id.
    static var resumeSaved: ChatLaunch { ChatLaunch(resume: savedChat.id) }

    /// The chat holding an attachment, as `chats show` read it, and its launch.
    static var attachedChat: SavedChat { SavedChat(document: ReadDocument(text: SessionDocuments.chatsShowAttached)!)! }
    static var resumeAttached: ChatLaunch { ChatLaunch(resume: attachedChat.id) }

    /// The chat with a message dropped, as the branch binary's `chats show`
    /// read it -- and as one before 55b did, its stderr's line beside it.
    static var warnedChat: SavedChat { SavedChat(document: ReadDocument(text: SessionDocuments.chatsShowWarnings)!)! }
    static var warnedChatOlder: SavedChat {
        SavedChat(document: ReadDocument(text: SessionDocuments.chatsShowWarningsOlder)!)!
    }

    /// What the markdown turn was sent as, recorded with it (TranscriptTests'
    /// `markdownLiveAndReadBack`).
    static let markdownTurn = "My scraper's TaskGroup dies when one host times out. How do I keep the batch going?"

    /// That conversation as `chats show` read it back.
    static var markdownChat: SavedChat { SavedChat(document: ReadDocument(text: SessionDocuments.chatsShowMarkdown)!)! }

    /// What the code turn was sent as, recorded with it (TranscriptTests'
    /// `codeLiveAndReadBack`).
    static let codeTurn = "Show me the retry everywhere it runs: the scraper, the app, the browser, the worker, the shell, the settings and the patch."

    /// That conversation as `chats show` read it back.
    static var codeChat: SavedChat { SavedChat(document: ReadDocument(text: SessionDocuments.chatsShowCode)!)! }

    /// What each `execute` stream was sent as (PlayNarrationTests).
    static let playTurn = "/play echo2 hello"
    static let refusedTurn = "/play nosuch hello"

    /// The symphonies `/play` offers, as `symphonies list` read them -- the
    /// library's reading of each row, its name and description.
    static var plays: [PlayChoice] {
        (ReadDocument(text: SessionDocuments.symphoniesList)?.entries ?? []).compactMap { entry in
            guard let name = entry["name"]?.stringValue else { return nil }
            return PlayChoice(name: name, description: entry["description"]?.stringValue ?? "")
        }
    }

    // MARK: - Lines the session's tests hold

    /// The `session` lines of a build before the CLI's 52d and of one with it
    /// -- `detach`, `set` and `compact` accepted (SessionStatesTests).
    static let olderBuild = #"{"type":"session","protocol_version":1,"model":"local","next_turn":1,"capabilities":{"events":[],"accepts":["user","answer","attach","hello","cancel"],"tools":true,"ask":true,"schema":"2026-10-13"}}"#
    static let newerBuild = #"{"type":"session","protocol_version":1,"model":"local","next_turn":1,"capabilities":{"events":[],"accepts":["user","answer","attach","hello","cancel","detach","set","compact"],"tools":true,"ask":true,"schema":"2026-10-20"}}"#

    /// The bare `session` line the transcript's tests open with (TranscriptTests).
    static let bareSession = #"{"type":"session","protocol_version":1,"model":"m"}"#

    /// The configured models the library hands the settings sheet over 3a's
    /// registered kinds, as it reads `models list` at launch: the chat and
    /// vision entries, never the embedding entry (`ModelRow.chatBackends`,
    /// the GUI's M4).
    static var chatChoices: [String] {
        ModelRow.chatBackends(ModelRow.list(from: ReadDocument(text: SessionDocuments.kindsRegistered)!))
    }

    // MARK: - Sessions

    static func lines(_ document: String) -> [String] {
        document.split(separator: "\n").map(String.init)
    }

    /// A session over a chat saying `lines`, short of the first line of type
    /// `silentFrom`, its child ending as `ending` says once every line is
    /// said; a saved conversation it resumes read back through `reader`.
    /// Not started.
    static func session(
        _ lines: [String], silentFrom: String? = nil, ending: SessionEnd? = nil, launch: ChatLaunch = ChatLaunch(),
        reader: PreviewReader? = nil
    ) -> SessionViewModel {
        SessionViewModel(
            starter: PreviewChats(lines: lines, silentFrom: silentFrom, ending: ending), launch: launch, reader: reader)
    }

    /// A session where no binary is found; not started.
    static func missing() -> SessionViewModel {
        SessionViewModel(starter: PreviewChats(missing: passedOver))
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

    /// A private session's launch: `--private`, nothing written -- or, on a
    /// build before it, `--no-recall`, deleted when it closes.
    static let privately = ChatLaunch(privately: true)
    static let noRecall = ChatLaunch(noRecall: true)

    /// An `execute` session's launch, under the config's suite; `orchestrate`
    /// offers its symphonies to the model.
    static func execute(orchestrate: Bool = false) -> ChatLaunch {
        ChatLaunch(execute: ChatLaunch.Execute(orchestrate: orchestrate))
    }

    // MARK: - A chat under a suite (GUI 4i)

    /// The recorded streams, by the suite each was launched under.
    static let suiteChats = PreviewSuiteChats(
        streams: [
            "pair": lines(SessionDocuments.chatUnderSuite), "fast": lines(SessionDocuments.resumedUnderSuite),
            ChatLaunch.noSuite: lines(SessionDocuments.resumedSuiteOff),
        ],
        refusals: ["vast": SessionDocuments.suiteOverBudget])

    /// The turn the recorded chats were sent.
    static let suiteTurn = "Which suite answers?"

    /// A chat opened under `pair`; not started.
    static func underSuite() -> SessionViewModel {
        SessionViewModel(starter: suiteChats, launch: ChatLaunch(suite: "pair"))
    }

    /// The recorded chat's id, as its `session` event names it.
    static var suiteChatID: String {
        lines(SessionDocuments.chatUnderSuite).first.flatMap { JSONValue.parse($0)?["chat_id"]?.stringValue } ?? ""
    }

    /// That chat, known by its id -- as the library knows a saved one -- under
    /// `pair`; not started.
    static func savedUnderSuite() -> SessionViewModel {
        SessionViewModel(starter: suiteChats, launch: ChatLaunch(resume: suiteChatID, suite: "pair"))
    }

    /// The suite turn sent and answered.
    static func answered(_ model: SessionViewModel) async {
        await converse(model, typed: suiteTurn)
        await until { model.transcript.acceptsInput && model.transcript.items.count > 1 }
    }

    /// Every recorded suite, each with the CLI's footprint: `fast` the
    /// default, `vast` over this machine's memory.
    static var suiteChoices: [SuiteChoice] {
        [
            ("cloud", SessionDocuments.suiteStatusCloud), ("fast", SessionDocuments.suiteStatusFast),
            ("pair", SessionDocuments.suiteStatusPair), ("shared", SessionDocuments.suiteStatusShared),
            ("vast", SessionDocuments.suiteStatusVast),
        ].map { name, status in
            SuiteChoice(
                name: name, footprint: ReadDocument(text: status).flatMap(SuiteFootprint.init(document:)), isDefault: name == "fast")
        }
    }

    /// The recorded turn under `pair`, then the suites as the library reads
    /// them for the chooser.
    static func choosingSuite(_ model: SessionViewModel) async {
        if !model.isExecute { await converse(model, typed: suiteTurn) } else { await ready(model) }
        model.defaultSuite = "fast"
        model.suiteChoices = suiteChoices
    }

    // MARK: - Driving a session as the person does

    /// Waits until `condition` holds -- five seconds at most, as the tests'
    /// `eventually` does.
    static func until(_ condition: () -> Bool) async {
        let deadline = ContinuousClock.now + .seconds(5)
        while !condition() && ContinuousClock.now < deadline {
            try? await Task.sleep(for: .milliseconds(10))
        }
    }

    /// Starts `model` -- nothing, once started -- and waits for its session
    /// to speak.
    static func ready(_ model: SessionViewModel) async {
        model.start()
        await until { model.transcript.status == .ready }
    }

    /// Sends `typed` once the session has spoken, then answers each card as
    /// it stands with the next of `answers`.
    static func converse(_ model: SessionViewModel, typed: String, answers: [String] = []) async {
        await ready(model)
        model.draft = typed
        model.submit()
        for answer in answers {
            await until { waiting(model) != nil }
            guard let card = waiting(model) else { return }
            model.answer(card, with: [answer])
        }
    }

    /// The card that waits on the person, by its id.
    static func waiting(_ model: SessionViewModel) -> Int? {
        model.transcript.standingCard
    }

    /// A chat that takes `set` and `detach`, an attachment sent, then a turn
    /// whose question stands -- and, while it does, a setting and a detach
    /// made: both held for the turn's end (SessionLinesTests'
    /// `setHeldUntilTheTurnEnds` and `detachAndAttachHeld`).
    static func holdingASetting(_ model: SessionViewModel) async {
        await ready(model)
        model.attach("Documents/report.md")
        model.draft = noteTurn
        model.submit()
        await until { waiting(model) != nil }
        await model.apply(RunSettings(temperature: 0.9))
        model.detach("Documents/report.md")
    }

    // MARK: - Transcripts, folded as the window folds them

    /// What `lines` fold to, short of the first line of type `silentFrom`:
    /// the person sends `typed` once the session has spoken and answers each
    /// card with the next of `answers` (PlayNarrationTests' `folded`). A
    /// saved conversation's `earlier` turns are folded in first, as the
    /// window hands them over before the first event.
    static func folded(
        _ lines: [String], typed: String? = nil, answers: [String] = [], silentFrom type: String? = nil,
        earlier: SavedChat? = nil, stderr: [String] = []
    ) -> Transcript {
        var transcript = Transcript()
        if let earlier { transcript.earlier(earlier, stderr: stderr) }
        var answers = answers[...]
        for line in lines {
            if let type, JSONValue.parse(line)?["type"]?.stringValue == type { break }
            let event = EventDecoder.decode(line: line)
            transcript.apply(.event(event))
            switch event.kind {
            case .session:
                if let typed { transcript.userSent(typed) }
            case .question:
                if let answer = answers.popFirst(), let card = transcript.items.last {
                    transcript.answered(card.id, with: [answer])
                }
            default:
                break
            }
        }
        return transcript
    }

    /// The mock-actor turn's question, its cards answered with `answers`.
    static func question(_ answers: [String] = []) -> QuestionCard {
        folded(mockActor, typed: noteTurn, answers: answers).items.lazy.compactMap { item -> QuestionCard? in
            if case .question(let card) = item.content { card } else { nil }
        }.first!
    }

    /// The question a stopped turn left standing, closed by its result.
    static var closedQuestion: QuestionCard {
        folded(questionCancelled, typed: noteTurn).items.lazy.compactMap { item -> QuestionCard? in
            if case .question(let card) = item.content { card } else { nil }
        }.first!
    }

    /// The permission prompt a stopped turn left standing -- its question
    /// answered `todo.md` first -- closed by its result.
    static var closedPermission: PermissionCard {
        folded(permissionCancelled, typed: noteTurn, answers: [noteAnswers[0]]).items.lazy.compactMap { item -> PermissionCard? in
            if case .permission(let card) = item.content { card } else { nil }
        }.first!
    }

    /// The chat with a message dropped, read back and resumed: the warning
    /// heads the earlier turns, and the resume's own notice follows the divider.
    static var warned: Transcript { folded(resumedWarnings, earlier: warnedChat) }

    /// The same chat read back by a binary before 55b: its stderr's line heads
    /// the turns, and the resume says nothing (stderr alone).
    static var warnedOlder: Transcript {
        folded(resumedWarningsOlder, earlier: warnedChatOlder, stderr: [SessionDocuments.chatsShowWarningsOlderErr])
    }

    /// The mock-actor turn's permission card, its cards answered with `answers`.
    static func permission(_ answers: [String] = []) -> PermissionCard {
        folded(mockActor, typed: noteTurn, answers: answers).items.lazy.compactMap { item -> PermissionCard? in
            if case .permission(let card) = item.content { card } else { nil }
        }.first!
    }

    /// The first thinking section `transcript` holds.
    static func activity(_ transcript: Transcript) -> Activity {
        transcript.items.lazy.compactMap { item -> Activity? in
            if case .activity(let activity) = item.content { activity } else { nil }
        }.first!
    }

    /// What a turn recalled, and the excerpts it drew on (SessionStatesTests'
    /// `recallAndSources`).
    static var recalledAndSources: Transcript {
        folded([
            #"{"type":"memory","chats":1,"decisions":2,"turn":1,"sessions":[{"id":"c7","name":"asyncio timeouts in the scraper"}]}"#,
            #"{"type":"sources","turn":1,"sources":[{"file":"report.pdf","pages":[41,42],"score":0.8,"retriever":"vector"}]}"#,
        ])
    }

    /// The excerpts the turn above drew on.
    static var sources: [Source] {
        recalledAndSources.items.lazy.compactMap { item -> [Source]? in
            if case .sources(let sources) = item.content { sources } else { nil }
        }.first!
    }

    /// A plan drawn in place between notices (TranscriptTests' `todoAndNotices`).
    static var plan: Transcript {
        folded([
            bareSession,
            #"{"type":"memory","chats":2,"decisions":1,"turn":1}"#,
            #"{"type":"todo","items":[{"text":"a","state":"active"}],"turn":1}"#,
            #"{"type":"notice","text":"context budget: 2 earlier exchanges not sent","turn":1}"#,
            #"{"type":"todo","items":[{"text":"a","state":"done"},{"text":"b","state":"active"}],"turn":1}"#,
        ], typed: "plan it")
    }

    /// An answer streamed, then its tokens (TranscriptTests' `deltasJoined`).
    static var answered: Transcript {
        folded([
            bareSession,
            #"{"type":"answer_start","turn":1}"#, #"{"type":"answer_delta","text":"caf","turn":1}"#,
            #"{"type":"answer_delta","text":"é ok","turn":1}"#, #"{"type":"answer_end","turn":1}"#,
            #"{"type":"result","text":"café ok","turn":1,"usage":{"input_tokens":4,"output_tokens":2}}"#,
        ], typed: "hi")
    }

    /// The markdown turn, streamed whole and answered.
    static var markdownLive: Transcript { folded(markdownAnswer, typed: markdownTurn) }

    /// The markdown turn mid-stream: its lines up to the delta that brought
    /// `self.client.get`, the code block's fence still open.
    static var markdownOpenFence: Transcript {
        var text = ""
        let through = markdownAnswer.firstIndex { line in
            guard let delta = JSONValue.parse(line), delta["type"]?.stringValue == "answer_delta" else { return false }
            text += delta["text"]?.stringValue ?? ""
            return text.contains("self.client.get")
        }
        return folded(Array(markdownAnswer[...(through ?? markdownAnswer.count - 1)]), typed: markdownTurn)
    }

    /// The markdown turn read back by `chats show`, as a reopened chat draws it.
    static var markdownResumed: Transcript { folded([], earlier: markdownChat) }

    /// The code turn, streamed whole and answered.
    static var codeLive: Transcript { folded(codeAnswer, typed: codeTurn) }

    /// The code turn read back by `chats show`, as a reopened chat draws it.
    static var codeResumed: Transcript { folded([], earlier: codeChat) }

    /// A turn that failed, in the CLI's words (TranscriptTests' `error`).
    static var failed: Transcript {
        folded([
            bareSession,
            #"{"type":"thinking","turn":1}"#,
            #"{"type":"error","message":"anthropic: 529 overloaded -- try again in a minute","turn":1}"#,
        ], typed: "hi")
    }

    /// A turn stopped mid-answer: the partial answer kept, the turn marked
    /// (TranscriptTests' `cancelled`).
    static var stopped: Transcript {
        var transcript = folded([
            bareSession, #"{"type":"answer_start","turn":1}"#, #"{"type":"answer_delta","text":"Once ","turn":1}"#,
        ], typed: "long story")
        transcript.cancelling()
        for line in [
            #"{"type":"answer_delta","text":"upon","turn":1}"#,
            #"{"type":"result","text":"Once upon","finish_reason":"cancelled","turn":1}"#,
        ] {
            transcript.apply(.event(EventDecoder.decode(line: line)))
        }
        return transcript
    }

    /// Lines this app does not know yet, each a quiet row (TranscriptTests'
    /// `unknownRows`).
    static var unknown: Transcript {
        folded([bareSession, #"{"type":"hologram","turn":1}"#, "not json at all", #"{"type":"task_round","task_id":"t"}"#])
    }

    // MARK: - Endings

    /// A child that exited on its own, badly, the stderr behind it
    /// (TranscriptTests' `endedFailed`).
    static var wentAway: SessionEnd {
        var tail = BoundedTail()
        tail.append(Data("loading\nfatal: the model went away\n".utf8))
        return SessionEnd(reason: .failed, exit: ChildExit(reason: .exited, status: 2), stderrTail: tail)
    }

    /// A session's endings as the transcript folds them from its `ended`:
    /// its input closed and the child gone, and one that failed.
    static let finished = ending(PreviewChat.wentWhenAsked)
    static let failedEnding = ending(wentAway)

    /// The ending the transcript folds from `end`.
    static func ending(_ end: SessionEnd) -> Ending {
        var transcript = folded([bareSession])
        transcript.apply(.ended(end))
        guard case .ended(let ending) = transcript.items.last?.content else { preconditionFailure("the transcript ends with its ending") }
        return ending
    }

    /// A new file's change, as the CLI's diff of a `write_file` says it
    /// (lib/src/cli/tests/business/tools/fs_test.cpp, "write_file of a new
    /// file"): no recorded session carries one.
    static let newFileDiff = "--- /dev/null\n+++ b/fresh/x.txt\n@@ -0,0 +1,2 @@\n+a\n+b\n"
}
#endif
