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
    /// The pipeline run in progress now, by id -- a cycle pass's among them.
    public let activePipeline: String?
    /// The newest pipeline run, as `train pipeline status` lists it.
    public let latestPipeline: PipelineRunRow?
    /// The cycle's headline -- `nil` until a pass (or a halt) has written
    /// its history, when `train cycle status` refuses with exit 1 (the CLI's
    /// 55h): read this first.
    public let cycle: CycleHeadline?

    public init(document: ReadDocument) {
        let v = document.value
        recent = (v["recent_runs"]?.arrayValue ?? []).compactMap(TrainingRun.init)
        running = (v["running"]?.arrayValue ?? []).compactMap(TrainingRun.init)
        runs = v["runs"]?.intValue ?? 0
        pipelines = v["pipelines"]?.intValue ?? 0
        cycleActive = v["cycle_active"]?.boolValue ?? false
        activePipeline = v["active_pipeline"]?.stringValue
        latestPipeline = v["latest_pipeline"].flatMap(PipelineRunRow.init)
        cycle = v["cycle"].flatMap(CycleHeadline.init)
    }
}

/// A pipeline run as `train pipeline status --output-format json` lists it
/// (the CLI's 55h, the admin route's body): the named spec it ran, its
/// status -- `running`, `complete`, `aborted` (a hard gate failed, or a
/// Ctrl-C) or `failed` (something else broke) -- and how many of its stages
/// passed. A cycle pass's pipeline is listed among them. Fields this client
/// does not know are passed over.
public struct PipelineRunRow: Sendable, Equatable, Identifiable {
    public let id: String
    public let specName: String
    public let status: String
    public let stages: Int
    public let passed: Int
    public let startedAt: String
    public let completedAt: String?

    public init?(_ value: JSONValue) {
        guard let id = value["id"]?.stringValue, !id.isEmpty else { return nil }
        self.id = id
        specName = value["spec_name"]?.stringValue ?? ""
        status = value["status"]?.stringValue ?? ""
        stages = value["stages"]?.intValue ?? 0
        passed = value["passed"]?.intValue ?? 0
        startedAt = value["started_at"]?.stringValue ?? ""
        completedAt = value["completed_at"]?.stringValue
    }

    /// Whether `train pipeline resume` carries it on: it stopped short.
    public var isResumable: Bool { PipelineRun.resumes(status) }

    /// The list, newest first as the CLI orders it; `data` is `[]`, never
    /// null, with no run yet.
    public static func list(from document: ReadDocument) -> [PipelineRunRow] {
        (document.entries ?? []).compactMap(PipelineRunRow.init)
    }
}

/// A pipeline run's manifest, whole: `train pipeline status <id>
/// --output-format json` (the CLI's 55h), `{"kind": "pipeline", "pipeline":
/// {…}}`. Each stage's word is the one a run's `stage` events carry.
public struct PipelineRun: Sendable, Equatable, Identifiable {
    /// One stage as the manifest records it.
    public struct Stage: Sendable, Equatable, Identifiable {
        /// From 1, as a run's `stage` events count; the manifest's own
        /// `index` counts from 0.
        public let index: Int
        public let name: String
        /// The stage's own run -- what `train eval` and `train promote` take.
        public let runID: String
        /// `pending`, `training`, `evaluating`, `fusing`, `passed` or
        /// `failed`: the manifest's own word.
        public let status: String
        /// The cumulative gate's score, 0 to 1, once this attempt was gated:
        /// `nil` before, though the manifest keeps a number there (0, or a
        /// resumed stage's last attempt's) -- see `PipelineRun.isGated`.
        public let score: Double?
        public let dataset: String
        public var id: Int { index }
    }

    public let id: String
    public let specName: String
    public let student: String
    public let baseModel: String
    public let status: String
    public let startedAt: String
    public let completedAt: String?
    public let stages: [Stage]

    /// The read's document: a pipeline's, never a plain run's.
    public init?(document: ReadDocument) {
        guard document.value["kind"]?.stringValue == "pipeline", let pipeline = document.value["pipeline"] else { return nil }
        self.init(pipeline)
    }

    /// The manifest itself, the document's `pipeline`.
    public init?(_ value: JSONValue) {
        guard let id = value["pipeline_run_id"]?.stringValue, !id.isEmpty else { return nil }
        self.id = id
        specName = value["spec_name"]?.stringValue ?? ""
        student = value["student"]?.stringValue ?? ""
        baseModel = value["base_model"]?.stringValue ?? ""
        status = value["status"]?.stringValue ?? ""
        startedAt = value["started_at"]?.stringValue ?? ""
        completedAt = value["completed_at"]?.stringValue
        stages = (value["stages"]?.arrayValue ?? []).enumerated().map { position, stage in
            let status = stage["status"]?.stringValue ?? ""
            let gated = Self.isGated(status) && stage["eval_results"] != nil
            return Stage(
                index: (stage["index"]?.intValue ?? position) + 1, name: stage["name"]?.stringValue ?? "",
                runID: stage["run_id"]?.stringValue ?? "", status: status,
                score: gated ? TrainingRun.number(stage["cumulative_score"]) : nil,
                dataset: stage["dataset"]?.stringValue ?? "")
        }
    }

    /// Whether `train pipeline resume` carries it on: it stopped short.
    public var isResumable: Bool { Self.resumes(status) }

    /// The first stage that has not passed: where a resume starts.
    public var resumesAt: Stage? { stages.first { $0.status != "passed" } }

    /// A run `train pipeline resume` continues: `aborted` or `failed`. The
    /// CLI refuses a complete one by name; a `running` one is left alone --
    /// from here it is another process's, or one a crash left (the console
    /// resumes that).
    static func resumes(_ status: String) -> Bool {
        status == "aborted" || status == "failed"
    }

    /// Whether a stage in `state` has been through its cumulative gate in
    /// this attempt: `fusing` (passed, merging into the next stage's base),
    /// `passed` or `failed`. Before that a score the manifest or a stage
    /// event carries is not this attempt's -- a resumed stage keeps its last
    /// one while it trains again.
    public static func isGated(_ state: String) -> Bool {
        state == "fusing" || state == "passed" || state == "failed"
    }
}

/// A named pipeline as the binary states it: `train pipeline list
/// --output-format json` lists them (`{"object":"list","data":[…]}`, `data`
/// `[]` and never null when none is named) and `train pipeline show <name>
/// --output-format json` is one (`{"object":"training.pipeline",…}`) -- the
/// CLI's M30. Each is what a `train pipeline run --pipeline` can name: the
/// student, and the stages in order, each with its dataset, its own eval
/// suite and the suites its cumulative gate holds it to. Fields this client
/// does not know are passed over.
public struct PipelineSpec: Sendable, Equatable, Identifiable {
    /// One stage of the plan, as the spec writes it.
    public struct Stage: Sendable, Equatable, Identifiable {
        /// From 1, in the spec's order -- as a run's `stage` events count.
        public let index: Int
        public let name: String
        /// A dataset's name, or a `.jsonl` path, as the spec names it.
        public let dataset: String
        /// Its own eval suite.
        public let evalSuite: String
        /// The suites its cumulative gate holds it to, at 100%: every earlier
        /// stage's, then its own.
        public let gate: [String]
        /// `""` is lora, the CLI's default.
        public let method: String
        /// `0` leaves each to the trainer's default.
        public let iters: Int
        public let batchSize: Int
        public let numLayers: Int
        public let gradCheckpoint: Bool
        public let maskPrompt: Bool
        /// The share of each earlier stage's dataset mixed into this one's.
        public let rehearsalFraction: Double
        public var id: Int { index }
    }

    /// What a run takes: the `training.pipelines` entry's name.
    public let name: String
    /// The spec file it was read from; `nil` for a `training.pipelines`
    /// entry.
    public let path: String?
    /// The stored snapshot it trains, as the spec names it.
    public let student: String
    public let stages: [Stage]
    public var id: String { name }

    /// One spec, a list's entry or `show`'s document.
    public init?(_ value: JSONValue) {
        guard let name = value["name"]?.stringValue, !name.isEmpty else { return nil }
        self.name = name
        path = value["path"]?.stringValue
        student = value["student"]?.stringValue ?? ""
        stages = (value["stages"]?.arrayValue ?? []).enumerated().map { position, stage in
            Stage(
                index: position + 1, name: stage["name"]?.stringValue ?? "", dataset: stage["dataset"]?.stringValue ?? "",
                evalSuite: stage["eval_suite"]?.stringValue ?? "",
                gate: (stage["gate"]?.arrayValue ?? []).compactMap(\.stringValue),
                method: stage["method"]?.stringValue ?? "", iters: stage["iters"]?.intValue ?? 0,
                batchSize: stage["batch_size"]?.intValue ?? 0, numLayers: stage["num_layers"]?.intValue ?? 0,
                gradCheckpoint: stage["grad_checkpoint"]?.boolValue ?? false,
                maskPrompt: stage["mask_prompt"]?.boolValue ?? false,
                rehearsalFraction: TrainingRun.number(stage["rehearsal_fraction"]) ?? 0)
        }
    }

    /// `show`'s document: one pipeline, never a list or another read's.
    public init?(document: ReadDocument) {
        guard document.object == "training.pipeline" else { return nil }
        self.init(document.value)
    }

    /// `list`'s document, in the binary's order.
    public static func list(from document: ReadDocument) -> [PipelineSpec] {
        (document.entries ?? []).compactMap(PipelineSpec.init)
    }
}

/// The cycle's headline in `train status` (its `cycle`): where the loop
/// stands, without its passes.
public struct CycleHeadline: Sendable, Equatable {
    public let backend: String
    /// The version the dual gate holds candidates to; 0 before one is set.
    public let anchorVersion: Int
    public let anchorScore: Double?
    public let consecutiveFails: Int
    public let totalRuns: Int
    public let halted: Bool
    public let haltReason: String

    /// `nil` for the `null` the CLI writes until the history exists.
    public init?(_ value: JSONValue) {
        guard value.objectValue != nil else { return nil }
        backend = value["backend"]?.stringValue ?? ""
        anchorVersion = value["anchor_version"]?.intValue ?? 0
        anchorScore = TrainingRun.number(value["anchor_score"])
        consecutiveFails = value["consecutive_fails"]?.intValue ?? 0
        totalRuns = value["total_runs"]?.intValue ?? 0
        halted = value["halted"]?.boolValue ?? false
        haltReason = value["halt_reason"]?.stringValue ?? ""
    }
}

/// The cycle's history: `train cycle status --output-format json` (the CLI's
/// 55h, the admin route's body) -- the anchor, the breaker, a halt and its
/// reason, every pass, and whether a pass holds the lock now. There is no
/// document until the first pass (or a halt) writes one: the read exits 1
/// then, so `train status`'s `cycle` is read first.
public struct CycleHistory: Sendable, Equatable {
    /// One pass as the history records it.
    public struct Pass: Sendable, Equatable, Identifiable {
        /// Its place in the history, from 1.
        public let id: Int
        public let runAt: String
        /// The source its data came from (`directory`, `sessions`); empty
        /// for a pass that found none.
        public let source: String
        public let datasetRows: Int
        /// The pipeline run it trained, empty when it trained nothing.
        public let pipelineID: String
        /// `pass`, `fail` or `skipped`, the history's word.
        public let gate: String
        public let gateReason: String
        public let cycleScore: Double?
        public let previousScore: Double?
        public let anchorScore: Double?
        /// The version it promoted; 0 for none.
        public let promotedVersion: Int
        /// What else the pass left -- the breaker tripping on it.
        public let note: String?
    }

    public let backend: String
    public let anchorVersion: Int
    public let anchorScore: Double?
    public let consecutiveFails: Int
    public let totalRuns: Int
    public let halted: Bool
    public let haltReason: String
    /// The consented sessions source's watermark.
    public let sessionsUntil: String
    /// Oldest first, as the history keeps them.
    public let runs: [Pass]
    /// Whether a pass holds the cycle's lock now.
    public let active: Bool

    public init?(document: ReadDocument) {
        let v = document.value
        guard v.objectValue != nil, v["halted"] != nil || v["runs"] != nil else { return nil }
        backend = v["backend"]?.stringValue ?? ""
        anchorVersion = v["anchor_version"]?.intValue ?? 0
        anchorScore = TrainingRun.number(v["anchor_score"])
        consecutiveFails = v["consecutive_fails"]?.intValue ?? 0
        totalRuns = v["total_runs"]?.intValue ?? 0
        halted = v["halted"]?.boolValue ?? false
        haltReason = v["halt_reason"]?.stringValue ?? ""
        sessionsUntil = v["sessions_until"]?.stringValue ?? ""
        active = v["active"]?.boolValue ?? false
        runs = (v["runs"]?.arrayValue ?? []).enumerated().map { position, pass in
            Pass(
                id: position + 1, runAt: pass["run_at"]?.stringValue ?? "", source: pass["source"]?.stringValue ?? "",
                datasetRows: pass["dataset_rows"]?.intValue ?? 0, pipelineID: pass["pipeline_id"]?.stringValue ?? "",
                gate: pass["gate"]?.stringValue ?? "", gateReason: pass["gate_reason"]?.stringValue ?? "",
                cycleScore: TrainingRun.number(pass["cycle_score"]), previousScore: TrainingRun.number(pass["prev_score"]),
                anchorScore: TrainingRun.number(pass["anchor_score"]),
                promotedVersion: pass["promoted_version"]?.intValue ?? 0,
                note: pass["note"]?.stringValue.flatMap { $0.isEmpty ? nil : $0 })
        }
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
