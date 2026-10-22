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
/// - **One live chat child.** Exactly one session has a child at a time.
///   Opening another ends the current one's first; leaving the place pauses
///   an idle session -- its child ends and its model is freed -- or, mid-turn,
///   once its turn ends; coming back resumes it with `--resume`.
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
    /// The ids listed when a new session opened: the one it is saved as is
    /// the one that appears.
    private var listedBeforeNew: Set<String>?
    /// The symphonies an execute session's `/play` offers.
    private var plays: [PlayChoice] = []

    public init(
        starter: any ChatStarting, runner: any CommandRunning, preferences: Preferences,
        now: @escaping () -> Date = Date.init
    ) {
        self.starter = starter
        self.runner = runner
        self.preferences = preferences
        self.now = now
        inspectorOpen = preferences.inspectorOpen
        current = SessionViewModel(starter: starter, launch: ChatLaunch(), reader: runner)
    }

    // MARK: - The place

    /// The app started: find the binary, read the list, and -- when Sessions
    /// is in view -- open the session left open last time, else a new one.
    public func start(inView: Bool) async {
        self.inView = inView
        listing = .reading
        switch await starter.locateBinary() {
        case .notFound:
            listing = .missing
            make(.new, starter: starter)
            if inView { current.start() }
            return
        case .found(let found, _):
            self.found = found
        }
        await refresh()
        await readModels()
        let last = preferences.openChat.flatMap { id in chats.contains { $0.id == id } ? id : nil }
        make(last.map(Target.chat) ?? .new, starter: located)
        if inView { current.start() }
    }

    /// Sessions came into view: resume the open session, read the list again.
    public func cameIntoView() async {
        inView = true
        pauseWhenIdle = false
        if current.isPaused {
            current.resume()
        } else if current.connection == .idle {
            current.start()
        }
        await refresh()
    }

    /// Another place took the window: pause the open session now if it is
    /// idle, else once its turn ends.
    public func leftView() async {
        inView = false
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
    /// fallback is deleted).
    public func shutDown() async {
        await close(current)
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

    /// Opens a saved conversation, or a new one. A turn running in the open
    /// session asks first.
    public func open(_ target: Target) async {
        if case .chat(let id) = target, current.chatID == id { return }
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
        case .stopTurn(let target): await switchTo(target)
        case .delete(let chat): await delete(chat)
        }
    }

    public func dismissPending() {
        pending = nil
    }

    /// Ends the open session's child -- a turn in it is cancelled the
    /// protocol's way -- then opens `target`: never two children at once.
    private func switchTo(_ target: Target) async {
        await close(current)
        make(target, starter: located)
        if inView { current.start() }
        await refresh()
    }

    private func make(_ target: Target, starter: any ChatStarting) {
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
        current.onAction = { [weak self] action in self?.act(action) }
        current.modelChoices = backends
    }

    /// Ends `session`'s child; a private session on the fallback is then
    /// deleted, as it promised -- once the list has named it.
    private func close(_ session: SessionViewModel) async {
        await session.shutDown()
        guard session.privateMode == .deletedOnClose, let found else { return }
        if session.chatID == nil { await refresh() }
        guard let id = session.chatID else { return }
        let outcome = await runner.command(binary: found.url, ["chats", "delete", id])
        if let failure = outcome.failure {
            self.failure = "The private session was not deleted: \(failure)"
        }
        await refresh()
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

    private var located: any ChatStarting {
        found.map { LocatedStarter(base: starter, found: $0) } ?? starter
    }

    private func observe(_ event: SessionEvent) {
        guard case .event(let event) = event else { return }
        switch event.kind {
        case .session(let info):
            lastAccepts = info.capabilities?.accepts ?? []
            lastEvents = info.capabilities?.events ?? []
            lastSchema = info.capabilities?.schema
            let session = current
            Task { await readWindow(of: info.model, for: session) }
        case .result, .error:
            // A turn ended: what it changed is read back, and a session out
            // of view is paused now it is idle.
            Task {
                await refresh()
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
    public func rename(_ chat: ChatSummary, to name: String) async {
        let name = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let found, !name.isEmpty, name != chat.name else { return }
        let outcome = await runner.command(binary: found.url, ["chats", "title", chat.id, name])
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
            await current.shutDown()
            make(.new, starter: located)
            if inView { current.start() }
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
