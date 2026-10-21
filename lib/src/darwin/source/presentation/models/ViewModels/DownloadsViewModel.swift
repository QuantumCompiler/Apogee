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
/// face yet (G18, G20): their steps tick as the sets they make land in
/// `models list --all`, read again as the verb speaks and at its end.
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
        /// --all` gives each: a set's handle, or a backend's name.
        public fileprivate(set) var landed: [String] = []
        public fileprivate(set) var endedAt: Date?
        /// The last word on it: a check's result, or the registration that
        /// followed a quantization -- in the CLI's words.
        public fileprivate(set) var outcome: String?
        /// The registration that follows a quantization, when asked.
        let registerAfter: QuantizeForm?
        fileprivate var job: (any JobRunning)?
        fileprivate var before: Set<String> = []
        fileprivate var lastRead = Date.distantPast
        fileprivate var registered = false

        init(
            id: Int, arguments: [String], reference: String, kind: Kind = .pull, steps: [StoreStep] = [],
            registerAfter: QuantizeForm? = nil, startedAt: Date = Date()
        ) {
            self.id = id
            self.arguments = arguments
            self.reference = reference
            self.kind = kind
            self.steps = steps
            self.registerAfter = registerAfter
            self.startedAt = startedAt
        }

        /// Where step `index` stands: a step that makes a set is done once as
        /// many sets have landed; the registration once the run ended well.
        public func state(of index: Int) -> StepState {
            if isDone(index) { return .done }
            return isRunning && steps.indices.first(where: { !isDone($0) }) == index ? .running : .waiting
        }

        private func isDone(_ index: Int) -> Bool {
            guard steps[index].makesSet else { return progress.status == .done && (registerAfter == nil || registered) }
            return steps[..<index].filter(\.makesSet).count < landed.count
        }

        /// The first set it made, for Show the weights: a handle where one
        /// landed, else a backend's name.
        public var firstLanded: String? { landed.first { $0.contains("/") } ?? landed.first }

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

    /// Runs the command the CLI named for carrying a stopped or failed pull
    /// on -- shown on its button before it runs.
    public func resume(_ download: Download) {
        guard !download.isRunning, let command = download.progress.resumeCommand else { return }
        run(command, reference: download.reference)
    }

    /// Runs the same pull again: a stopped download's finished steps stand,
    /// and the CLI skips what is already stored.
    public func again(_ download: Download) {
        guard !download.isRunning else { return }
        run(download.arguments.filter { $0 != "--output-format" && $0 != "stream-json" }, reference: download.reference)
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
        revision += 1
        finished?()
    }

    // MARK: - The store's long runs

    /// `models convert …`, the convert sheet's command as typed.
    public func convert(_ form: ConvertForm) {
        startStore(form.arguments, kind: .convert, title: form.handle, steps: form.steps)
    }

    /// `models quantize …`, then -- asked -- the `config add-backend` that
    /// registers what it made.
    public func quantize(_ form: QuantizeForm) {
        startStore(form.arguments, kind: .quantize, title: form.handle, steps: form.steps, registerAfter: form.register ? form : nil)
    }

    /// `models repair <name>`: the files checked against their record, as a job.
    public func check(_ name: String) {
        startStore(["models", "repair", name], kind: .check, title: name, steps: [])
    }

    private func startStore(_ arguments: [String], kind: Kind, title: String, steps: [StoreStep], registerAfter: QuantizeForm? = nil) {
        guard let binary else { return }
        nextID += 1
        let download = Download(
            id: nextID, arguments: arguments, reference: title, kind: kind, steps: steps, registerAfter: registerAfter,
            startedAt: now())
        downloads.insert(download, at: 0)
        revision += 1
        Task {
            download.before = await storeNames() ?? []
            let job = starter.startJob(binary: binary, arguments: arguments, streaming: false)
            download.job = job
            revision += 1
            await followStore(download, job)
        }
    }

    /// Follows a store job: what it says shown, the store read back as it
    /// speaks (at most every two seconds) and at its end, the registration
    /// that follows a quantization, its last word.
    private func followStore(_ download: Download, _ job: any JobRunning) async {
        for await update in job.updates {
            download.progress.apply(update)
            if case .said = update, now().timeIntervalSince(download.lastRead) >= 2 {
                await readBack(download)
            }
            revision += 1
        }
        download.job = nil
        await readBack(download)
        if download.progress.status == .done {
            if download.kind == .check {
                download.outcome = download.progress.notices.last
            } else if let form = download.registerAfter {
                await register(download, form)
            }
        }
        download.endedAt = now()
        revision += 1
        finished?()
    }

    /// Registers the GGUF a quantization made: its file from `models info`
    /// of the set that landed, then the form's `config add-backend`. A
    /// refusal leaves the GGUF made and is said in the CLI's words.
    private func register(_ download: Download, _ form: QuantizeForm) async {
        guard let reader, let binary,
            let handle = download.landed.first(where: { $0.contains("/gguf/") }),
            let info = await reader.read(binary: binary, ["models", "info", handle]).document.map(ModelInfo.init),
            let path = info.field("model_path"), let arguments = form.registration(path: path)
        else {
            download.outcome = "Not registered: the new GGUF was not found in the store's read-back."
            return
        }
        let outcome = await reader.command(binary: binary, arguments)
        if let failure = outcome.failure {
            download.outcome = "The GGUF is made; registering it was refused: \(failure)"
        } else {
            download.registered = true
            if case .done(let stdout) = outcome {
                download.outcome = stdout.split(whereSeparator: \.isNewline).first.map(String.init)
            }
            await readBack(download)
        }
    }

    private func storeNames() async -> Set<String>? {
        guard let reader, let binary, let document = await reader.read(binary: binary, ["models", "list", "--all"]).document else {
            return nil
        }
        return Set(ModelRow.list(from: document).map(\.name))
    }

    private func readBack(_ download: Download) async {
        download.lastRead = now()
        guard let names = await storeNames() else { return }
        let new = names.subtracting(download.before)
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

    /// Stops a pull as Ctrl-C does; what it finished stands.
    public func stop(_ download: Download) async {
        await download.job?.stop(grace: .seconds(10))
    }

    /// Takes a finished download off the list.
    public func dismiss(_ download: Download) {
        guard !download.isRunning else { return }
        downloads.removeAll { $0.id == download.id }
        revision += 1
    }

    /// The running pulls' children, for the monitor.
    public var children: [(label: String, pid: Int32)] {
        downloads.compactMap { download in
            download.job?.processIdentifier.map { ("Download: \(download.reference)", $0) }
        }
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
