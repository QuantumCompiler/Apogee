import BusinessReads
import DataProtocol
import Foundation
import Observation

/// The Models place: the library of every configured and stored model, one
/// model's details, and the roles.
///
/// SwiftUI-free (ADR 0002). Every change is the command a person would type,
/// asked first where it destroys, and read back after (`models list`,
/// `models status`); nothing is mirrored. Unknowns are said, never guessed.
@MainActor
@Observable
public final class ModelsViewModel {
    public enum Tab: String, CaseIterable, Identifiable, Sendable {
        case backends
        case store
        case jobs
        case roles

        public var id: String { rawValue }

        public var title: String {
            switch self {
            case .backends: "Backends"
            case .store: "Stored weights"
            case .jobs: "Jobs"
            case .roles: "Roles"
            }
        }
    }

    public enum Listing: Equatable {
        case notRead
        case reading
        case read
        case failed(String)
        /// No apogee binary was found.
        case missing
    }

    /// A destructive change waiting on the person's word: the delete, and
    /// the CLI's own plan for it, read before asking (GUI 4e).
    public struct PendingDelete: Equatable {
        public let row: ModelRow
        public let plan: DeletePlan
    }

    /// A rename asked for: the backend alone, or the model its backends share.
    public struct Renaming: Equatable, Identifiable {
        public enum Kind: Equatable, Sendable {
            /// `config rename-backend`: the backend and every reference to it
            /// -- the roles, the suites, the saved chats -- the store left alone.
            case backend
            /// `models rename`: every quantization's backend and the stored
            /// folder they run from.
            case model
        }

        public let row: ModelRow
        public let kind: Kind
        public var id: String { row.id + (kind == .backend ? "#backend" : "#model") }

        /// The name being changed: the backend's, or the model's.
        public var name: String { kind == .backend ? row.backend : row.renameName }

        /// The command, as typed, before its new name.
        public var command: [String] {
            kind == .backend ? ["config", "rename-backend", row.backend] : ["models", "rename", row.renameName]
        }
    }

    public var tab: Tab = .backends
    public private(set) var rows: [ModelRow] = []
    public private(set) var listing: Listing = .notRead
    public private(set) var selected: String?
    /// The selected model's `models info`, when read.
    public private(set) var info: ModelInfo?
    public private(set) var infoFailure: String?
    public private(set) var roles: [RoleAssignment] = []
    public private(set) var rolesFailure: String?
    public var pendingDelete: PendingDelete?
    /// A backend whose entry waits on the person's word to go: `config
    /// delete-backend`, its weights kept under Stored weights.
    public var removing: String?
    public var renaming: Renaming?
    /// The names `models rename` takes, as the binary completes them: its
    /// GGUF families and stored folders. The rule is the CLI's, asked, never
    /// re-derived (M2).
    public private(set) var modelNames: Set<String> = []
    /// A command running now, by what it is doing: its buttons wait.
    public private(set) var working: String?
    /// The last change that failed, in the CLI's words.
    public private(set) var failure: String?
    /// What the last delete said beyond what it removed, until dismissed.
    public private(set) var narration: DeleteNarration?
    /// Says one line in the window's toast.
    public var say: (@MainActor (String) -> Void)?
    /// Checks a model's files as a job on the Jobs tab (2i): the
    /// composition's way to it.
    public var checkFiles: (@MainActor (String) -> Void)?
    /// Starts a new session on a backend: the composition's way to Sessions.
    public var useInSession: (@MainActor (String) -> Void)?

    private let runner: any CommandRunning
    private var binary: URL?

    public init(runner: any CommandRunning) {
        self.runner = runner
    }

    // MARK: - Reading

    /// The binary was found (or not): the place can read.
    public func connect(binary: URL?) async {
        self.binary = binary
        if binary == nil {
            listing = .missing
            return
        }
        await refresh()
    }

    /// Models came into view: read again.
    public func appeared() async {
        guard binary != nil else { return }
        await refresh()
    }

    /// Reads `models list`, `models status`, and the selected model's details.
    public func refresh() async {
        guard let binary else { return }
        if listing != .read { listing = .reading }
        let outcome = await runner.read(binary: binary, ["models", "list"])
        if let document = outcome.document {
            rows = ModelRow.list(from: document)
            listing = .read
        } else {
            listing = .failed(outcome.failure ?? "the list could not be read")
        }
        await readRoles()
        await readModelNames()
        if let selected {
            if rows.contains(where: { $0.name == selected }) {
                await readInfo(selected)
            } else {
                close()
            }
        }
    }

    private func readRoles() async {
        guard let binary else { return }
        let outcome = await runner.read(binary: binary, ["models", "status"])
        if let document = outcome.document {
            roles = RoleAssignment.list(from: document)
            rolesFailure = nil
        } else {
            rolesFailure = outcome.failure
        }
    }

    /// `apogee __complete models rename ""`: the names the verb takes.
    private func readModelNames() async {
        guard let binary else { return }
        guard case .done(let stdout) = await runner.command(binary: binary, ["__complete", "models", "rename", ""]) else {
            modelNames = []
            return
        }
        modelNames = Set(stdout.split(whereSeparator: \.isNewline).map(String.init).filter { !$0.isEmpty })
    }

    private func readInfo(_ name: String) async {
        guard let binary else { return }
        let outcome = await runner.read(binary: binary, ["models", "info", name])
        guard selected == name else { return }
        if let document = outcome.document {
            info = ModelInfo(document: document)
            infoFailure = nil
        } else {
            info = nil
            infoFailure = outcome.failure
        }
    }

    // MARK: - The list

    /// The backends, under the CLI's own groups, in its order: what sessions,
    /// suites and roles can use -- local first, then cloud and subscriptions.
    /// Stored weights are the store's, under Stored weights (2h).
    public var groups: [ModelGroup] {
        ModelGroup.grouping(rows.filter { !$0.isStored })
    }

    /// The Backends tab's count; the store counts its own.
    public func count(_ tab: Tab) -> Int? {
        tab == .backends ? rows.filter { !$0.isStored }.count : nil
    }

    public var emptyText: String? {
        switch listing {
        case .notRead, .reading: return nil
        case .missing: return "Apogee isn't installed, so there are no models to list."
        case .failed(let message): return "The models could not be read: \(message)"
        case .read: break
        }
        return groups.isEmpty ? "Nothing here yet." : nil
    }

    public var selectedRow: ModelRow? {
        selected.flatMap { name in rows.first { $0.name == name } }
    }

    /// Opens a model's details: `models info <name>` -- a backend, or stored
    /// weights' handle.
    public func select(_ row: ModelRow) async {
        selected = row.name
        info = nil
        infoFailure = nil
        await readInfo(row.name)
    }

    public func close() {
        selected = nil
        info = nil
        infoFailure = nil
    }

    // MARK: - The details

    /// The facts the details pane shows: the CLI's record, in its words.
    public var facts: [Field] { info?.fields ?? [] }

    /// The record as the details draw it: its drafter's lines apart, where
    /// it has one, since the drafter has its own place there (`draft`).
    public var recordFacts: [Field] {
        draft == nil ? facts : facts.filter { $0.label != "draft" && $0.label != "drafter" }
    }

    /// The backend's drafter, where its `models info` names one (the CLI's
    /// 53i): an `mlx` backend paired with one. `nil` everywhere else, and on
    /// every backend of a binary before 53i.
    public var draft: BackendDraft? { info.flatMap(BackendDraft.init) }

    /// The typed facts, where the document carries them (the CLI's 52e): the
    /// memory a conversation holds, the window, the lineage. Before 52e, or
    /// for a backend's document, there are none and the record stands alone.
    public var typed: [Field] {
        guard let data = info?.data else { return [] }
        var fields: [Field] = []
        if let format = data.format { fields.append(Field("Format", format)) }
        if let bytes = data.bytes { fields.append(Field("On disk", Self.size(bytes))) }
        if let window = data.window {
            var text = "\(window.tokens.formatted()) tokens"
            if let source = window.source { text += source == "default" ? " (the default)" : " (\(source))" }
            if let trained = window.trained { text += "; trained for \(trained.formatted())" }
            fields.append(Field("Window", text))
        }
        if let cache = data.cache {
            fields.append(Field("Cache", Self.size(cache.bytes) + (cache.type.map { " at \($0)" } ?? "")))
        }
        if let memory = info?.memory {
            fields.append(Field("Memory", "\(Self.size(memory)) at that window: the weights and the cache"))
        } else if data.cache == nil && data.bytes != nil {
            fields.append(Field("Memory", "not known: the files state no cache"))
        }
        if let handle = data.handle { fields.append(Field("Handle", handle)) }
        return fields
    }

    /// Where the set came from, step by step back.
    public var lineage: [ModelInfo.Step] { info?.data?.lineage ?? [] }

    /// A path the record names, for Show in Finder.
    public var path: String? {
        for label in ["path", "model_path", "file", "directory"] {
            if let value = info?.field(label), value.hasPrefix("/") || value.hasPrefix("~") {
                return (value as NSString).expandingTildeInPath
            }
        }
        return nil
    }

    /// Bytes as the design writes them.
    /// A row's status: the CLI's state; for stored weights whose state only
    /// repeats their format, what they are -- a GGUF or MLX model a backend
    /// could point at is not configured, while SafeTensors sets, pipelines and
    /// voice models, which no backend ever runs, are stored.
    public static func status(_ row: ModelRow) -> String {
        guard row.isStored, row.state == row.format else { return row.state }
        return ["gguf", "mlx"].contains(row.state) ? "not configured" : "stored"
    }

    public static func size(_ bytes: Int) -> String {
        ByteCountFormatter.string(fromByteCount: Int64(bytes), countStyle: .file)
    }

    // MARK: - Changing

    /// Asks before `models delete <name>`: the CLI's own plan read first, so
    /// the question names what it names, as Stored weights asks. A plan the
    /// CLI refuses is its failure, and nothing is asked.
    public func askToDelete(_ row: ModelRow) async {
        guard let binary, working == nil else { return }
        working = DeletePlan.reading
        let reading = await DeletePlan.read(row.name, runner: runner, binary: binary)
        working = nil
        switch reading {
        case .plan(let plan):
            pendingDelete = PendingDelete(row: row, plan: plan)
        case .refused(let why):
            failure = why
        }
    }

    /// Whether the delete waiting on the person's word offers to take the
    /// backends that use its files: only where its plan names one.
    public var offersAndBackends: Bool { pendingDelete?.plan.backends.isEmpty == false }

    /// `models delete <name> --yes` -- the person said yes here -- with
    /// `--and-backends` only when they chose it too (the CLI's `--yes` never
    /// implies it), and only where its plan named a backend; read back, its
    /// words kept.
    public func confirmDelete(andBackends: Bool) async {
        guard let pending = pendingDelete else { return }
        let withBackends = andBackends && offersAndBackends
        pendingDelete = nil
        var arguments = ["models", "delete", pending.row.name, "--yes"]
        if withBackends { arguments.append("--and-backends") }
        await run(arguments, doing: "Deleting \(pending.row.name)", done: "Deleted \(pending.row.name)", narrated: true)
        if selected == pending.row.name { close() }
    }

    /// Asks before removing a backend's entry.
    public func askToRemove(_ row: ModelRow) {
        removing = row.backend
    }

    /// `config delete-backend <name>`: the entry goes; its weights stay under
    /// Stored weights, ready to register again. Read back.
    public func confirmRemove() async {
        guard let name = removing else { return }
        removing = nil
        await run(["config", "delete-backend", name], doing: "Removing \(name)", done: "Removed the backend \(name)")
        if selected == name { close() }
    }

    /// `config set-backend <backend> --draft ""`: the backend lets its
    /// drafter go, and answers alone again (the CLI's 53i). Read back.
    public func unpair(_ row: ModelRow) async {
        await run(["config", "set-backend", row.backend, "--draft", ""], doing: "Unpairing", done: "\(row.backend) drafts with nothing now")
    }

    /// Whether the model a backend belongs to can be renamed whole: the
    /// binary completes its name for `models rename`.
    public func renamesAsModel(_ row: ModelRow) -> Bool {
        modelNames.contains(row.renameName)
    }

    public func askToRename(_ row: ModelRow, _ kind: Renaming.Kind) {
        guard kind == .backend || renamesAsModel(row) else { return }
        renaming = Renaming(row: row, kind: kind)
    }

    /// `config rename-backend <backend> <new name>`, or `models rename <model>
    /// <new name>`, read back.
    public func rename(_ renaming: Renaming, to name: String) async {
        let name = name.trimmingCharacters(in: .whitespacesAndNewlines)
        self.renaming = nil
        guard !name.isEmpty, name != renaming.name else { return }
        await run(renaming.command + [name], doing: "Renaming", done: "Renamed to \(name)")
    }

    /// `models repair <name>`: verifies against the record, fixes a damaged
    /// SafeTensors set in place -- a re-hash of up to tens of gigabytes, so a
    /// job on the Jobs tab, with Stop, its result in its own words (2i).
    public func repair(_ row: ModelRow) {
        checkFiles?(row.name)
    }

    /// Points a role at a backend: its `config set-default…` verb, read back
    /// through `models status`.
    public func assign(_ role: RoleAssignment, to backend: String) async {
        guard backend != role.backend || !role.isSet else { return }
        await run(role.verb + [backend], doing: "Setting \(role.role)", done: "\(role.role) is \(backend)")
    }

    /// Runs a change and reads back, its result said. `narrated`, what the
    /// CLI printed beyond what it removed is kept too, never dropped for the
    /// `done` line (a delete's, GUI 4e).
    private func run(_ arguments: [String], doing: String, done: String?, narrated: Bool = false) async {
        guard let binary, working == nil else { return }
        working = doing
        let outcome = await runner.command(binary: binary, arguments)
        working = nil
        await refresh()
        if let failure = outcome.failure {
            self.failure = failure
            say?("That didn't work: \(failure)")
        } else {
            failure = nil
            if narrated, case .done(let stdout) = outcome, let said = DeleteNarration(title: done ?? "Done", said: stdout) {
                narration = said
            }
            if case .done(let stdout) = outcome, done == nil {
                say?(stdout.split(separator: "\n").last.map(String.init) ?? "Done")
            } else if let done {
                say?(done)
            }
        }
    }

    public func dismissFailure() {
        failure = nil
    }

    /// Takes the last delete's words away, once read.
    public func dismissNarration() {
        narration = nil
    }

    // MARK: - Roles

    /// What each role is for, in the design's words.
    public static func about(_ role: String) -> String {
        switch role {
        case "chat": "Answers in sessions and every command that names no model."
        case "embedding": "Turns documents into vectors for retrieval."
        case "extraction": "Pulls structured facts out of text."
        case "vision": "Describes images for a chat model that can't see."
        case "transcription": "Turns recordings into text."
        case "utility": "Chores: titles, compaction, search queries, large tool results."
        default: ""
        }
    }

    /// The backends a role can point at: configured ones, never stored
    /// weights -- every one for every role, as the CLI's `config
    /// set-default…` verbs take any backend, so an `mlx` entry over an
    /// embedding model is offered to the embedding role (the CLI's 53h).
    public var backends: [String] { rows.filter { !$0.isStored }.map(\.backend) }

    /// Whether a new session can be opened on a backend: a chat backend, a
    /// cloud one, or a local GGUF or MLX one -- but not an `mlx` entry over a
    /// kind it does not chat with (an embedding model, a drafter, a video or
    /// voice model, a kind this app does not know), which the CLI refuses a
    /// chat (its 53e).
    public func opensSessions(_ row: ModelRow) -> Bool {
        guard !row.isStored, row.mlxKind?.chats != false else { return false }
        return row.roles.contains("chat") || row.section == "cloud" || row.group == "gguf" || row.group == "mlx"
    }
}

/// A backend's drafter, as its `models info` states it (the CLI's 53i): the
/// `draft:` line -- the drafter's directory, as the config holds it, then what
/// the CLI says of it beside the base, drafting or why it cannot -- and the
/// `drafter:` line, what the drafter is. A backend with no drafter, and every
/// backend of a binary before 53i, has none.
public struct BackendDraft: Equatable, Sendable {
    /// The drafter's directory: what `--draft` was given, written absolute.
    public let path: String
    /// The CLI's words after it: `drafts beside it through mlx-vlm's
    /// speculative path (…)`, or `cannot draft beside it: …`.
    public let said: String
    /// The CLI's line for the drafter itself, where it reads one.
    public let drafter: String?

    public init?(_ info: ModelInfo) {
        guard let line = info.field("draft"), !line.isEmpty else { return nil }
        if let split = line.range(of: " -- ") {
            path = String(line[..<split.lowerBound])
            said = String(line[split.upperBound...])
        } else {
            path = line
            said = ""
        }
        drafter = info.field("drafter")
    }
}

/// A group of the library, as the CLI groups `models list`: by what a local
/// model is stored as, then each provider.
public struct ModelGroup: Equatable, Identifiable {
    public let id: String
    public let title: String
    public let rows: [ModelRow]

    /// The local groups in the CLI's order; any cloud group after, in the
    /// order it first appears.
    static let localOrder = ["safetensors", "gguf", "mlx", "pipeline", "voice", "ollama-cli", "other"]

    static func title(_ group: String) -> String {
        switch group {
        case "safetensors": "SafeTensors"
        case "gguf": "GGUF"
        case "mlx": "MLX"
        case "pipeline": "Pipelines"
        case "voice": "Voices"
        case "ollama-cli": "Ollama"
        case "anthropic": "Anthropic"
        case "openai": "OpenAI"
        case "google": "Google"
        case "claude-cli": "Claude Code"
        case "codex-cli": "Codex"
        case "gemini-cli": "Gemini CLI"
        case "other": "Other"
        default: group.prefix(1).uppercased() + group.dropFirst()
        }
    }

    public static func grouping(_ rows: [ModelRow]) -> [ModelGroup] {
        var order: [String] = []
        var byGroup: [String: [ModelRow]] = [:]
        for row in rows {
            if byGroup[row.group] == nil { order.append(row.group) }
            byGroup[row.group, default: []].append(row)
        }
        let local = localOrder.filter { byGroup[$0] != nil }
        let rest = order.filter { !localOrder.contains($0) }
        return (local + rest).map { ModelGroup(id: $0, title: title($0), rows: byGroup[$0]!) }
    }
}
