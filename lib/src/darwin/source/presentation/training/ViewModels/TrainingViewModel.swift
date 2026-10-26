import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Observation

/// The Training place: runs and their gates and promotions, the regimes, and
/// the datasets with their kits -- each view a document re-read after, each
/// act the person's command.
///
/// SwiftUI-free (ADR 0002). A long act -- a run, a gate, a promotion, a
/// regime, a synthesis, a prepare -- is supervised on the jobs core, its busy
/// line the CLI's own words: `train run` takes no machine stream, so nothing
/// is decided from them. No training starts without the person's launch.
@MainActor
@Observable
public final class TrainingViewModel {
    public enum Tab: String, CaseIterable, Identifiable, Sendable {
        case runs
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
    /// person forces it.
    public struct PromoteForm: Equatable, Sendable {
        public var run = ""
        public var backend = ""
        /// `gguf` or `mlx`; nil lets the CLI choose by what `--as` runs.
        public var target: String?
        /// A GGUF level; nil keeps F16.
        public var quantize: String?
        public var force = false

        public init() {}

        public var arguments: [String]? {
            let backend = backend.trimmingCharacters(in: .whitespaces)
            guard !run.isEmpty, !backend.isEmpty else { return nil }
            var arguments = ["train", "promote", run, "--as", backend]
            if let target { arguments += ["--target", target] }
            if let quantize { arguments += ["--quantize", quantize] }
            if force { arguments.append("--force") }
            return arguments
        }

        public static let levels = ["Q2_K", "Q3_K_M", "Q4_K_M", "Q5_K_M", "Q6_K", "Q8_0", "F16"]
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

    /// A destructive act waiting on the person's word.
    public enum Pending: Equatable, Sendable {
        case rollback(backend: String)
        case deleteDataset(String)

        /// `train rollback <b>`, or `datasets delete <d> --yes` -- the person
        /// said yes here.
        public var arguments: [String] {
            switch self {
            case .rollback(let backend): ["train", "rollback", backend]
            case .deleteDataset(let name): ["datasets", "delete", name, "--yes"]
            }
        }
    }

    public enum Sheet: Equatable, Sendable {
        case run
        case promote
        case synth
        case create
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
    /// The configured backends a teacher or a promotion can name.
    public private(set) var backends: [String] = []
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
    public var pending: Pending?
    /// Each read's standing: `train status` (the runs), `train versions`,
    /// `train regime list`, `datasets kits`, `datasets list`, and `models list
    /// --all` (the students and the backends the sheets offer).
    public private(set) var runsListing: Listing = .notRead
    public private(set) var versionsListing: Listing = .notRead
    public private(set) var regimesListing: Listing = .notRead
    public private(set) var kitsListing: Listing = .notRead
    public private(set) var datasetsListing: Listing = .notRead
    public private(set) var modelsListing: Listing = .notRead
    /// The act running now: what it is, and its progress.
    public private(set) var running: String?
    public private(set) var run: JobProgress?
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

    /// With no apogee, the place is its empty state -- `missingText`, said
    /// where its lists are -- never an alert, so no visit raises one (GUI 4h).
    public func connect(binary: URL?) async {
        self.binary = binary
        guard binary != nil else {
            (runsListing, versionsListing, regimesListing) = (.missing, .missing, .missing)
            (kitsListing, datasetsListing, modelsListing) = (.missing, .missing, .missing)
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
            backends = rows.filter { !$0.isStored }.map(\.backend)
        }
        modelsListing = .of(models)
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

    /// The gate's verdict, said plainly.
    public static func gate(_ run: TrainingRun) -> String {
        switch run.evalPassed {
        case .some(true): "passed the gate" + (run.evalScore.map { String(format: " (%.0f%%)", $0 * 100) } ?? "")
        case .some(false): "failed the gate" + (run.evalScore.map { String(format: " (%.0f%%)", $0 * 100) } ?? "") + " -- promote refuses it unless forced"
        case .none: "not gated yet"
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
        sheet = .promote
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
        start(arguments, doing: "Promoting")
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

    public func confirm() async {
        guard let pending else { return }
        self.pending = nil
        _ = await change(pending.arguments)
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
            say?(stdout.split(whereSeparator: \.isNewline).last.map(String.init) ?? "Done")
        }
        return true
    }

    public var isRunning: Bool { job != nil }

    public func stop() async {
        await job?.stop(grace: .seconds(10))
    }

    private func start(_ arguments: [String], doing: String) {
        guard let binary, job == nil else { return }
        running = doing
        run = JobProgress()
        let job = starter.startJob(binary: binary, arguments: arguments, streaming: false, folder: nil)
        self.job = job
        Task {
            for await update in job.updates { run?.apply(update) }
            self.job = nil
            let progress = run
            run = nil
            running = nil
            await refresh()
            switch progress?.status {
            case .done: say?(progress?.notices.first ?? "\(doing): done")
            case .failed(let message): failure = message
            case .stopped: say?("Stopped: what was finished stays")
            default: break
            }
        }
    }

    public func dismissFailure() {
        failure = nil
    }
}
