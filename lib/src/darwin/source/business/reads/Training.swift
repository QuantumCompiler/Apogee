import DataProtocol
import Foundation

/// One training run, as `train status --output-format json` records it: what
/// was trained on what, how it ended, and -- once gated -- the gate's verdict.
public struct TrainingRun: Sendable, Equatable, Identifiable {
    public let id: String
    public let kind: String
    /// `complete`, `failed`, `running` ..., in the CLI's word.
    public let status: String
    public let method: String
    public let trainer: String
    public let iterations: Int?
    public let finalLoss: Double?
    public let baseModel: String
    public let dataset: String
    public let startedAt: String
    public let finishedAt: String
    /// The gate's verdict and score (`train eval`), nil until gated.
    public let evalPassed: Bool?
    public let evalScore: Double?

    public init?(_ value: JSONValue) {
        guard let id = value["id"]?.stringValue, !id.isEmpty else { return nil }
        self.id = id
        kind = value["kind"]?.stringValue ?? ""
        status = value["status"]?.stringValue ?? ""
        method = value["method"]?.stringValue ?? ""
        trainer = value["trainer"]?.stringValue ?? ""
        iterations = value["iterations"]?.intValue
        finalLoss = Self.number(value["final_loss"])
        baseModel = value["base_model"]?.stringValue ?? ""
        dataset = value["dataset"]?.stringValue ?? ""
        startedAt = value["started_at"]?.stringValue ?? ""
        finishedAt = value["finished_at"]?.stringValue ?? ""
        evalPassed = value["eval_passed"]?.boolValue
        evalScore = Self.number(value["eval_score"])
    }

    static func number(_ value: JSONValue?) -> Double? {
        if case .number(let n)? = value { n } else { nil }
    }
}

/// A backend's promoted versions, as `train versions --output-format json`
/// lists each ledger: which version is active, and every version promoted.
public struct VersionLedger: Sendable, Equatable, Identifiable {
    public struct Version: Sendable, Equatable, Identifiable {
        public let version: Int
        public let runID: String
        /// The GGUF file, an MLX version's directory -- or, for an adapter
        /// version (the CLI's 53b), its adapter: a GGUF LoRA, or an mlx-lm
        /// adapter directory.
        public let path: String
        public let promotedAt: String
        public let evalScore: Double?
        public let evalPassed: Bool?
        public let pruned: Bool
        /// The adapter itself is the version (`train promote --adapter`),
        /// applied over the entry's base rather than fused into it.
        public let isAdapter: Bool
        /// Where its gate scored it, when that was the base it serves on
        /// (53c): `tiny-Q4_K_M.gguf (Q4_K_M)`.
        public let evalScoredOn: String?
        public var id: Int { version }
    }

    public let backend: String
    public let active: Int?
    public let versions: [Version]
    public var id: String { backend }

    public init?(_ value: JSONValue) {
        guard let backend = value["backend_name"]?.stringValue else { return nil }
        self.backend = backend
        active = value["active_version"]?.intValue
        versions = (value["versions"]?.arrayValue ?? []).compactMap { v in
            guard let version = v["version"]?.intValue else { return nil }
            let adapter = v["adapter_path"]?.stringValue
            return Version(
                version: version, runID: v["run_id"]?.stringValue ?? "",
                path: v["gguf_path"]?.stringValue ?? v["mlx_path"]?.stringValue ?? adapter ?? "",
                promotedAt: v["promoted_at"]?.stringValue ?? "",
                evalScore: TrainingRun.number(v["eval_score"]), evalPassed: v["eval_passed"]?.boolValue,
                pruned: v["pruned_at"] != nil, isAdapter: adapter != nil,
                evalScoredOn: v["eval_scored_on"]?.stringValue)
        }
    }

    /// Whether the backend holds adapter versions -- its active one is an
    /// adapter -- or merged ones; `nil` with none active. The CLI never mixes
    /// the two on one backend (53b), and refuses a promote of the other kind
    /// into it, naming the way.
    public var holdsAdapters: Bool? {
        guard let active, let version = versions.first(where: { $0.version == active }) else { return nil }
        return version.isAdapter
    }

    public static func list(from document: ReadDocument) -> [VersionLedger] {
        (document.entries ?? []).compactMap(VersionLedger.init)
    }
}

/// `train status --output-format json`: the runs, what is running, the
/// pipelines and the cycle.
public struct TrainingStatus: Sendable, Equatable {
    public let recent: [TrainingRun]
    public let running: [TrainingRun]
    public let runs: Int
    public let pipelines: Int
    public let cycleActive: Bool

    public init(document: ReadDocument) {
        let v = document.value
        recent = (v["recent_runs"]?.arrayValue ?? []).compactMap(TrainingRun.init)
        running = (v["running"]?.arrayValue ?? []).compactMap(TrainingRun.init)
        runs = v["runs"]?.intValue ?? 0
        pipelines = v["pipelines"]?.intValue ?? 0
        cycleActive = v["cycle_active"]?.boolValue ?? false
    }
}

/// A regime, as `train regime list | show` carries it: its kits in order and
/// its tuning.
public struct TrainingRegime: Sendable, Equatable, Identifiable {
    public let name: String
    public let description: String
    public let kits: [String]
    /// `shipped`, `seeded` or `config`.
    public let source: String
    public let student: String
    public let teacher: String
    public let promoteAs: String
    public var id: String { name }

    public init?(_ value: JSONValue) {
        guard let name = value["name"]?.stringValue else { return nil }
        self.name = name
        description = value["description"]?.stringValue ?? ""
        kits = (value["kits"]?.arrayValue ?? []).compactMap(\.stringValue)
        source = value["source"]?.stringValue ?? ""
        student = value["student"]?.stringValue ?? ""
        teacher = value["teacher"]?.stringValue ?? ""
        promoteAs = value["promote_as"]?.stringValue ?? ""
    }

    public static func list(from document: ReadDocument) -> [TrainingRegime] {
        (document.entries ?? []).compactMap(TrainingRegime.init)
    }
}

/// A training kit (`datasets kits`): a skill, and the eval items it gates on.
public struct TrainingKit: Sendable, Equatable, Identifiable {
    public let name: String
    public let description: String
    public let evalItems: Int
    public var id: String { name }

    public static func list(from document: ReadDocument) -> [TrainingKit] {
        (document.entries ?? []).compactMap { v in
            v["name"]?.stringValue.map { TrainingKit(name: $0, description: v["description"]?.stringValue ?? "", evalItems: v["eval_items"]?.intValue ?? 0) }
        }
    }
}

/// A dataset (`datasets list | info`): its examples and their shape.
public struct TrainingDataset: Sendable, Equatable, Identifiable {
    public let name: String
    public let lines: Int
    public let bytes: Int
    /// `chat`, `completion` ..., as the CLI reads it.
    public let shape: String
    public let path: String
    public var id: String { name }

    public init?(_ value: JSONValue) {
        guard let name = value["name"]?.stringValue else { return nil }
        self.name = name
        lines = value["lines"]?.intValue ?? 0
        bytes = value["bytes"]?.intValue ?? 0
        shape = value["shape"]?.stringValue ?? ""
        path = value["path"]?.stringValue ?? ""
    }

    public static func list(from document: ReadDocument) -> [TrainingDataset] {
        (document.entries ?? []).compactMap(TrainingDataset.init)
    }
}
