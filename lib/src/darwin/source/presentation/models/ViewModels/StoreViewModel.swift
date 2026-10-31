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
    /// An MLX set's kind, as its row states it (the CLI's 53e); `nil` on
    /// every other format, and on every row of a binary before 53e.
    public let kind: MLXKind?
    public var id: String { handle }

    /// The model folder: the handle's first part, what `models rename` and a
    /// whole model's `models delete` take.
    public var model: String { handle.split(separator: "/").first.map(String.init) ?? handle }

    /// The format as the design names it.
    public var formatName: String {
        ["safetensors": "SafeTensors", "gguf": "GGUF", "mlx": "MLX", "pipeline": "Pipeline", "voice": "Voice"][format] ?? format
    }

    /// The format with an MLX set's kind beside it, as the CLI wrote the
    /// word: `MLX · vision`. Where no kind is stated -- a binary before the
    /// CLI's 53e -- the format alone, as before.
    public var formatLabel: String {
        guard format == "mlx", let kind else { return formatName }
        return formatName + " · " + kind.word
    }

    /// What an MLX set is for the app's offers: its stated kind, else a chat
    /// model's (`MLXKind.reading`); `nil` on every other format.
    public var mlxKind: MLXKind? { format == "mlx" ? MLXKind.reading(kind) : nil }

    /// What the design says of a set beside its facts: why it doesn't run
    /// yet, or what registering does.
    public var advice: String? {
        if format == "safetensors" && consumed {
            return "A conversion has used these weights. They stay, so you can convert them again at another precision or to MLX; deleting them keeps what was made."
        }
        if format == "safetensors" {
            return "SafeTensors weights don't run as they are. Convert them to GGUF for llama.cpp, or to MLX for Apple silicon."
        }
        if isRunnable && backend == nil && mlxKind == .embedding {
            return "Stored but not registered, so the embedding role can't name it yet. Registering writes one config entry; nothing is copied or loaded."
        }
        if isRunnable && backend == nil {
            return "Stored but not registered, so no session can use it yet. Registering writes one config entry; nothing is copied or loaded."
        }
        return nil
    }

    /// Whether it runs once a backend points at it: a GGUF, or an MLX set of
    /// a kind an `mlx` backend runs -- chat or vision, or embedding -- as
    /// `config scan --register` decides it (the CLI's 53e and 53h). A
    /// drafter, a video or voice model, or a kind this app does not know, no
    /// backend entry runs.
    public var isRunnable: Bool { format == "gguf" || mlxKind?.registers == true }

    /// The state the table shows, in words: the CLI's own wherever it has
    /// one beyond its format's name.
    public var state: String {
        if consumed { return "used by a conversion" }
        if let backend { return "backend \(backend)" }
        switch cliState {
        case "safetensors": return "weights only · convert to run"
        case "ok", "mlx":
            // What uses a set no backend entry runs, by its kind (the CLI's
            // `config scan` says the same beside each).
            switch mlxKind {
            case .drafter: return "drafts beside its base"
            case .video: return "for video generate"
            case .voice: return "for voice say"
            case .other: return "nothing here runs it"
            default: return isRunnable ? "not registered" : cliState
            }
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
/// scan`, a size from a document, what a delete strands from the delete's own
/// plan (`models delete` without `--yes`), and what it did from its words.
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
        /// An MLX set's kind (the CLI's 53e), `nil` for a GGUF: what the entry
        /// is for -- a chat model's, a vision model's, or one the embedding
        /// role names.
        public let kind: MLXKind?
        /// An embedding model's vector width, as its `models info` states it.
        public let vectorWidth: Int?

        public init(handle: String, type: String, path: String, name: String, projector: String?, kind: MLXKind?, vectorWidth: Int?) {
            self.handle = handle
            self.type = type
            self.path = path
            self.name = name
            self.projector = projector
            self.kind = kind
            self.vectorWidth = vectorWidth
        }

        /// What registering makes, as the sheet says it: an entry sessions
        /// use, one that also reads images, or one only the embedding role
        /// names -- an `mlx` entry over an embedding model embeds and answers
        /// no chat (the CLI's 53h).
        public var about: String {
            let runtime = type == "mlx" ? "MLX" : "llama.cpp"
            switch kind {
            case .embedding?:
                let width = vectorWidth.map { " Its vectors are \($0) wide." } ?? ""
                return "An entry the embedding role can name, embedding with these weights on \(runtime). It answers no chat, so no session uses it." + width
            case .vision?:
                return "A backend sessions, suites and roles can use, running these weights on \(runtime). It reads images: mlx-vlm built it with its towers."
            default:
                return "A backend sessions, suites and roles can use, running these weights on \(runtime)."
            }
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

    /// `config set-backend <base> --draft <drafter>`, as the pair sheet
    /// builds it (the CLI's 53i): a drafter paired with the base of an `mlx`
    /// backend, chosen among those the binary's own completion offers it --
    /// the base's family and width, the one its record names. The app never
    /// works the pairing out itself.
    public struct PairForm: Equatable, Sendable {
        /// A backend the drafter pairs with, and the set it runs.
        public struct Base: Equatable, Sendable, Identifiable {
            public let backend: String
            /// The base's store handle, where Stored weights found it.
            public let handle: String?
            public var id: String { backend }
        }

        /// The drafter's store handle.
        public let drafter: String
        /// What `--draft` takes: the drafter's directory, as its `models
        /// info` names it -- else its handle, which the CLI takes too.
        public let draft: String
        /// The CLI's own line for the drafter (`a drafter (…) for a
        /// 2560-wide gemma4 model, its record naming Gemma4-E4B`).
        public let said: String?
        /// The bases the binary offers, in the list's order.
        public let bases: [Base]
        /// The one picked: the first offered, until the person picks.
        public var base: String?

        public init(drafter: String, draft: String, said: String?, bases: [Base]) {
            self.drafter = drafter
            self.draft = draft
            self.said = said
            self.bases = bases
            base = bases.first?.backend
        }

        /// The command, as typed; `nil` while no base is offered.
        public var arguments: [String]? {
            base.map { ["config", "set-backend", $0, "--draft", draft] }
        }
    }

    /// A change waiting on the person's word.
    public enum Pending: Equatable, Sendable {
        /// `models delete <handle>`: one set, as the CLI's plan says it.
        case deleteSet(StoredSet, plan: DeletePlan)
        /// `models delete <model>`: every set of it, as the CLI's plan says it.
        case deleteModel(String, plan: DeletePlan)
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
    /// The pair sheet, open (the GUI's 3b).
    public var pairing: PairForm?
    /// Whether a delete takes the backends that run from it too: the
    /// person's choice, never implied.
    public var andBackends = false
    public var renaming: String?
    public private(set) var working: String?
    public private(set) var failure: String?
    /// What the last delete said beyond what it removed, until dismissed.
    public private(set) var narration: DeleteNarration?
    public var say: (@MainActor (String) -> Void)?

    private let runner: any CommandRunning
    private var binary: URL?
    /// The registered sets' handles, by their backend.
    private var registered: [String: String] = [:]
    /// The `mlx` backends, in the list's order: where a drafter can pair.
    private var mlxBackends: [String] = []
    /// Each `mlx` backend's drafter, by its directory, as the backend's
    /// `models info` names it (`draft:`, the CLI's 53i).
    private var drafts: [String: String] = [:]

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
                backend: nil, consumed: consumed, note: row.note, kind: row.mlxKind)
        }
        registered = [:]
        drafts = [:]
        mlxBackends = rows.filter { !$0.isStored && $0.backendType == "mlx" }.map(\.backend)
        for row in rows where !row.isStored && (row.backendType == "llamacpp" || row.backendType == "mlx") {
            guard let handle = await handle(of: row.backend) else { continue }
            registered[row.backend] = handle
            found.append(
                StoredSet(
                    handle: handle, format: row.format ?? (row.backendType == "mlx" ? "mlx" : "gguf"), precision: row.quant,
                    cliState: row.state, backend: row.backend, consumed: false, note: row.note, kind: row.mlxKind))
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
            let info = await runner.read(binary: binary, ["models", "info", backend]).document.map(ModelInfo.init)
        else { return nil }
        if let draft = BackendDraft(info) { drafts[backend] = draft.path }
        guard let path = info.field("model_path") else { return nil }
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

    /// Whether a SafeTensors set converts to GGUF, or with `mlx` to MLX, as
    /// its `models info` states it (`data.converts_to`, the CLI's M20). Where
    /// the document does not say -- a binary before M20 -- it is offered, and
    /// the CLI's refusal is the answer (M3). A voice model converts to MLX
    /// only where its document says so (the CLI's 53m, with mlx-audio
    /// installed): before then no voice model converted at all.
    public func converts(_ set: StoredSet, mlx: Bool) -> Bool {
        if set.format == "voice" {
            return infos[set.handle]?.data?.convertsTo?.contains(mlx ? "mlx" : "gguf") == true
        }
        guard set.format == "safetensors" else { return false }
        guard let formats = infos[set.handle]?.data?.convertsTo else { return true }
        return formats.contains(mlx ? "mlx" : "gguf")
    }

    /// What the design says beside a set's facts -- none for SafeTensors that
    /// take no conversion, whose facts say why in the CLI's own words; for a
    /// drafter, where its pairing goes.
    public func advice(_ set: StoredSet) -> String? {
        if set.format == "safetensors", infos[set.handle]?.data?.convertsTo?.isEmpty == true { return nil }
        if set.mlxKind == .drafter { return pairingNote(set) }
        return set.advice
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
            projector: infos[set.handle]?.field("projector"), kind: set.mlxKind,
            vectorWidth: infos[set.handle]?.data?.vectorWidth)
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
        guard converts(set, mlx: mlx) else { return }
        let info = infos[set.handle]
        convertForm = ConvertForm(
            handle: set.handle, mlx: mlx, bytes: bytes(set), conversion: info?.data?.mlxConversion,
            towersSaid: info?.field("towers"))
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

    // MARK: - What a set offers

    /// One thing Stored weights offers on a set.
    public enum Action: Equatable, Sendable {
        case convertToGGUF
        case convertToMLX
        case quantize
        /// `config add-backend`, through the register sheet.
        case register
        /// A new session on the backend that runs it.
        case useInSession(backend: String)
        /// `config set-default-embedding <backend>`: the embedding role
        /// pointed at the entry (the CLI's 53h).
        case useForEmbeddings(backend: String)
        case removeBackend(String)
        /// The Video place, where a clip is made with it.
        case newClip
        /// `config set-default-video <handle>`.
        case makeDefaultVideo
        /// The Voice place, which speaks with it.
        case openVoice
        /// `config set-backend <its base's backend> --draft <it>`, through
        /// the pair sheet (the GUI's 3b, the CLI's 53i).
        case pairWithBase
        /// `config set-backend <backend> --draft ""`: the backend this drafter
        /// drafts for lets it go.
        case unpair(backend: String)
        case checkFiles
        case showInFinder
        case delete
    }

    /// What Stored weights offers on `set`, in the order the details draw
    /// them: its conversions; then what its format and kind run on -- a chat
    /// or vision model registers and opens a session, an embedding model
    /// registers for the embedding role, a drafter pairs with its base, a
    /// video model makes clips and a voice model speaks -- each kind only its
    /// own (the CLI's 53e); then its files. A kind this app does not know
    /// offers its delete alone, and the removal of an entry that points at it.
    public func actions(_ set: StoredSet) -> [Action] {
        var out: [Action] = []
        if converts(set, mlx: false) { out.append(.convertToGGUF) }
        if converts(set, mlx: true) { out.append(.convertToMLX) }
        if isUnquantized(set) { out.append(.quantize) }
        let unregistered = set.backend == nil && !set.consumed
        switch set.format == "gguf" ? MLXKind.chat : set.mlxKind {
        case .chat?, .vision?:
            if unregistered { out.append(.register) }
            if let backend = set.backend { out += [.useInSession(backend: backend), .removeBackend(backend)] }
        case .embedding?:
            if unregistered { out.append(.register) }
            if let backend = set.backend { out += [.useForEmbeddings(backend: backend), .removeBackend(backend)] }
        case .drafter?:
            out.append(.pairWithBase)
            out += pairedWith(set).map { .unpair(backend: $0) }
        case .video?:
            out += [.newClip, .makeDefaultVideo]
        case .voice?:
            out.append(.openVoice)
        case .other?:
            return (set.backend.map { [Action.removeBackend($0)] } ?? []) + [.delete]
        case nil:
            if set.format == "pipeline" { out += [.newClip, .makeDefaultVideo] }
            if set.format == "voice" { out.append(.openVoice) }
        }
        // An entry over a kind no backend runs -- written by hand -- goes
        // the way any entry does.
        if let backend = set.backend, set.mlxKind?.registers == false { out.append(.removeBackend(backend)) }
        return out + [.checkFiles, .showInFinder, .delete]
    }

    /// What a drafter is for, and where it drafts now: beside the bases it
    /// is paired with, or none yet.
    public func pairingNote(_ set: StoredSet) -> String {
        let paired = pairedWith(set)
        guard !paired.isEmpty else {
            return "A drafter runs beside its base, as the draft of the mlx backend over it, and never alone. Pair with its base offers the backends the CLI pairs it with: its base's family and width, the one its record names."
        }
        return "The draft of \(paired.joined(separator: " and ")): every answer there is the base's own, drafted ahead by it through mlx-vlm's speculative path."
    }

    /// The backends a drafter drafts for: each whose `draft:` names its
    /// directory, as the backends' `models info` read them back.
    public func pairedWith(_ set: StoredSet) -> [String] {
        guard set.mlxKind == .drafter, let path = infos[set.handle]?.field("model_path") else { return [] }
        return mlxBackends.filter { drafts[$0] == path }
    }

    /// Opens the pair sheet: the `mlx` backends the binary's completion
    /// offers this drafter (`__complete config set-backend <backend> --draft
    /// ""`, the CLI's 53i), each with the set it runs. None offered, the
    /// sheet says so in the CLI's words for the drafter.
    public func askToPair(_ set: StoredSet) async {
        guard let binary, set.mlxKind == .drafter else { return }
        let path = infos[set.handle]?.field("model_path")
        var bases: [PairForm.Base] = []
        for backend in mlxBackends {
            guard case .done(let offered) = await runner.command(binary: binary, ["__complete", "config", "set-backend", backend, "--draft", ""])
            else { continue }
            let words = offered.split(whereSeparator: \.isNewline).map { $0.trimmingCharacters(in: .whitespaces) }
            if words.contains(set.handle) || path.map(words.contains) == true {
                bases.append(PairForm.Base(backend: backend, handle: registered[backend]))
            }
        }
        pairing = PairForm(drafter: set.handle, draft: path ?? set.handle, said: infos[set.handle]?.field("drafter"), bases: bases)
    }

    /// The person's yes: the pairing, read back.
    public func pair() async {
        guard let form = pairing, let arguments = form.arguments, let base = form.base else { return }
        pairing = nil
        await run(arguments, doing: "Pairing", done: "\(base) drafts with \(form.drafter)")
    }

    /// `config set-backend <backend> --draft ""`: the pairing undone, read back.
    public func unpair(_ backend: String) async {
        await run(["config", "set-backend", backend, "--draft", ""], doing: "Unpairing", done: "\(backend) drafts with nothing now")
    }

    /// `config set-default-embedding <backend>`: the embedding role pointed
    /// at an `mlx` entry over an embedding model, read back (the CLI's 53h).
    public func useForEmbeddings(_ backend: String) async {
        await run(["config", "set-default-embedding", backend], doing: "Setting", done: "Embeddings use \(backend)")
    }

    // MARK: - Removing

    /// Asks before `models delete <handle>`: the CLI's own plan read first,
    /// so the question names what it names. A plan the CLI refuses is its
    /// failure, and nothing is asked.
    public func askToDelete(_ set: StoredSet) async {
        guard let plan = await plan(set.handle) else { return }
        andBackends = false
        pending = .deleteSet(set, plan: plan)
    }

    /// Asks before `models delete <model>`, every set of it, its plan read
    /// first as a set's is.
    public func askToDeleteModel(_ model: String) async {
        guard let plan = await plan(model) else { return }
        andBackends = false
        pending = .deleteModel(model, plan: plan)
    }

    /// `models delete <name>` without `--yes`: the plan, nothing removed.
    private func plan(_ name: String) async -> DeletePlan? {
        guard let binary, working == nil else { return nil }
        working = DeletePlan.reading
        let reading = await DeletePlan.read(name, runner: runner, binary: binary)
        working = nil
        switch reading {
        case .plan(let plan):
            return plan
        case .refused(let why):
            failure = why
            return nil
        }
    }

    /// Whether the delete waiting on the person's word offers to take the
    /// backends that run from what goes: only where its plan names one.
    public var offersAndBackends: Bool {
        switch pending {
        case .deleteSet(_, let plan)?, .deleteModel(_, let plan)?: !plan.backends.isEmpty
        default: false
        }
    }

    public func askToRemoveBackend(_ name: String) {
        pending = .removeBackend(name)
    }

    /// The person's yes: the delete (with `--and-backends` only when chosen,
    /// and only where its plan named a backend), its words kept; the removal;
    /// or the scan's registration -- each read back.
    public func confirm() async {
        guard let pending else { return }
        // The backends go only where the plan named one and the person chose it.
        let withBackends = andBackends && offersAndBackends
        self.pending = nil
        switch pending {
        case .deleteSet(let set, _):
            await run(
                ["models", "delete", set.handle, "--yes"] + (withBackends ? ["--and-backends"] : []), doing: "Deleting",
                done: "Deleted \(set.handle)", narrated: true)
        case .deleteModel(let model, _):
            await run(
                ["models", "delete", model, "--yes"] + (withBackends ? ["--and-backends"] : []), doing: "Deleting",
                done: "Deleted \(model)", narrated: true)
        case .removeBackend(let name):
            await run(["config", "delete-backend", name], doing: "Removing", done: "Removed the backend \(name)")
        case .registerAll:
            await run(
                ["config", "scan", "--register"], doing: "Registering", done: nil, titled: "Registered what the scan found",
                words: Self.registration(in:))
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

    /// What `config scan --register` printed after its report -- the
    /// report the question already showed -- the registration itself; all
    /// of it where no blank line ends a report.
    static func registration(in said: String) -> String {
        guard let end = said.range(of: "\n\n") else { return said }
        return String(said[end.upperBound...])
    }

    /// Runs a change and reads the store back, its result said: the `done`
    /// line, else the CLI's last. `narrated`, what the CLI printed beyond
    /// what it removed is kept too, never dropped for the `done` line (a
    /// delete's, GUI 4e); any other change's `words` are kept, under
    /// `titled` or the `done` line, where they say a role the CLI moved on
    /// the person's behalf -- a registration setting the default, a removal
    /// clearing the roles that named its entry (the CLI's M33, GUI M16).
    private func run(
        _ arguments: [String], doing: String, done: String?, narrated: Bool = false, titled: String? = nil,
        words: (String) -> String = { $0 }
    ) async {
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
            if case .done(let stdout) = outcome,
                let said = narrated
                    ? DeleteNarration(title: done ?? "Done", said: stdout)
                    : DeleteNarration(title: titled ?? done ?? "Done", movingRoles: words(stdout))
            {
                narration = said
            }
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

    /// Takes the last delete's words away, once read.
    public func dismissNarration() {
        narration = nil
    }
}
