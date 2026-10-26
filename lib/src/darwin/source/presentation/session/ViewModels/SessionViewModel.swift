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
    /// What was attached this session, by the path given: the input's chips.
    public private(set) var attachments: [String] = []
    /// The run settings as last chosen: the launch's, then each change.
    public private(set) var settings: RunSettings
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
            for await event in session.events {
                if let read = earlier {
                    earlier = nil
                    if let chat = await read.value { transcript.earlier(chat) }
                }
                if case .ended = event, pausing {
                    // Freed, not finished: no ending card.
                    pausing = false
                    transcript.paused()
                } else {
                    transcript.apply(event)
                }
                observer?(event)
            }
            outbox.finish()
            self.outbox = nil
            self.session = nil
        }
    }

    /// `chats show <id>` for the saved conversation this session opens, run
    /// beside its child; `nil` when there is none to read -- a new chat, or
    /// no reader. It answers `nil` too when the binary has no such read (one
    /// before the CLI's M27 refuses it) or prints no chat: the session's own
    /// notice then says the earlier turns are kept, as before.
    private func readEarlier(binary: URL) -> Task<SavedChat?, Never>? {
        guard let reader, let id = chatID else { return nil }
        return Task {
            await reader.read(binary: binary, SavedChat.arguments(id: id)).document.flatMap(SavedChat.init(document:))
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

    /// Its child's process id, while it has one.
    public var processIdentifier: Int32? { session?.processIdentifier }

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

    /// Attaches a file, folder or glob: its outcome arrives as notices.
    public func attach(_ path: String) {
        guard let outbox else { return }
        if !attachments.contains(path) { attachments.append(path) }
        outbox.yield(.attach(path))
    }

    /// Detaches it, where the build takes `detach`; else there is no way to,
    /// and the chip offers none (the design's G2 fallback).
    public func detach(_ path: String) {
        guard canDetach, let outbox else { return }
        attachments.removeAll { $0 == path }
        outbox.yield(.detach(path))
    }

    /// Compacts the conversation now, where the build takes `compact`.
    public func compact() {
        guard canCompact, let outbox else { return }
        outbox.yield(.compact)
    }

    /// Moves the run settings. A build that takes `set` moves them from the
    /// next turn and says each; an older one is restarted with the new flags,
    /// carrying the conversation on with `--resume`, and the app says so.
    public func apply(_ change: RunSettings) async {
        guard !change.isEmpty, !isShutDown else { return }
        settings = settings.merging(change)
        if canSet, let outbox {
            outbox.yield(.set(change))
            return
        }
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

    static func words(_ settings: RunSettings) -> String {
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

    /// Answers the question or permission card `id`: one answer per item, in order.
    public func answer(_ id: Int, with answers: [String]) {
        guard let outbox, !answers.isEmpty else { return }
        transcript.answered(id, with: answers)
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
