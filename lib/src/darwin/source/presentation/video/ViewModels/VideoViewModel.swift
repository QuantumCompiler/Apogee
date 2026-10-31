import BusinessJobs
import BusinessMedia
import BusinessReads
import DataPreferences
import Foundation
import Observation

/// The Video place's clips: the library -- the records in `~/Movies/Apogee`
/// and the folders the person adds -- a clip's page, the generate sheet, the
/// run supervised, and the one-time install consent.
///
/// SwiftUI-free (ADR 0002). The library is records, not a database: re-read
/// on coming into view and after every run. Every run is the command a person
/// would type; the runtime is installed only on an explicit yes, and the
/// install is a supervised run of the place's like any other (GUI 4f): its
/// card, Stop, its failure said, the refused clip run again once it is in.
@MainActor
@Observable
public final class VideoViewModel {
    public enum Tab: String, CaseIterable, Identifiable, Sendable {
        case clips
        case compositions

        public var id: String { rawValue }
        public var title: String { self == .clips ? "Clips" : "Compositions" }
    }

    /// What the sheet's model picker holds: the default the CLI falls back
    /// to (`models.default_video`), one of the store's video models, or
    /// another the person names.
    public enum ModelChoice: Hashable, Sendable {
        case byDefault
        case model(String)
        case another
    }

    public var tab: Tab = .clips
    public private(set) var clips: [ClipRecord] = []
    public private(set) var skipped: [MediaLibrary.Skipped] = []
    public private(set) var selected: String?
    public var sheetOpen = false
    public var form = ClipForm()
    /// The run going now, and the one before it until dismissed.
    public private(set) var run: JobProgress?
    public private(set) var runArguments: [String]?
    /// The install a refused run named, waiting on the person's yes.
    public private(set) var consent: InstallConsent?
    /// The install the person said yes to, as it runs and, ended, until
    /// dismissed: what the clips list draws as its card.
    public private(set) var installing: RuntimeInstall?
    /// Whether an install another place started runs now (the composition
    /// reads the children registry): a second `train setup` into the one
    /// environment is never offered beside it.
    public var installElsewhere: @MainActor () -> Bool = { false }
    public var say: (@MainActor (String) -> Void)?
    /// Whether the build streams a run's steps (the CLI's 52b).
    public var streams: @MainActor () -> Bool = { false }
    /// Reads the store's video models: the composition's runner.
    public var reader: (any CommandRunning)?
    /// The store's video models, as `models list` states them, in the order
    /// the sheet offers them (the CLI's 53l): each pipeline on PyTorch, each
    /// MLX video model on MLX. Empty before the read, and where the store
    /// holds none -- then the sheet names a pipeline as typed, as before.
    public private(set) var models: [MediaModel] = []
    /// `models.default_video`, as `config get` prints it; `nil` when unset.
    public private(set) var defaultModel: String?
    /// The sheet's model, as picked.
    public private(set) var choice: ModelChoice = .byDefault

    private let starter: any JobStarting
    private let preferences: Preferences
    private let home: URL
    private var binary: URL?
    private var job: (any JobRunning)?
    private var installJob: (any JobRunning)?
    /// The refused run, run again once its install is in -- and only then.
    private var retryAfterInstall: [String]?

    public init(starter: any JobStarting, preferences: Preferences, home: URL) {
        self.starter = starter
        self.preferences = preferences
        self.home = home
    }

    public func connect(binary: URL?) {
        self.binary = binary
        refresh()
        Task { await readModels() }
    }

    // MARK: - The library

    /// `~/Movies/Apogee`: where clips are made, and the first folder read.
    public var outputFolder: URL { home.appendingPathComponent("Movies/Apogee") }

    /// Every folder the library reads.
    public var folders: [URL] { [outputFolder] + preferences.videoFolders.map { URL(fileURLWithPath: $0) } }

    public func appeared() {
        refresh()
        Task { await readModels() }
    }

    /// The store's video models (`models list`) and the default
    /// (`config get models.default_video`): what the sheet offers, and the
    /// engine each runs on.
    public func readModels() async {
        guard let reader, let binary else { return }
        if let list = await reader.read(binary: binary, ["models", "list"]).document {
            models = MediaModel.videoModels(in: list)
        }
        if case .done(let text) = await reader.command(binary: binary, ["config", "get", "models.default_video"]) {
            let value = text.trimmingCharacters(in: .whitespacesAndNewlines)
            defaultModel = value.isEmpty ? nil : value
        }
        // A model picked that the store no longer holds goes back to the default.
        if case .model(let handle) = choice, !models.contains(where: { $0.handle == handle }) { choose(.byDefault) }
    }

    /// The records, read again from the folders.
    public func refresh() {
        let scan = MediaLibrary.clips(in: folders)
        clips = scan.records
        skipped = scan.skipped
        if let selected, !clips.contains(where: { $0.output == selected }) { self.selected = nil }
    }

    /// A folder the person adds to the library.
    public func add(folder: URL) {
        var kept = preferences.videoFolders
        guard folder != outputFolder, !kept.contains(folder.path) else { return }
        kept.append(folder.path)
        preferences.videoFolders = kept
        refresh()
    }

    public func remove(folder: URL) {
        preferences.videoFolders = preferences.videoFolders.filter { $0 != folder.path }
        refresh()
    }

    public func select(_ clip: ClipRecord) {
        selected = clip.output
    }

    public var selectedClip: ClipRecord? { selected.flatMap { s in clips.first { $0.output == s } } }

    /// Where a clip came from, back to the first.
    public func chain(_ clip: ClipRecord) -> [ClipRecord] { MediaLibrary.chain(of: clip, in: clips) }

    // MARK: - Making

    /// The sheet, fresh, or from a clip: run again or continue it.
    public func newClip(from clip: ClipRecord? = nil, extend: Bool = false) {
        form = clip.map { ClipForm.from($0, extend: extend) } ?? ClipForm()
        choice = .byDefault
        sheetOpen = true
    }

    /// Picks the sheet's model: the default (nothing passed), one of the
    /// store's by its handle, or another typed by the person. Where the
    /// picked model's folder holds a pipeline beside its MLX conversion, the
    /// engine is the person's choice and passed as `--engine`; anywhere else
    /// the directory decides and the sheet only says it.
    public func choose(_ choice: ModelChoice) {
        self.choice = choice
        form.engine = nil
        switch choice {
        case .byDefault, .another:
            form.model = ""
        case .model(let handle):
            form.model = handle
            if let model = models.first(where: { $0.handle == handle }), MediaModel.choosesEngine(model, among: models) {
                form.engine = model.engine
            }
        }
    }

    /// How the picker names a model among the store's.
    public func label(_ model: MediaModel) -> String { model.label(among: models) }

    /// The sheet's engine line: what the clip will run on, as the model's
    /// directory says -- the CLI decides, the sheet says it. `nil` where
    /// nothing is known to say: no video model read, or a record run again
    /// that states no engine.
    public var engineLine: String? {
        switch form.start {
        case .again(let record), .extend(let record):
            guard let ran = clips.first(where: { $0.output == record || ($0.sidecar ?? $0.output + ".json") == record }),
                let engine = ran.recordedEngine
            else { return nil }
            return "Runs on \(engine.name), as its record did: its own model, \(ran.model)."
        case .new:
            break
        }
        switch choice {
        case .byDefault:
            guard let name = defaultModel else {
                return models.isEmpty
                    ? nil
                    : "No default video model is set, so the CLI refuses a clip that names none: pick one."
            }
            guard let model = MediaModel.named(name, in: models) else {
                return "The default, \(name), runs on what its directory is."
            }
            return "The default, \(model.handle), runs on " + Self.runsOn(model.engine) + "."
        case .model(let handle):
            guard let model = models.first(where: { $0.handle == handle }) else { return nil }
            var line = "Runs on " + Self.runsOn(model.engine) + "."
            if let engine = form.engine {
                line += " \(model.folder) holds a pipeline and its MLX conversion, so the engine is yours: --engine \(engine.word)."
            }
            return line
        case .another:
            return "Runs on what its directory is: PyTorch for a diffusers pipeline, MLX for an MLX video model."
        }
    }

    /// An engine and what runs it on there, in the CLI's words (`on MLX
    /// (mlx-video)`).
    static func runsOn(_ engine: MediaEngine) -> String {
        switch engine {
        case .torch: "PyTorch, through diffusers: a diffusers pipeline"
        case .mlx: "MLX, through mlx-video: an MLX video model"
        case .other(let word): word
        }
    }

    /// Where the sheet's clip will be written: a free name in `~/Movies/Apogee`.
    public var plannedOutput: URL {
        var base = MediaLibrary.stem(form.prompt)
        if base.isEmpty {
            switch form.start {
            case .again(let record), .extend(let record):
                base = ((record as NSString).lastPathComponent as NSString).deletingPathExtension
                base = (base as NSString).deletingPathExtension
            case .new:
                base = "clip"
            }
        }
        return MediaLibrary.freePath(in: outputFolder, base: base, ext: "mp4")
    }

    /// The sheet's command, as typed.
    public var plannedArguments: [String]? { form.arguments(output: plannedOutput.path) }

    /// Whether the place runs something now: a clip, or the install one
    /// needs.
    public var isRunning: Bool { job != nil || installJob != nil }

    /// Whether the install the person said yes to runs now.
    public var isInstalling: Bool { installJob != nil }

    /// Generates: the sheet's command, supervised -- never beside an install,
    /// whose runtime the clip may need.
    public func generate() {
        guard let arguments = plannedArguments else { return }
        if let installing, isInstalling {
            say?("Installing first: the clip can be made once \(installing.command) has finished")
            return
        }
        try? FileManager.default.createDirectory(at: outputFolder, withIntermediateDirectories: true)
        sheetOpen = false
        start(arguments)
    }

    private func start(_ arguments: [String]) {
        guard let binary, job == nil, installJob == nil else { return }
        let streaming = streams()
        let full = streaming ? Argv.adding(["--output-format", "stream-json"], to: arguments) : arguments
        runArguments = full
        run = JobProgress()
        let job = starter.startJob(binary: binary, arguments: full, streaming: streaming)
        self.job = job
        Task {
            for await update in job.updates { run?.apply(update) }
            self.job = nil
            refresh()
            finished(arguments)
        }
    }

    private func finished(_ arguments: [String]) {
        guard let run else { return }
        switch run.status {
        case .done:
            if let output = arguments.firstIndex(of: "-o").map({ arguments[$0 + 1] }) {
                selected = output
                say?("Made \((output as NSString).lastPathComponent)")
            }
        case .stopped:
            say?("Stopped: nothing was written")
        case .failed(let message):
            // A run refused because the runtime is not installed names the
            // command that installs it -- in its stream's error since the
            // CLI's M29, on stderr alone before it: the app asks, never assumes.
            guard let asked = InstallConsent(error: run.error, failure: message, stderr: run.stderr) else { break }
            if installElsewhere() {
                say?("\(asked.command) is already running: generate the clip again once it has finished")
            } else {
                consent = asked
                retryAfterInstall = arguments
            }
        case .running:
            break
        }
    }

    /// `train setup --with video` (or `voice`), when the CLI's words name it.
    public static func installCommand(in text: String) -> [String]? {
        MediaLibrary.installCommand(in: text)
    }

    /// Stops what runs as Ctrl-C does: the clip -- nothing is written, the
    /// output untouched -- or the install, which then runs nothing again.
    public func stop() async {
        if let job {
            await job.stop(grace: .seconds(10))
        } else {
            await installJob?.stop(grace: .seconds(10))
        }
    }

    public func dismissRun() {
        guard job == nil else { return }
        run = nil
        runArguments = nil
    }

    // MARK: - Consent and the install

    /// The person said yes: the install, supervised -- its card, Stop -- and
    /// once it is in, what was refused run again. A failed or stopped install
    /// is said, and runs nothing again.
    public func install() {
        guard let consent, let binary, job == nil, installJob == nil else { return }
        self.consent = nil
        let retry = retryAfterInstall
        retryAfterInstall = nil
        installing = RuntimeInstall(arguments: consent.arguments)
        let job = starter.startJob(binary: binary, arguments: consent.arguments, streaming: false)
        installJob = job
        Task {
            // Its own fold, drawn as it moves: what it ends as is acted on
            // even when its card was put away the moment its end was said.
            var installing = RuntimeInstall(arguments: consent.arguments)
            for await update in job.updates {
                installing.apply(update)
                self.installing = installing
            }
            installJob = nil
            switch installing.phase {
            case .installed:
                say?("Installed: \(installing.command)" + (retry == nil ? "" : " -- making the clip again"))
                if let retry { start(retry) }
            case .failed:
                // An install its environment's Python cannot take names the
                // rebuild that can (the CLI's M19): asked next, the refused
                // clip kept to follow it.
                if let next = InstallConsent(afterFailed: consent.arguments, failure: installing.failure, stderr: installing.progress.stderr) {
                    self.installing = nil
                    self.consent = next
                    retryAfterInstall = retry
                } else {
                    say?("Not installed: \(installing.failure ?? installing.command)")
                }
            case .stopped:
                say?("The install was stopped before it finished: \(installing.command) runs it again")
            case .running:
                break
            }
        }
    }

    /// The person said no: nothing is installed, and the run stays refused.
    public func decline() {
        defer {
            consent = nil
            retryAfterInstall = nil
        }
        say?("Not installed. \(consent?.command ?? "apogee train setup") installs it whenever you want it")
    }

    /// The ended install's card, put away -- once its own end is said; a
    /// running one stays.
    public func dismissInstall() {
        guard installing?.phase != .running else { return }
        installing = nil
    }
}
