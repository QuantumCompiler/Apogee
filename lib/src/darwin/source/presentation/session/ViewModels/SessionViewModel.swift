import BusinessReads
import BusinessSession
import DataDiscovery
import DataProtocol
import Foundation
import Observation

/// One chat session in the window: the binary found, the session started,
/// its events folded into the transcript, and the user's acts sent back as
/// the protocol's lines -- in the order they were made.
///
/// SwiftUI-free (ADR 0002): everything the window shows is `connection`,
/// `transcript` and `draft`, and everything it does is a method here.
@MainActor
@Observable
public final class SessionViewModel {
    public enum Connection: Equatable {
        /// Not started.
        case idle
        /// Looking for the apogee binary.
        case locating
        /// None found: how to install, and every place looked.
        case missing(hint: String, places: [String])
        /// A session runs on this binary.
        case connected(binary: String, version: String)
    }

    public private(set) var connection: Connection = .idle
    public private(set) var transcript = Transcript()
    /// What the user is typing.
    public var draft = ""
    /// The saved conversation this is, by its id: the one it resumed, or the
    /// one the library found it saved as. `nil` for a new one not yet known.
    public private(set) var chatID: String?
    /// Told every event, after the transcript has folded it: how the library
    /// reads back what a turn changed.
    public var observer: (@MainActor (SessionEvent) -> Void)?
    /// Where the acts the library owns go: a new session, rename, delete ...
    public var onAction: (@MainActor (SessionAction) -> Void)?
    /// Told what each of its launches' discovery found -- and passed over --
    /// before its child opens: the library says an override passed over
    /// (4c).
    public var onLocated: (@MainActor (DiscoveryOutcome) -> Void)?
    /// What the session holds attached, by the path given: the input's chips
    /// -- each `attach` sent, and a saved conversation's own as `chats show`
    /// names them (the path each `attach` gave), so `detach` reaches them.
    public private(set) var attachments: [String] = []
    /// The run settings the session runs on: the launch's, then each change
    /// it took -- a `set` line sent between turns and not refused by name, or
    /// a restart's flags. A relaunch carries them (`launch.settings`).
    public private(set) var settings: RunSettings
    /// The lines made while the session could not take them -- a turn
    /// running or a card standing -- held, in order, until the turn ends.
    /// While a question or permission prompt waits the CLI drops every line
    /// but its answer, with no notice, one sent mid-turn before the question
    /// included (52d; machine-mode.md -> The session's own doors): so nothing
    /// is sent, and nothing shown applied, until the session can take it.
    public private(set) var held: [HeldLine] = []
    /// The context window of the session's model in tokens, when the library
    /// could read it (a stored model's `window` in `models list`).
    public var contextWindow: Int?
    /// The models the settings sheet offers: the configured backends.
    public var modelChoices: [String] = []
    /// What the window should present for the person: files, settings.
    public var presenting: Presenting?
    /// The folder the session works in, which `@` completes paths from.
    public var folder: URL = FileManager.default.homeDirectoryForCurrentUser
    /// The symphonies `/play` offers in an `execute` session, as `symphonies
    /// list` read them.
    public var plays: [PlayChoice] = []
    /// The config's default suite, the one an `execute` opened without
    /// `--suite` runs under; empty when none is set, `nil` unread.
    public var defaultSuite: String?
    /// The suites the chooser offers, each with the CLI's word on its fit,
    /// as the library read them when the chooser was asked for.
    public var suiteChoices: [SuiteChoice] = []
    /// The last turn sent, for Try again.
    public private(set) var lastSent: String?

    private let starter: any ChatStarting
    /// What reads a saved conversation's earlier turns back (`chats show`);
    /// `nil` reads none, and a resumed session says its turns aren't shown.
    private let reader: (any CommandRunning)?
    private var launch: ChatLaunch
    private var session: (any ChatSession)?
    private var outbox: AsyncStream<Outbound>.Continuation?
    /// Its child is being ended to free it, not for good.
    private var pausing = false
    /// The next child opens on an empty transcript (`start`), so a saved
    /// conversation's earlier turns are read back beside it. A pause's
    /// resume and a restart keep their rows, and read nothing again.
    private var readsEarlier = false
    /// The session's run: located, opened, its events read to the end.
    private var running: Task<Void, Never>?
    /// The launch in flight -- its binary being located, no child yet -- by
    /// its number; `nil` once it has opened its child, or when none is. A
    /// stop or a pause calls it off and a newer launch supersedes it: either
    /// way it opens no child, so the one-live-child law holds in that window
    /// too.
    private var launching: Int?
    private var launches = 0
    /// The shutdown, once asked: every later ask joins it, and nothing starts
    /// the session again.
    private var ending: Task<Void, Never>?
    /// The `set` lines sent whose replies are still to come.
    private var setsSent: [SetSent] = []
    /// The suite a switch moved the session from, until the child it made
    /// speaks: one that ends first -- the CLI refused the suite at launch --
    /// is put back on it.
    private var switching: SuiteFlags?

    /// What a launch says of its suite: `--suite` and `--force`.
    private struct SuiteFlags {
        var suite: String?
        var force: Bool
    }

    /// A `set` line sent: the settings it moved, in the order the CLI answers
    /// them, and what each was before it.
    private struct SetSent {
        var keys: [String]
        var before: RunSettings
    }

    /// A saved conversation read back: its turns, and what the read said on
    /// stderr from a binary whose document carries no `warnings`.
    private struct EarlierRead: Sendable {
        let chat: SavedChat
        let stderr: [String]
    }

    /// The grace a child is given to go when asked -- its input closed, a turn
    /// in it cancelled -- before it is terminated (`ChatSession.stop`); the
    /// library's quit waits on a child no longer than this.
    public static let grace: Duration = .seconds(5)

    /// A line to write, queued so lines leave in the order the user made them.
    private enum Outbound {
        case user(String)
        case answer(String)
        case cancel
        case attach(String)
        case detach(String)
        case set(RunSettings)
        case compact
    }

    /// A new chat.
    public convenience init(starter: any ChatStarting) {
        self.init(starter: starter, launch: ChatLaunch())
    }

    /// A chat started as `launch` says -- new, or resuming a saved one, whose
    /// earlier turns are then not read back.
    public convenience init(starter: any ChatStarting, launch: ChatLaunch) {
        self.init(starter: starter, launch: launch, reader: nil)
    }

    /// A chat started as `launch` says; a saved conversation it opens is
    /// read back through `reader` -- `chats show <id>` -- and its earlier
    /// turns drawn above the live ones (the CLI's M27).
    public init(starter: any ChatStarting, launch: ChatLaunch, reader: (any CommandRunning)?) {
        self.starter = starter
        self.reader = reader
        self.launch = launch
        chatID = launch.resume
        settings = launch.settings
        if let folder = launch.folder { self.folder = folder }
    }

    /// An `execute` session (its door's), rather than a chat.
    public var isExecute: Bool { launch.execute != nil }

    /// What an `execute` session was opened with, as its flags said it:
    /// `execute · suite duo · orchestrating · in ingest`. The session's
    /// events name its model, never its suite, so the suite is the launch's
    /// -- or the config's default, read -- and no count is guessed.
    public var executeBanner: String? {
        guard let execute = launch.execute else { return nil }
        var parts = ["execute"]
        if let suite = execute.suite {
            parts.append("suite \(suite)")
        } else if let defaultSuite, !defaultSuite.isEmpty {
            parts.append("suite \(defaultSuite), the default")
        } else if defaultSuite != nil {
            parts.append("no suite")
        }
        if execute.orchestrate { parts.append("orchestrating") }
        if let folder = launch.folder { parts.append("in \(folder.lastPathComponent)") }
        return parts.joined(separator: " · ")
    }

    /// The word for none: `--suite off`.
    public static let noSuite = ChatLaunch.noSuite

    /// The suite the session was launched under, as its flags said it: a
    /// name, `off` for none, or `nil` -- left to the CLI.
    public var suite: String? { launch.namedSuite }

    /// What the session runs under, as far as its flags and the config say:
    /// `suite pair`, `no suite`, `suite fast, the default`. `nil` where only
    /// the CLI knows -- a saved chat resumed with no `--suite` runs under the
    /// suite it last had, which neither the `session` event nor `chats show`
    /// states.
    public var suiteWords: String? {
        switch launch.namedSuite {
        case ChatLaunch.noSuite?:
            return "no suite"
        case let name?:
            return "suite \(name)" + (launch.forceSuite ? ", forced past its memory" : "")
        case nil:
            guard launch.resume == nil, let defaultSuite else { return nil }
            return defaultSuite.isEmpty ? "no suite" : "suite \(defaultSuite), the default"
        }
    }

    /// Whether the session can move to another suite: one shut down or with
    /// no binary cannot, nor a conversation nothing of which is written once
    /// it has a turn -- no `--resume` could carry it on.
    public var canSwitchSuite: Bool {
        if isShutDown || isLostIfEnded { return false }
        if case .missing = connection { return false }
        return true
    }

    /// Whether a turn has been sent in what the window shows.
    private var hasTurns: Bool {
        transcript.items.contains { if case .user = $0.content { true } else { false } }
    }

    /// How this session is kept private, when it is.
    public var privateMode: PrivateMode? {
        if launch.privately || transcript.isPrivate { return .nothingWritten }
        return launch.noRecall ? .deletedOnClose : nil
    }

    /// Whether ending its child would lose the conversation for good: one
    /// that writes nothing (`--private`, so no chat id), a turn sent in it,
    /// its child not ended. No `--resume` can carry it on -- the CLI refuses
    /// `--private` with `--resume` -- so the library never pauses it, and a
    /// switch away asks first. An ordinary session, or a private one with no
    /// turn yet, loses nothing by a pause.
    public var isLostIfEnded: Bool {
        guard privateMode == .nothingWritten, chatID == nil, transcript.status != .ended else { return false }
        return transcript.items.contains { if case .user = $0.content { true } else { false } }
    }

    /// What a private session's state says of what is kept.
    public static func privateWords(_ mode: PrivateMode) -> String {
        switch mode {
        case .nothingWritten: "Nothing of this session is written: no transcript, no recall in later sessions."
        case .deletedOnClose: "Never recalled in later sessions, and deleted when you close it. This apogee writes it until then."
        }
    }

    /// The library found this session saved as `id`.
    public func identify(_ id: String) {
        if chatID == nil {
            chatID = id
        }
    }

    // MARK: - The session

    /// Finds the binary and starts a session on it; again after one has ended
    /// or none was found -- never once it has been shut down.
    public func start() {
        guard !isShutDown else { return }
        switch connection {
        case .locating: return
        case .connected where transcript.status != .ended: return
        default: break
        }
        connection = .locating
        transcript = Transcript()
        // A new child on an empty transcript: nothing the last one held or
        // was sent stands for it. A saved conversation's attachments come
        // back with its read.
        held = []
        setsSent = []
        attachments = []
        readsEarlier = true
        beginLaunch()
    }

    /// Begins a launch -- the binary located, then its child opened -- by its
    /// own number, superseding any launch still locating.
    private func beginLaunch() {
        launches += 1
        let number = launches
        launching = number
        running = Task { await connect(number) }
    }

    /// Calls the launch in flight off, if one is still locating: it opens no
    /// child. Its task is cancelled too, so a locate that heeds cancellation
    /// stops early.
    private func callOffLaunch() {
        guard launching != nil else { return }
        launching = nil
        running?.cancel()
    }

    private func connect(_ number: Int) async {
        let located = await starter.locateBinary()
        // Called off while the binary was located -- stopped, paused, or
        // superseded by a newer launch: no child is opened. The open below is
        // synchronous on the main actor, so no stop can land between this
        // check and the child: one asked from here on finds `session` set.
        guard launching == number else { return }
        launching = nil
        onLocated?(located)
        let readsEarlier = self.readsEarlier
        self.readsEarlier = false
        switch located {
        case .notFound(let skipped):
            connection = .missing(
                hint: DiscoveryOutcome.installHint,
                places: skipped.map { "\($0.candidate.url.path): \($0.reason)" })
        case .found(let found, _):
            connection = .connected(binary: found.url.path, version: found.version)
            var launch = self.launch
            launch.resume = chatID
            // A saved conversation opened afresh: its earlier turns are read
            // beside the child starting, and handed to the transcript before
            // its first event -- the events wait in the stream until the read
            // has answered, so whichever comes first, the earlier turns stand
            // above the live ones and the notice is never drawn over them.
            var earlier = readsEarlier ? readEarlier(binary: found.url) : nil
            let session = starter.openChat(binary: found.url, launch: launch)
            self.session = session
            let (lines, outbox) = AsyncStream.makeStream(of: Outbound.self)
            self.outbox = outbox
            Task { await Self.write(lines, to: session) }
            var spoke = false
            var refusal: String?
            for await event in session.events {
                if let read = earlier {
                    earlier = nil
                    if let read = await read.value { readBack(read) }
                }
                if case .ended(let end) = event, !pausing, !spoke, switching != nil {
                    // The child a switch made ended before it spoke: the CLI
                    // refused the suite at launch, in these words.
                    refusal = end.stderrTail.lastLine ?? end.message
                }
                if case .ended = event, pausing {
                    // Freed, not finished: no ending card.
                    pausing = false
                    transcript.paused()
                } else {
                    transcript.apply(event)
                }
                if case .event(let event) = event {
                    if case .session = event.kind {
                        spoke = true
                        switching = nil
                    }
                    heard(event)
                }
                // The turn ended: what waited for it goes now, before any
                // line the person makes next.
                releaseHeld()
                observer?(event)
            }
            outbox.finish()
            self.outbox = nil
            self.session = nil
            if let refusal, let before = switching { putSuiteBack(before, refusal: refusal) }
        }
    }

    /// `chats show <id>` for the saved conversation this session opens, run
    /// beside its child; `nil` when there is none to read -- a new chat, or
    /// no reader. It answers `nil` too when the binary has no such read (one
    /// before the CLI's M27 refuses it) or prints no chat: the session's own
    /// notice then says the earlier turns are kept, as before.
    private func readEarlier(binary: URL) -> Task<EarlierRead?, Never>? {
        guard let reader, let id = chatID else { return nil }
        return Task {
            let read = await reader.readKeepingStderr(binary: binary, SavedChat.arguments(id: id))
            guard let chat = read.outcome.document.flatMap(SavedChat.init(document:)) else { return nil }
            // A binary before the CLI's 55b says what it could not read on
            // stderr alone; one with it says it in the document.
            return EarlierRead(chat: chat, stderr: chat.warnings == nil ? read.stderrLines : [])
        }
    }

    /// The saved conversation read back: its turns above the live ones, what
    /// the read could not carry over at their head, and what it holds
    /// attached as chips.
    private func readBack(_ read: EarlierRead) {
        transcript.earlier(read.chat, stderr: read.stderr)
        for attachment in read.chat.attachments where !attachments.contains(attachment.name) {
            attachments.append(attachment.name)
        }
    }

    /// Writes the queued lines one at a time, in order.
    private static func write(_ lines: AsyncStream<Outbound>, to session: any ChatSession) async {
        for await line in lines {
            switch line {
            case .user(let text): _ = try? await session.send(user: text)
            case .answer(let text): try? await session.answer(text)
            case .cancel: try? await session.cancel()
            case .attach(let path): try? await session.attach(path: path, graph: nil)
            case .detach(let path): try? await session.detach(path: path)
            case .set(let settings): try? await session.set(settings)
            case .compact: try? await session.compact()
            }
        }
    }

    /// Ends the session for good: a launch still locating its binary is
    /// called off, so it opens no child; a turn in flight is cancelled and the
    /// child asked to go, terminated only if it will not. Every later call
    /// joins the first, and nothing starts the session again.
    public func shutDown() async {
        if ending == nil {
            callOffLaunch()
            ending = Task { await session?.stop(grace: Self.grace) }
        }
        await ending?.value
    }

    /// Whether it has been shut down: ended for good, never started again.
    public var isShutDown: Bool { ending != nil }

    /// Whether it holds a child, or is launching one: what the one-live-child
    /// law counts.
    public var isLive: Bool { session != nil || launching != nil }

    /// Whether its child was ended to free it, to be resumed when it is back
    /// in view.
    public var isPaused: Bool { transcript.status == .paused }

    /// Ends the child to free it -- its model unloads -- keeping the
    /// conversation shown; `resume()` carries it on with `--resume`. Returns
    /// once the child has gone. A session still locating its binary has its
    /// launch called off, and is paused before it has a child.
    public func pause() async {
        guard !isShutDown, transcript.status != .ended, !isPaused else { return }
        if let session {
            pausing = true
            await session.stop(grace: Self.grace)
            // Returns once its events are read to the end: paused, not pausing.
            await running?.value
        } else if launching != nil {
            callOffLaunch()
            transcript.paused()
        }
    }

    /// Carries a paused session on: a new child resuming the saved
    /// conversation. One that was never saved -- no turn sent, or its id never
    /// found -- starts afresh instead, since nothing of it could be resumed.
    public func resume() {
        guard isPaused, !isShutDown else { return }
        if chatID == nil {
            transcript = Transcript()
        } else {
            transcript.reopening()
        }
        beginLaunch()
    }

    // MARK: - The user's acts

    /// Whether the draft can be sent now.
    public var canSend: Bool {
        transcript.acceptsInput && outbox != nil
            && !draft.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
    }

    /// Whether there is a turn to stop.
    public var canCancel: Bool { transcript.turnInFlight && outbox != nil }

    /// Sends the draft as a turn.
    public func submit() {
        let text = draft.trimmingCharacters(in: .whitespacesAndNewlines)
        guard canSend else { return }
        draft = ""
        send(text)
    }

    private func send(_ text: String) {
        guard let outbox else { return }
        lastSent = text
        transcript.userSent(text)
        outbox.yield(.user(text))
    }

    /// Sends the last turn again, as typed, after a turn failed -- whatever
    /// is in the draft stays there.
    public func retry() {
        guard let lastSent, transcript.acceptsInput else { return }
        send(lastSent)
    }

    // MARK: - The input's reach

    /// Whether the session takes `detach`, `set`, `compact` -- the CLI's 52d,
    /// announced in its `capabilities.accepts`.
    public var canDetach: Bool { transcript.accepts.contains("detach") }
    public var canSet: Bool { transcript.accepts.contains("set") }
    public var canCompact: Bool { transcript.accepts.contains("compact") }

    /// Whether the session takes a line now: its child is there, no turn
    /// runs and no card stands -- between turns, or before the first.
    public var takesLinesNow: Bool {
        guard outbox != nil, transcript.standingCard == nil else { return false }
        switch transcript.status {
        case .ready, .connecting: return true
        default: return false
        }
    }

    /// The settings the sheet starts from: what the session runs on, with
    /// what is held for it laid over.
    public var intendedSettings: RunSettings {
        held.reduce(settings) { settings, line in
            if case .set(let change) = line { settings.merging(change) } else { settings }
        }
    }

    /// Attaches a file, folder or glob: its outcome arrives as notices. Made
    /// mid-turn, it waits for the turn to end.
    public func attach(_ path: String) {
        guard outbox != nil else { return }
        sendOrHold(.attach(path))
    }

    /// Detaches it, where the build takes `detach`; else there is no way to,
    /// and the chip offers none (the design's G2 fallback). Made mid-turn, it
    /// waits for the turn to end, the chip as it was.
    public func detach(_ path: String) {
        guard canDetach, outbox != nil else { return }
        sendOrHold(.detach(path))
    }

    /// Compacts the conversation now, where the build takes `compact`.
    public func compact() {
        guard canCompact, outbox != nil else { return }
        sendOrHold(.compact)
    }

    /// Moves the run settings. A build that takes `set` moves them from the
    /// next turn and says each -- sent between turns, so held while a turn
    /// runs; an older one is restarted with the new flags, carrying the
    /// conversation on with `--resume`, and the app says so. Either way a
    /// relaunch after carries them.
    public func apply(_ change: RunSettings) async {
        guard !change.isEmpty, !isShutDown else { return }
        if canSet, outbox != nil {
            sendOrHold(.set(change))
            return
        }
        settings = settings.merging(change)
        launch.settings = settings
        let carried = chatID != nil || !transcript.items.contains { if case .user = $0.content { true } else { false } }
        transcript.note(
            carried
                ? "restarting with \(Self.words(change)) -- this apogee changes settings by restarting the session; the conversation carries on, and a local model loads again"
                : "restarting with \(Self.words(change)) -- the earlier turns can't be carried on: this session isn't in the saved list")
        await restart()
    }

    /// Ends the child and starts it again on `launch`, resuming the saved
    /// conversation when there is one. A launch still locating is superseded;
    /// a session shut down while the old child went starts nothing.
    private func restart() async {
        if let session, transcript.status != .ended {
            pausing = true
            await session.stop(grace: Self.grace)
            await running?.value
        } else {
            callOffLaunch()
        }
        guard !isShutDown else { return }
        if chatID == nil {
            let notes = transcript.items.filter { if case .notice = $0.content { true } else { false } }
            transcript = Transcript()
            for case .notice(let text) in notes.map(\.content) { transcript.note(text) }
        } else {
            transcript.reopening()
        }
        beginLaunch()
    }

    /// `line` now if the session takes it, else held for the turn's end.
    private func sendOrHold(_ line: HeldLine) {
        if takesLinesNow { sendLine(line) } else { held.append(line) }
    }

    /// What was held, in the order made, once the session can take it -- a
    /// suite's move last, since it restarts the child the lines go to.
    private func releaseHeld() {
        guard !held.isEmpty, takesLinesNow else { return }
        let lines = held
        held = []
        let moves = lines.filter { if case .suite = $0 { true } else { false } }
        lines.filter { !moves.contains($0) }.forEach(sendLine)
        if case .suite(let suite, let force)? = moves.last {
            Task { await moveSuite(to: suite, force: force) }
        }
    }

    // MARK: - The suite

    /// Moves the session under `suite` -- `off` for none -- as the CLI's
    /// `/suite` does, `force` running one it prices over this machine's
    /// memory. Machine mode has no line for it -- `set` refuses a suite whole,
    /// and slash commands are the terminal's -- so the session restarts with
    /// `--suite`, the conversation carried on by `--resume`, and the
    /// transcript says so (the open call's default, GUI 4i). Made while a turn
    /// runs or a card stands, it waits for the turn's end, as a line does.
    /// The conversation moves to the suite's chat member, as `/suite` moves
    /// it: the relaunch carries no `-m`, which would win over it. A suite the
    /// CLI refuses at launch is put back, said in its words, and the session
    /// carries on under the one before.
    public func switchSuite(to suite: String, force: Bool = false) async {
        guard canSwitchSuite, !(isExecute && suite == ChatLaunch.noSuite) else { return }
        held.removeAll { if case .suite = $0 { true } else { false } }
        guard suite != launch.namedSuite || force != launch.forceSuite else { return }
        if midTurn {
            held.append(.suite(suite, force: force))
            return
        }
        await moveSuite(to: suite, force: force)
    }

    /// A turn runs, or is stopping, or a card waits on the person: a restart
    /// now would end what the turn is doing.
    private var midTurn: Bool {
        if case .stopping = transcript.status { return true }
        return transcript.turnInFlight || transcript.standingCard != nil
    }

    private func moveSuite(to suite: String, force: Bool) async {
        guard !isShutDown else { return }
        let before = SuiteFlags(suite: launch.namedSuite, force: launch.forceSuite)
        launch.name(suite: suite, force: force)
        settings.model = nil
        launch.settings.model = nil
        transcript.note(Self.moveWords(suite, force: force, turns: hasTurns, carried: chatID != nil))
        switching = before
        // Not running now -- paused, never started, or still locating its
        // binary -- the launch to come carries the flags; else a new child.
        if isPaused || connection == .idle || launching != nil { return }
        await restart()
    }

    /// The suite put back after the CLI refused the one asked for, and the
    /// session started again under it.
    private func putSuiteBack(_ before: SuiteFlags, refusal: String) {
        switching = nil
        let asked = launch.namedSuite
        launch.name(suite: before.suite, force: before.force)
        transcript.note(
            "not moved to \(Self.suiteName(asked)): \(refusal) -- the session carries on under \(Self.suiteName(before.suite))")
        guard !isShutDown else { return }
        if chatID == nil {
            let notes = transcript.items.filter { if case .notice = $0.content { true } else { false } }
            transcript = Transcript()
            for case .notice(let text) in notes.map(\.content) { transcript.note(text) }
        } else {
            transcript.reopening()
        }
        beginLaunch()
    }

    /// A suite flag in words: `suite pair`, `no suite`, `the suite it had`.
    nonisolated static func suiteName(_ suite: String?) -> String {
        switch suite {
        case ChatLaunch.noSuite?: "no suite"
        case let name?: "suite \(name)"
        case nil: "the suite it had"
        }
    }

    /// What a move says in the transcript: where to, by which flags, and what
    /// becomes of the conversation.
    nonisolated static func moveWords(_ suite: String, force: Bool, turns: Bool, carried: Bool) -> String {
        let flags = "--suite \(suite)" + (force ? " --force" : "")
        let off = suite == ChatLaunch.noSuite
        guard turns else {
            return off ? "starting with no suite (\(flags))" : "starting under suite \(suite) (\(flags))"
        }
        let to = off ? "leaving the suite (\(flags))" : "switching to suite \(suite) (\(flags))"
        let how = carried
            ? "the conversation carries on" + (off ? " on its model" : " on the suite's chat member") + ", and a local model loads again"
            : "the earlier turns can't be carried on: this session isn't in the saved list"
        return "\(to) -- machine mode carries no suite, so the session restarts under it; \(how)"
    }

    /// Writes `line`, and shows what it does from now: the chip, the setting.
    private func sendLine(_ line: HeldLine) {
        guard let outbox else { return }
        switch line {
        case .attach(let path):
            if !attachments.contains(path) { attachments.append(path) }
            outbox.yield(.attach(path))
        case .detach(let path):
            attachments.removeAll { $0 == path }
            outbox.yield(.detach(path))
        case .set(let change):
            setsSent.append(SetSent(keys: Self.keys(change), before: settings))
            settings = settings.merging(change)
            launch.settings = settings
            outbox.yield(.set(change))
        case .compact:
            outbox.yield(.compact)
        case .suite(let suite, let force):
            Task { await moveSuite(to: suite, force: force) }
        }
    }

    /// An event read while `set` replies are due. The CLI answers a `set`
    /// line before it reads the next, one notice per setting in order --
    /// `<what> -- from the next turn`, or `set <key>: <why>` for a value it
    /// refused by name -- or one `set: <why>` for a line it refused whole
    /// (machine-mode.md -> The session's own doors). A refused value is put
    /// back: the session never took it, so neither the settings shown nor a
    /// relaunch's flags carry it. Once a turn's events begin, every reply is in.
    private func heard(_ event: ProtocolEvent) {
        guard !setsSent.isEmpty else { return }
        if event.turn != nil {
            setsSent = []
            return
        }
        guard case .notice(let text) = event.kind, var line = setsSent.first else { return }
        if text.hasPrefix("set: ") {
            setsSent.removeFirst()
            line.keys.forEach { putBack($0, from: line.before) }
            return
        }
        let refused = line.keys.first { text.hasPrefix("set \($0): ") }
        if let refused {
            line.keys.removeAll { $0 == refused }
        } else if text.hasSuffix(" -- from the next turn"), !line.keys.isEmpty {
            line.keys.removeFirst()
        } else {
            // A warning beside a reply, or a line of something else.
            return
        }
        if line.keys.isEmpty { setsSent.removeFirst() } else { setsSent[0] = line }
        if let refused { putBack(refused, from: line.before) }
    }

    /// The setting `key` as it was in `before`: on the session's settings,
    /// unless a later `set` still to be answered moves it too -- then that
    /// line's reply decides, from this value.
    private func putBack(_ key: String, from before: RunSettings) {
        if let later = setsSent.firstIndex(where: { $0.keys.contains(key) }) {
            setsSent[later].before = setsSent[later].before.taking(key, from: before)
        } else {
            settings = settings.taking(key, from: before)
            launch.settings = settings
        }
    }

    /// The settings `change` moves, in the order the CLI applies and answers
    /// them: the model first (`DriverLine.set`'s order).
    static func keys(_ change: RunSettings) -> [String] {
        [
            change.model.map { _ in "model" }, change.system.map { _ in "system" }, change.think.map { _ in "think" },
            change.effort.map { _ in "effort" }, change.temperature.map { _ in "temperature" },
        ].compactMap { $0 }
    }

    nonisolated static func words(_ settings: RunSettings) -> String {
        var parts: [String] = []
        if let model = settings.model { parts.append("model \(model)") }
        if let think = settings.think { parts.append("thinking \(think)") }
        if let effort = settings.effort { parts.append("effort \(effort)") }
        if settings.system != nil { parts.append("a new system prompt") }
        if let temperature = settings.temperature { parts.append("temperature \(temperature.formatted())") }
        return parts.joined(separator: ", ")
    }

    /// The context meter: the last finished turn's input tokens against the
    /// window.
    public var meter: ContextMeter {
        var used: Int?
        for item in transcript.items.reversed() {
            if case .answer(let answer) = item.content, answer.finished, let usage = answer.usage {
                used = usage.inputTokens
                break
            }
        }
        return ContextMeter(used: used, window: contextWindow)
    }

    // MARK: - Slash commands and mentions

    /// The commands the draft's `/` names, those this build can carry out.
    public var slashSuggestions: [SlashCommand] {
        guard draft.hasPrefix("/"), !draft.contains(" ") else { return [] }
        let typed = draft.lowercased()
        return SlashCommand.all.filter { $0.name.hasPrefix(typed) && available($0) }
    }

    /// Whether a command has what it needs now: `/compact` and `/detach` the
    /// 52d lines, `/stop` a turn in flight, `/rename` and `/delete` a saved
    /// conversation, `/play` an `execute` session.
    public func available(_ command: SlashCommand) -> Bool {
        switch command.act {
        case .play: isExecute
        case .compact: canCompact && outbox != nil
        case .detachAll: canDetach && !attachments.isEmpty
        case .stop: canCancel
        case .library(.rename), .library(.delete): chatID != nil && privateMode == nil
        case .library(.chooseSuite): canSwitchSuite
        default: true
        }
    }

    /// Runs a slash command: the draft is cleared, the act done or handed on.
    public func run(_ command: SlashCommand) {
        guard available(command) else { return }
        if command.act == .play {
            // The line is the turn: `/play <symphony> <input>`, sent as typed.
            draft = "/play "
            return
        }
        draft = ""
        switch command.act {
        case .library(let action): onAction?(action)
        case .present(let what): presenting = what
        case .compact: compact()
        case .detachAll: attachments.forEach(detach)
        case .stop: cancel()
        case .play: break
        }
    }

    /// The symphonies `/play ` offers for the name being typed after it.
    public var playSuggestions: [PlayChoice] {
        guard isExecute, draft.hasPrefix("/play ") else { return [] }
        let typed = draft.dropFirst("/play ".count)
        guard !typed.contains(" ") else { return [] }
        return plays.filter { $0.name.hasPrefix(typed) }
    }

    /// Completes the `/play` line with the symphony `name`; its input follows.
    public func complete(play name: String) {
        guard isExecute, draft.hasPrefix("/play ") else { return }
        draft = "/play \(name) "
    }

    /// The paths `@` offers for what is typed after it.
    public var mentionSuggestions: [String] {
        guard let typed = Mentions.typing(in: draft) else { return [] }
        return Mentions.candidates(for: typed, in: folder)
    }

    /// Completes the `@` mention being typed with `path`.
    public func complete(mention path: String) {
        guard let typed = Mentions.typing(in: draft) else { return }
        draft = String(draft.dropLast(typed.count)) + path + (path.hasSuffix("/") ? "" : " ")
    }

    /// Answers the question or permission card `id`: one answer per item, in
    /// order -- only while it stands. A card answered already, or closed by
    /// its turn's end, takes none, and no `answer` line is written: the CLI
    /// would drop it, or read it as the answer to a later question.
    public func answer(_ id: Int, with answers: [String]) {
        guard let outbox, !answers.isEmpty, transcript.answered(id, with: answers) else { return }
        for answer in answers {
            outbox.yield(.answer(answer))
        }
    }

    /// Stops the turn in flight through the protocol: it ends in its own
    /// result, and the session goes on.
    public func cancel() {
        guard canCancel, let outbox else { return }
        transcript.cancelling()
        outbox.yield(.cancel)
    }

    // MARK: - What the window says

    /// The session's state, in a few words.
    public var statusText: String {
        switch connection {
        case .idle, .locating: return "Starting"
        case .missing: return "Apogee not found"
        case .connected: break
        }
        return switch transcript.status {
        case .connecting: "Starting apogee"
        case .paused: "Paused"
        case .ready: "Ready"
        case .thinking: "Thinking"
        case .working: "Working"
        case .answering: "Answering"
        case .waiting: "Waiting for you"
        case .stopping: "Stopping"
        case .ended: "Session ended"
        }
    }

    /// What the input says while it is not the user's.
    public var inputPrompt: String {
        switch transcript.status {
        case .waiting: "Answer the question above"
        case .ended: "The session has ended"
        case .connecting, .paused: "Starting apogee"
        default: "Message Apogee"
        }
    }
}

extension RunSettings {
    /// `self` with the setting `key` as `other` has it.
    fileprivate func taking(_ key: String, from other: RunSettings) -> RunSettings {
        var taken = self
        switch key {
        case "model": taken.model = other.model
        case "system": taken.system = other.system
        case "think": taken.think = other.think
        case "effort": taken.effort = other.effort
        case "temperature": taken.temperature = other.temperature
        default: break
        }
        return taken
    }
}
