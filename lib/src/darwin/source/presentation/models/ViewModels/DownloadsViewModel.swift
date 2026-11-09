import BusinessJobs
import BusinessReads
import Foundation
import Observation

/// The Jobs tab: a pull run as the supervised job it is -- its steps, its
/// bytes, Stop -- and, since 2i, the store's long runs: a conversion, a
/// quantization, a check of the files. A stopped step runs again by the
/// command the CLI names for it; each finished job is read back into the
/// library.
///
/// SwiftUI-free (ADR 0002). Progress is the 52b stream where the build has
/// it; on a build before it, a busy line sampled from stderr -- shown, never
/// decided on (the design's G9 fallback). The store's verbs have no machine
/// face yet (G18, G20): their steps tick on the sets their own run names as
/// it stores them -- a quantization's GGUF once it ended well, a conversion's
/// each as its output prints it -- each placed in the job's own model folder
/// by `models info` (GUI 4e, 6d). What a job registers, ticks or shows is
/// what its own run says it made, never the store's difference, which
/// another job's sets and backends are part of -- a sibling quantization of
/// the same model among them, landing in the same folder (GUI 6d); and a job
/// run again, or resumed, is the same job. The store is still read before a
/// store job runs, as its first step (GUI 4e), and a Stop pressed while that
/// read runs ends the card with nothing started (GUI 6d).
@MainActor
@Observable
public final class DownloadsViewModel {
    /// What a job is.
    public enum Kind: Equatable, Sendable {
        case pull
        case convert
        case quantize
        case check
    }

    /// Where one of a store job's steps stands.
    public enum StepState: Equatable, Sendable {
        case waiting
        case running
        case done
    }

    /// One run, and what it has said.
    public final class Download: Identifiable {
        public let id: Int
        /// What a person would type after `apogee`.
        public let arguments: [String]
        public let reference: String
        public let kind: Kind
        /// A store job's steps, as its sheet listed them.
        public let steps: [StoreStep]
        public let startedAt: Date
        public fileprivate(set) var progress = JobProgress()
        /// What landed in the store since it began, by the name `models list
        /// --all` gives each: a set's handle in its own model folder, or the
        /// name of a backend its own run registered. Read as it runs, and
        /// never what a step ticks on or Show the weights opens: a sibling
        /// job's set lands in the same folder (GUI 6d).
        public fileprivate(set) var landed: [String] = []
        /// The set its own run says it made: a quantization's GGUF, as `models
        /// info` names the file its output printed (GUI 4e). `nil` until then,
        /// and for the jobs that do not say.
        public fileprivate(set) var made: String?
        /// The sets a conversion's own run named as it stored them, by
        /// handle, in order: each path its output printed on a line of its
        /// own -- a GGUF, never its projector, or an MLX directory -- that
        /// `models info` places in its own model folder (GUI 6d). Kept by a
        /// resume or a Run again, which are the same job.
        public fileprivate(set) var named: [String] = []
        public fileprivate(set) var endedAt: Date?
        /// The last word on it: a check's result, or the registration that
        /// followed a quantization -- in the CLI's words.
        public fileprivate(set) var outcome: String?
        /// The registration that follows a quantization, when asked.
        let registerAfter: QuantizeForm?
        /// The model folder its command works in -- the first part of the
        /// handle it was given -- where what it makes lands; `nil` for a pull,
        /// and for a name that is no handle.
        let folder: String?
        fileprivate var job: (any JobRunning)?
        /// Whether Stop was pressed before its child existed -- while the
        /// store's first read ran -- so that read's answer starts nothing
        /// (GUI 6d).
        fileprivate var stoppedBeforeRun = false
        /// The store's names before it first ran; `nil` until read.
        fileprivate var before: Set<String>?
        fileprivate var lastRead = Date.distantPast
        fileprivate var registered = false
        /// The backend the app registered after its run: a quantization's.
        fileprivate var registeredAfter: String?

        /// The backends its own run registered: those its words name -- a
        /// chain's `registered <name>` and `already registered: <name>` (the
        /// CLI's `register_backends`, and its MLX twin) -- and the one the
        /// app registered after it.
        var ownBackends: Set<String> {
            var names = Set(progress.notices.compactMap(DownloadsViewModel.registeredName))
            if let registeredAfter { names.insert(registeredAfter) }
            return names
        }

        init(
            id: Int, arguments: [String], reference: String, kind: Kind = .pull, steps: [StoreStep] = [],
            registerAfter: QuantizeForm? = nil, folder: String? = nil, startedAt: Date = Date()
        ) {
            self.id = id
            self.arguments = arguments
            self.reference = reference
            self.kind = kind
            self.steps = steps
            self.registerAfter = registerAfter
            self.folder = folder
            self.startedAt = startedAt
        }

        /// Where step `index` stands: a step that makes a set is done once its
        /// own run has named as many sets; the registration once the run
        /// ended well.
        public func state(of index: Int) -> StepState {
            if isDone(index) { return .done }
            return isRunning && steps.indices.first(where: { !isDone($0) }) == index ? .running : .waiting
        }

        private func isDone(_ index: Int) -> Bool {
            guard steps[index].makesSet else { return progress.status == .done && (registerAfter == nil || registered) }
            switch kind {
            // A quantization's one set is the one its run named, once it ended
            // well: made now, or found made already. Never a set that landed in
            // its folder, where a sibling quantization's lands too (GUI 6d).
            case .quantize: return made != nil
            // A conversion's sets are those its run named as it stored them.
            case .convert: return steps[..<index].filter(\.makesSet).count < named.count
            case .pull, .check: return false
            }
        }

        /// The first set it made, for Show the weights: the one its run named
        /// -- a quantization's GGUF, a conversion's first set -- and none for
        /// a run that named none (GUI 6d).
        public var firstLanded: String? {
            switch kind {
            case .quantize: made
            case .convert: named.first
            case .pull, .check: nil
            }
        }

        /// The command, as typed.
        public var command: String { (["apogee"] + arguments).joined(separator: " ") }

        public var isRunning: Bool { progress.status == .running }
    }

    /// The levels a converted pull can be quantized to (the CLI's own list).
    public static let levels = ["Q2_K", "Q3_K_M", "Q4_K_M", "Q5_K_M", "Q6_K", "Q8_0", "F16"]

    /// The form.
    public var reference = ""
    /// Pull the full SafeTensors weights, convert, and register each level.
    public var convert = false
    public var chosenLevels: Set<String> = ["Q4_K_M"]

    public private(set) var downloads: [Download] = []
    /// Moves with every change to a download, for the views that follow one.
    public private(set) var revision = 0
    /// Told when a pull ends, so the library reads the store again.
    public var finished: (@MainActor () -> Void)?
    /// Whether the build streams a job's progress (the CLI's 52b), as the
    /// last session announced; `false` -- the fallback -- until one has.
    public var streams: @MainActor () -> Bool = { false }

    /// Reads the store back for a store job's steps: `models list --all`.
    public var reader: (any CommandRunning)?
    /// Opens a set under Stored weights: Show the weights.
    public var showWeights: (@MainActor (String) -> Void)?
    /// The clock a job's time is told by.
    public var now: @MainActor () -> Date = Date.init

    private let starter: any JobStarting
    private var binary: URL?
    private var nextID = 0

    public init(starter: any JobStarting) {
        self.starter = starter
    }

    public func connect(binary: URL?) {
        self.binary = binary
    }

    // MARK: - Starting

    /// The pull the form describes, as its arguments; `nil` until it names one.
    public var formArguments: [String]? {
        let ref = reference.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !ref.isEmpty, !ref.contains(" ") else { return nil }
        var arguments = ["models", "pull", ref]
        if convert {
            let levels = Self.levels.filter(chosenLevels.contains)
            arguments += ["--safetensors"] + (levels.isEmpty ? ["--register"] : ["--register-with", levels.joined(separator: ",")])
        }
        return arguments
    }

    public var canStart: Bool { binary != nil && formArguments != nil }

    /// Starts the form's pull.
    public func start() {
        guard let arguments = formArguments else { return }
        run(arguments, reference: arguments[2])
        reference = ""
    }

    /// Runs the command the CLI named for carrying a stopped or failed run on
    /// -- shown on its button before it runs; a run that finished names none.
    /// A store job's resume is that job again: its kind, steps and
    /// registration, followed as it was (GUI 4e).
    public func resume(_ download: Download) {
        guard !download.isRunning, let command = download.progress.resumeCommand else { return }
        if download.kind == .pull {
            run(command, reference: download.reference)
        } else {
            rerun(download, command)
        }
    }

    /// Runs the same job again: a stopped download's finished steps stand,
    /// and the CLI skips what is already stored. A store job runs again as
    /// itself (GUI 4e).
    public func again(_ download: Download) {
        guard !download.isRunning else { return }
        if download.kind == .pull {
            run(download.arguments.filter { $0 != "--output-format" && $0 != "stream-json" }, reference: download.reference)
        } else {
            rerun(download, download.arguments)
        }
    }

    /// The verbs that take `--output-format stream-json` (the CLI's 52b).
    static let streamingVerbs: Set<[String]> = [["models", "pull"], ["video", "generate"], ["video", "compose"], ["voice", "say"]]

    private func run(_ arguments: [String], reference: String) {
        guard let binary else { return }
        let streaming = streams() && Self.streamingVerbs.contains(Array(arguments.prefix(2)))
        let full = arguments + (streaming ? ["--output-format", "stream-json"] : [])
        nextID += 1
        let download = Download(id: nextID, arguments: full, reference: reference)
        downloads.insert(download, at: 0)
        let job = starter.startJob(binary: binary, arguments: full, streaming: streaming)
        download.job = job
        revision += 1
        Task { await follow(download, job) }
    }

    private func follow(_ download: Download, _ job: any JobRunning) async {
        for await update in job.updates {
            download.progress.apply(update)
            revision += 1
        }
        download.job = nil
        download.endedAt = now()
        revision += 1
        finished?()
    }

    // MARK: - The store's long runs

    /// `models convert …`, the convert sheet's command as typed.
    public func convert(_ form: ConvertForm) {
        startStore(form.arguments, kind: .convert, title: form.handle, steps: form.steps, folder: Self.folder(of: form.handle))
    }

    /// `models quantize …`, then -- asked -- the `config add-backend` that
    /// registers what it made.
    public func quantize(_ form: QuantizeForm) {
        startStore(
            form.arguments, kind: .quantize, title: form.handle, steps: form.steps, registerAfter: form.register ? form : nil,
            folder: Self.folder(of: form.handle))
    }

    /// `models repair <name>`: the files checked against their record, as a job.
    public func check(_ name: String) {
        startStore(["models", "repair", name], kind: .check, title: name, steps: [], folder: Self.folder(of: name))
    }

    /// The backend a run's line says it registered: `registered <name>` or
    /// `already registered: <name>`, a name being one word.
    nonisolated static func registeredName(_ line: String) -> String? {
        let line = line.trimmingCharacters(in: .whitespaces)
        for opening in ["already registered: ", "registered "] where line.hasPrefix(opening) {
            let name = line.dropFirst(opening.count)
            return name.isEmpty || name.contains(" ") ? nil : String(name)
        }
        return nil
    }

    /// The model folder a store handle names -- `<model>/<format>/<id>`'s
    /// first part -- or `nil` for a name that is no handle (a backend's).
    static func folder(of name: String) -> String? {
        let parts = name.split(separator: "/")
        return parts.count == 3 ? String(parts[0]) : nil
    }

    /// A store job again, as itself: its kind, its steps, its registration,
    /// its folder -- and the store as it was before the job first ran, with
    /// what it has made since, so the steps it finished stay ticked and the
    /// CLI, which skips what is made, is not made to look as if it repeated
    /// them (GUI 4e): a conversion's named sets. A quantization's GGUF is
    /// named again by the run that makes it -- or finds it made -- so a run
    /// again starts with its step not done (GUI 6d).
    private func rerun(_ download: Download, _ arguments: [String]) {
        startStore(
            arguments, kind: download.kind, title: download.reference, steps: download.steps,
            registerAfter: download.registerAfter, folder: download.folder, before: download.before, landed: download.landed,
            named: download.named)
    }

    /// Starts a store job: the store read first -- what is there before it
    /// runs (GUI 4e) -- unless a run of the same job read it already. A read
    /// that fails is the job's failed first step, said in the read's words,
    /// and nothing runs. A Stop pressed while the read runs has already ended
    /// the card: the read's answer is dropped, and nothing is launched
    /// (GUI 6d).
    private func startStore(
        _ arguments: [String], kind: Kind, title: String, steps: [StoreStep], registerAfter: QuantizeForm? = nil,
        folder: String?, before: Set<String>? = nil, landed: [String] = [], named: [String] = []
    ) {
        guard let binary else { return }
        nextID += 1
        let download = Download(
            id: nextID, arguments: arguments, reference: title, kind: kind, steps: steps, registerAfter: registerAfter,
            folder: folder, startedAt: now())
        download.before = before
        download.landed = landed
        download.named = named
        downloads.insert(download, at: 0)
        revision += 1
        guard download.before == nil else {
            launch(download, binary)
            return
        }
        Task {
            let read = await readStore()
            guard !download.stoppedBeforeRun else { return }
            switch read {
            case .names(let names):
                download.before = names
                launch(download, binary)
            case .failed(let why):
                download.progress.apply(.ended(.notStarted("Couldn't read the store first, so nothing ran -- apogee models list --all: \(why)")))
                download.endedAt = now()
                revision += 1
            }
        }
    }

    private func launch(_ download: Download, _ binary: URL) {
        let job = starter.startJob(binary: binary, arguments: download.arguments, streaming: false)
        download.job = job
        revision += 1
        Task { await followStore(download, job) }
    }

    /// Follows a store job: what it says shown, each set a conversion names
    /// as it stores it taken as made, the store read back as it speaks (at
    /// most every two seconds) and at its end, the registration that follows
    /// a quantization, its last word.
    private func followStore(_ download: Download, _ job: any JobRunning) async {
        for await update in job.updates {
            download.progress.apply(update)
            if case .said(let line) = update {
                if download.kind == .convert, let path = Self.storedPath(line) {
                    await name(download, path)
                }
                if now().timeIntervalSince(download.lastRead) >= 2 {
                    await readBack(download)
                }
            }
            revision += 1
        }
        download.job = nil
        await readBack(download)
        if download.progress.status == .done {
            if download.kind == .check {
                download.outcome = download.progress.notices.last
            } else if download.kind == .quantize {
                let made = await identify(download)
                if let form = download.registerAfter { await register(download, form, made) }
            }
        }
        download.endedAt = now()
        revision += 1
        finished?()
    }

    /// What a quantization made, by its own run: `models quantize` prints the
    /// GGUF it stored on a line of its own (or under "already quantized:"),
    /// and `models info <that path>` answers with its handle -- taken only
    /// when it is a GGUF in the job's own model folder. Never the store's
    /// difference: beside another job, that holds the other job's GGUF too.
    private func identify(_ download: Download) async -> Made {
        guard let path = Self.madePath(download.progress.notices) else { return .unnamed }
        guard let reader, let binary,
            let info = await reader.read(binary: binary, ["models", "info", path]).document.map(ModelInfo.init),
            let handle = info.data?.handle, info.data?.format == "gguf",
            download.folder == nil || Self.folder(of: handle) == download.folder
        else { return .unlisted(path) }
        download.made = handle
        revision += 1
        return .listed(info)
    }

    /// What a quantization's own run says it made.
    private enum Made {
        /// The set the store lists for the file it printed.
        case listed(ModelInfo)
        /// A file it printed that is no GGUF of its model the store lists.
        case unlisted(String)
        /// Its output names no file.
        case unnamed
    }

    /// The stored GGUF a quantization's output names: the first line that is
    /// a path to a `.gguf` file -- what `models quantize` prints for the
    /// file it stored, before any projector.
    static func madePath(_ said: [String]) -> String? {
        said.lazy.map { $0.trimmingCharacters(in: .whitespaces) }.first { $0.hasPrefix("/") && $0.hasSuffix(".gguf") }
    }

    /// A set a store verb names as it stores it: a line that is an absolute
    /// path alone -- a GGUF, never the projector stored beside it
    /// (`-mmproj.gguf`), or an MLX conversion's directory. `models convert`
    /// prints so each GGUF it stores, each its chained quantizations store,
    /// and each MLX directory it makes -- under "already converted:" or
    /// "already quantized:" for one it finds made (the CLI's
    /// `models_pull.cpp`); `nil` for any other line.
    static func storedPath(_ line: String) -> String? {
        let path = line.trimmingCharacters(in: .whitespaces)
        guard path.hasPrefix("/") else { return nil }
        if path.hasSuffix(".gguf") { return path.hasSuffix("-mmproj.gguf") ? nil : path }
        return path.contains("/mlx/") ? path : nil
    }

    /// Takes a set a conversion's own run named: `models info <path>` answers
    /// with its handle, taken only for a GGUF or an MLX set in the job's own
    /// model folder that is not the set it converts -- each once, in the
    /// order named (GUI 6d). A path the store does not place there ticks
    /// nothing.
    private func name(_ download: Download, _ path: String) async {
        guard let reader, let binary,
            let info = await reader.read(binary: binary, ["models", "info", path]).document.map(ModelInfo.init),
            let handle = info.data?.handle, ["gguf", "mlx"].contains(info.data?.format ?? ""),
            handle != download.reference, download.folder == nil || Self.folder(of: handle) == download.folder,
            !download.named.contains(handle)
        else { return }
        download.named.append(handle)
        revision += 1
    }

    /// Registers the GGUF a quantization made, as its own run named it: the
    /// form's `config add-backend` with that set's file. A refusal leaves the
    /// GGUF made and is said in the CLI's words.
    private func register(_ download: Download, _ form: QuantizeForm, _ made: Made) async {
        let info: ModelInfo
        switch made {
        case .listed(let listed):
            info = listed
        case .unlisted(let path):
            download.outcome = "Not registered: the store doesn't list \(path), which its run named, as a GGUF of \(download.folder ?? "its model")."
            return
        case .unnamed:
            download.outcome = "Not registered: its output names no GGUF it made."
            return
        }
        guard let reader, let binary, let path = info.field("model_path"), let arguments = form.registration(path: path) else {
            download.outcome = "Not registered: the store names no file for \(download.made ?? "what it made")."
            return
        }
        let outcome = await reader.command(binary: binary, arguments)
        if let failure = outcome.failure {
            download.outcome = "The GGUF is made; registering it was refused: \(failure)"
        } else {
            download.registered = true
            download.registeredAfter = arguments[2]
            if case .done(let stdout) = outcome {
                download.outcome = stdout.split(whereSeparator: \.isNewline).first.map(String.init)
            }
            await readBack(download)
        }
    }

    /// The store's names, as `models list --all` gives them, or why not.
    private enum StoreRead {
        case names(Set<String>)
        case failed(String)
    }

    private func readStore() async -> StoreRead {
        guard let reader, let binary else { return .failed("nothing here reads it") }
        let outcome = await reader.read(binary: binary, ["models", "list", "--all"])
        guard let document = outcome.document else { return .failed(outcome.failure ?? "no document") }
        return .names(Set(ModelRow.list(from: document).map(\.name)))
    }

    private func readBack(_ download: Download) async {
        download.lastRead = now()
        guard case .names(let names) = await readStore() else { return }
        // Only its own work counts: a set in its own model folder, a backend
        // its own run registered. Another job's, landing meanwhile, is not
        // this one's.
        let own = download.ownBackends
        let new = names.subtracting(download.before ?? []).filter { name in
            guard let landed = Self.folder(of: name) else { return own.contains(name) }
            return download.folder == nil || landed == download.folder
        }
        // Kept in the order they landed: what is new now after what came before.
        download.landed = download.landed.filter(new.contains) + new.subtracting(download.landed).sorted()
        revision += 1
    }

    /// How long a job has run, or ran: the app's own clock, said plainly.
    public func elapsed(_ download: Download) -> String {
        let seconds = max(0, Int((download.endedAt ?? now()).timeIntervalSince(download.startedAt)))
        return seconds < 60 ? "\(seconds) s" : "\(seconds / 60) min \(seconds % 60) s"
    }

    // MARK: - Stopping

    /// What a store job stopped before its child existed says.
    public static let stoppedBeforeRun = "Stopped before it ran: nothing ran"

    /// Stops a run as Ctrl-C does; what it finished stands. A store job still
    /// reading the store before its child starts -- the card shows Stop then
    /// too -- ends stopped at once, saying nothing ran, and the read's answer
    /// starts nothing (GUI 6d): Stop always ends a job (GUI 4c).
    public func stop(_ download: Download) async {
        if let job = download.job {
            await job.stop(grace: .seconds(10))
            return
        }
        guard download.isRunning, download.kind != .pull else { return }
        download.stoppedBeforeRun = true
        download.progress.apply(.ended(.stoppedBeforeStart(Self.stoppedBeforeRun)))
        download.outcome = Self.stoppedBeforeRun
        download.endedAt = now()
        revision += 1
    }

    /// Takes a finished download off the list -- one stopped before it ran
    /// among them, whose store read may still be out: it starts nothing.
    public func dismiss(_ download: Download) {
        guard !download.isRunning else { return }
        downloads.removeAll { $0.id == download.id }
        revision += 1
    }

    /// Whether any pull is running: the Models place's dot.
    public var running: Int { downloads.filter(\.isRunning).count }

    /// What a download's step line says, in a few words: the stream's stage,
    /// or -- without it -- the busy line's words for the step, "working"
    /// when they name none (the fallback's honest degrade).
    public static func now(_ progress: JobProgress) -> String {
        if let stage = progress.orderedStages.last(where: { $0.stage.state == "started" }) {
            return "\(stage.index) of \(progress.stageCount ?? stage.index): \(stage.stage.label)"
        }
        guard let busy = progress.busy?.lowercased() else { return "working" }
        for (word, said) in [("download", "downloading"), ("pull", "downloading"), ("convert", "converting"),
            ("quantiz", "quantizing"), ("regist", "registering"), ("hash", "checking"), ("verif", "checking")]
        where busy.contains(word) {
            return said
        }
        return "working"
    }
}
