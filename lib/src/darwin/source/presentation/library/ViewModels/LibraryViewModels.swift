import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Observation

/// A definition file open in the app's editor: the file the CLI reads, its
/// text as it was and as edited, and what the binary said after the save.
public struct DefinitionEditor: Equatable, Sendable {
    public let title: String
    public let path: String
    public let original: String
    public var text: String
    /// The binary's word on the saved file -- its refusal verbatim, or that
    /// it stands -- nil before a save.
    public var verdict: String?
    public var refused = false

    public var changed: Bool { text != original }
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
/// without the files, and the person's own prompt and schema edited in place.
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
    public var selected: String?
    public var createForm = CreateForm()
    public var creating = false
    public var editor: DefinitionEditor?
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
        if let binary { data = await dataDirectory(runner, binary) }
        await refresh()
    }

    public func refresh() async {
        guard let binary else { return }
        agents = await runner.read(binary: binary, ["agents", "list"]).document.map(AgentEntry.list) ?? agents
    }

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
        editor = DefinitionEditor(title: "\(agent.name) · \(file)", path: path, original: text, text: text)
    }

    /// Writes the file the CLI reads, and reads the agents back. Nothing in
    /// this build checks an agent's files short of running it; that is said.
    public func save() async {
        guard var editor else { return }
        do {
            try editor.text.write(toFile: editor.path, atomically: true, encoding: .utf8)
        } catch {
            editor.verdict = "Not saved: \(error.localizedDescription)"
            editor.refused = true
            self.editor = editor
            return
        }
        await refresh()
        editor.verdict = "Saved. Apogee checks an agent's prompt and schema when it runs -- its first run says what is wrong, if anything."
        editor.refused = false
        self.editor = editor
    }

    public func closeEditor() {
        editor = nil
    }

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

/// The Library's symphonies: the catalog, a play supervised with its stages
/// narrated, the person's own spec files edited in place and checked by the
/// binary, copies and deletes as their verbs.
@MainActor
@Observable
public final class SymphoniesViewModel {
    public private(set) var symphonies: [Symphony] = []
    public var selected: String?
    public var input = ""
    /// A play running now, and the last play's document.
    public private(set) var playing: JobProgress?
    public private(set) var play: SymphonyPlay?
    public var editor: DefinitionEditor?
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
        await refresh()
    }

    public func refresh() async {
        guard let binary else { return }
        symphonies = await runner.read(binary: binary, ["symphonies", "list"]).document.map(Symphony.list) ?? symphonies
    }

    public var current: Symphony? { selected.flatMap { n in symphonies.first { $0.name == n } } }

    public func select(_ symphony: Symphony) {
        guard symphony.name != selected else { return }
        selected = symphony.name
        play = nil
    }

    /// `symphonies play <name> --input <text> --output-format json`,
    /// supervised: the stage lines as it goes, the stages' answers at the end.
    public func playIt() {
        guard let binary, let symphony = current, job == nil else { return }
        let input = input.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !input.isEmpty else { return }
        play = nil
        playing = JobProgress()
        let job = starter.startJob(binary: binary, arguments: ["symphonies", "play", symphony.name, "--input", input, "--output-format", "json"], streaming: false, folder: nil)
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
            if let document = ReadDocument(text: stdout.joined(separator: "\n")), let play = SymphonyPlay(document: document) {
                self.play = play
            } else if case .failed(let message)? = progress?.status {
                failure = message
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
        editor = DefinitionEditor(title: symphony.name, path: path, original: text, text: text)
    }

    /// Writes the spec file, then asks the binary: `symphonies show` refuses
    /// a file it cannot read, in its words, and lists the problems of one it
    /// can.
    public func save() async {
        guard var editor, let binary, let name = selected else { return }
        do {
            try editor.text.write(toFile: editor.path, atomically: true, encoding: .utf8)
        } catch {
            editor.verdict = "Not saved: \(error.localizedDescription)"
            editor.refused = true
            self.editor = editor
            return
        }
        let outcome = await runner.read(binary: binary, ["symphonies", "show", name])
        if let document = outcome.document, let symphony = Symphony(document.value) {
            editor.refused = !symphony.problems.isEmpty
            editor.verdict = symphony.problems.isEmpty ? "Saved, and apogee reads it." : symphony.problems.joined(separator: "\n")
        } else {
            editor.refused = true
            editor.verdict = outcome.failure ?? "apogee could not read it"
        }
        self.editor = editor
        await refresh()
    }

    /// Puts the file back as it was before the edit.
    public func restore() async {
        guard var editor else { return }
        editor.text = editor.original
        self.editor = editor
        await save()
    }

    public func closeEditor() {
        editor = nil
    }

    /// `symphonies create <name> --from <this>`: a config entry to change.
    public func copy() async {
        let name = copyName.trimmingCharacters(in: .whitespaces)
        guard let binary, let source = current, !name.isEmpty else { return }
        let outcome = await runner.command(binary: binary, ["symphonies", "create", name, "--from", source.name])
        copyName = ""
        await refresh()
        if let failure = outcome.failure { self.failure = failure } else { selected = name }
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

/// The Library's MCP servers: each connected to as `mcp list` connects, the
/// presets registered byte-identically, enable and disable, and one tool
/// tried as the loop would call it.
@MainActor
@Observable
public final class MCPViewModel {
    public private(set) var servers: [MCPServer] = []
    public private(set) var presets: [MCPPreset] = []
    public private(set) var reading = false
    public var selected: String?
    public var tool = ""
    public var arguments = "{}"
    /// A tool's answer, as `mcp test` printed it.
    public private(set) var tried: String?
    public private(set) var failure: String?
    public var say: (@MainActor (String) -> Void)?

    private let runner: any CommandRunning
    private var binary: URL?

    public init(runner: any CommandRunning) {
        self.runner = runner
    }

    public func connect(binary: URL?) async {
        self.binary = binary
        await refresh()
    }

    /// The binary, without reading: listing connects to every server, so
    /// it waits until the place comes into view.
    public func use(binary: URL?) {
        self.binary = binary
    }

    /// `mcp list` -- it connects to each server, so it takes a moment -- and
    /// `mcp presets`.
    public func refresh() async {
        guard let binary else { return }
        reading = true
        servers = await runner.read(binary: binary, ["mcp", "list"]).document.map(MCPServer.list) ?? servers
        presets = await runner.read(binary: binary, ["mcp", "presets"]).document.map(MCPPreset.list) ?? presets
        reading = false
    }

    public var current: MCPServer? { selected.flatMap { n in servers.first { $0.name == n } } }

    /// `mcp create <name> --from <preset>`, byte for byte the hand-typed one.
    public func register(_ preset: MCPPreset, as name: String) async {
        let name = name.trimmingCharacters(in: .whitespaces)
        guard !name.isEmpty else { return }
        await change(["mcp", "create", name, "--from", preset.name])
        selected = name
    }

    /// `mcp enable | disable <name>`, read back.
    public func setEnabled(_ server: MCPServer, _ enabled: Bool) async {
        await change(["mcp", enabled ? "enable" : "disable", server.name])
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

    private func change(_ arguments: [String]) async {
        guard let binary else { return }
        let outcome = await runner.command(binary: binary, arguments)
        await refresh()
        if let failure = outcome.failure {
            self.failure = failure
        } else if case .done(let stdout) = outcome {
            say?(stdout.split(whereSeparator: \.isNewline).first.map(String.init) ?? "Done")
        }
    }

    public func dismissFailure() {
        failure = nil
    }
}
