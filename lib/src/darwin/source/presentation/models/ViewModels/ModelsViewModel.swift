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
    /// The selected backend's adapters and their pins, where it is a type
    /// that takes them (llama.cpp, mlx; the CLI's 53a); `nil` for any other,
    /// or unread.
    public private(set) var adapters: AdaptersView?
    /// Why the selected backend's adapters could not be read, in the CLI's
    /// words.
    public private(set) var adaptersFailure: String?
    /// The adapters being edited: the sheet's.
    public var editingAdapters: AdaptersForm?
    /// Each `mlx` backend's drafter, by its name: as its row states it (the
    /// CLI's M32), else -- a list that states none -- as its `models info`'s
    /// `draft:` line says it (the CLI's 53i, GUI M5), read with the list.
    /// Empty against a binary that states no kind (before 53e), which is
    /// never asked.
    public private(set) var drafts: [String: BackendDraft] = [:]
    /// The pair sheet on a base, open (GUI M5).
    public var pairingDrafter: DrafterPairing?
    /// Says one line in the window's toast.
    public var say: (@MainActor (String) -> Void)?
    /// Checks a model's files as a job on the Jobs tab (2i): the
    /// composition's way to it.
    public var checkFiles: (@MainActor (String) -> Void)?
    /// Starts a new session on a backend: the composition's way to Sessions.
    public var useInSession: (@MainActor (String) -> Void)?
    /// The binary's vocabulary date, as the app read it at launch
    /// (`__machine-schema`, the session library's): from the CLI's M32 a
    /// list states each `mlx` row's drafter, so a row without one has none
    /// paired (GUI M16).
    public var vocabulary: @MainActor () -> String? = { nil }

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

    /// Reads `models list` -- each `mlx` entry's drafter from its row where
    /// the list states drafters (GUI M16), else from its `models info` where
    /// the list states kinds (GUI M5) -- `models status`, and the selected
    /// model's details.
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
        await readDrafts()
        await readRoles()
        await readModelNames()
        if let selected {
            if rows.contains(where: { $0.name == selected }) {
                await readInfo(selected)
                await readAdapters()
            } else {
                close()
            }
        }
    }

    /// Whether `row` is a backend that takes adapters: a llama.cpp or mlx
    /// entry (the CLI's `takes_adapters`).
    public static func takesAdapters(_ row: ModelRow) -> Bool {
        !row.isStored && (row.backendType == "llamacpp" || row.backendType == "mlx")
    }

    /// The selected backend's adapters: `config get backends.<name>.adapters`
    /// (readable since the CLI's 55g) -- and, where it lists any, `check`'s
    /// Adapters rows for their pins (53c), the doctor's document standing
    /// whatever its exit, since it exits 1 when a row fails.
    private func readAdapters() async {
        guard let binary, let row = selectedRow, Self.takesAdapters(row) else {
            adapters = nil
            adaptersFailure = nil
            return
        }
        let name = row.name
        let listed = await runner.command(binary: binary, ["config", "get", "backends.\(row.backend).adapters"])
        guard selected == name else { return }
        guard case .done(let text) = listed else {
            adapters = nil
            adaptersFailure = listed.failure
            return
        }
        adaptersFailure = nil
        let list = BackendAdapter.list(text)
        guard !list.isEmpty else {
            adapters = AdaptersView(backend: row.backend, rows: [], checkFailure: nil)
            return
        }
        let report = await runner.report(binary: binary, ["check"])
        guard selected == name else { return }
        let checks = report.document.map { AdapterCheck.rows(for: list, of: row.backend, in: $0.value) } ?? [:]
        let failure = report.document == nil
            ? "The doctor could not be read: \(report.failure ?? "apogee printed nothing")"
            : checks.isEmpty ? "The doctor names none of them: this apogee's check has no Adapters section" : nil
        adapters = AdaptersView(
            backend: row.backend, rows: list.map { AdaptersView.Row(adapter: $0, check: checks[$0.path]) }, checkFailure: failure)
    }

    /// Each `mlx` entry's drafter. From its row, where the row states one
    /// (the CLI's M32): no read. A row without one has none paired where the
    /// list states drafters (`ModelRow.statesDrafts`: a row carries one, or
    /// the binary is dated M32's or later) -- again no read. Otherwise, a
    /// binary before M32 or one that hasn't said, from each entry's `models
    /// info` as M5 read it, since the list names no pairing there: the
    /// `draft:` line is where the CLI says it. A read that fails leaves that
    /// backend unmarked: nothing is guessed.
    private func readDrafts() async {
        guard let binary else { return }
        let stated = ModelRow.statesDrafts(rows, vocabulary: vocabulary())
        var found: [String: BackendDraft] = [:]
        for row in rows where !row.isStored {
            if let draft = row.draft {
                found[row.backend] = BackendDraft(draft)
                continue
            }
            guard !stated, Self.readsDraft(row) else { continue }
            if let info = await runner.read(binary: binary, ["models", "info", row.backend]).document.map(ModelInfo.init),
                let draft = BackendDraft(info)
            {
                found[row.backend] = draft
            }
        }
        drafts = found
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
            let read = ModelInfo(document: document)
            info = read
            infoFailure = nil
            // The details and the table's mark say the same drafter.
            if let row = rows.first(where: { $0.name == name }), Self.readsDraft(row) {
                drafts[row.backend] = BackendDraft(read)
            }
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

    /// The Backends table's format cell, line by line. An `mlx` entry whose
    /// row states its kind (the CLI's 53e) says it as Stored weights does,
    /// `MLX · vision`, its precision on the line under it. Every other row --
    /// and every row of a binary before 53e, which states no kind -- reads as
    /// it always has: its format and precision on one line.
    public static func formatLines(_ row: ModelRow) -> [String] {
        let today = [row.format, row.quant].compactMap { $0 }.joined(separator: " · ")
        guard !row.isStored, row.backendType == "mlx", let kind = row.mlxKind else { return [today] }
        return ["MLX · " + kind.word] + (row.quant.map { [$0] } ?? [])
    }

    /// Whether `row`'s drafter is read with the list: an `mlx` entry whose row
    /// states its kind. Only an `mlx` entry takes a `draft:` (the CLI's 53i),
    /// and a binary that states no kind (before 53e) has no drafters, so
    /// nothing is read from it that it did not read before.
    static func readsDraft(_ row: ModelRow) -> Bool {
        !row.isStored && row.backendType == "mlx" && row.mlxKind != nil
    }

    /// The mark beside a base with a drafter paired, as its `models info`'s
    /// `draft:` line says it: the drafter named -- its store handle where the
    /// list still names a stored drafter at that path, else the path as the
    /// CLI wrote it -- and whether it drafts there or the CLI's words for why
    /// it cannot. `nil` for a backend with none, and for every backend of a
    /// binary before 53i.
    public func drafterMark(_ row: ModelRow) -> DrafterMark? {
        guard !row.isStored, let draft = drafts[row.backend] else { return nil }
        let stored = rows.first { $0.isStored && $0.mlxKind == .drafter && draft.path.hasSuffix("/" + $0.model) }
        return DrafterMark(drafter: stored?.model ?? draft.path, drafts: draft.drafts, said: draft.said)
    }

    /// Whether **Pair a drafter…** is offered on `row`: an `mlx` entry over a
    /// `vision` model -- mlx-vlm's build, the only base a drafter runs beside
    /// (the CLI's 53i) -- with no drafter paired. Never on mlx-lm's build
    /// (`chat`) or any other kind, and never on a binary that states no kind.
    public func offersDrafterPairing(_ row: ModelRow) -> Bool {
        Self.readsDraft(row) && row.mlxKind == .vision && drafts[row.backend] == nil
    }

    /// Opens the pair sheet on a base: the drafters the binary's own
    /// completion offers it (`__complete config set-backend <base> --draft
    /// ""`, the CLI's 53i) -- its family and width, the base its record
    /// names -- each with its directory and its line, from its `models info`.
    /// The app works no pairing out itself; none offered, the sheet says so.
    public func askToPairDrafter(_ row: ModelRow) async {
        guard let binary, offersDrafterPairing(row), working == nil else { return }
        working = "Asking which drafters pair"
        let completed = await runner.command(binary: binary, ["__complete", "config", "set-backend", row.backend, "--draft", ""])
        guard case .done(let stdout) = completed else {
            working = nil
            failure = completed.failure
            return
        }
        var offers: [DrafterPairing.Offer] = []
        for handle in stdout.split(whereSeparator: \.isNewline).map({ $0.trimmingCharacters(in: .whitespaces) }) where !handle.isEmpty {
            let info = await runner.read(binary: binary, ["models", "info", handle]).document.map(ModelInfo.init)
            offers.append(DrafterPairing.Offer(handle: handle, path: info?.field("model_path"), said: info?.field("drafter")))
        }
        working = nil
        pairingDrafter = DrafterPairing(backend: row.backend, offers: offers)
    }

    /// The person's yes: `config set-backend <base> --draft <drafter path>`,
    /// read back -- the details' Drafter and the table's mark then say it as
    /// the CLI does.
    public func pairDrafter() async {
        guard let form = pairingDrafter, let arguments = form.arguments, let offer = form.offer else { return }
        pairingDrafter = nil
        await run(arguments, doing: "Pairing", done: "\(form.backend) drafts with \(offer.handle)")
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
        adapters = nil
        adaptersFailure = nil
        await readInfo(row.name)
        await readAdapters()
    }

    public func close() {
        selected = nil
        info = nil
        infoFailure = nil
        adapters = nil
        adaptersFailure = nil
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
    /// Stored weights, ready to register again. Read back, a role the CLI
    /// cleared with it said in its words (the CLI's M33).
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

    /// The adapters sheet, over what `row` applies now.
    public func editAdapters(_ row: ModelRow) {
        guard Self.takesAdapters(row) else { return }
        editingAdapters = AdaptersForm(
            backend: row.backend, backendType: row.backendType, adapters: adapters?.rows.map(\.adapter) ?? [])
    }

    /// `config set-backend <name> --adapters …` as the sheet leaves it --
    /// `""` takes them all off -- read back: the list and the doctor's pins
    /// again, the CLI's own line said, a refusal in its words.
    public func applyAdapters() async {
        guard let form = editingAdapters else { return }
        editingAdapters = nil
        await run(form.arguments, doing: "Setting \(form.backend)'s adapters", done: nil)
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
    /// `done` line (a delete's, GUI 4e); any other change's words are kept
    /// where they say a role the CLI moved on the person's behalf -- a
    /// removal clearing the roles that named its entry (the CLI's M33).
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
            if case .done(let stdout) = outcome,
                let said = narrated
                    ? DeleteNarration(title: done ?? "Done", said: stdout) : DeleteNarration(title: done ?? "Done", movingRoles: stdout)
            {
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
    /// Roles shows every entry; only the chat role's picker narrows
    /// (`choices(for:)`, M4).
    public var backends: [String] { rows.filter { !$0.isStored }.map(\.backend) }

    /// What a role's picker offers. Every role but chat: every configured
    /// backend, as above. The chat role -- `config set-default`, what answers
    /// in sessions -- only those a chat runs on (`ModelRow.chats`, the GUI's
    /// M4): never an `mlx` entry over an embedding model, a drafter, a video
    /// or voice model, or a kind this app does not know, whose chat the CLI
    /// refuses, though `config set-default` takes it. The entry the role is
    /// set to now keeps its place whatever it is, so a chat role already
    /// pointed at one is shown as it stands, never hidden.
    public func choices(for role: RoleAssignment) -> [String] {
        guard role.role == "chat" else { return backends }
        let current = role.isSet ? role.backend : nil
        return rows.filter { !$0.isStored && ($0.chats || $0.backend == current) }.map(\.backend)
    }

    /// Whether a new session can be opened on a backend: a chat backend, a
    /// cloud one, or a local GGUF or MLX one -- but not an `mlx` entry over a
    /// kind it does not chat with (an embedding model, a drafter, a video or
    /// voice model, a kind this app does not know), which the CLI refuses a
    /// chat (its 53e) -- the answer every chat picker asks (`ModelRow.chats`,
    /// M4).
    public func opensSessions(_ row: ModelRow) -> Bool {
        guard !row.isStored, row.chats else { return false }
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

    /// The drafter as a `models list` row states it (the CLI's M32): its
    /// path, and the CLI's refusal where it cannot draft, said after its
    /// `cannot draft beside it: ` as the `draft:` line says it. A row says
    /// nothing more of one that drafts, so its words are none.
    public init(_ draft: ModelRow.Draft) {
        path = draft.path
        said = draft.canDraft ? "" : Self.cannot + (draft.refusal ?? "")
        drafter = nil
    }

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

    /// The CLI's words before why a pairing cannot run (`models info`'s
    /// `draft:` line, 53i).
    static let cannot = "cannot draft beside it: "

    /// Why the drafter cannot run beside the backend, in the CLI's words --
    /// its drafter gone, mlx-lm's build of the base, a base of another width,
    /// with the way out it names -- or `nil` where it says nothing of the
    /// kind (it drafts).
    public var refusal: String? {
        said.hasPrefix(Self.cannot) ? String(said.dropFirst(Self.cannot.count)) : nil
    }

    /// Whether the CLI says it drafts there: every line but a refusal.
    public var drafts: Bool { refusal == nil }
}

/// The Backends table's mark on a base with a drafter paired (GUI M5): a
/// small glyph, the drafter's name and the CLI's words in its tooltip --
/// drawn as a warning where the CLI says it can no longer draft.
public struct DrafterMark: Equatable, Sendable {
    /// The drafter: its store handle, or its path where the store no longer
    /// lists it.
    public let drafter: String
    /// Whether it drafts beside the base, as the CLI says.
    public let drafts: Bool
    /// The CLI's words on it (`drafts beside it …` or `cannot draft beside
    /// it: …`).
    public let said: String

    public init(drafter: String, drafts: Bool, said: String) {
        self.drafter = drafter
        self.drafts = drafts
        self.said = said
    }

    /// The tooltip: the drafter, then the CLI's words.
    public var help: String { "Drafter: " + drafter + (said.isEmpty ? "" : " -- " + said) }
}

/// `config set-backend <base> --draft <drafter path>`, as the pair sheet on a
/// base builds it (GUI M5; the CLI's 53i): a drafter chosen among those the
/// binary's completion offers that base, never worked out here.
public struct DrafterPairing: Equatable, Identifiable, Sendable {
    /// A drafter the completion offers, as its `models info` says it.
    public struct Offer: Equatable, Identifiable, Sendable {
        /// Its store handle, as the completion offers it.
        public let handle: String
        /// Its directory (`model_path`), where its `models info` was read.
        public let path: String?
        /// The CLI's line for it (`a drafter (…) for a 2560-wide gemma4
        /// model, its record naming Gemma4-E4B`).
        public let said: String?
        public var id: String { handle }

        public init(handle: String, path: String?, said: String?) {
            self.handle = handle
            self.path = path
            self.said = said
        }

        /// What `--draft` is given: its directory, as 3b passes it -- else
        /// its handle, which the CLI takes too (both recorded taken).
        public var draft: String { path ?? handle }
    }

    /// The base's backend.
    public let backend: String
    /// What the binary offers, in its order.
    public let offers: [Offer]
    /// The drafter picked: the first offered, until the person picks.
    public var chosen: String?
    public var id: String { backend }

    public init(backend: String, offers: [Offer]) {
        self.backend = backend
        self.offers = offers
        chosen = offers.first?.handle
    }

    /// The drafter picked, as offered.
    public var offer: Offer? { offers.first { $0.handle == chosen } }

    /// The command, as typed; `nil` while none is offered.
    public var arguments: [String]? {
        offer.map { ["config", "set-backend", backend, "--draft", $0.draft] }
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
