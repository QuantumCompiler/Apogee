import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Observation

/// The Training place: runs and their gates and promotions -- merged, or the
/// adapter alone (the CLI's 53b, gated on a quantized base where it will
/// serve, 53c) -- the named pipelines, run, resumed and watched stage by
/// stage, and the unattended cycle, its state, one pass, a halt and a resume
/// (the CLI's 55h) -- the regimes, and the datasets with their kits, pulled
/// from Hugging Face or made here -- each view a document re-read after, each
/// act the person's command.
///
/// SwiftUI-free (ADR 0002). A long act -- a run, a gate, a promotion, a
/// pipeline, a cycle pass, a regime, a pull, a synthesis, a prepare -- is
/// supervised on the jobs core, one at a time. `train run` takes no machine
/// stream, so its busy line is the CLI's own words and nothing is decided
/// from them; a pipeline and a cycle pass stream their stages, iterations and
/// narration (55h), drawn as they come and read back from their documents
/// after. No training starts without the person's word, and nothing here
/// schedules the cycle: that stays the person's own scheduler's.
///
/// Since the CLI's M30 the binary states what 4j and 4i worked around, and
/// its word decides: the named pipelines and their stage plans are `train
/// pipeline list | show`'s; a halt taken while a pass runs is kept, the pass
/// saying so in a notice; and `datasets pull` streams as a job, its document
/// naming the folder it landed in. An older binary keeps 4j's and 4i's ways,
/// told apart as honestly as each allows: a refused `train pipeline list`
/// (the read itself), and a vocabulary dated before `keepsHaltSince` or
/// `streamsPullSince` (the halt and the pull, which no read can ask before
/// they run).
@MainActor
@Observable
public final class TrainingViewModel {
    public enum Tab: String, CaseIterable, Identifiable, Sendable {
        case runs
        case pipelines
        case cycle
        case regimes
        case datasets
        public var id: String { rawValue }
        public var title: String { rawValue.capitalized }
    }

    /// `train run <student> --dataset <d>`, as a person types it.
    public struct RunForm: Equatable, Sendable {
        public var student = ""
        public var dataset = ""
        /// `lora` (the CLI's default) or `qlora`; nil leaves it out.
        public var method: String?
        public var iters = ""
        public var batchSize = ""
        public var numLayers = ""
        public var gradCheckpoint = false
        public var maskPrompt = false

        public init() {}

        public var arguments: [String]? {
            guard !student.isEmpty, !dataset.isEmpty else { return nil }
            var arguments = ["train", "run", student, "--dataset", dataset]
            if let method { arguments += ["--method", method] }
            for (flag, value) in [("--iters", iters), ("--batch-size", batchSize), ("--num-layers", numLayers)] {
                let value = value.trimmingCharacters(in: .whitespaces)
                if !value.isEmpty { arguments += [flag, value] }
            }
            if gradCheckpoint { arguments.append("--grad-checkpoint") }
            if maskPrompt { arguments.append("--mask-prompt") }
            return arguments
        }
    }

    /// `train promote <run> --as <backend>`, the gate honoured unless the
    /// person forces it: merged -- fused, converted, registered -- or, with
    /// `--adapter` (the CLI's 53b), the run's adapter alone, registered on an
    /// existing entry's adapters over the base it runs. Each mode carries only
    /// the flags the CLI takes in it: a merge's `--target`, `--quantize` and
    /// `--keep-fused`, and the adapter gate's `--suite` and `--judge` (53c,
    /// which score the candidate where an entry's quantized base will serve
    /// it) -- the CLI refuses each in the other mode.
    public struct PromoteForm: Equatable, Sendable {
        public var run = ""
        public var backend = ""
        /// `gguf` or `mlx`; nil lets the CLI choose by what `--as` runs.
        public var target: String?
        /// A GGUF level; nil keeps F16.
        public var quantize: String?
        public var force = false
        /// `--adapter`: no fuse -- the adapter itself is the version.
        public var adapter = false
        /// `--suite`: the suite an adapter's gate scores it on, over an
        /// entry's quantized base; empty leaves it to the CLI -- the run's
        /// recorded eval's, else `training.eval_suite_path`.
        public var suite = ""
        /// `--judge`: the backend that judges items without `expected` in
        /// that gate; nil leaves it to the CLI -- the recorded eval's, else
        /// `training.judge_backend`.
        public var judge: String?
        /// `--trainer`: the run's recorded trainer overridden; nil keeps it.
        public var trainer: String?
        /// `--keep-fused`: a merge keeps its fused SafeTensors checkpoint
        /// under the run.
        public var keepFused = false

        public init() {}

        public var arguments: [String]? {
            let backend = backend.trimmingCharacters(in: .whitespaces)
            guard !run.isEmpty, !backend.isEmpty else { return nil }
            var arguments = ["train", "promote", run, "--as", backend]
            if adapter {
                arguments.append("--adapter")
                let suite = suite.trimmingCharacters(in: .whitespaces)
                if !suite.isEmpty { arguments += ["--suite", suite] }
                if let judge { arguments += ["--judge", judge] }
            } else {
                if let target { arguments += ["--target", target] }
                if let quantize { arguments += ["--quantize", quantize] }
                if keepFused { arguments.append("--keep-fused") }
            }
            if let trainer { arguments += ["--trainer", trainer] }
            if force { arguments.append("--force") }
            return arguments
        }

        /// The form the CLI's refusal points at: the other mode where the
        /// refusal names it as the way -- `promote into it with --adapter`, or
        /// `without --adapter` -- else this one, to change as the words say.
        public func door(after refusal: String) -> PromoteForm {
            var form = self
            if !adapter && refusal.contains("with --adapter") {
                form.adapter = true
            } else if adapter && refusal.contains("without --adapter") {
                form.adapter = false
            }
            return form
        }

        public static let levels = ["Q2_K", "Q3_K_M", "Q4_K_M", "Q5_K_M", "Q6_K", "Q8_0", "F16"]
        /// `--trainer`'s choices, as the CLI lists them.
        public static let trainers = ["auto", "mlx", "peft", "mock"]
    }

    /// A promotion the CLI refused or failed: the form as it was sent, and
    /// the CLI's words, which the sheet opens again with.
    public struct RefusedPromotion: Equatable, Sendable {
        public let form: PromoteForm
        public let refusal: String
    }

    /// `datasets pull owner/name[@revision][:file]` finished: where the CLI
    /// put the files and what it said of them -- `<n> file(s), <size>` --
    /// for the `datasets prepare` that makes them trainer-ready, which the
    /// tab offers.
    public struct PulledDataset: Equatable, Sendable {
        /// The directory `datasets prepare` takes, as the CLI named it.
        public let path: String
        /// `2 file(s), 3 MiB`.
        public let summary: String

        /// From what an older pull printed as text (4i): its `Convert it
        /// with:  apogee datasets prepare <path>` line, and the count line
        /// before it. `nil` when it names no prepare.
        public init?(said lines: [String]) {
            let door = "apogee datasets prepare "
            guard let line = lines.last(where: { $0.contains(door) }), let range = line.range(of: door) else { return nil }
            path = String(line[range.upperBound...]).trimmingCharacters(in: .whitespaces)
            summary = Self.countLine(lines) ?? ""
        }

        /// From a streamed pull's closing document (the CLI's M30), `{object:
        /// "dataset.pull", ref, path, files, bytes}`: the folder is the
        /// document's `path`, whatever its lines said; the summary its own
        /// count line where it said one, else the document's counts. `nil`
        /// for any other document.
        public init?(document: JSONValue, said lines: [String]) {
            guard document["object"]?.stringValue == "dataset.pull", let path = document["path"]?.stringValue, !path.isEmpty
            else { return nil }
            self.path = path
            let files = document["files"]?.intValue ?? 0
            let size = ByteCountFormatter.string(fromByteCount: Int64(document["bytes"]?.intValue ?? 0), countStyle: .file)
            summary = Self.countLine(lines) ?? "\(files) file\(files == 1 ? "" : "s"), \(size)"
        }

        /// The pull's own count line, `2 file(s), 3 MiB` -- never its opening
        /// `downloading …` line.
        private static func countLine(_ lines: [String]) -> String? {
            lines.last { $0.contains(" file(s), ") && !$0.hasPrefix("downloading ") }
        }
    }

    /// `datasets synth <name> --teacher <b> --kit <k>`.
    public struct SynthForm: Equatable, Sendable {
        public var name = ""
        public var teacher = ""
        public var kit = ""
        public var count = ""
        public var topic = ""

        public init() {}

        public var arguments: [String]? {
            let name = name.trimmingCharacters(in: .whitespaces)
            guard !name.isEmpty, !teacher.isEmpty, !kit.isEmpty else { return nil }
            var arguments = ["datasets", "synth", name, "--teacher", teacher, "--kit", kit]
            let count = count.trimmingCharacters(in: .whitespaces)
            if !count.isEmpty { arguments += ["--count", count] }
            let topic = topic.trimmingCharacters(in: .whitespaces)
            if !topic.isEmpty { arguments += ["--topic", topic] }
            return arguments
        }
    }

    /// An act waiting on the person's word: a destructive one, or one that
    /// trains -- as the TUI asks before its `p` and `c` -- or changes what
    /// the person's scheduler does.
    public enum Pending: Equatable, Sendable {
        case rollback(backend: String)
        case deleteDataset(String)
        /// `train pipeline run --pipeline <name>`: each stage a training run,
        /// held to the cumulative gate.
        case runPipeline(String)
        /// `train pipeline resume <id>`: from the first stage that has not
        /// passed.
        case resumePipeline(String)
        /// `train cycle run`: one pass, now.
        case runCycle
        /// `train cycle halt`: every pass refuses until a resume.
        case haltCycle
        /// `train cycle resume`: the halt and the failure count cleared.
        case resumeCycle

        /// The command as a person types it -- `train rollback <b>`,
        /// `datasets delete <d> --yes` (the person said yes here), a
        /// pipeline's or the cycle's verb. A run is started with its stream
        /// asked for besides.
        public var arguments: [String] {
            switch self {
            case .rollback(let backend): ["train", "rollback", backend]
            case .deleteDataset(let name): ["datasets", "delete", name, "--yes"]
            case .runPipeline(let name): ["train", "pipeline", "run", "--pipeline", name]
            case .resumePipeline(let id): ["train", "pipeline", "resume", id]
            case .runCycle: ["train", "cycle", "run"]
            case .haltCycle: ["train", "cycle", "halt"]
            case .resumeCycle: ["train", "cycle", "resume"]
            }
        }

        /// Whether yes takes something away: a version pointer, a dataset.
        public var isDestructive: Bool {
            switch self {
            case .rollback, .deleteDataset: true
            case .runPipeline, .resumePipeline, .runCycle, .haltCycle, .resumeCycle: false
            }
        }
    }

    public enum Sheet: Equatable, Sendable {
        case run
        case promote
        case synth
        case create
        case pull
    }

    /// What the act running now shows live, besides its line: a pipeline's
    /// stages, or a cycle pass -- its narration and its pipeline's stages.
    public enum Live: Equatable, Sendable {
        case pipeline
        case cycle
    }

    /// A pipeline stage as the page draws it -- from the run going now, live,
    /// or from its manifest as last read.
    public struct StageRow: Equatable, Sendable, Identifiable {
        /// From 1.
        public let index: Int
        public let name: String
        /// The manifest's word: `pending`, `training`, `evaluating`,
        /// `fusing`, `passed` or `failed`.
        public let state: String
        /// The stage's own run, once it has one.
        public let runID: String?
        /// The cumulative gate's score -- only once this attempt is past its
        /// gate (`PipelineRun.isGated`).
        public let score: Double?
        /// The trainer's latest iteration, while this stage trains.
        public let iteration: JobProgress.Iteration?
        public var id: Int { index }
    }

    /// Where one of the place's reads stands: not read yet, read, no apogee,
    /// or a failed read -- said where what it reads is drawn, in the CLI's
    /// words, as Records and Tasks say theirs (GUI 4h). A failed refresh keeps
    /// the rows read before.
    public enum Listing: Equatable, Sendable {
        case notRead
        case read
        case missing
        case failed(String)

        static func of(_ outcome: ReadOutcome) -> Listing {
            outcome.document != nil ? .read : .failed(outcome.failure ?? "apogee printed nothing to read")
        }
    }

    public var tab: Tab = .runs
    public private(set) var status: TrainingStatus?
    public private(set) var ledgers: [VersionLedger] = []
    public private(set) var regimes: [TrainingRegime] = []
    public private(set) var kits: [TrainingKit] = []
    public private(set) var datasets: [TrainingDataset] = []
    /// The stored SafeTensors sets a run can train, by handle.
    public private(set) var students: [String] = []
    /// The configured backends a teacher or a promotion's judge can name:
    /// those a chat runs on, as both roles chat -- never an `mlx` entry over
    /// an embedding model, a drafter, a video or voice model, or a kind this
    /// app does not know (`ModelRow.chatBackends`, the GUI's M4).
    public private(set) var backends: [String] = []
    /// The configured entries an adapter can go on: llama.cpp and mlx ones
    /// (the CLI's 53a), the only types that take `adapters` -- and of those,
    /// the ones a chat runs on (M4).
    public private(set) var adapterBackends: [String] = []
    public var selectedRun: String?
    public var selectedRegime: String?
    public var selectedDataset: String?
    public var sheet: Sheet?
    public var runForm = RunForm()
    public var promoteForm = PromoteForm()
    public var synthForm = SynthForm()
    public var createName = ""
    /// `template`, `sessions` or `empty`.
    public var createFrom = "template"
    /// `datasets pull`'s reference: `owner/name[@revision][:file]`.
    public var pullRef = ""
    /// The last pull that finished, until dismissed: where its files are, for
    /// Prepare.
    public private(set) var pulled: PulledDataset?
    /// The promotion the CLI last refused, until the person changes it or
    /// lets it go: what the alert's Change the promotion… reopens.
    public private(set) var refusedPromotion: RefusedPromotion?
    /// The CLI's refusal the promote sheet was opened again with, said above
    /// its fields.
    public private(set) var promoteRefusal: String?
    public var pending: Pending?
    /// The named pipelines' names -- `training.pipelines` entries -- as
    /// `train pipeline list` lists them (the CLI's M30); on an older binary,
    /// which refuses that read, as it completes `train pipeline run
    /// --pipeline` (4j).
    public private(set) var pipelineSpecs: [String] = []
    /// The named pipelines as `train pipeline list` states them: each one's
    /// student and stages. Empty on an older binary, whose names are
    /// completion's alone.
    public private(set) var specs: [PipelineSpec] = []
    /// The named pipeline whose page is open -- its stage plan -- in place
    /// of a run's.
    public private(set) var openedSpec: String?
    /// The open spec as `train pipeline show <name>` last stated it.
    private var shownSpec: PipelineSpec?
    /// Why the open spec could not be shown, in the CLI's words.
    public private(set) var specFailure: String?
    /// Every pipeline run, newest first, as `train pipeline status` lists
    /// them -- a cycle pass's among them.
    public private(set) var pipelineRuns: [PipelineRunRow] = []
    /// The pipeline run whose page is open; `nil` shows the one running here.
    public private(set) var selectedPipelineRun: String?
    /// The open run's manifest, as `train pipeline status <id>` last read it.
    public private(set) var pipelineRun: PipelineRun?
    /// Why the open run's manifest could not be read, in the CLI's words.
    public private(set) var pipelineRunFailure: String?
    /// The cycle's history, as `train cycle status` last read it: `nil` while
    /// `train status` says there is none yet -- then the read, which exits 1,
    /// is never asked.
    public private(set) var cycle: CycleHistory?
    /// Each read's standing: `train status` (the runs), `train versions`,
    /// `train regime list`, `datasets kits`, `datasets list`, and `models list
    /// --all` (the students and the backends the sheets offer).
    public private(set) var runsListing: Listing = .notRead
    public private(set) var versionsListing: Listing = .notRead
    public private(set) var regimesListing: Listing = .notRead
    public private(set) var kitsListing: Listing = .notRead
    public private(set) var datasetsListing: Listing = .notRead
    public private(set) var modelsListing: Listing = .notRead
    /// `train pipeline status`'s standing.
    public private(set) var pipelinesListing: Listing = .notRead
    /// The cycle's: `train status`'s when that failed, else `train cycle
    /// status`'s -- read, with no history yet, without asking it.
    public private(set) var cycleListing: Listing = .notRead
    /// The act running now: what it is, and its progress.
    public private(set) var running: String?
    public private(set) var run: JobProgress?
    /// What the act running now shows live, besides its line.
    public private(set) var live: Live?
    public private(set) var failure: String?
    public var say: (@MainActor (String) -> Void)?
    /// The binary's vocabulary date, as the app read it at launch
    /// (`__machine-schema`, GUI 4k) and each session refreshed it; `nil`
    /// until it has said -- an older binary's ways, the fallback.
    public var vocabulary: @MainActor () -> String? = { nil }

    private let runner: any CommandRunning
    private let starter: any JobStarting
    private var binary: URL?
    private var job: (any JobRunning)?
    /// The notice a pass run here ended with when a halt taken while it ran
    /// was kept.
    private var haltNotice: String?

    public init(runner: any CommandRunning, starter: any JobStarting) {
        self.runner = runner
        self.starter = starter
    }

    /// With no apogee, the place is its empty state -- `missingText`, said
    /// where its lists are -- never an alert, so no visit raises one (GUI 4h).
    public func connect(binary: URL?) async {
        self.binary = binary
        guard binary != nil else {
            (runsListing, versionsListing, regimesListing) = (.missing, .missing, .missing)
            (kitsListing, datasetsListing, modelsListing) = (.missing, .missing, .missing)
            (pipelinesListing, cycleListing) = (.missing, .missing)
            return
        }
        await refresh()
    }

    public func appeared() async {
        await refresh()
    }

    /// Every document the place shows, read again. Each failed read is said
    /// where what it reads is drawn, the rows read before kept.
    public func refresh() async {
        guard let binary else { return }
        let read = await runner.read(binary: binary, ["train", "status"])
        if let document = read.document { status = TrainingStatus(document: document) }
        runsListing = .of(read)
        await readCycle(after: read)
        await readPipelines()
        let versions = await runner.read(binary: binary, ["train", "versions"])
        if let document = versions.document { ledgers = VersionLedger.list(from: document) }
        versionsListing = .of(versions)
        let regimes = await runner.read(binary: binary, ["train", "regime", "list"])
        if let document = regimes.document { self.regimes = TrainingRegime.list(from: document) }
        regimesListing = .of(regimes)
        let kits = await runner.read(binary: binary, ["datasets", "kits"])
        if let document = kits.document { self.kits = TrainingKit.list(from: document) }
        kitsListing = .of(kits)
        let datasets = await runner.read(binary: binary, ["datasets", "list"])
        if let document = datasets.document { self.datasets = TrainingDataset.list(from: document) }
        datasetsListing = .of(datasets)
        let models = await runner.read(binary: binary, ["models", "list", "--all"])
        if let document = models.document {
            let rows = ModelRow.list(from: document)
            students = rows.filter { $0.isStored && $0.format == "safetensors" }.map(\.name)
            backends = ModelRow.chatBackends(rows)
            adapterBackends = rows.filter { !$0.isStored && $0.chats && Self.takesAdapters($0.backendType) }.map(\.backend)
        }
        modelsListing = .of(models)
    }

    /// The named pipelines, the runs, and the open page's document again.
    private func readPipelines() async {
        guard let binary else { return }
        await readSpecs(binary)
        let read = await runner.read(binary: binary, ["train", "pipeline", "status"])
        if let document = read.document { pipelineRuns = PipelineRunRow.list(from: document) }
        pipelinesListing = .of(read)
        if let open = selectedPipelineRun { await readPipelineRun(open) }
        if let spec = openedSpec { await readSpec(spec) }
    }

    /// The named pipelines as `train pipeline list` states them (the CLI's
    /// M30). The read itself tells the binaries apart: an older one refuses
    /// it (`A subcommand is required`), and its names are then completion's,
    /// as 4j read them -- no plan, nothing said of the refusal.
    private func readSpecs(_ binary: URL) async {
        if let document = await runner.read(binary: binary, ["train", "pipeline", "list"]).document {
            specs = PipelineSpec.list(from: document)
            pipelineSpecs = specs.map(\.name)
            return
        }
        specs = []
        if case .done(let said) = await runner.command(binary: binary, ["__complete", "train", "pipeline", "run", "--pipeline", ""]) {
            pipelineSpecs = Self.candidates(said)
        }
    }

    /// A named pipeline's page opened in place of a run's: its stage plan as
    /// `train pipeline show <name>` states it.
    public func openSpec(_ name: String) async {
        openedSpec = name
        selectedPipelineRun = nil
        specFailure = nil
        await readSpec(name)
    }

    private func readSpec(_ name: String) async {
        guard let binary else { return }
        let read = await runner.read(binary: binary, ["train", "pipeline", "show", name])
        guard openedSpec == name else { return }
        if let document = read.document, let spec = PipelineSpec(document: document) {
            shownSpec = spec
            specFailure = nil
        } else {
            shownSpec = nil
            specFailure = read.failure ?? "apogee showed no pipeline \(name)"
        }
    }

    /// The open spec's plan: `show`'s, or -- until it answers -- the list's
    /// entry by that name.
    public var openSpecPlan: PipelineSpec? {
        guard let name = openedSpec else { return nil }
        if let shownSpec, shownSpec.name == name { return shownSpec }
        return specFailure == nil ? specs.first { $0.name == name } : nil
    }

    /// The completion's candidates: one a line, a description after a tab
    /// where one follows, never an option.
    static func candidates(_ said: String) -> [String] {
        said.split(whereSeparator: \.isNewline)
            .map { String($0.split(separator: "\t").first ?? "").trimmingCharacters(in: .whitespaces) }
            .filter { !$0.isEmpty && !$0.hasPrefix("-") }
    }

    /// A pipeline run's page opened, its manifest read; `nil` opens the one
    /// running here.
    public func openPipelineRun(_ id: String?) async {
        selectedPipelineRun = id
        openedSpec = nil
        pipelineRunFailure = nil
        guard let id else { return }
        await readPipelineRun(id)
    }

    private func readPipelineRun(_ id: String) async {
        guard let binary else { return }
        let read = await runner.read(binary: binary, ["train", "pipeline", "status", id])
        guard selectedPipelineRun == id else { return }
        if let document = read.document, let manifest = PipelineRun(document: document) {
            pipelineRun = manifest
            pipelineRunFailure = nil
        } else {
            pipelineRunFailure = read.failure ?? "apogee printed no pipeline run \(id)"
        }
    }

    /// The cycle, read only once `train status` says it has a history:
    /// before the first pass (or a halt) writes one, `train cycle status`
    /// exits 1 (the CLI's 55h), and the place's empty state says so instead.
    private func readCycle(after statusRead: ReadOutcome) async {
        guard let binary else { return }
        guard statusRead.document != nil else {
            cycleListing = .of(statusRead)
            return
        }
        guard status?.cycle != nil else {
            cycle = nil
            cycleListing = .read
            return
        }
        let read = await runner.read(binary: binary, ["train", "cycle", "status"])
        if let document = read.document { cycle = CycleHistory(document: document) }
        cycleListing = .of(read)
    }

    static let missingWords = "Apogee isn't installed, so there is nothing to train."

    /// What each tab says where its list is when there is no apogee.
    public var missingText: String? { runsListing == .missing ? Self.missingWords : nil }

    /// What the runs list says: no apogee, or why `train status` failed.
    public var runsText: String? { Self.text(runsListing, "runs", missing: true) }
    /// What the promoted versions say when `train versions` failed.
    public var versionsText: String? { Self.text(versionsListing, "promoted versions") }
    /// What the regimes list says: no apogee, or why it failed.
    public var regimesText: String? { Self.text(regimesListing, "regimes", missing: true) }
    /// What the datasets list says: no apogee, or why it failed.
    public var datasetsText: String? { Self.text(datasetsListing, "datasets", missing: true) }
    /// What the kits say when `datasets kits` failed.
    public var kitsText: String? { Self.text(kitsListing, "kits") }
    /// What the sheets that pick a student or a teacher say when `models list
    /// --all` failed.
    public var modelsText: String? { Self.text(modelsListing, "stored models") }
    /// What the pipeline runs say: no apogee, or why `train pipeline status`
    /// failed.
    public var pipelinesText: String? { Self.text(pipelinesListing, "pipeline runs", missing: true) }
    /// What the cycle says: no apogee, or why its read failed.
    public var cycleText: String? { Self.text(cycleListing, "cycle", missing: true) }

    /// A list's line: why its read failed, or -- for a tab's own list -- that
    /// there is no apogee, said once a tab.
    private static func text(_ listing: Listing, _ what: String, missing: Bool = false) -> String? {
        switch listing {
        case .notRead, .read: nil
        case .missing: missing ? missingWords : nil
        case .failed(let message): "The \(what) could not be read: \(message)"
        }
    }

    /// What is running first, then the recent runs -- each once.
    public var runs: [TrainingRun] {
        let running = status?.running ?? []
        return running + (status?.recent ?? []).filter { run in !running.contains { $0.id == run.id } }
    }

    public var currentRun: TrainingRun? { selectedRun.flatMap { id in runs.first { $0.id == id } } }
    public var currentRegime: TrainingRegime? { selectedRegime.flatMap { n in regimes.first { $0.name == n } } }
    public var currentDataset: TrainingDataset? { selectedDataset.flatMap { n in datasets.first { $0.name == n } } }

    /// Whether a backend of `type` takes `adapters`: llama.cpp and mlx (the
    /// CLI's `takes_adapters`, 53a).
    public static func takesAdapters(_ type: String) -> Bool {
        type == "llamacpp" || type == "mlx"
    }

    /// What `backend`'s ledger holds, as `train versions` read it -- adapter
    /// versions or merged ones, and which is active -- for the promote sheet
    /// to say before the CLI is asked; `nil` with no version promoted.
    public func ledgerWords(for backend: String) -> String? {
        let backend = backend.trimmingCharacters(in: .whitespaces)
        guard let ledger = ledgers.first(where: { $0.backend == backend }), let holds = ledger.holdsAdapters,
            let active = ledger.active
        else { return nil }
        return "\(backend) holds \(holds ? "adapter" : "merged") versions (active v\(active) of \(ledger.versions.count))"
    }

    /// The gate's verdict, said plainly.
    public static func gate(_ run: TrainingRun) -> String {
        switch run.evalPassed {
        case .some(true): "passed the gate" + (run.evalScore.map { String(format: " (%.0f%%)", $0 * 100) } ?? "")
        case .some(false): "failed the gate" + (run.evalScore.map { String(format: " (%.0f%%)", $0 * 100) } ?? "") + " -- promote refuses it unless forced"
        case .none: "not gated yet"
        }
    }

    // MARK: - Pipelines

    /// The open run's manifest, when it is the open run's.
    public var openPipeline: PipelineRun? {
        pipelineRun.flatMap { $0.id == selectedPipelineRun ? $0 : nil }
    }

    /// The pipeline run going here now, once its stream has named it.
    public var livePipelineID: String? { live == .pipeline ? run?.pipelineRunID : nil }

    /// Whether the open page is the pipeline run going here: no run and no
    /// spec is chosen, or the chosen run is it.
    public var showsLivePipeline: Bool {
        guard live == .pipeline, let run, openedSpec == nil else { return false }
        guard let open = selectedPipelineRun else { return true }
        return run.pipelineRunID == nil || run.pipelineRunID == open
    }

    /// The stages of the run going here, as its stream moved them: each its
    /// manifest's word, its run, its gate's score once gated, and the
    /// trainer's latest iteration on the stage that trains.
    public var liveStages: [StageRow] {
        run.map(Self.stages(of:)) ?? []
    }

    /// The open page's stages: live while it is the run going here, else
    /// its manifest's as last read.
    public var pipelineStages: [StageRow] {
        if showsLivePipeline { return liveStages }
        return openPipeline.map(Self.stages(of:)) ?? []
    }

    /// A run's stages as its stream moved them; a score only once its stage
    /// is past its gate, and the iteration on the stage that trains.
    public static func stages(of progress: JobProgress) -> [StageRow] {
        progress.orderedStages.map { entry in
            StageRow(
                index: entry.index, name: entry.stage.label, state: entry.stage.state, runID: entry.stage.runID,
                score: PipelineRun.isGated(entry.stage.state) ? entry.stage.cumulativeScore : nil,
                iteration: entry.stage.state == "training" && progress.iteration?.stage == entry.index ? progress.iteration : nil)
        }
    }

    /// A run's stages as its manifest records them.
    public static func stages(of run: PipelineRun) -> [StageRow] {
        run.stages.map {
            StageRow(
                index: $0.index, name: $0.name, state: $0.status, runID: $0.runID.isEmpty ? nil : $0.runID, score: $0.score,
                iteration: nil)
        }
    }

    /// Whether `run` can be resumed from here: it stopped short, and nothing
    /// runs in this place now.
    public func canResume(_ run: PipelineRun) -> Bool {
        run.isResumable && !isRunning && binary != nil
    }

    /// Where a resume starts, in a line: `Resume from stage 2, refusals`.
    public static func resumeWords(_ run: PipelineRun) -> String? {
        guard run.isResumable, let stage = run.resumesAt else { return nil }
        return "Resume from stage \(stage.index), \(stage.name)"
    }

    /// A stage in a line: its word, the gate's score once gated, and the
    /// trainer's facts while it trains.
    public static func words(_ stage: StageRow) -> String {
        var parts = [stage.state]
        if let score = stage.score { parts.append("cumulative " + percent(score)) }
        if let iteration = stage.iteration {
            parts.append("iter \(iteration.iteration)" + (iteration.iterations.map { "/\($0)" } ?? ""))
            if let loss = iteration.loss { parts.append(String(format: "loss %.4f", loss)) }
            if let throughput = iteration.throughput { parts.append(String(format: "%.1f it/s", throughput)) }
        }
        return parts.joined(separator: " · ")
    }

    /// A pipeline run in a line: its spec, its status, its stages passed.
    public static func words(_ row: PipelineRunRow) -> String {
        [row.specName, row.status, "\(row.passed) of \(row.stages) stage\(row.stages == 1 ? "" : "s") passed"]
            .filter { !$0.isEmpty }
            .joined(separator: " · ")
    }

    /// A named pipeline in a line, as its row says it: `2 stages · student
    /// tiny`.
    public static func words(_ spec: PipelineSpec) -> String {
        let stages = "\(spec.stages.count) stage\(spec.stages.count == 1 ? "" : "s")"
        return spec.student.isEmpty ? stages : "\(stages) · student \(spec.student)"
    }

    /// Where a spec is written, as `train pipeline show` says it: a
    /// `training.pipelines` entry, or the file it was read from.
    public static func source(_ spec: PipelineSpec) -> String {
        spec.path.map { "the spec file \($0)" } ?? "a training.pipelines entry"
    }

    /// A planned stage in a line: its dataset, and the suites its cumulative
    /// gate holds it to -- `dataset regress · gate hello + nope`, as the
    /// CLI's GATE column joins them.
    public static func planWords(_ stage: PipelineSpec.Stage) -> String {
        let gate = stage.gate.isEmpty ? stage.evalSuite : stage.gate.joined(separator: " + ")
        return ["dataset \(stage.dataset)", gate.isEmpty ? "" : "gate \(gate)"].filter { !$0.isEmpty }.joined(separator: " · ")
    }

    /// A planned stage's tuning as the spec writes it, each default left to
    /// the trainer unsaid: `lora · 4 iterations`.
    public static func tuningWords(_ stage: PipelineSpec.Stage) -> String {
        var parts = [stage.method.isEmpty ? "lora" : stage.method]
        if stage.iters > 0 { parts.append("\(stage.iters) iteration\(stage.iters == 1 ? "" : "s")") }
        if stage.batchSize > 0 { parts.append("batch \(stage.batchSize)") }
        if stage.numLayers > 0 { parts.append("\(stage.numLayers) layer\(stage.numLayers == 1 ? "" : "s")") }
        if stage.gradCheckpoint { parts.append("gradient checkpointing") }
        if stage.maskPrompt { parts.append("completion-only loss") }
        if stage.rehearsalFraction > 0 { parts.append("rehearses \(percent(stage.rehearsalFraction)) of each earlier dataset") }
        return parts.joined(separator: " · ")
    }

    /// The rule every planned stage is held to, in the words `train pipeline
    /// show` closes with.
    public static let gateRule = "A stage passes when its gate -- its own suite and every earlier stage's -- scores 100%."

    /// A score, 0 to 1, as a whole percent.
    public static func percent(_ score: Double) -> String {
        String(format: "%.0f%%", score * 100)
    }

    static let noSpecsWords =
        "No named pipelines. A pipeline is a training.pipelines entry in your config -- its stages, each a dataset and an eval suite, gated cumulatively; a spec file runs from the console (apogee train pipeline run --pipeline <file>)."

    /// What the named pipelines say when there is none.
    public var specsText: String? {
        guard binary != nil, pipelinesListing != .notRead, pipelineSpecs.isEmpty else { return nil }
        return Self.noSpecsWords
    }

    // MARK: - The cycle

    /// Whether a pass holds the cycle now: this place's own, or -- as last
    /// read -- another's, the person's scheduler's.
    public var cycleHeld: Bool {
        live == .cycle || cycle?.active == true || status?.cycleActive == true
    }

    /// Whether the loop is halted, as last read.
    public var cycleHalted: Bool {
        cycle?.halted ?? status?.cycle?.halted ?? false
    }

    /// One pass can start: nothing runs here, no pass holds the cycle, and
    /// it isn't halted -- each refused by the CLI otherwise, and said here
    /// first.
    public var canRunCycle: Bool { binary != nil && !isRunning && !cycleHeld && !cycleHalted }

    /// The vocabulary's date from which the binary keeps a halt taken while
    /// a pass runs -- the pass finishes and says so in a notice (the CLI's
    /// M30, 2026-10-26). M30 kept the date its M29 had set, so a development
    /// build between the two announces it too; no release does.
    public static let keepsHaltSince = "2026-10-26"

    /// Whether the binary keeps a halt taken while a pass holds the cycle
    /// (M30), as its vocabulary says; an older one, or one that hasn't said,
    /// loses it when the pass records itself.
    public var keepsHalt: Bool { (vocabulary() ?? "") >= Self.keepsHaltSince }

    /// A halt can be taken: not halted already -- and, on a binary that loses
    /// a halt taken during a pass, no pass holds the cycle.
    public var canHaltCycle: Bool { binary != nil && !cycleHalted && (!cycleHeld || keepsHalt) }

    /// A resume is offered: the loop is halted.
    public var canResumeCycle: Bool { binary != nil && cycleHalted }

    /// Why Halt isn't offered while a pass holds the cycle, on a binary that
    /// loses a halt taken then (before the CLI's M30).
    static let heldWords =
        "A pass holds the cycle now. A halt taken during a pass is lost when the pass records itself -- Stop it, if it is this place's, or halt once it has ended."

    /// What a halt does to the pass holding the cycle, on a binary that
    /// keeps it: the CLI's words, `train cycle halt`'s answer then.
    static let keptWords = "The pass running now finishes, and keeps the halt"

    /// What the cycle's controls say about its state, when something holds
    /// them back.
    public var cycleHoldText: String? {
        if cycleHeld && !keepsHalt { return Self.heldWords }
        guard cycleHalted else { return nil }
        let reason = cycle?.haltReason ?? status?.cycle?.haltReason ?? ""
        let halted = "Halted: \(reason.isEmpty ? "'apogee train cycle halt' was run" : reason)."
        return cycleHeld
            ? "\(halted) \(Self.keptWords): every pass after it refuses -- your scheduler's too -- until Resume clears it."
            : "\(halted) Every pass refuses -- your scheduler's too -- until Resume clears it."
    }

    /// The notice a pass running here says when a halt taken while it ran
    /// is kept, once it has said it (the CLI's M30) -- machine-mode.md's
    /// `[cycle] halted while this pass ran (…) -- the halt is kept: …`.
    public var liveHaltNotice: String? {
        live == .cycle ? run.flatMap { Self.haltNotice(in: $0.notices) } : nil
    }

    /// The notice the last pass run here ended with, its halt kept, while the
    /// loop stays halted: drawn on the tab, as the CLI said it.
    public var keptHalt: String? { cycleHalted ? haltNotice : nil }

    /// The pass's own word that a halt taken while it ran is kept: its
    /// notice, never a `pass` of state `halted` (the breaker's).
    static func haltNotice(in notices: [String]) -> String? {
        notices.last { $0.hasPrefix("[cycle] halted while this pass ran") }
    }

    /// The cycle is unattended by design: its schedule is the person's own.
    public static let scheduleWords =
        "The cycle is unattended by design: your own scheduler -- launchd or cron -- runs apogee train cycle run. Nothing here schedules it; Run one pass is a single pass, now."

    static let noHistoryWords =
        "No pass has run yet, so the cycle has no history: its first pass writes one -- Run one pass here, or your scheduler's first run. A pass needs training.cycle in your config; the run says so when it's missing."

    /// What the cycle says with no history yet -- `train status`'s `cycle`
    /// null -- rather than the read that exits 1.
    public var noHistoryText: String? {
        cycleListing == .read && cycle == nil ? Self.noHistoryWords : nil
    }

    /// The anchor, in a line, as the CLI's `train cycle status` says it:
    /// `v1 (100%)`, or not set yet.
    public static func anchorWords(_ cycle: CycleHistory) -> String {
        cycle.anchorVersion > 0
            ? "v\(cycle.anchorVersion)" + (cycle.anchorScore.map { " (\(percent($0)))" } ?? "")
            : "not set -- the first passing pass sets it"
    }

    /// A pass in a line: its gate, its score against the last pass and the
    /// anchor, what it promoted, where its data came from. A score of 0 is
    /// one the pass never reached -- a pipeline that aborted before the dual
    /// gate -- and goes unsaid, as the CLI's own table prints it `-`.
    public static func words(_ pass: CycleHistory.Pass) -> String {
        var parts = [pass.gate]
        func reached(_ score: Double?) -> Double? { score.flatMap { $0 == 0 ? nil : $0 } }
        if let score = reached(pass.cycleScore) {
            let against = [("previous", pass.previousScore), ("anchor", pass.anchorScore)]
                .compactMap { name, value in reached(value).map { "\(name) \(percent($0))" } }
            parts.append("scored \(percent(score))" + (against.isEmpty ? "" : " (\(against.joined(separator: ", ")))"))
        }
        if pass.promotedVersion > 0 { parts.append("promoted v\(pass.promotedVersion)") }
        if !pass.source.isEmpty { parts.append("\(pass.datasetRows) example\(pass.datasetRows == 1 ? "" : "s") from \(pass.source)") }
        return parts.joined(separator: " · ")
    }

    // MARK: - The person's word

    /// The question the confirm asks.
    public var pendingTitle: String {
        switch pending {
        case .rollback(let backend): "Point \(backend) back at its previous version?"
        case .deleteDataset(let name): "Delete the dataset \(name)?"
        case .runPipeline(let name): "Run the pipeline \(name)?"
        case .resumePipeline(let id):
            openPipeline.flatMap { $0.id == id ? $0.resumesAt : nil }.map { "Resume \(id) from stage \($0.index), \($0.name)?" }
                ?? "Resume \(id)?"
        case .runCycle: "Run the training cycle once?"
        case .haltCycle: "Halt the training cycle?"
        case .resumeCycle: "Resume the training cycle?"
        case nil: ""
        }
    }

    /// What saying yes does, under the command: the TUI's words before its
    /// `p` and `c`, and the CLI's for the cycle's halt and resume.
    public var pendingConsequence: String? {
        switch pending {
        case .runPipeline: "Each stage is a training run, held to the cumulative gate -- this machine is held until it ends."
        case .resumePipeline:
            "What passed stays; each stage from there is a training run, held to the cumulative gate -- this machine is held until it ends."
        case .runCycle: "It trains, gates and may promote -- this machine is held until it ends."
        case .haltCycle:
            "Every 'cycle run' refuses until 'cycle resume' -- your scheduler's passes too."
                + (cycleHeld && keepsHalt ? " \(Self.keptWords)." : "")
        case .resumeCycle: "It clears the halt and the failure count: your scheduler's next pass runs."
        case .rollback, .deleteDataset, nil: nil
        }
    }

    /// The confirm's button.
    public var pendingAction: String {
        switch pending {
        case .runPipeline: "Start"
        case .resumePipeline: "Resume"
        case .runCycle: "Run one pass"
        case .haltCycle: "Halt"
        case .resumeCycle: "Resume"
        case .rollback, .deleteDataset, nil: "Go ahead"
        }
    }

    // MARK: - Sheets

    public func newRun() {
        runForm = RunForm()
        runForm.student = students.first ?? ""
        runForm.dataset = datasets.first?.name ?? ""
        sheet = .run
    }

    public func newPromotion(of run: TrainingRun) {
        promoteForm = PromoteForm()
        promoteForm.run = run.id
        promoteRefusal = nil
        sheet = .promote
    }

    /// The refused promotion's sheet again, with the CLI's words above it and
    /// the mode its refusal names as the way (`PromoteForm.door`).
    public func changePromotion() {
        guard let refused = refusedPromotion else { return }
        refusedPromotion = nil
        failure = nil
        promoteForm = refused.form.door(after: refused.refusal)
        promoteRefusal = refused.refusal
        sheet = .promote
    }

    /// The pull sheet: a dataset's reference on Hugging Face.
    public func newPull() {
        pullRef = ""
        sheet = .pull
    }

    /// `datasets pull <ref>`, as typed -- what the sheet shows: nothing
    /// without a reference. A binary that streams the pull is started with
    /// its stream asked for besides (`pull()`).
    public var pullArguments: [String]? {
        let ref = pullRef.trimmingCharacters(in: .whitespaces)
        return ref.isEmpty ? nil : ["datasets", "pull", ref]
    }

    /// The vocabulary's date from which `datasets pull` streams as a job
    /// (the CLI's M30, 2026-10-26) -- the date M29 set, as `keepsHaltSince`
    /// says.
    public static let streamsPullSince = "2026-10-26"

    /// Whether the binary streams `datasets pull` as a job (the CLI's M30),
    /// as its vocabulary says: its bytes as they move, a Ctrl-C ending it
    /// with nothing written, its document naming where it landed. An older
    /// binary refuses the flag ("The following arguments were not expected:
    /// stream-json --output-format"), and its pull runs as text, as 4i ran
    /// it.
    public var streamsPull: Bool { (vocabulary() ?? "") >= Self.streamsPullSince }

    /// The bytes the act running now has moved, as its stream counts them --
    /// a streamed pull's, file by file.
    public var pullBytes: JobProgress.Bytes? { run?.bytes }

    /// Bytes in a line: `49 KB of 100 KB · data/train.jsonl (1 of 2)`, the
    /// total only when the stream knew it.
    public static func bytesWords(_ bytes: JobProgress.Bytes) -> String {
        let done = ByteCountFormatter.string(fromByteCount: Int64(bytes.done), countStyle: .file)
        let total = bytes.total.map { " of " + ByteCountFormatter.string(fromByteCount: Int64($0), countStyle: .file) } ?? ""
        let file = bytes.file.map { " · \($0)" + (bytes.index.map { i in " (\(i) of \(bytes.count ?? i))" } ?? "") } ?? ""
        return done + total + file
    }

    public func newSynthesis() {
        synthForm = SynthForm()
        synthForm.teacher = backends.first ?? ""
        synthForm.kit = kits.first?.name ?? ""
        sheet = .synth
    }

    public func newDataset() {
        createName = ""
        createFrom = "template"
        sheet = .create
    }

    // MARK: - Acts

    public func startRun() {
        guard let arguments = runForm.arguments else { return }
        sheet = nil
        start(arguments, doing: "Training")
    }

    /// `train eval <run>`: the gate, on the run's own suite unless named.
    public func gate(_ run: TrainingRun, suite: String? = nil) {
        start(["train", "eval", run.id] + (suite.map { ["--suite", $0] } ?? []), doing: "Gating")
    }

    public func promote() {
        guard let arguments = promoteForm.arguments else { return }
        sheet = nil
        promoteRefusal = nil
        refusedPromotion = nil
        start(arguments, doing: "Promoting", act: .promote(promoteForm))
    }

    /// `datasets pull <ref>`: a Hugging Face dataset's data files downloaded
    /// for `datasets prepare`, supervised, Stop as Ctrl-C. On a binary that
    /// streams it (M30) a job's stream -- its bytes, its document naming the
    /// folder; on an older one its lines as text. Once done, Prepare is
    /// offered on the directory it names.
    public func pull() {
        guard let arguments = pullArguments else { return }
        sheet = nil
        pulled = nil
        let streamed = streamsPull
        start(arguments + (streamed ? Self.stream : []), doing: "Pulling", act: .pull(streamed: streamed))
    }

    /// The pulled dataset's banner, once read.
    public func dismissPulled() {
        pulled = nil
    }

    /// `train regime run <name>`: synth, pipeline, promote -- the regime's own
    /// tuning unless the spec names none.
    public func runRegime(_ regime: TrainingRegime) {
        start(["train", "regime", "run", regime.name], doing: "Running \(regime.name)")
    }

    public func synthesize() {
        guard let arguments = synthForm.arguments else { return }
        sheet = nil
        start(arguments, doing: "Synthesizing")
    }

    /// `datasets prepare <path>`: a local file made trainer-ready.
    public func prepare(_ path: String) {
        start(["datasets", "prepare", path], doing: "Preparing")
    }

    /// `datasets create <name> --from <template|sessions|empty>`, read back;
    /// the new dataset is selected only once the binary made it -- a refusal
    /// keeps the selection and is said (GUI 4h).
    public func createDataset() async {
        let name = createName.trimmingCharacters(in: .whitespaces)
        guard !name.isEmpty else { return }
        sheet = nil
        if await change(["datasets", "create", name, "--from", createFrom]) { selectedDataset = name }
    }

    /// The person said yes: a run started supervised, its stream asked for;
    /// any other act run and read back.
    public func confirm() async {
        guard let pending else { return }
        self.pending = nil
        switch pending {
        case .runPipeline(let name):
            selectedPipelineRun = nil
            openedSpec = nil
            start(pending.arguments + Self.stream, doing: "Running \(name)", act: .pipeline(name))
        case .resumePipeline(let id):
            selectedPipelineRun = id
            openedSpec = nil
            start(pending.arguments + Self.stream, doing: "Resuming \(id)", act: .pipeline(id))
        case .runCycle:
            haltNotice = nil
            start(pending.arguments + Self.stream, doing: "Running one pass of the cycle", act: .cycle)
        case .resumeCycle:
            if await change(pending.arguments) { haltNotice = nil }
        case .rollback, .deleteDataset, .haltCycle:
            _ = await change(pending.arguments)
        }
    }

    /// The stream a pipeline or a cycle pass is run with (the CLI's 55h):
    /// its stages, iterations and narration as events, its read's document
    /// last.
    static let stream = ["--output-format", "stream-json"]

    /// Runs a change, reads back, and says how it went -- the answer's last
    /// line, as the CLI wrote it but for its indent; whether it was done.
    private func change(_ arguments: [String]) async -> Bool {
        guard let binary else { return false }
        let outcome = await runner.command(binary: binary, arguments)
        await refresh()
        if let failure = outcome.failure {
            self.failure = failure
            return false
        }
        if case .done(let stdout) = outcome {
            say?(stdout.split(whereSeparator: \.isNewline).last.map { $0.trimmingCharacters(in: .whitespaces) } ?? "Done")
        }
        return true
    }

    public var isRunning: Bool { job != nil }

    public func stop() async {
        await job?.stop(grace: .seconds(10))
    }

    /// What a supervised act is, for what it shows live and what its end
    /// leaves behind.
    private enum Act {
        case other
        /// A promotion, as its form sent it: a refusal keeps it to change.
        case promote(PromoteForm)
        /// A pull: its end names the directory to prepare -- its document's
        /// when it was `streamed` (M30), else its last lines.
        case pull(streamed: Bool)
        /// A pipeline run or resume -- its spec's name or its run's id --
        /// streamed: its stages live, its manifest read back after.
        case pipeline(String)
        /// A cycle pass, streamed: its narration and its pipeline's stages
        /// live, the history read back after.
        case cycle
    }

    private func start(_ arguments: [String], doing: String, act: Act = .other) {
        guard let binary, job == nil else { return }
        running = doing
        run = JobProgress()
        let streaming: Bool
        switch act {
        case .pipeline: (live, streaming) = (.pipeline, true)
        case .cycle: (live, streaming) = (.cycle, true)
        case .pull(let streamed): (live, streaming) = (nil, streamed)
        case .other, .promote: (live, streaming) = (nil, false)
        }
        let job = starter.startJob(binary: binary, arguments: arguments, streaming: streaming, folder: nil)
        self.job = job
        Task {
            for await update in job.updates { run?.apply(update) }
            self.job = nil
            let progress = run
            // The run's page stays open on it, read back from its manifest.
            if case .pipeline = act, let id = progress?.pipelineRunID, openedSpec == nil,
                selectedPipelineRun == nil || selectedPipelineRun == id
            {
                selectedPipelineRun = id
            }
            // A halt taken while the pass ran, kept: the pass's own word.
            if case .cycle = act { haltNotice = Self.haltNotice(in: progress?.notices ?? []) }
            run = nil
            running = nil
            live = nil
            await refresh()
            switch progress?.status {
            case .done:
                if case .pull(let streamed) = act, let pulled = Self.pulled(progress, streamed: streamed) {
                    self.pulled = pulled
                    say?("Pulled: \(pulled.summary.isEmpty ? pulled.path : pulled.summary)")
                } else if case .pipeline(let name) = act {
                    say?("Pipeline \(name): \(openPipeline?.status ?? "done")")
                } else if case .cycle = act {
                    say?(haltNotice ?? progress?.pass?.text ?? "The pass ended")
                } else {
                    say?(progress?.notices.first ?? "\(doing): done")
                }
            case .failed(let message):
                if case .promote(let form) = act { refusedPromotion = RefusedPromotion(form: form, refusal: message) }
                failure = message
            case .stopped:
                if case .pipeline = act {
                    say?("Stopped: what passed stays -- Resume continues it")
                } else if case .pull(true) = act, let words = progress?.error {
                    // The stream's own last word on a Ctrl-C: nothing kept.
                    say?(words)
                } else {
                    say?("Stopped: what was finished stays")
                }
            default: break
            }
        }
    }

    /// What a finished pull leaves to prepare: a streamed one's folder from
    /// its closing document (M30), an older one's from its last lines.
    private static func pulled(_ progress: JobProgress?, streamed: Bool) -> PulledDataset? {
        let said = progress?.notices ?? []
        guard streamed else { return PulledDataset(said: said) }
        return progress?.documents.last.flatMap { PulledDataset(document: $0, said: said) }
    }

    /// The failure read: a refused promotion is let go with it.
    public func dismissFailure() {
        failure = nil
        refusedPromotion = nil
    }
}
