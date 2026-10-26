import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Observation

/// The Training place: runs and their gates and promotions -- merged, or the
/// adapter alone (the CLI's 53b, gated on a quantized base where it will
/// serve, 53c) -- the regimes, and the datasets with their kits, pulled from
/// Hugging Face or made here -- each view a document re-read after, each act
/// the person's command.
///
/// SwiftUI-free (ADR 0002). A long act -- a run, a gate, a promotion, a
/// regime, a pull, a synthesis, a prepare -- is supervised on the jobs core,
/// its busy line the CLI's own words: `train run` takes no machine stream, so
/// nothing is decided from them. No training starts without the person's
/// launch, and a refused promotion's sheet opens again with the CLI's words.
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
    /// put the files and what it said of them, from its own last lines --
    /// `<n> file(s), <size>`, and the `datasets prepare` that makes them
    /// trainer-ready, which the tab offers.
    public struct PulledDataset: Equatable, Sendable {
        /// The directory `datasets prepare` takes, as the CLI named it.
        public let path: String
        /// `2 file(s), 3 MiB`.
        public let summary: String

        /// From what the pull printed: its `Convert it with:  apogee datasets
        /// prepare <path>` line, and the count line before it. `nil` when it
        /// names no prepare.
        public init?(said lines: [String]) {
            let door = "apogee datasets prepare "
            guard let line = lines.last(where: { $0.contains(door) }), let range = line.range(of: door) else { return nil }
            path = String(line[range.upperBound...]).trimmingCharacters(in: .whitespaces)
            summary = lines.last(where: { $0.contains(" file(s), ") && !$0.hasPrefix("downloading ") }) ?? ""
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
        case pull
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
    /// The configured entries an adapter can go on: llama.cpp and mlx ones
    /// (the CLI's 53a), the only types that take `adapters`.
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
            adapterBackends = rows.filter { !$0.isStored && Self.takesAdapters($0.backendType) }.map(\.backend)
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

    /// `datasets pull <ref>`, as typed: nothing without a reference.
    public var pullArguments: [String]? {
        let ref = pullRef.trimmingCharacters(in: .whitespaces)
        return ref.isEmpty ? nil : ["datasets", "pull", ref]
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
    /// for `datasets prepare`, supervised -- its lines the CLI's, Stop as
    /// Ctrl-C. Once done, Prepare is offered on the directory it names.
    public func pull() {
        guard let arguments = pullArguments else { return }
        sheet = nil
        pulled = nil
        start(arguments, doing: "Pulling", act: .pull)
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

    /// What a supervised act is, for what its end leaves behind.
    private enum Act {
        case other
        /// A promotion, as its form sent it: a refusal keeps it to change.
        case promote(PromoteForm)
        /// A pull: its end names the directory to prepare.
        case pull
    }

    private func start(_ arguments: [String], doing: String, act: Act = .other) {
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
            case .done:
                if case .pull = act, let pulled = PulledDataset(said: progress?.notices ?? []) {
                    self.pulled = pulled
                    say?("Pulled: \(pulled.summary.isEmpty ? pulled.path : pulled.summary)")
                } else {
                    say?(progress?.notices.first ?? "\(doing): done")
                }
            case .failed(let message):
                if case .promote(let form) = act { refusedPromotion = RefusedPromotion(form: form, refusal: message) }
                failure = message
            case .stopped: say?("Stopped: what was finished stays")
            default: break
            }
        }
    }

    /// The failure read: a refused promotion is let go with it.
    public func dismissFailure() {
        failure = nil
        refusedPromotion = nil
    }
}
