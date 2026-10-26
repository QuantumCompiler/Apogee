import BusinessReads
import BusinessSession
import DataDiscovery
import DataPreferences
import DataProtocol
import Foundation
import Observation
import PresentationSession

/// The Sessions place: the saved conversations, the one open, and the
/// inspector beside it.
///
/// Two laws from the design, kept here (SwiftUI-free, ADR 0002):
/// - **One live chat child.** Exactly one session has a child at a time, in
///   every window: opening another ends the current one's first -- a launch
///   still locating its binary called off, its child gone -- before the next
///   is made. Leaving the place pauses an idle session -- its child ends and
///   its model is freed -- or, mid-turn, once its turn ends; coming back
///   resumes it with `--resume`. A conversation nothing of which is written
///   can't be resumed, so it is never paused: it stays the one live child,
///   and a switch away asks first. The window closing and the app quitting
///   end it too, and nothing starts again until the place is opened anew.
/// - **Read back; don't mirror.** The list is `chats list` as last read --
///   read again on focus, after every turn and after every change the app
///   makes -- and nothing else.
@MainActor
@Observable
public final class SessionLibraryViewModel {
    public enum Listing: Equatable {
        case notRead
        case reading
        case read
        /// The read failed, in the CLI's words.
        case failed(String)
        /// No apogee binary was found; the session window says how to install.
        case missing
    }

    /// A change that waits on the person's word.
    public enum Pending: Equatable {
        /// A turn is running in the open session: stop it to open `target`?
        case stopTurn(for: Target)
        /// The open session is a private conversation that can't be reopened
        /// (`SessionViewModel.isLostIfEnded`): end it to open `target`, or
        /// keep it running?
        case endPrivate(for: Target)
        /// Delete this saved conversation? It cannot be undone.
        case delete(ChatSummary)
    }

    /// What to open.
    public enum Target: Equatable {
        case new
        /// A session that writes nothing: `--private` where the build has it
        /// (the CLI's 52d), else `--no-recall` and deleted when it closes.
        case privately
        case chat(String)
        /// `apogee execute`, as its door's sheet launches it.
        case execute(ChatLaunch)
    }

    /// The execute door (the CLI's 27s, 41a-41c; the TUI's picker door, ADR
    /// 0011): `apogee execute` under a suite or bare on one model, opened in a
    /// folder -- a project's context and the coder persona come with one, said
    /// by the session's own notices -- and the suite's symphonies offered to
    /// its chat model with `--orchestrate`.
    public struct ExecuteForm: Equatable, Sendable {
        /// `--suite`; `nil` leaves it to the config: its default suite, else
        /// bare on the model.
        public var suite: String?
        /// `-m`; empty is the suite's chat member, or the default backend.
        public var model = ""
        /// The folder it opens in; the home folder when `nil`.
        public var folder: URL?
        /// `--orchestrate`, a suite's alone.
        public var orchestrate = false

        public init() {}

        /// The launch, as typed: orchestrate only where `orchestrates` says
        /// a suite is there to offer.
        public func launch(orchestrates: Bool) -> ChatLaunch {
            let model = model.trimmingCharacters(in: .whitespaces)
            return ChatLaunch(
                settings: model.isEmpty ? nil : RunSettings(model: model),
                execute: ChatLaunch.Execute(suite: suite, orchestrate: orchestrate && orchestrates), folder: folder)
        }
    }

    public private(set) var chats: [ChatSummary] = []
    public private(set) var listing: Listing = .notRead
    /// What the list is filtered by: a part of a name, any case.
    public var query = ""
    /// The open session.
    public private(set) var current: SessionViewModel
    public private(set) var pending: Pending?
    /// The roles, as `models status` reads them, for the inspector.
    public private(set) var roles: [Field] = []
    public private(set) var rolesFailure: String?
    /// The last change that failed, in the CLI's words.
    public private(set) var failure: String?
    /// Moves when the person asked to search (⇧⌘F): the field takes focus.
    public private(set) var searchRequests = 0
    /// The saved conversation being renamed: the sheet's.
    public var renaming: ChatSummary?
    public var inspectorOpen: Bool {
        didSet { preferences.inspectorOpen = inspectorOpen }
    }
    /// Says one line in the window's toast.
    public var say: (@MainActor (String) -> Void)?
    /// The execute door's sheet, open, and what it says.
    public var executeDoorOpen = false
    public var executeForm = ExecuteForm()
    /// The configured suites, `config get suites` as last read.
    public private(set) var suites: [String] = []
    /// `models.default_suite`: empty when none is set, `nil` unread.
    public private(set) var defaultSuite: String?
    /// The person's override, passed over by discovery while another binary
    /// answered -- its path and why -- so the app says it runs another binary
    /// than the one it was pointed at: once at launch, and in Settings ›
    /// General beside the binary in use (4c). `nil` when the override was
    /// used, is unset, or nothing answered.
    public private(set) var skippedOverride: (path: String, reason: String)?

    private let starter: any ChatStarting
    private let runner: any CommandRunning
    private let preferences: Preferences
    private let now: () -> Date
    private var found: FoundBinary?
    private var kinds: [String: ModelKind] = [:]
    /// The configured backends, from `models list`.
    private var backends: [String] = []
    private var modelsRead = false
    /// The line types the last session announced: whether this build has
    /// the CLI's 52d (`set`, `detach`, `compact` -- and `chat --private`).
    private var lastAccepts: [String]?
    /// The event types the last session announced: whether this build streams
    /// a long job's progress (the CLI's 52b announces `job`).
    private var lastEvents: [String]?
    /// The vocabulary's date the last session announced.
    private var lastSchema: String?
    /// The model the next new session starts on, once.
    private var nextModel: String?
    private var inView = false
    /// The open session leaves view mid-turn: pause it when the turn ends.
    private var pauseWhenIdle = false
    /// A rename of the open session that waits for its turn to end, on a
    /// binary that would put its own title back (`keepsOutsideRenames`).
    private var renameWhenIdle: (chat: ChatSummary, name: String)?
    /// The ids listed when a new session opened: the one it is saved as is
    /// the one that appears.
    private var listedBeforeNew: Set<String>?
    /// The symphonies an execute session's `/play` offers.
    private var plays: [PlayChoice] = []
    /// What the quit's wait is measured on.
    private let clock: any Clock<Duration>
    /// The passed-over override has been said: it is said once a launch.
    private var saidSkippedOverride = false
    /// The window closed or the app is quitting: no child starts again until
    /// `start(inView:)` opens the place once more.
    private var shut = false
    /// The open session's close has run to its end -- its child gone, any
    /// delete it promised done -- so a quit has nothing left to wait for.
    private var closed = false
    /// Every promised delete begun, by the chat it deletes: a later ask --
    /// a second close of the session, the quit's -- joins the first, so
    /// `chats delete` runs once per chat. Each answers its failure, if any.
    private var deletions: [String: Task<String?, Never>] = [:]

    /// The library; `clock` is what a quit's wait on a child is measured on.
    public init(
        starter: any ChatStarting, runner: any CommandRunning, preferences: Preferences,
        now: @escaping () -> Date = Date.init, clock: any Clock<Duration> = ContinuousClock()
    ) {
        self.starter = starter
        self.runner = runner
        self.preferences = preferences
        self.now = now
        self.clock = clock
        inspectorOpen = preferences.inspectorOpen
        current = SessionViewModel(starter: starter, launch: ChatLaunch(), reader: runner)
    }

    // MARK: - The place

    /// The app started, or its window opened again: find the binary, read
    /// the list, and -- when Sessions is in view -- open the session left open
    /// last time, else a new one. Ordered after the shutdown in flight: a
    /// window closed and opened again within the grace starts nothing until
    /// the child before it has gone.
    public func start(inView: Bool) async {
        await closeCurrent()
        shut = false
        self.inView = inView
        listing = .reading
        let outcome = await starter.locateBinary()
        switch outcome {
        case .notFound:
            listing = .missing
            await replace(with: .new, starter: starter)
            return
        case .found(let found, _):
            self.found = found
            skippedOverride = outcome.skippedOverride.map { ($0.candidate.url.path, $0.reason) }
            sayOnce(outcome)
        }
        await refresh()
        await readModels()
        let last = preferences.openChat.flatMap { id in chats.contains { $0.id == id } ? id : nil }
        await replace(with: last.map(Target.chat) ?? .new, starter: located)
    }

    /// Sessions came into view: resume the open session, read the list again
    /// -- with the window closed, nothing starts until it opens again.
    public func cameIntoView() async {
        inView = true
        pauseWhenIdle = false
        if !shut {
            if current.isPaused {
                current.resume()
            } else if current.connection == .idle {
                current.start()
            }
        }
        await refresh()
    }

    /// Another place took the window: pause the open session now if it is
    /// idle, else once its turn ends. A conversation nothing of which is
    /// written is never paused -- no `--resume` could carry it on, and the
    /// CLI refuses `--private` with one -- so it stays the one live child
    /// until it is closed.
    public func leftView() async {
        inView = false
        guard !current.isLostIfEnded else { return }
        if busy {
            pauseWhenIdle = true
        } else {
            await current.pause()
        }
    }

    /// A turn is running in the open session, or stopping.
    private var busy: Bool {
        if case .stopping = current.transcript.status { return true }
        return current.transcript.turnInFlight
    }

    /// The window closed: the open session ends (and a private one on the
    /// fallback is deleted), and nothing starts again until the window opens.
    public func shutDown() async {
        shut = true
        await closeCurrent()
    }

    /// Whether quitting must wait: the open session holds a child or is
    /// launching one, or a delete its private fallback promised has not run.
    public var needsShutdown: Bool {
        !closed && (current.isLive || current.privateMode == .deletedOnClose)
    }

    /// ⌘Q: nothing starts again; the open session is ended -- a launch still
    /// locating called off, its child asked to go and terminated if it will
    /// not -- then the delete its private fallback promised runs. The child is
    /// waited for at most the session's grace, on the library's clock: one
    /// that will not end by then is given up, so the app still quits, and its
    /// stop's escalation carries on. A close already in flight -- a switch's,
    /// the window's -- is joined, never repeated.
    public func quit() async -> ShutdownReport {
        shut = true
        let session = current
        let ended = await finishes(within: SessionViewModel.grace) { await session.shutDown() }
        let failure = await deletePromised(of: session)
        if session === current { closed = true }
        return ShutdownReport(ended: ended, deleteFailure: failure)
    }

    /// Waits for `work` at most `limit` on the library's clock: whether it
    /// finished in time. Given up, the work carries on.
    private func finishes(within limit: Duration, _ work: @escaping @MainActor @Sendable () async -> Void) async -> Bool {
        let first = FirstAnswer()
        let clock = self.clock
        return await withCheckedContinuation { continuation in
            first.continuation = continuation
            let timer = Task { @MainActor in
                try? await clock.sleep(for: limit)
                first.give(false)
            }
            Task { @MainActor in
                await work()
                timer.cancel()
                first.give(true)
            }
        }
    }

    // MARK: - Reading

    /// Reads `chats list` again; and, when its rows name backends (the CLI's
    /// 52a), `models list` for each backend's kind.
    public func refresh() async {
        guard let found else { return }
        if listing != .read { listing = .reading }
        let outcome = await runner.read(binary: found.url, ["chats", "list"])
        guard let document = outcome.document else {
            listing = .failed(outcome.failure ?? "the list could not be read")
            return
        }
        chats = ChatSummary.list(from: document)
        listing = .read
        identifyNewSession()
        if chats.contains(where: { $0.backend != nil }) && !modelsRead {
            await readModels()
        }
    }

    /// Reads `models list` once: each backend's kind (the glyphs) and the
    /// configured backends (the settings sheet's choices).
    private func readModels() async {
        guard let found, let models = await runner.read(binary: found.url, ["models", "list"]).document else { return }
        modelsRead = true
        kinds = ModelKind.byBackend(from: models)
        var names: [String] = []
        for entry in models.entries ?? [] {
            if let backend = entry["backend"]?.stringValue, !names.contains(backend) { names.append(backend) }
        }
        backends = names
        current.modelChoices = names
    }

    /// The window of the model a session names, for its meter: `models info
    /// <model>` -- a stored set's typed `data.window.tokens` (the CLI's 52e),
    /// else the leading count of its `window` field ("8192 tokens (the
    /// default)"). An API model states none: unknown, never guessed.
    private func readWindow(of model: String, for session: SessionViewModel) async {
        guard let found, let info = await runner.read(binary: found.url, ["models", "info", model]).document else { return }
        session.contextWindow = Self.window(in: info)
    }

    static func window(in info: ReadDocument) -> Int? {
        if let tokens = info.value["data"]?["window"]?["tokens"]?.intValue, tokens > 0 { return tokens }
        guard let text = Field.list(from: info).first(where: { $0.label == "window" })?.value else { return nil }
        let digits = text.prefix { $0.isNumber }
        return Int(digits).flatMap { $0 > 0 ? $0 : nil }
    }

    /// Reads `models status` for the inspector.
    public func refreshRoles() async {
        guard let found else { return }
        let outcome = await runner.read(binary: found.url, ["models", "status"])
        if let document = outcome.document {
            roles = Field.list(from: document)
            rolesFailure = nil
        } else {
            rolesFailure = outcome.failure
        }
    }

    /// A new session is saved by its first turn under an id the app did not
    /// choose: the one id that appeared since it opened is it. Two at once
    /// (another session saved meanwhile) name none, rather than guess.
    private func identifyNewSession() {
        guard current.chatID == nil, current.privateMode != .nothingWritten, let before = listedBeforeNew else { return }
        let appeared = chats.map(\.id).filter { !before.contains($0) }
        if appeared.count == 1, let id = appeared.first {
            current.identify(id)
            listedBeforeNew = nil
            if current.privateMode == nil {
                preferences.openChat = id
            }
        }
    }

    // MARK: - Opening

    /// Opens a saved conversation, or a new one. A private conversation that
    /// can't be reopened asks first, and so does a turn running in the open
    /// session.
    public func open(_ target: Target) async {
        if case .chat(let id) = target, current.chatID == id { return }
        if current.isLostIfEnded {
            pending = .endPrivate(for: target)
            return
        }
        if current.transcript.turnInFlight {
            pending = .stopTurn(for: target)
            return
        }
        await switchTo(target)
    }

    /// ⌘N.
    public func newSession() async {
        await open(.new)
    }

    /// A new session on `model` -- `-m` -- from the models library's "Use in
    /// a new session".
    public func newSession(on model: String) async {
        nextModel = model
        await open(.new)
    }

    /// Whether the build streams a long job's events (52b), as the last
    /// session announced; `false` until one has -- the fallback.
    public var streamsJobs: Bool { lastEvents?.contains("job") ?? false }

    /// Whether the build carries the CLI's whole track 52 (schema 2026-10-20
    /// or later): a new brief planned alone (52c's dry run) among it.
    public var hasTrack52: Bool { (lastSchema ?? "") >= "2026-10-20" }

    /// Whether a running session keeps a rename made from outside -- `chats
    /// title` beside it -- rather than putting its own title back on its next
    /// save (the CLI's 55b), as the last session's vocabulary says: 2026-10-25
    /// or later. 55b shipped before the schema moved to that date (55h's
    /// change), so every build announcing it keeps outside renames; one built
    /// between the two announces 2026-10-24 and is taken as older -- the side
    /// that still renames correctly, at the cost of a pause.
    public var keepsOutsideRenames: Bool { (lastSchema ?? "") >= "2026-10-25" }

    /// The found binary's version, as `apogee version` said it.
    public var binaryVersion: String? { found?.version }

    /// The binary the library found, for the other places' reads.
    public var binaryURL: URL? { found?.url }

    /// ⇧⌘N.
    public func newPrivateSession() async {
        await open(.privately)
    }

    /// ⌥⌘N: the execute door, with the suites read for it.
    public func openExecuteDoor() async {
        executeForm = ExecuteForm()
        executeDoorOpen = true
        await readSuites()
    }

    /// `config get suites` and `config get models.default_suite`.
    private func readSuites() async {
        guard let found else { return }
        if case .done(let text) = await runner.command(binary: found.url, ["config", "get", "suites"]) {
            suites = Suite.names(text)
        }
        if case .done(let text) = await runner.command(binary: found.url, ["config", "get", "models.default_suite"]) {
            defaultSuite = text.trimmingCharacters(in: .whitespacesAndNewlines)
        }
    }

    /// The configured backends, as `models list` named them: the door's models.
    public var modelChoices: [String] { backends }

    /// Whether the door's session has a suite to orchestrate: one named, or
    /// the default. Bare, the CLI refuses `--orchestrate`.
    public var canOrchestrate: Bool {
        executeForm.suite != nil || !(defaultSuite ?? "").isEmpty
    }

    /// The door's session, as the sheet says it: the catalog read first, for
    /// `/play`; then the one live child, as every opening is.
    public func openExecute() async {
        executeDoorOpen = false
        if let found, let document = await runner.read(binary: found.url, ["symphonies", "list"]).document {
            plays = Symphony.list(from: document).map { PlayChoice(name: $0.name, description: $0.description) }
        }
        await open(.execute(executeForm.launch(orchestrates: canOrchestrate)))
    }

    /// Whether a private session writes nothing (this build has `--private`),
    /// or is deleted when it closes; unknown until a session has spoken.
    public var privateIsNothingWritten: Bool { lastAccepts?.contains("set") ?? false }

    /// ⇧⌘F.
    public func requestSearch() {
        searchRequests += 1
    }

    public func confirm() async {
        guard let pending else { return }
        self.pending = nil
        switch pending {
        case .stopTurn(let target), .endPrivate(let target): await switchTo(target)
        case .delete(let chat): await delete(chat)
        }
    }

    /// Keeps things as they are: no switch, no delete -- a private
    /// conversation asked about keeps running.
    public func dismissPending() {
        pending = nil
    }

    /// Ends the open session's child -- a turn in it is cancelled the
    /// protocol's way -- then opens `target`: never two children at once.
    private func switchTo(_ target: Target) async {
        await replace(with: target, starter: located)
        await refresh()
    }

    /// Opens `target` in place of the open session. Whatever is open when the
    /// new one is made has been closed first -- its launch called off, its
    /// child gone -- even one another opening made meanwhile, so two openings
    /// at once leave the last one's child alone.
    private func replace(with target: Target, starter: any ChatStarting) async {
        var leaving: SessionViewModel
        repeat {
            leaving = current
            await closeCurrent()
        } while leaving !== current
        make(target, starter: starter)
        if inView && !shut { current.start() }
    }

    private func make(_ target: Target, starter: any ChatStarting) {
        closed = false
        switch target {
        case .new:
            let model = nextModel
            nextModel = nil
            current = SessionViewModel(
                starter: starter, launch: ChatLaunch(settings: model.map { RunSettings(model: $0) }), reader: runner)
            listedBeforeNew = Set(chats.map(\.id))
            preferences.openChat = nil
        case .privately:
            let nothingWritten = privateIsNothingWritten
            current = SessionViewModel(
                starter: starter, launch: ChatLaunch(privately: nothingWritten, noRecall: !nothingWritten),
                reader: runner)
            listedBeforeNew = nothingWritten ? nil : Set(chats.map(\.id))
            preferences.openChat = nil
        case .chat(let id):
            current = SessionViewModel(starter: starter, launch: ChatLaunch(resume: id), reader: runner)
            listedBeforeNew = nil
            preferences.openChat = id
        case .execute(let launch):
            current = SessionViewModel(starter: starter, launch: launch, reader: runner)
            current.plays = plays
            current.defaultSuite = defaultSuite
            listedBeforeNew = Set(chats.map(\.id))
            preferences.openChat = nil
        }
        current.observer = { [weak self] event in self?.observe(event) }
        // A session that locates for itself -- none was found at launch --
        // may find a binary while passing the override over: said too.
        current.onLocated = { [weak self] outcome in
            guard let self, let skip = outcome.skippedOverride else { return }
            skippedOverride = (skip.candidate.url.path, skip.reason)
            sayOnce(outcome)
        }
        current.onAction = { [weak self] action in self?.act(action) }
        current.modelChoices = backends
    }

    /// Ends the open session's child -- its launch called off, a turn in it
    /// cancelled the protocol's way -- then deletes it, once, if its private
    /// fallback promised so. A second close of the same session joins the
    /// first: its child is stopped once and its delete runs once.
    private func closeCurrent() async {
        let session = current
        await session.shutDown()
        await deletePromised(of: session)
        if session === current { closed = true }
    }

    /// The private fallback's promise, kept once `session`'s child has gone:
    /// its chat deleted -- once the list has named it. Run once per chat; a
    /// second ask joins the first. Returns the failure, in the CLI's words.
    @discardableResult
    private func deletePromised(of session: SessionViewModel) async -> String? {
        guard session.privateMode == .deletedOnClose, let found else { return nil }
        if session.chatID == nil { await refresh() }
        guard let id = session.chatID else { return nil }
        if let begun = deletions[id] { return await begun.value }
        let deletion = Task { await delete(privately: id, binary: found.url) }
        deletions[id] = deletion
        return await deletion.value
    }

    private func delete(privately id: String, binary: URL) async -> String? {
        let outcome = await runner.command(binary: binary, ["chats", "delete", id])
        if let failure = outcome.failure {
            self.failure = "The private session was not deleted: \(failure)"
        }
        await refresh()
        return outcome.failure
    }

    /// An act the open session's window hands up.
    private func act(_ action: SessionAction) {
        switch action {
        case .newSession: Task { await newSession() }
        case .newPrivateSession: Task { await newPrivateSession() }
        case .rename: renaming = currentChat
        case .delete: if let chat = currentChat { askToDelete(chat) }
        case .toggleInspector: inspectorOpen.toggle()
        case .openChat(let id): Task { await open(.chat(id)) }
        }
    }

    /// Discovery as run once, for the sessions after it: what it passed over
    /// is the library's to say (`skippedOverride`), so the sessions' own
    /// outcome lists nothing passed over.
    private var located: any ChatStarting {
        found.map { LocatedStarter(base: starter, found: $0) } ?? starter
    }

    /// Says, the first time this launch's discovery passes the override
    /// over, which binary runs instead and why the override was not used.
    private func sayOnce(_ outcome: DiscoveryOutcome) {
        guard !saidSkippedOverride, case .found(let found, _) = outcome, let skip = outcome.skippedOverride else { return }
        saidSkippedOverride = true
        say?("Not using \(skip.candidate.url.path): \(skip.reason) -- running \(found.url.path) instead")
    }

    private func observe(_ event: SessionEvent) {
        guard case .event(let event) = event else {
            // The child ended before its turn did: a rename that waited for
            // the turn is made now, its child gone, by `chats title` alone.
            if let waiting = renameWhenIdle {
                renameWhenIdle = nil
                Task { await rename(waiting.chat, to: waiting.name) }
            }
            return
        }
        switch event.kind {
        case .session(let info):
            lastAccepts = info.capabilities?.accepts ?? []
            lastEvents = info.capabilities?.events ?? []
            lastSchema = info.capabilities?.schema
            let session = current
            Task { await readWindow(of: info.model, for: session) }
        case .result, .error:
            // A turn ended: what it changed is read back, a rename that waited
            // for it is made, and a session out of view is paused now it is
            // idle.
            Task {
                await refresh()
                if let waiting = renameWhenIdle {
                    renameWhenIdle = nil
                    await rename(waiting.chat, to: waiting.name)
                }
                if pauseWhenIdle && !inView {
                    pauseWhenIdle = false
                    await current.pause()
                }
            }
        default:
            break
        }
    }

    // MARK: - Changing

    /// Renames a saved conversation: `chats title <id> <name>`, read back.
    ///
    /// The open session's own conversation, while its child runs, is renamed
    /// so that its next turn keeps the name: in place on a binary that keeps
    /// outside renames (the CLI's 55b); on an older one, whose running session
    /// would save its own title back over it, the session is paused first and
    /// resumed after -- an ordinary chat resumes by its id -- and mid-turn the
    /// rename waits for the turn to end.
    public func rename(_ chat: ChatSummary, to name: String) async {
        let name = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let found, !name.isEmpty, name != chat.name else { return }
        let session = current
        let pauses = chat.id == session.chatID && session.isLive && !keepsOutsideRenames
        if pauses && busy {
            renameWhenIdle = (chat, name)
            say?("Renaming when this turn ends")
            return
        }
        if pauses { await session.pause() }
        let outcome = await runner.command(binary: found.url, ["chats", "title", chat.id, name])
        if pauses && session === current && inView && !shut { session.resume() }
        await refresh()
        if let failure = outcome.failure {
            self.failure = failure
            say?("Not renamed: \(failure)")
        } else {
            failure = nil
            say?("Renamed to \(name)")
        }
    }

    /// Asks before deleting.
    public func askToDelete(_ chat: ChatSummary) {
        pending = .delete(chat)
    }

    /// `chats delete <id>`, read back. The open session, if it is the one,
    /// ends first and a new one opens in its place.
    private func delete(_ chat: ChatSummary) async {
        guard let found else { return }
        if current.chatID == chat.id {
            await replace(with: .new, starter: located)
        }
        let outcome = await runner.command(binary: found.url, ["chats", "delete", chat.id])
        await refresh()
        if let failure = outcome.failure {
            self.failure = failure
            say?("Not deleted: \(failure)")
        } else {
            failure = nil
            say?("Deleted \(chat.name)")
        }
    }

    public func dismissFailure() {
        failure = nil
    }

    // MARK: - What the list shows

    /// The list, filtered and grouped by date, newest first.
    public var groups: [ChatGroup] {
        let needle = query.trimmingCharacters(in: .whitespaces).lowercased()
        // A private session on the fallback is saved until it closes: never listed.
        let listed = current.privateMode == nil ? chats : chats.filter { $0.id != current.chatID }
        let shown = needle.isEmpty ? listed : listed.filter { $0.name.lowercased().contains(needle) }
        return ChatGroup.grouping(shown, now: now())
    }

    /// What the list says when it has no rows to show.
    public var emptyText: String? {
        switch listing {
        case .notRead, .reading: return nil
        case .missing: return "Apogee isn't installed, so there are no sessions to list."
        case .failed(let message): return "The sessions could not be read: \(message)"
        case .read: break
        }
        if chats.isEmpty { return "No saved sessions yet. Each one is listed here after its first turn." }
        if groups.isEmpty { return "No sessions match “\(query)”." }
        return nil
    }

    /// The kind of model a saved conversation runs on, when its row names its
    /// backend and that backend's kind is known; `nil` draws the one neutral
    /// glyph (the design's G8 fallback).
    public func kind(of chat: ChatSummary) -> ModelKind? {
        chat.backend.flatMap { kinds[$0] }
    }

    /// When a row was last changed, as the list shows it: the time today or
    /// yesterday, the weekday within the week, the date before that.
    public func when(_ chat: ChatSummary, calendar: Calendar = .current) -> String {
        guard let updated = chat.updated else { return "" }
        let days = calendar.dateComponents(
            [.day], from: calendar.startOfDay(for: updated), to: calendar.startOfDay(for: now())
        ).day ?? .max
        var style = Date.FormatStyle(timeZone: calendar.timeZone)
        style.calendar = calendar
        switch days {
        case ...1: return updated.formatted(style.hour().minute())
        case 2...7: return updated.formatted(style.weekday(.abbreviated))
        default: return updated.formatted(style.month(.abbreviated).day())
        }
    }

    /// Whether `chat` is the open session.
    public func isOpen(_ chat: ChatSummary) -> Bool {
        current.chatID == chat.id
    }

    /// The open session's saved row, once it has one.
    public var currentChat: ChatSummary? {
        current.chatID.flatMap { id in chats.first { $0.id == id } }
    }

    // MARK: - The inspector

    /// The inspector's sections, from what the session said and the reads.
    public var inspector: Inspector {
        Inspector(
            chat: currentChat, chatID: current.chatID, model: current.transcript.model,
            kind: currentChat.flatMap(kind(of:)), lastTurn: lastUsage, roles: roles,
            rolesFailure: rolesFailure)
    }

    private var lastUsage: Usage? {
        for item in current.transcript.items.reversed() {
            if case .answer(let answer) = item.content, answer.finished { return answer.usage }
        }
        return nil
    }
}

/// What a quit did, for the app's log.
public struct ShutdownReport: Equatable, Sendable {
    /// Whether the open session's child ended within the grace. One that
    /// would not is given up, so the app can quit; its stop's escalation
    /// carries on.
    public let ended: Bool
    /// The private fallback's promised delete, when it failed: the CLI's words.
    public let deleteFailure: String?

    public init(ended: Bool, deleteFailure: String?) {
        self.ended = ended
        self.deleteFailure = deleteFailure
    }
}

/// The first of two answers, given once: a wait's, or its limit's.
@MainActor
private final class FirstAnswer {
    var continuation: CheckedContinuation<Bool, Never>?

    func give(_ answer: Bool) {
        continuation?.resume(returning: answer)
        continuation = nil
    }
}

/// A date group of the session list.
public struct ChatGroup: Equatable, Identifiable {
    public let title: String
    public let chats: [ChatSummary]
    public var id: String { title }

    static let order = ["Today", "Yesterday", "Previous 7 days", "Earlier"]

    /// `chats` in the design's groups, in its order, empty groups left out;
    /// a row with no readable date is Earlier.
    public static func grouping(_ chats: [ChatSummary], now: Date, calendar: Calendar = .current) -> [ChatGroup] {
        let today = calendar.startOfDay(for: now)
        func group(_ chat: ChatSummary) -> String {
            guard let updated = chat.updated else { return "Earlier" }
            let day = calendar.startOfDay(for: updated)
            let days = calendar.dateComponents([.day], from: day, to: today).day ?? .max
            switch days {
            case ...0: return "Today"
            case 1: return "Yesterday"
            case 2...7: return "Previous 7 days"
            default: return "Earlier"
            }
        }
        let grouped = Dictionary(grouping: chats, by: group)
        return order.compactMap { title in grouped[title].map { ChatGroup(title: title, chats: $0) } }
    }
}

/// What the inspector shows beside the open session.
public struct Inspector: Equatable {
    public let chat: ChatSummary?
    public let chatID: String?
    /// The model the session named in its `session` event.
    public let model: String?
    public let kind: ModelKind?
    /// The last finished turn's tokens, when the provider reported them.
    public let lastTurn: Usage?
    public let roles: [Field]
    public let rolesFailure: String?

    /// The session's own facts, label and value.
    public var session: [Field] {
        guard let chat else {
            return [Field("Saved", chatID == nil ? "after its first turn" : "yes")]
        }
        var fields = [Field("Name", chat.name), Field("Turns", String(chat.turns))]
        if let updated = chat.updated {
            fields.append(Field("Updated", updated.formatted(date: .abbreviated, time: .shortened)))
        }
        fields.append(Field("Id", chat.id))
        return fields
    }

    /// The model it runs on.
    public var modelFacts: [Field] {
        var fields: [Field] = []
        if let model { fields.append(Field("Model", model)) }
        if let backend = chat?.backend { fields.append(Field("Backend", backend)) }
        if let pinned = chat?.model { fields.append(Field("Pinned", pinned)) }
        if let kind {
            fields.append(Field("Runs", Self.words(kind)))
        }
        return fields
    }

    /// The last turn's tokens, as reported -- absent is not zero.
    public var turnFacts: [Field] {
        guard let lastTurn else { return [Field("Tokens", "not reported")] }
        return [
            Field("In", lastTurn.inputTokens.map(String.init) ?? "not reported"),
            Field("Out", lastTurn.outputTokens.map(String.init) ?? "not reported"),
        ]
    }

    static func words(_ kind: ModelKind) -> String {
        switch kind {
        case .local: "on this machine"
        case .api: "cloud API, billed"
        case .subscription: "subscription"
        }
    }
}
