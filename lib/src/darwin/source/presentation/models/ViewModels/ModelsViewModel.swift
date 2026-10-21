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
        case all
        case local
        case cloud
        case downloads
        case roles

        public var id: String { rawValue }

        public var title: String {
            switch self {
            case .all: "All"
            case .local: "On this Mac"
            case .cloud: "Cloud & subscriptions"
            case .downloads: "Downloads"
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

    /// A destructive change waiting on the person's word.
    public struct PendingDelete: Equatable {
        public let row: ModelRow
    }

    public var tab: Tab = .all
    public private(set) var rows: [ModelRow] = []
    /// The stored sets the list folded (a conversion consumed them), and
    /// whether the person asked to see them: `models list --all`.
    public private(set) var folded = 0
    public private(set) var showingAll = false
    public private(set) var listing: Listing = .notRead
    public private(set) var selected: String?
    /// The selected model's `models info`, when read.
    public private(set) var info: ModelInfo?
    public private(set) var infoFailure: String?
    public private(set) var roles: [RoleAssignment] = []
    public private(set) var rolesFailure: String?
    public var pendingDelete: PendingDelete?
    public var renaming: ModelRow?
    /// A command running now, by what it is doing: its buttons wait.
    public private(set) var working: String?
    /// The last change that failed, in the CLI's words.
    public private(set) var failure: String?
    /// Says one line in the window's toast.
    public var say: (@MainActor (String) -> Void)?
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
        showingAll = false
        await refresh()
    }

    /// Reads `models list`, `models status`, and the selected model's details.
    public func refresh() async {
        guard let binary else { return }
        if listing != .read { listing = .reading }
        let outcome = await runner.read(binary: binary, ["models", "list"] + (showingAll ? ["--all"] : []))
        if let document = outcome.document {
            rows = ModelRow.list(from: document)
            folded = ModelRow.folded(in: document)
            listing = .read
        } else {
            listing = .failed(outcome.failure ?? "the list could not be read")
        }
        await readRoles()
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

    /// The rows the tab shows, under the CLI's own groups, in its order.
    public var groups: [ModelGroup] {
        let shown = rows.filter { row in
            switch tab {
            case .local: row.section == "local"
            case .cloud: row.section == "cloud"
            default: true
            }
        }
        return ModelGroup.grouping(shown)
    }

    /// Counts for the tabs.
    public func count(_ tab: Tab) -> Int? {
        switch tab {
        case .all: rows.count
        case .local: rows.filter { $0.section == "local" }.count
        case .cloud: rows.filter { $0.section == "cloud" }.count
        case .downloads, .roles: nil
        }
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

    /// The folded sets listed too, or folded again: `models list --all`,
    /// held while the place is open.
    public func showAll(_ all: Bool) async {
        guard showingAll != all else { return }
        showingAll = all
        await refresh()
    }

    /// The CLI's own line for the fold, its count from the list.
    public var foldedText: String? {
        guard folded > 0, !showingAll else { return nil }
        return folded == 1
            ? "1 snapshot consumed by a conversion is folded"
            : "\(folded) snapshots consumed by conversions are folded"
    }

    public func close() {
        selected = nil
        info = nil
        infoFailure = nil
    }

    // MARK: - The details

    /// The facts the details pane shows: the CLI's record, in its words.
    public var facts: [Field] { info?.fields ?? [] }

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

    public func askToDelete(_ row: ModelRow) {
        pendingDelete = PendingDelete(row: row)
    }

    /// `models delete <name> --yes` -- the person said yes here -- with
    /// `--and-backends` only when they chose it too (the CLI's `--yes` never
    /// implies it); read back.
    public func confirmDelete(andBackends: Bool) async {
        guard let pending = pendingDelete else { return }
        pendingDelete = nil
        var arguments = ["models", "delete", pending.row.name, "--yes"]
        if andBackends { arguments.append("--and-backends") }
        await run(arguments, doing: "Deleting \(pending.row.name)", done: "Deleted \(pending.row.name)")
        if selected == pending.row.name { close() }
    }

    /// `models rename <model> <new name>`, read back.
    public func rename(_ row: ModelRow, to name: String) async {
        let name = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !name.isEmpty, name != row.renameName else { return }
        await run(["models", "rename", row.renameName, name], doing: "Renaming", done: "Renamed to \(name)")
    }

    /// `models repair <name>`: verifies against the record, fixes a damaged
    /// SafeTensors set in place; what it found is its own words.
    public func repair(_ row: ModelRow) async {
        await run(["models", "repair", row.name], doing: "Checking \(row.name)", done: nil)
    }

    /// Points a role at a backend: its `config set-default…` verb, read back
    /// through `models status`.
    public func assign(_ role: RoleAssignment, to backend: String) async {
        guard backend != role.backend || !role.isSet else { return }
        await run(role.verb + [backend], doing: "Setting \(role.role)", done: "\(role.role) is \(backend)")
    }

    private func run(_ arguments: [String], doing: String, done: String?) async {
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

    /// The backends a role can point at: configured ones, never stored weights.
    public var backends: [String] { rows.filter { !$0.isStored }.map(\.backend) }
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
