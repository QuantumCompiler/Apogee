import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Observation

/// A definition file open in the app's editor: the file the CLI reads, its
/// text as it was when opened, as the file holds it now and as edited, and
/// what the binary said after the save.
public struct DefinitionEditor: Equatable, Sendable {
    /// Why closing the editor now would lose something, so it asks first
    /// (GUI 6f).
    public enum CloseReason: Equatable, Sendable {
        /// Typing not yet saved.
        case unsaved
        /// The last save was refused by apogee, and the file now holds that
        /// text rather than the one it held when opened.
        case refusedOnDisk
    }

    public let title: String
    public let path: String
    public let original: String
    public var text: String
    /// The text the file holds, as far as this editor knows: read at open,
    /// moved by every write that landed -- a save or a put-back (GUI 6f).
    public internal(set) var onDisk: String
    /// Whether apogee refused the text the file holds now.
    public internal(set) var onDiskRefused = false
    /// The binary's word on the saved file -- its refusal verbatim, or that
    /// it stands -- nil before a save.
    public var verdict: String?
    public var refused = false
    /// A save running now: the editor is read-only until it ends, so nothing
    /// typed meanwhile is lost (GUI 4h).
    public var saving = false

    init(title: String, path: String, original: String, text: String? = nil) {
        self.title = title
        self.path = path
        self.original = original
        self.text = text ?? original
        onDisk = original
    }

    /// Whether the text differs from what the file held when opened: what
    /// Save keys on, and what Put it back as it was undoes.
    public var changed: Bool { text != original }

    /// Whether the text differs from what the file holds now: typing that
    /// closing would lose. A save that landed clears it (GUI 6f).
    public var unsaved: Bool { text != onDisk }

    /// Whether the file holds something other than what it held when opened.
    public var rewritten: Bool { onDisk != original }

    /// Why closing now would lose something; nil when it loses nothing, or
    /// while a save runs -- an editor closed mid-save stays closed, its text
    /// already on its way to the file (GUI 4h). A refused text on disk is
    /// asked about first: putting it back is the only way to the original.
    public var closeReason: CloseReason? {
        if saving { return nil }
        if onDiskRefused && rewritten { return .refusedOnDisk }
        return unsaved ? .unsaved : nil
    }

    /// The file's name, as the close prompt says it.
    public var fileName: String { URL(fileURLWithPath: path).lastPathComponent }

    /// The close prompt's question.
    public func question(_ reason: CloseReason) -> String {
        switch reason {
        case .unsaved: "Discard your changes to \(fileName)?"
        case .refusedOnDisk: "\(fileName) holds the text apogee refused"
        }
    }

    /// What the close prompt says under its question.
    public func explanation(_ reason: CloseReason) -> String {
        switch reason {
        case .unsaved:
            return "What you typed isn't saved, and closing loses it."
        case .refusedOnDisk:
            let said =
                "Put it back as it was writes the text it held when you opened it, and apogee reads it again. Leave it keeps the refused text in the file, and apogee can't play it until it is fixed."
            return unsaved ? said + " What you typed since the save is lost either way." : said
        }
    }

    /// A write of `saved` landed: the file holds it now, not yet judged.
    mutating func wrote(_ saved: String) {
        onDisk = saved
        onDiskRefused = false
    }

    /// A save of `saved` ended: written onto the editor as it stands now --
    /// never onto the copy taken when Save was pressed, which would put back
    /// whatever was typed since. Re-enabled with the binary's word; text that
    /// moved on during the save is kept and said unsaved.
    mutating func endSave(of saved: String, verdict: String, refused: Bool) {
        saving = false
        self.refused = refused
        if onDisk == saved { onDiskRefused = refused }
        self.verdict = text == saved ? verdict : verdict + "\nWhat you typed since pressing Save isn't saved yet."
    }
}

/// A view-model that owns a definition editor: the agents' and the
/// symphonies'. The editor sheet is drawn over it, and closing goes through
/// it, asked first when something would be lost (GUI 6f).
@MainActor
public protocol DefinitionEditing: AnyObject {
    var editor: DefinitionEditor? { get set }
    /// The close prompt standing now, and why; nil when none is.
    var closing: DefinitionEditor.CloseReason? { get set }
    /// Whether the editor offers Put it back as it was now.
    var offersRestore: Bool { get }
    func save() async
    /// Puts the file back as it was when opened, and saves that.
    func restore() async
}

@MainActor
extension DefinitionEditing {
    /// Close or Esc: closes at once when nothing would be lost (nothing
    /// typed, a save that stood, a save running), else raises the prompt
    /// and leaves the editor open.
    public func requestClose() {
        guard let editor else { return }
        if let reason = editor.closeReason {
            closing = reason
        } else {
            closeEditor()
        }
    }

    /// Closes now, asking nothing.
    public func closeEditor() {
        closing = nil
        editor = nil
    }

    /// The prompt's Discard: closes, the typing gone, the file as it is.
    public func discardAndClose() {
        closeEditor()
    }

    /// The prompt's Leave it: closes with the refused text in the file.
    public func leaveAndClose() {
        closeEditor()
    }

    /// The prompt's Keep editing (and its Esc): the editor as it was.
    public func keepEditing() {
        closing = nil
    }

    /// The prompt's Put it back as it was: writes the original, and closes
    /// once apogee has answered -- and only if the file then holds the
    /// original; a write that failed keeps the editor open, saying why.
    public func restoreAndClose() async {
        closing = nil
        guard let path = editor?.path else { return }
        await restore()
        guard let editor, editor.path == path, !editor.saving, !editor.rewritten else { return }
        closeEditor()
    }
}

/// Where one of the Library's lists stands: not read yet, read, no apogee,
/// or a failed read -- said where the list is drawn, in the CLI's words, as
/// Records and Tasks say theirs (GUI 4h). A failed refresh keeps the rows
/// read before, as theirs do; it is never silently kept.
public enum LibraryListing: Equatable, Sendable {
    case notRead
    case read
    case missing
    case failed(String)

    /// The line drawn with the list of `what`; nil when there is nothing to
    /// say.
    func text(_ what: String) -> String? {
        switch self {
        case .notRead, .read: nil
        case .missing: "Apogee isn't installed, so there are no \(what) to list."
        case .failed(let message): "The \(what) could not be read: \(message)"
        }
    }

    /// A list read: its document's rows, or why there are none.
    static func of(_ outcome: ReadOutcome) -> LibraryListing {
        outcome.document != nil ? .read : .failed(outcome.failure ?? "apogee printed nothing to read")
    }
}

/// The data directory the definitions' relative paths name: `apogee config
/// path`'s file sits in `<data>/config/`.
@MainActor
func dataDirectory(_ runner: any CommandRunning, _ binary: URL) async -> URL? {
    guard case .done(let stdout) = await runner.command(binary: binary, ["config", "path"]),
        let line = stdout.split(whereSeparator: \.isNewline).first
    else { return nil }
    return URL(fileURLWithPath: String(line)).deletingLastPathComponent().deletingLastPathComponent()
}

/// The Library's agents: `agents list`, the scaffold's create, delete with or
/// without the files, and the person's own prompt and schema edited in place
/// -- put back as it was after any save, and closed asked first when typing
/// would be lost (GUI 6f).
///
/// SwiftUI-free (ADR 0002). No GUI-side parser: nothing in this build checks
/// an agent's files short of running it, so a save is read back and said as
/// such.
@MainActor
@Observable
public final class AgentsViewModel {
    /// `agents create <name>`, as a person types it.
    public struct CreateForm: Equatable, Sendable {
        public var name = ""
        public var description = ""
        public var model = ""
        /// `read-only` (the CLI's default), `all` or `none`; nil leaves it out.
        public var tools: String?
        public var noSchema = false

        public init() {}

        public var arguments: [String]? {
            let name = name.trimmingCharacters(in: .whitespaces)
            guard !name.isEmpty else { return nil }
            var arguments = ["agents", "create", name]
            let description = description.trimmingCharacters(in: .whitespaces)
            if !description.isEmpty { arguments += ["--description", description] }
            let model = model.trimmingCharacters(in: .whitespaces)
            if !model.isEmpty { arguments += ["--model", model] }
            if let tools { arguments += ["--tools", tools] }
            if noSchema { arguments.append("--no-schema") }
            return arguments
        }
    }

    public private(set) var agents: [AgentEntry] = []
    public private(set) var listing: LibraryListing = .notRead
    public var selected: String?
    public var createForm = CreateForm()
    public var creating = false
    public var editor: DefinitionEditor?
    /// The close prompt standing over the editor, and why (GUI 6f).
    public var closing: DefinitionEditor.CloseReason?
    /// An agent waiting on the person's word, and whether its files go too.
    public var pendingDelete: AgentEntry?
    public private(set) var failure: String?
    public var say: (@MainActor (String) -> Void)?

    private let runner: any CommandRunning
    private var binary: URL?
    private(set) var data: URL?

    public init(runner: any CommandRunning) {
        self.runner = runner
    }

    public func connect(binary: URL?) async {
        self.binary = binary
        guard let binary else {
            listing = .missing
            return
        }
        data = await dataDirectory(runner, binary)
        await refresh()
    }

    /// `agents list`; a failure is said where the list is, the rows read
    /// before kept.
    public func refresh() async {
        guard let binary else { return }
        let outcome = await runner.read(binary: binary, ["agents", "list"])
        if let document = outcome.document { agents = AgentEntry.list(from: document) }
        listing = .of(outcome)
    }

    /// What the list says under its rows: no apogee, or why it could not be
    /// read.
    public var emptyText: String? { listing.text("agents") }

    public var current: AgentEntry? { selected.flatMap { n in agents.first { $0.name == n } } }

    public func create() async {
        guard let arguments = createForm.arguments else { return }
        creating = false
        if await change(arguments) { selected = createForm.name.trimmingCharacters(in: .whitespaces) }
    }

    /// `agents delete <name>`, its files kept (`--keep-files`) or removed
    /// (`--purge`) as the person chose -- the CLI never asks on a pipe.
    public func confirmDelete(purge: Bool) async {
        guard let agent = pendingDelete else { return }
        pendingDelete = nil
        _ = await change(["agents", "delete", agent.name, purge ? "--purge" : "--keep-files"])
    }

    /// Opens one of the person's own agent files -- a prompt or a schema --
    /// in the editor. A shipped agent's files are Apogee's, read only.
    public func edit(_ agent: AgentEntry, file: String) {
        guard !agent.bundled, let data else { return }
        let path = data.appendingPathComponent(file).path
        guard let text = try? String(contentsOfFile: path, encoding: .utf8) else {
            failure = "\(file) could not be read"
            return
        }
        closing = nil
        editor = DefinitionEditor(title: "\(agent.name) · \(file)", path: path, original: text)
    }

    /// Writes the file the CLI reads, and reads the agents back. Nothing in
    /// this build checks an agent's files short of running it; that is said.
    /// The editor is read-only until the read-back ends (`saving`), and the
    /// end lands on the editor as it stands then: one closed meanwhile stays
    /// closed.
    public func save() async {
        guard var editor, !editor.saving else { return }
        let saved = editor.text
        do {
            try saved.write(toFile: editor.path, atomically: true, encoding: .utf8)
        } catch {
            editor.verdict = "Not saved: \(error.localizedDescription)"
            editor.refused = true
            self.editor = editor
            return
        }
        editor.wrote(saved)
        editor.saving = true
        self.editor = editor
        await refresh()
        guard self.editor?.path == editor.path else { return }
        self.editor?.endSave(
            of: saved, verdict: "Saved. Apogee checks an agent's prompt and schema when it runs -- its first run says what is wrong, if anything.",
            refused: false)
    }

    /// Puts the file back as it was when opened -- after any save, refused
    /// or not, since nothing checks an agent's files before it runs and a
    /// save is the only point to undo from (GUI 6f).
    public func restore() async {
        guard var editor, !editor.saving else { return }
        editor.text = editor.original
        self.editor = editor
        await save()
    }

    /// Put it back as it was is offered once a save has rewritten the file.
    public var offersRestore: Bool { editor?.rewritten == true }

    private func change(_ arguments: [String]) async -> Bool {
        guard let binary else { return false }
        let outcome = await runner.command(binary: binary, arguments)
        await refresh()
        if let failure = outcome.failure {
            self.failure = failure
            return false
        }
        if case .done(let stdout) = outcome {
            say?(stdout.split(whereSeparator: \.isNewline).first.map(String.init) ?? "Done")
        }
        return true
    }

    public func dismissFailure() {
        failure = nil
    }
}

extension AgentsViewModel: DefinitionEditing {}

/// The Library's symphonies: the catalog, a play supervised with its stages
/// narrated -- given its image where its input takes one --, the person's
/// own spec files edited in place and checked by the binary, closed asked
/// first when typing or the original would be lost (GUI 6f), copies and
/// deletes as their verbs.
@MainActor
@Observable
public final class SymphoniesViewModel {
    public private(set) var symphonies: [Symphony] = []
    public private(set) var listing: LibraryListing = .notRead
    public var selected: String?
    public var input = ""
    /// The image a play gives a symphony whose input takes one (`--image`):
    /// the file's path, as chosen; cleared when another symphony is chosen
    /// (GUI 6f).
    public var image: String?
    /// A play running now, and the last play's document.
    public private(set) var playing: JobProgress?
    public private(set) var play: SymphonyPlay?
    /// The symphony the running play plays, and the one the last play's
    /// document is of: each lands on its own page and no other (GUI 4h).
    public private(set) var playingName: String?
    public private(set) var playedName: String?
    public var editor: DefinitionEditor?
    /// The close prompt standing over the editor, and why (GUI 6f).
    public var closing: DefinitionEditor.CloseReason?
    public var copyName = ""
    public var pendingDelete: Symphony?
    public private(set) var failure: String?
    public var say: (@MainActor (String) -> Void)?

    private let runner: any CommandRunning
    private let starter: any JobStarting
    private var binary: URL?
    private var job: (any JobRunning)?

    public init(runner: any CommandRunning, starter: any JobStarting) {
        self.runner = runner
        self.starter = starter
    }

    public func connect(binary: URL?) async {
        self.binary = binary
        guard binary != nil else {
            listing = .missing
            return
        }
        await refresh()
    }

    /// `symphonies list`; a failure is said where the list is, the rows read
    /// before kept.
    public func refresh() async {
        guard let binary else { return }
        let outcome = await runner.read(binary: binary, ["symphonies", "list"])
        if let document = outcome.document { symphonies = Symphony.list(from: document) }
        listing = .of(outcome)
    }

    /// What the list says under its rows: no apogee, or why it could not be
    /// read.
    public var emptyText: String? { listing.text("symphonies") }

    /// The last play's document, on the page of the symphony it played --
    /// never another's.
    public var shownPlay: SymphonyPlay? {
        guard let playedName, playedName == selected else { return nil }
        return play
    }

    /// The running play's progress, with its Stop, on its own symphony's
    /// page alone.
    public var shownProgress: JobProgress? {
        guard let playingName, playingName == selected else { return nil }
        return playing
    }

    public var current: Symphony? { selected.flatMap { n in symphonies.first { $0.name == n } } }

    /// Chooses a symphony; another one's image goes, since it was chosen for
    /// the one before.
    public func select(_ symphony: Symphony) {
        if selected != symphony.name { image = nil }
        selected = symphony.name
    }

    /// The image types the CLI's `symphonies play --image` names; the binary
    /// judges the file itself.
    public static let imageTypes = ["png", "jpg", "gif", "webp", "bmp"]

    /// The chosen image's file name, as the page shows it.
    public var imageName: String? { image.map { URL(fileURLWithPath: $0).lastPathComponent } }

    /// Whether the symphony in view takes an image and none is chosen yet:
    /// Play waits, and the page says so. Only the symphony's own document
    /// decides -- `input.image` (GUI 6f).
    public var needsImage: Bool { current?.takesImage == true && image == nil }

    /// Whether Play can start: a symphony, no play running, its input, and
    /// its image where it takes one.
    public var canPlay: Bool {
        guard binary != nil, current != nil, job == nil, !needsImage else { return false }
        return !input.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
    }

    /// `symphonies play <name> --input <text> [--image <path>] --output-format
    /// json`, supervised: the stage lines as it goes, the stages' answers at
    /// the end. `--image` goes only to a symphony whose input takes one.
    public func playIt() {
        guard canPlay, let binary, let symphony = current else { return }
        let input = input.trimmingCharacters(in: .whitespacesAndNewlines)
        var arguments = ["symphonies", "play", symphony.name, "--input", input]
        if symphony.takesImage, let image { arguments += ["--image", image] }
        arguments += ["--output-format", "json"]
        play = nil
        playedName = nil
        playing = JobProgress()
        playingName = symphony.name
        let job = starter.startJob(binary: binary, arguments: arguments, streaming: false, folder: nil)
        self.job = job
        Task {
            var stdout: [String] = []
            for await update in job.updates {
                if case .said(let line) = update { stdout.append(line) }
                playing?.apply(update)
            }
            self.job = nil
            let progress = playing
            playing = nil
            playingName = nil
            if let document = ReadDocument(text: stdout.joined(separator: "\n")), let play = SymphonyPlay(document: document) {
                self.play = play
                playedName = symphony.name
            } else if case .failed(let message)? = progress?.status {
                failure = "\(symphony.name): \(message)"
            }
        }
    }

    public var isPlaying: Bool { job != nil }

    public func stop() async {
        await job?.stop(grace: .seconds(10))
    }

    /// Opens the person's own spec file in the editor; a shipped starter or a
    /// config entry is edited at a prompt (`symphonies edit`, the $EDITOR's).
    public func edit(_ symphony: Symphony) {
        guard symphony.isOwnFile, let path = symphony.path, let text = try? String(contentsOfFile: path, encoding: .utf8) else { return }
        closing = nil
        editor = DefinitionEditor(title: symphony.name, path: path, original: text)
    }

    /// Writes the spec file, then asks the binary: `symphonies show` refuses
    /// a file it cannot read, in its words, and lists the problems of one it
    /// can. The editor is read-only until the binary has answered
    /// (`saving`), and its word lands on the editor as it stands then: one
    /// closed meanwhile stays closed.
    public func save() async {
        guard var editor, !editor.saving, let binary, let name = selected else { return }
        let saved = editor.text
        do {
            try saved.write(toFile: editor.path, atomically: true, encoding: .utf8)
        } catch {
            editor.verdict = "Not saved: \(error.localizedDescription)"
            editor.refused = true
            self.editor = editor
            return
        }
        editor.wrote(saved)
        editor.saving = true
        self.editor = editor
        let outcome = await runner.read(binary: binary, ["symphonies", "show", name])
        if self.editor?.path == editor.path {
            if let document = outcome.document, let symphony = Symphony(document.value) {
                self.editor?.endSave(
                    of: saved, verdict: symphony.problems.isEmpty ? "Saved, and apogee reads it." : symphony.problems.joined(separator: "\n"),
                    refused: !symphony.problems.isEmpty)
            } else {
                self.editor?.endSave(of: saved, verdict: outcome.failure ?? "apogee could not read it", refused: true)
            }
        }
        await refresh()
    }

    /// Puts the file back as it was before the edit, and asks the binary
    /// again.
    public func restore() async {
        guard var editor, !editor.saving else { return }
        editor.text = editor.original
        self.editor = editor
        await save()
    }

    /// Put it back as it was is offered once apogee refused a save (2e).
    public var offersRestore: Bool { editor?.refused == true }

    /// `symphonies create <name> --from <this>`: a config entry to change.
    public func copy() async {
        let name = copyName.trimmingCharacters(in: .whitespaces)
        guard let binary, let source = current, !name.isEmpty else { return }
        let outcome = await runner.command(binary: binary, ["symphonies", "create", name, "--from", source.name])
        copyName = ""
        await refresh()
        if let failure = outcome.failure {
            self.failure = failure
        } else {
            image = nil
            selected = name
        }
    }

    /// `symphonies delete <name>`: a config entry, after the person's word.
    public func confirmDelete() async {
        guard let binary, let symphony = pendingDelete else { return }
        pendingDelete = nil
        let outcome = await runner.command(binary: binary, ["symphonies", "delete", symphony.name])
        await refresh()
        if let failure = outcome.failure { self.failure = failure }
    }

    public func dismissFailure() {
        failure = nil
    }
}

extension SymphoniesViewModel: DefinitionEditing {}

/// The Library's MCP servers: each connected to as `mcp list` connects, the
/// presets registered byte-identically, enable and disable, and one tool
/// tried as the loop would call it.
@MainActor
@Observable
public final class MCPViewModel {
    public private(set) var servers: [MCPServer] = []
    public private(set) var presets: [MCPPreset] = []
    /// Where `mcp list` and `mcp presets` stand, each said apart (GUI 4h).
    public private(set) var serversListing: LibraryListing = .notRead
    public private(set) var presetsListing: LibraryListing = .notRead
    public private(set) var reading = false
    public var selected: String?
    public var tool = ""
    public var arguments = "{}"
    /// A tool's answer, as `mcp test` printed it.
    public private(set) var tried: String?
    public private(set) var failure: String?
    /// The server whose removal waits on the person's word (GUI 4k).
    public var pendingRemove: MCPServer?
    public var say: (@MainActor (String) -> Void)?

    private let runner: any CommandRunning
    private var binary: URL?

    public init(runner: any CommandRunning) {
        self.runner = runner
    }

    public func connect(binary: URL?) async {
        use(binary: binary)
        await refresh()
    }

    /// The binary, without reading: listing connects to every server, so
    /// it waits until the place comes into view.
    public func use(binary: URL?) {
        self.binary = binary
        if binary == nil {
            serversListing = .missing
            presetsListing = .missing
        } else if serversListing == .missing {
            serversListing = .notRead
            presetsListing = .notRead
        }
    }

    /// `mcp list` -- it connects to each server, so it takes a moment, and a
    /// wedged server makes it time out -- and `mcp presets`. Each failure is
    /// said where its list is, the rows read before kept.
    public func refresh() async {
        guard let binary else { return }
        reading = true
        let list = await runner.read(binary: binary, ["mcp", "list"])
        if let document = list.document { servers = MCPServer.list(from: document) }
        serversListing = .of(list)
        let shipped = await runner.read(binary: binary, ["mcp", "presets"])
        if let document = shipped.document { presets = MCPPreset.list(from: document) }
        presetsListing = .of(shipped)
        reading = false
    }

    /// What the servers' list says under its rows.
    public var serversText: String? { serversListing.text("MCP servers") }
    /// What the presets say in their place.
    public var presetsText: String? { presetsListing.text("presets") }

    public var current: MCPServer? { selected.flatMap { n in servers.first { $0.name == n } } }

    /// `mcp create <name> --from <preset>`, byte for byte the hand-typed one;
    /// the new server is selected only once the binary made it -- a refusal
    /// keeps the selection and is said (GUI 4h). Whether it was made.
    @discardableResult
    public func register(_ preset: MCPPreset, as name: String) async -> Bool {
        let name = name.trimmingCharacters(in: .whitespaces)
        guard !name.isEmpty, await change(["mcp", "create", name, "--from", preset.name]) else { return false }
        selected = name
        return true
    }

    /// `mcp enable | disable <name>`, read back.
    public func setEnabled(_ server: MCPServer, _ enabled: Bool) async {
        _ = await change(["mcp", enabled ? "enable" : "disable", server.name])
    }

    /// Asks before removing a server (GUI 4k).
    public func askToRemove(_ server: MCPServer) {
        pendingRemove = server
    }

    /// `config delete-mcp-server <name>` once the person said yes -- its
    /// config entry removed, its files left alone, as the CLI says -- read
    /// back by `mcp list`; the selection goes with a server that went.
    public func confirmRemove() async {
        guard let server = pendingRemove else { return }
        pendingRemove = nil
        let removed = await change(["config", "delete-mcp-server", server.name])
        if removed, selected == server.name, !servers.contains(where: { $0.name == server.name }) {
            selected = nil
            tool = ""
            tried = nil
        }
    }

    /// `mcp test <server> <tool> <arguments>`: its answer as printed.
    public func tryTool() async {
        guard let binary, let server = current, !tool.isEmpty else { return }
        switch await runner.command(binary: binary, ["mcp", "test", server.name, tool, arguments]) {
        case .done(let stdout):
            tried = stdout.trimmingCharacters(in: .newlines)
            failure = nil
        case let other:
            tried = nil
            failure = other.failure
        }
    }

    /// Runs a change, reads back, and says how it went; whether it was done.
    private func change(_ arguments: [String]) async -> Bool {
        guard let binary else { return false }
        let outcome = await runner.command(binary: binary, arguments)
        await refresh()
        if let failure = outcome.failure {
            self.failure = failure
            return false
        }
        if case .done(let stdout) = outcome {
            say?(stdout.split(whereSeparator: \.isNewline).first.map(String.init) ?? "Done")
        }
        return true
    }

    public func dismissFailure() {
        failure = nil
    }
}
