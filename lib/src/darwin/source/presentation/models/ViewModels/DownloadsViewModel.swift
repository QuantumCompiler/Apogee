import BusinessJobs
import Foundation
import Observation

/// Downloads: a pull run as the supervised job it is -- its steps, its bytes,
/// Stop -- and the stopped step run again by the command the CLI names for
/// it. Each finished pull is read back into the library.
///
/// SwiftUI-free (ADR 0002). Progress is the 52b stream where the build has
/// it; on a build before it, a busy line sampled from stderr -- shown, never
/// decided on (the design's G9 fallback).
@MainActor
@Observable
public final class DownloadsViewModel {
    /// One run, and what it has said.
    public final class Download: Identifiable {
        public let id: Int
        /// What a person would type after `apogee`.
        public let arguments: [String]
        public let reference: String
        public fileprivate(set) var progress = JobProgress()
        fileprivate var job: (any JobRunning)?

        init(id: Int, arguments: [String], reference: String) {
            self.id = id
            self.arguments = arguments
            self.reference = reference
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
