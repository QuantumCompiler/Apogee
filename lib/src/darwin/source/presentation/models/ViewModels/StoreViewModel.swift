import BusinessReads
import DataProtocol
import Foundation
import Observation

/// One set in the model store, named by the handle every store verb takes
/// (`<model>/<format>/<id>`), as the CLI's documents state it.
public struct StoredSet: Equatable, Identifiable, Sendable {
    public let handle: String
    /// `safetensors`, `gguf`, `mlx`, `pipeline`, `voice`.
    public let format: String
    /// The precision the list states (`Q4_K_M`, `mixed 4/8-bit (group 64)`).
    public let precision: String?
    /// The CLI's own state for its row: `ok`, `safetensors`, `cannot load: …`.
    public let cliState: String
    /// The backend that runs it, when one does.
    public let backend: String?
    /// Listed only by `models list --all`: a conversion consumed it.
    public let consumed: Bool
    /// Its row's note, as the CLI wrote it.
    public let note: String?
    public var id: String { handle }

    /// The model folder: the handle's first part, what `models rename` and a
    /// whole model's `models delete` take.
    public var model: String { handle.split(separator: "/").first.map(String.init) ?? handle }

    /// The format as the design names it.
    public var formatName: String {
        ["safetensors": "SafeTensors", "gguf": "GGUF", "mlx": "MLX", "pipeline": "Pipeline", "voice": "Voice"][format] ?? format
    }

    /// What the design says of a set beside its facts: why it doesn't run
    /// yet, or what registering does.
    public var advice: String? {
        if format == "safetensors" && consumed {
            return "A conversion has used these weights. They stay, so you can convert them again at another precision or to MLX; deleting them keeps what was made."
        }
        if format == "safetensors" {
            return "SafeTensors weights don't run as they are. Convert them to GGUF for llama.cpp, or to MLX for Apple silicon."
        }
        if isRunnable && backend == nil {
            return "Stored but not registered, so no session can use it yet. Registering writes one config entry; nothing is copied or loaded."
        }
        return nil
    }

    /// Whether it runs once a backend points at it.
    public var isRunnable: Bool { format == "gguf" || format == "mlx" }

    /// The state the table shows, in words: the CLI's own wherever it has
    /// one beyond its format's name.
    public var state: String {
        if consumed { return "used by a conversion" }
        if let backend { return "backend \(backend)" }
        switch cliState {
        case "safetensors": return "weights only · convert to run"
        case "ok", "mlx": return isRunnable ? "not registered" : cliState
        case "pipeline": return "pipeline"
        case "voice": return "voice model"
        default: return cliState
        }
    }
}

/// The Models place's Stored weights: every set by handle, grouped by model,
/// each with its typed facts, what it was made from and what was made from
/// it -- and the store's quick verbs, each the CLI's own and read back.
///
/// SwiftUI-free (ADR 0002). Sets come from `models list` (plain and `--all`,
/// whose difference marks what a conversion consumed); a registered set from
/// its backend's `models info` and then `models info <its path>`, which names
/// its handle. Each set's own `models info` is read after the table, bounded,
/// never holding it up. Nothing is predicted: a name comes from `config
/// scan`, a size from a document.
@MainActor
@Observable
public final class StoreViewModel {
    /// `config add-backend`, as the register sheet builds it.
    public struct RegisterForm: Equatable, Sendable {
        public let handle: String
        /// `llamacpp` or `mlx`, by the set's format.
        public let type: String
        public let path: String
        public var name: String
        /// `--context-size`; empty leaves the model's own.
        public var window = ""
        /// `--cache-type`; `nil` leaves the CLI's default, q8_0.
        public var cache: String?
        /// `--mmproj-path`, carried when the set has a projector.
        public let projector: String?

        public init(handle: String, type: String, path: String, name: String, projector: String?) {
            self.handle = handle
            self.type = type
            self.path = path
            self.name = name
            self.projector = projector
        }

        public var arguments: [String]? {
            let name = name.trimmingCharacters(in: .whitespaces)
            guard !name.isEmpty else { return nil }
            var arguments = ["config", "add-backend", name, "--type", type, "--model-path", path]
            if let projector { arguments += ["--mmproj-path", projector] }
            let window = window.trimmingCharacters(in: .whitespaces)
            if !window.isEmpty { arguments += ["--context-size", window] }
            if let cache, type == "llamacpp" { arguments += ["--cache-type", cache] }
            return arguments
        }

        public static let caches = ["f16", "q8_0", "q4_0"]
    }

    /// A change waiting on the person's word.
    public enum Pending: Equatable, Sendable {
        /// `models delete <handle>`: one set.
        case deleteSet(StoredSet)
        /// `models delete <model>`: every set of it.
        case deleteModel(String)
        /// `config delete-backend <name>`: the entry; the weights stay.
        case removeBackend(String)
        /// `config scan --register`, its own report shown first.
        case registerAll(report: String)
    }

    public private(set) var sets: [StoredSet] = []
    /// How many sets the plain list folded; `--all` shows them.
    public private(set) var folded = 0
    public var showingAll = false {
        didSet { if showingAll != oldValue { Task { await refresh() } } }
    }
    /// Each set's `models info`, as read.
    public private(set) var infos: [String: ModelInfo] = [:]
    public private(set) var selected: String?
    public var register: RegisterForm?
    /// The convert and quantize sheets, open (2i).
    public var convertForm: ConvertForm?
    public var quantizeForm: QuantizeForm?
    /// Starts a store job on the Jobs tab: the composition's way to it.
    public var startConvert: (@MainActor (ConvertForm) -> Void)?
    public var startQuantize: (@MainActor (QuantizeForm) -> Void)?
    public var startCheck: (@MainActor (String) -> Void)?
    public var pending: Pending?
    /// Whether a delete takes the backends that run from it too: the
    /// person's choice, never implied.
    public var andBackends = false
    public var renaming: String?
    public private(set) var working: String?
    public private(set) var failure: String?
    public var say: (@MainActor (String) -> Void)?

    private let runner: any CommandRunning
    private var binary: URL?
    /// The registered sets' handles, by their backend.
    private var registered: [String: String] = [:]

    public init(runner: any CommandRunning) {
        self.runner = runner
    }

    public func connect(binary: URL?) async {
        self.binary = binary
    }

    /// The sets: the lists, the registered sets' handles, then each set's
    /// details.
    public func refresh() async {
        guard let binary else { return }
        guard let all = await runner.read(binary: binary, ["models", "list", "--all"]).document,
            let plain = await runner.read(binary: binary, ["models", "list"]).document
        else { return }
        folded = ModelRow.folded(in: plain)
        let listed = Set(ModelRow.list(from: plain).map(\.name))
        let rows = ModelRow.list(from: all)
        var found: [StoredSet] = rows.filter(\.isStored).compactMap { row in
            let consumed = !listed.contains(row.name)
            if consumed && !showingAll { return nil }
            return StoredSet(
                handle: row.model, format: row.format ?? row.group, precision: row.quant, cliState: row.state,
                backend: nil, consumed: consumed, note: row.note)
        }
        registered = [:]
        for row in rows where !row.isStored && (row.backendType == "llamacpp" || row.backendType == "mlx") {
            guard let handle = await handle(of: row.backend) else { continue }
            registered[row.backend] = handle
            found.append(
                StoredSet(
                    handle: handle, format: row.format ?? (row.backendType == "mlx" ? "mlx" : "gguf"), precision: row.quant,
                    cliState: row.state, backend: row.backend, consumed: false, note: row.note))
        }
        sets = found.sorted { ($0.model, $0.format, $0.handle) < ($1.model, $1.format, $1.handle) }
        if let selected, !sets.contains(where: { $0.handle == selected }) { self.selected = nil }
        for set in sets where infos[set.handle] == nil || set.handle == selected {
            await readInfo(set.handle)
        }
    }

    /// A backend's stored set: its `models info` names its file, and `models
    /// info <that path>` names the set's handle -- `nil` for weights outside
    /// the store.
    private func handle(of backend: String) async -> String? {
        guard let binary,
            let info = await runner.read(binary: binary, ["models", "info", backend]).document.map(ModelInfo.init),
            let path = info.field("model_path")
        else { return nil }
        guard let document = await runner.read(binary: binary, ["models", "info", path]).document else { return nil }
        let set = ModelInfo(document: document)
        if let handle = set.data?.handle {
            infos[handle] = set
            return handle
        }
        return nil
    }

    private func readInfo(_ handle: String) async {
        guard let binary, let document = await runner.read(binary: binary, ["models", "info", handle]).document else { return }
        infos[handle] = ModelInfo(document: document)
    }

    // MARK: - What the table and the pane show

    /// The sets, by model folder, in the store's order.
    public var groups: [(model: String, sets: [StoredSet])] {
        var out: [(model: String, sets: [StoredSet])] = []
        for set in sets {
            if let last = out.indices.last, out[last].model == set.model {
                out[last].sets.append(set)
            } else {
                out.append((set.model, [set]))
            }
        }
        return out
    }

    /// The line above the table: how many models and sets.
    public var summary: String {
        let models = Set(sets.map(\.model)).count
        return "\(models) \(models == 1 ? "model" : "models"), \(sets.count) \(sets.count == 1 ? "set" : "sets"), in the model store"
    }

    /// A group's line: its sets, and their size when every one states it.
    public func summary(of sets: [StoredSet]) -> String {
        let count = "\(sets.count) \(sets.count == 1 ? "set" : "sets")"
        let sizes = sets.compactMap(bytes)
        guard sizes.count == sets.count, !sizes.isEmpty else { return count }
        return count + " · " + StoreViewModel.size(sizes.reduce(0, +))
    }

    /// A row's made-from line: its first lineage step, when it was made here.
    public func madeFromLine(_ set: StoredSet) -> String? {
        guard let step = infos[set.handle]?.data?.lineage.first, let from = step.from,
            step.kind == "converted" || step.kind == "quantized"
        else { return nil }
        return "\(step.kind) from \(from)"
    }

    /// A stated size in the CLI's own units: whole KiB and MiB, GiB to a
    /// tenth, each truncated as the CLI truncates them.
    public nonisolated static func size(_ bytes: Int) -> String {
        let bytes = max(bytes, 0)
        if bytes < 1024 { return "\(bytes) B" }
        if bytes < 1 << 20 { return "\(bytes >> 10) KiB" }
        if bytes < 1 << 30 { return "\(bytes >> 20) MiB" }
        let tenths = bytes * 10 / (1 << 30)
        return "\(tenths / 10).\(tenths % 10) GiB"
    }

    /// What the fold says: the Models table's sentence since M1.
    public var foldedText: String? {
        guard folded > 0, !showingAll else { return nil }
        return folded == 1
            ? "1 snapshot consumed by a conversion is folded"
            : "\(folded) snapshots consumed by conversions are folded"
    }

    /// The bytes a set takes, when a document states them.
    public func bytes(_ set: StoredSet) -> Int? {
        infos[set.handle]?.data?.bytes
    }

    public var current: StoredSet? { selected.flatMap { handle in sets.first { $0.handle == handle } } }
    public var currentInfo: ModelInfo? { selected.flatMap { infos[$0] } }

    public func select(_ handle: String) async {
        selected = handle
        await readInfo(handle)
    }

    public func close() {
        selected = nil
    }

    /// The set a backend runs, for Show its weights.
    public func handle(forBackend backend: String) -> String? { registered[backend] }

    /// What it was made from: the lineage's steps, each `from` a link when it
    /// is a set in the store.
    public var madeFrom: [ModelInfo.Step] { currentInfo?.data?.lineage ?? [] }

    /// What was made from it, as the CLI's `made from it` lines name them.
    public var madeFromIt: [String] {
        guard let text = currentInfo?.field("made from it"), !text.hasPrefix("nothing") else { return [] }
        return text.split(whereSeparator: \.isNewline).map { line in
            line.trimmingCharacters(in: .whitespaces).components(separatedBy: " (").first ?? String(line)
        }
    }

    /// Whether `handle` is a set the store lists -- a link, not just words.
    public func isListed(_ handle: String) -> Bool { sets.contains { $0.handle == handle } }

    /// The set's folder or file, as its record says it.
    public var path: String? { currentInfo?.field("model_path") ?? currentInfo?.field("path") }

    // MARK: - Register

    /// Opens the register sheet: the name `config scan` lists it under, by
    /// the CLI's own naming; the path and any projector from `models info`.
    public func askToRegister(_ set: StoredSet) async {
        guard let binary, set.isRunnable, let path = infos[set.handle]?.field("model_path") else { return }
        var name = ""
        if case .done(let report) = await runner.command(binary: binary, ["config", "scan"]) {
            name = Self.scanName(for: path, in: report) ?? ""
        }
        register = RegisterForm(
            handle: set.handle, type: set.format == "mlx" ? "mlx" : "llamacpp", path: path, name: name,
            projector: infos[set.handle]?.field("projector"))
    }

    /// The name `config scan` lists a stored model's path under: each entry is
    /// its name and state, then its path.
    static func scanName(for path: String, in report: String) -> String? {
        let lines = report.split(whereSeparator: \.isNewline).map(String.init)
        for (index, line) in lines.enumerated() where index > 0 && line.trimmingCharacters(in: .whitespaces) == path {
            return lines[index - 1].split(separator: " ").first.map(String.init)
        }
        return nil
    }

    public func registerIt() async {
        guard let arguments = register?.arguments else { return }
        register = nil
        await run(arguments, doing: "Registering", done: "Registered \(arguments[2])")
    }

    /// Asks before `config scan --register`, showing the scan's own report.
    public func askToRegisterAll() async {
        guard let binary, case .done(let report) = await runner.command(binary: binary, ["config", "scan"]) else { return }
        pending = .registerAll(report: report)
    }

    /// How many sets the store holds that no backend points at.
    public var unregistered: Int {
        sets.filter { $0.isRunnable && $0.backend == nil && !$0.consumed && $0.cliState != "safetensors" }.count
    }

    // MARK: - The long runs (2i)

    /// Whether a GGUF is unquantized -- F16, BF16 or F32 -- by the precision
    /// its row states, else by the level its file is named with (the CLI
    /// names what it makes `<base>-<level>.gguf`).
    public func isUnquantized(_ set: StoredSet) -> Bool {
        guard set.format == "gguf" else { return false }
        let levels = ["F16", "BF16", "F32"]
        if let precision = set.precision { return levels.contains(precision.uppercased()) }
        guard let path = infos[set.handle]?.field("model_path") else { return false }
        let stem = URL(fileURLWithPath: path).deletingPathExtension().lastPathComponent.uppercased()
        return levels.contains { stem.hasSuffix("-" + $0) }
    }

    /// Opens the convert sheet: a GGUF for llama.cpp, or an MLX model.
    public func askToConvert(_ set: StoredSet, mlx: Bool) {
        guard set.format == "safetensors" else { return }
        convertForm = ConvertForm(handle: set.handle, mlx: mlx, bytes: bytes(set))
    }

    public func convert() {
        guard let form = convertForm else { return }
        convertForm = nil
        startConvert?(form)
    }

    /// Opens the quantize sheet, its name the CLI's `<base>-<level>`.
    public func askToQuantize(_ set: StoredSet) {
        guard isUnquantized(set) else { return }
        quantizeForm = QuantizeForm(handle: set.handle, base: modelBaseName(set.model), bytes: bytes(set))
    }

    public func quantize() {
        guard let form = quantizeForm else { return }
        quantizeForm = nil
        startQuantize?(form)
    }

    /// Check the files: `models repair <handle>`, as a job.
    public func check(_ set: StoredSet) {
        startCheck?(set.handle)
    }

    // MARK: - Removing

    public func askToDelete(_ set: StoredSet) {
        andBackends = false
        pending = .deleteSet(set)
    }

    public func askToDeleteModel(_ model: String) {
        andBackends = false
        pending = .deleteModel(model)
    }

    public func askToRemoveBackend(_ name: String) {
        pending = .removeBackend(name)
    }

    /// The person's yes: the delete (with `--and-backends` only when chosen),
    /// the removal, or the scan's registration -- each read back.
    public func confirm() async {
        guard let pending else { return }
        self.pending = nil
        switch pending {
        case .deleteSet(let set):
            await run(["models", "delete", set.handle, "--yes"] + (andBackends ? ["--and-backends"] : []), doing: "Deleting", done: "Deleted \(set.handle)")
        case .deleteModel(let model):
            await run(["models", "delete", model, "--yes"] + (andBackends ? ["--and-backends"] : []), doing: "Deleting", done: "Deleted \(model)")
        case .removeBackend(let name):
            await run(["config", "delete-backend", name], doing: "Removing", done: "Removed the backend \(name)")
        case .registerAll:
            await run(["config", "scan", "--register"], doing: "Registering", done: nil)
        }
    }

    public func dismissPending() {
        pending = nil
    }

    /// `models rename <model> <new>`: its backends follow, as the CLI does it.
    public func rename(_ model: String, to name: String) async {
        let name = name.trimmingCharacters(in: .whitespaces)
        renaming = nil
        guard !name.isEmpty, name != model else { return }
        await run(["models", "rename", model, name], doing: "Renaming", done: "Renamed to \(name)")
    }

    /// `config set-default-video <handle>`: the pipeline `video generate` runs.
    public func makeDefaultVideo(_ set: StoredSet) async {
        await run(["config", "set-default-video", set.handle], doing: "Setting", done: "Video generates with \(set.handle)")
    }

    private func run(_ arguments: [String], doing: String, done: String?) async {
        guard let binary, working == nil else { return }
        working = doing
        let outcome = await runner.command(binary: binary, arguments)
        working = nil
        infos = [:]
        await refresh()
        if let failure = outcome.failure {
            self.failure = failure
        } else {
            failure = nil
            if let done {
                say?(done)
            } else if case .done(let stdout) = outcome {
                say?(stdout.split(whereSeparator: \.isNewline).last.map(String.init) ?? "Done")
            }
        }
    }

    public func dismissFailure() {
        failure = nil
    }
}
