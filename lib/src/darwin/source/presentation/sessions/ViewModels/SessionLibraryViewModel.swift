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
        case chat(String)
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
    public var inspectorOpen: Bool {
        didSet { preferences.inspectorOpen = inspectorOpen }
    }
    /// Says one line in the window's toast.
    public var say: (@MainActor (String) -> Void)?

    private let starter: any ChatStarting
    private let runner: any CommandRunning
    private let preferences: Preferences
    private let now: () -> Date
    private var found: FoundBinary?
    private var kinds: [String: ModelKind] = [:]
    private var inView = false
    /// The open session leaves view mid-turn: pause it when the turn ends.
    private var pauseWhenIdle = false
    /// The ids listed when a new session opened: the one it is saved as is
    /// the one that appears.
    private var listedBeforeNew: Set<String>?

    public init(
        starter: any ChatStarting, runner: any CommandRunning, preferences: Preferences,
        now: @escaping () -> Date = Date.init
    ) {
        self.starter = starter
        self.runner = runner
        self.preferences = preferences
        self.now = now
        inspectorOpen = preferences.inspectorOpen
        current = SessionViewModel(starter: starter)
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

    /// The window closed: the open session ends.
    public func shutDown() async {
        await current.shutDown()
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
        if chats.contains(where: { $0.backend != nil }),
            let models = await runner.read(binary: found.url, ["models", "list"]).document
        {
            kinds = ModelKind.byBackend(from: models)
        }
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
        guard current.chatID == nil, let before = listedBeforeNew else { return }
        let appeared = chats.map(\.id).filter { !before.contains($0) }
        if appeared.count == 1, let id = appeared.first {
            current.identify(id)
            listedBeforeNew = nil
            preferences.openChat = id
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
        await current.shutDown()
        make(target, starter: located)
        if inView { current.start() }
        await refresh()
    }

    private func make(_ target: Target, starter: any ChatStarting) {
        switch target {
        case .new:
            current = SessionViewModel(starter: starter)
            listedBeforeNew = Set(chats.map(\.id))
            preferences.openChat = nil
        case .chat(let id):
            current = SessionViewModel(starter: starter, launch: ChatLaunch(resume: id))
            listedBeforeNew = nil
            preferences.openChat = id
        }
        current.observer = { [weak self] event in self?.observe(event) }
    }

    private var located: any ChatStarting {
        found.map { LocatedStarter(base: starter, found: $0) } ?? starter
    }

    private func observe(_ event: SessionEvent) {
        guard case .event(let event) = event else { return }
        switch event.kind {
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
        let shown = needle.isEmpty ? chats : chats.filter { $0.name.lowercased().contains(needle) }
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
