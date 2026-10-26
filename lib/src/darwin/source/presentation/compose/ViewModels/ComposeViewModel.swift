import BusinessJobs
import BusinessMedia
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Observation

/// Compositions: a brief made into a narrated video, scene by scene, stopped
/// and resumed truthfully, and edited by instruction with its diff.
///
/// SwiftUI-free (ADR 0002). The project document is the progress: while a
/// composition renders, the screen re-reads the document the composer saves
/// after every step -- never an accounting of its own. Every run is the
/// person's command; stopping keeps the finished scenes, and the same command
/// again renders only what the project lacks.
///
/// Since GUI 4f every refusal has its door here, as on its siblings': the
/// voice model is named where the store holds two or more, and a runtime
/// not installed is asked about and installed under supervision, then the
/// refused composition run again. A plan says it was planned. The edit's
/// preview is a run of the place's -- Stop reaches it, a time limit bounds
/// it, nothing renders beside it. The scan's passed-over projects are
/// listed, and the sheet says before the run whether the captions burn in.
@MainActor
@Observable
public final class ComposeViewModel {
    /// What the sheet is doing.
    public enum Sheet: Equatable, Sendable {
        case new
        case edit(ProjectRecord)
    }

    public private(set) var projects: [ProjectRecord] = []
    /// The documents the scan passed over, and why: not JSON, or a project
    /// whose video is not where it says -- listed as Clips and Voice list
    /// theirs.
    public private(set) var skipped: [MediaLibrary.Skipped] = []
    public private(set) var selected: String?
    public var sheet: Sheet?
    public var form = ComposeForm()
    /// The edit's preview: the edited project a dry run printed, and its diff.
    public private(set) var preview: ProjectRecord?
    /// How long the edit's dry run is given before it is stopped: one
    /// planning call, a planner that never answers bounded and said.
    public let previewLimit: Duration
    /// The run going now (its output), its progress.
    public private(set) var running: String?
    public private(set) var run: JobProgress?
    public private(set) var failure: String?
    public var say: (@MainActor (String) -> Void)?
    /// Whether the build streams a run's events (52b), and whether it plans a
    /// new brief alone (52c's dry run) -- the last session's announcement.
    public var streams: @MainActor () -> Bool = { false }
    public var plansAlone: @MainActor () -> Bool = { false }
    /// The library's folders: where compositions are written, and read.
    public var folders: @MainActor () -> [URL] = { [] }
    /// Reads the store's voice models and `check`: the composition's runner.
    public var reader: (any CommandRunning)?

    /// The store's voice models, read as the say sheet reads them, in the
    /// order it offers them (PyTorch first); the new sheet names one -- with
    /// `--voice-model` -- only where there are two or more, which the CLI
    /// refuses to choose between (the Voice place's rule).
    public private(set) var voiceModels: [MediaModel] = []
    /// The voice model picked, by its store handle.
    public private(set) var voiceModel: String?
    /// Whether this machine's ffmpeg burns the captions in, as `check` says;
    /// `nil` before `check` has answered, or where it has no such row.
    public private(set) var captions: CaptionsAbility?
    /// Whether `check` has answered since the sheet opened.
    public private(set) var checkRead = false

    /// The install a refused composition named, waiting on the person's yes.
    public private(set) var consent: InstallConsent?
    /// The install the person said yes to, as it runs and, ended, until
    /// dismissed: what the list draws as its card.
    public private(set) var installing: RuntimeInstall?
    /// Whether an install another place started runs now (the composition
    /// reads the children registry): a second `train setup` into the one
    /// environment is never offered beside it.
    public var installElsewhere: @MainActor () -> Bool = { false }

    private let starter: any JobStarting
    private let pollInterval: Duration
    private var binary: URL?
    private var job: (any JobRunning)?
    private var previewJob: (any JobRunning)?
    private var previewTimedOut = false
    private var installJob: (any JobRunning)?
    /// The arguments of each composition's last run, to resume it exactly.
    private var commands: [String: [String]] = [:]

    /// A refused composition, run again once its install is in -- and only then.
    private struct Retry {
        let arguments: [String]
        let output: String
        let resumable: [String]
    }

    private var retryAfterInstall: Retry?

    public init(starter: any JobStarting, pollInterval: Duration = .seconds(1), previewLimit: Duration = .seconds(300)) {
        self.starter = starter
        self.pollInterval = pollInterval
        self.previewLimit = previewLimit
    }

    public func connect(binary: URL?) {
        self.binary = binary
        refresh()
    }

    public func appeared() {
        refresh()
    }

    /// The projects, read again from the folders, and those passed over.
    public func refresh() {
        let scan = MediaLibrary.projects(in: folders())
        projects = scan.records
        skipped = scan.skipped
    }

    /// What the new sheet needs of the store: its voice models (`models
    /// list`) and whether its ffmpeg burns captions (`check`'s `ffmpeg
    /// captions` row, the CLI's 52e). Read each time the sheet opens -- and
    /// only then, so no `check` runs for a place merely shown.
    public func readStore() async {
        guard let reader, let binary else { return }
        if let read = await MediaModel.readVoiceModels(reader, binary: binary) {
            voiceModels = read
            if voiceModel == nil || !read.contains(where: { $0.handle == voiceModel }) { voiceModel = read.first?.handle }
        }
        if let document = await reader.report(binary: binary, ["check"]).document {
            captions = CaptionsAbility(check: document.value)
            checkRead = true
        }
    }

    public func select(_ project: ProjectRecord) {
        selected = project.output
    }

    public var selectedProject: ProjectRecord? {
        selected.flatMap { s in projects.first { $0.output == s } }
    }

    /// The project an edit came from, when it is in the library.
    public func parent(of project: ProjectRecord) -> ProjectRecord? {
        project.parent.flatMap { p in projects.first { $0.output == p || $0.output + ".json" == p } }
    }

    // MARK: - Making

    private var outputFolder: URL? { folders().first }

    public func newComposition() {
        form = ComposeForm()
        preview = nil
        sheet = .new
        Task { await readStore() }
    }

    /// Picks the voice model the composition narrates with, by its handle.
    public func choose(voiceModel handle: String) {
        guard voiceModels.contains(where: { $0.handle == handle }) else { return }
        voiceModel = handle
    }

    /// How the picker names a voice model among the store's.
    public func label(_ model: MediaModel) -> String { model.label(among: voiceModels) }

    /// Whether the new sheet names a voice model: the store holds two or more.
    public var namesVoiceModel: Bool { voiceModels.count > 1 }

    /// Where the sheet's composition is written: a free name in the first
    /// folder -- free of an `.srt` too while it has captions, which the
    /// composer keeps beside the video and never writes over (the CLI's 55c).
    public var plannedOutput: URL? {
        guard let folder = outputFolder else { return nil }
        let base: String
        switch sheet {
        case .edit(let project):
            base = (project.name as NSString).deletingPathExtension + "-edit"
        default:
            base = MediaLibrary.stem(form.brief).isEmpty ? "composition" : MediaLibrary.stem(form.brief)
        }
        return MediaLibrary.freePath(in: folder, base: base, ext: "mp4", besides: form.captions ? ["srt"] : [])
    }

    public var plannedArguments: [String]? {
        plannedOutput.flatMap { formed.arguments(output: $0.path) }
    }

    /// The form as it runs: the voice model picked, where one is named.
    private var formed: ComposeForm {
        var form = form
        if namesVoiceModel, let voiceModel { form.voiceModel = voiceModel }
        return form
    }

    /// What the new sheet says of the captions before the run, from
    /// `check`'s row: burned in, or kept beside the video as an `.srt` -- the
    /// CLI decides, the sheet says it. `nil` with captions off, or before
    /// `check` has answered.
    public var captionsLine: String? {
        guard form.captions, checkRead else { return nil }
        guard let captions else {
            return "This apogee's check doesn't say whether its ffmpeg burns captions in: the composition's page says once it renders."
        }
        if captions.burns { return "Captions will be burned into the frames: this ffmpeg has libass." }
        let srt = plannedOutput.map { $0.deletingPathExtension().appendingPathExtension("srt").lastPathComponent } ?? "an .srt"
        return "Captions will be kept beside the video as \(srt), not burned in: \(captions.reason)."
    }

    /// What brings the ability, in `check`'s words, while the captions would
    /// be kept rather than burned in.
    public var captionsRemedy: String? {
        guard form.captions, let captions, !captions.burns else { return nil }
        return captions.remedy
    }

    /// Whether the place runs something now: a composition, the edit's
    /// preview, or the install one needs.
    public var isRunning: Bool { job != nil || previewJob != nil || installJob != nil }

    /// Whether the install the person said yes to runs now.
    public var isInstalling: Bool { installJob != nil }

    /// Whether the edit's preview -- its dry run -- runs now.
    public var previewing: Bool { previewJob != nil }

    /// Composes the sheet's brief: planned and rendered in one run with
    /// Stop -- or, where the build plans alone (52c), planned first and shown,
    /// then rendered by the same command.
    public func compose(planFirst: Bool = false) {
        guard let arguments = plannedArguments, let output = plannedOutput?.path else { return }
        guard !waitsForTheInstall() else { return }
        if let folder = outputFolder { try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true) }
        sheet = nil
        start(planFirst && plansAlone() ? Argv.adding(["--dry-run"], to: arguments) : arguments, output: output, resumable: arguments)
    }

    /// Renders a planned composition, or resumes a stopped one: its last
    /// command again, which renders only what the project lacks.
    public func resume(_ project: ProjectRecord) {
        guard !waitsForTheInstall() else { return }
        let arguments = commands[project.output] ?? Self.resumeArguments(project)
        start(arguments, output: project.output, resumable: arguments)
    }

    /// For a project whose command the app did not run: its brief and output
    /// -- the composer reads the rest from the project it keeps. The brief
    /// after `--`, as a new composition's (`Argv`).
    static func resumeArguments(_ project: ProjectRecord) -> [String] {
        ["video", "compose", "-o", project.output] + Argv.text(project.brief)
    }

    /// Whether an install runs, said: nothing composes beside it.
    private func waitsForTheInstall() -> Bool {
        guard let installing, isInstalling else { return false }
        say?("Installing first: compose once \(installing.command) has finished")
        return true
    }

    private func start(_ arguments: [String], output: String, resumable: [String]) {
        guard let binary, job == nil, previewJob == nil, installJob == nil else { return }
        let streaming = streams()
        // A brief may read like a flag; only an option plans.
        let planning = Argv.options(of: arguments).contains("--dry-run")
        let full = streaming ? Argv.adding(["--output-format", "stream-json"], to: arguments) : arguments
        commands[output] = resumable
        running = output
        selected = output
        run = JobProgress()
        failure = nil
        let job = starter.startJob(binary: binary, arguments: full, streaming: streaming)
        self.job = job
        Task {
            for await update in job.updates { run?.apply(update) }
            self.job = nil
            running = nil
            refresh()
            let name = (output as NSString).lastPathComponent
            switch run?.status {
            case .done:
                // A dry run plans on paper: its project is planned, never
                // composed, and Render it on its page renders the plan.
                say?(planning ? "Planned \(name): nothing is rendered yet -- Render it, on its page, renders the plan" : "Composed \(name)")
            case .stopped:
                say?(planning ? "Stopped: nothing was planned" : "Stopped: the finished scenes are kept")
            case .failed(let message):
                refused(message, Retry(arguments: arguments, output: output, resumable: resumable))
            default:
                break
            }
        }
        // The project document is the progress: read it as the composer saves it.
        Task {
            while self.running == output {
                if let project = ProjectRecord.beside(output) {
                    if let index = projects.firstIndex(where: { $0.output == output }) {
                        projects[index] = project
                    } else {
                        projects.insert(project, at: 0)
                    }
                }
                try? await Task.sleep(for: pollInterval)
            }
        }
    }

    /// A failed run: a runtime not installed -- named on stderr alone, its
    /// stream silent -- is asked about, never assumed; anything else is the
    /// alert, in the CLI's words.
    private func refused(_ message: String, _ retry: Retry) {
        guard let asked = InstallConsent(failure: message, stderr: run?.stderr ?? "") else {
            failure = message
            return
        }
        if installElsewhere() {
            say?("\(asked.command) is already running: compose again once it has finished")
        } else {
            consent = asked
            retryAfterInstall = retry
        }
    }

    /// Stops what runs as Ctrl-C does: a composition -- every finished scene
    /// stays -- the edit's preview, or the install, which then runs nothing
    /// again.
    public func stop() async {
        if let job {
            await job.stop(grace: .seconds(10))
        } else if let previewJob {
            await previewJob.stop(grace: .seconds(10))
        } else {
            await installJob?.stop(grace: .seconds(10))
        }
    }

    // MARK: - Consent and the install

    /// The person said yes: the install, supervised -- its card, Stop -- and
    /// once it is in, the refused composition run again; refused then for
    /// the other runtime, it is asked about in turn. A failed or stopped
    /// install is said, and runs nothing again.
    public func install() {
        guard let consent, let binary, !isRunning else { return }
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
                say?("Installed: \(installing.command)" + (retry == nil ? "" : " -- composing again"))
                if let retry { start(retry.arguments, output: retry.output, resumable: retry.resumable) }
            case .failed:
                say?("Not installed: \(installing.failure ?? installing.command)")
            case .stopped:
                say?("The install was stopped before it finished: \(installing.command) runs it again")
            case .running:
                break
            }
        }
    }

    /// The person said no: nothing is installed, and the composition stays
    /// refused.
    public func decline() {
        let command = consent?.command ?? "apogee train setup"
        consent = nil
        retryAfterInstall = nil
        say?("Not installed. \(command) installs it whenever you want it")
    }

    /// The ended install's card, put away -- once its own end is said; a
    /// running one stays.
    public func dismissInstall() {
        guard installing?.phase != .running else { return }
        installing = nil
    }

    // MARK: - Editing

    public func edit(_ project: ProjectRecord) {
        form = ComposeForm()
        form.from = project.output
        preview = nil
        sheet = .edit(project)
    }

    /// The edit planned on paper: `--dry-run`, its project printed as JSON --
    /// the diff from its own comparison (52c), or the documents compared.
    /// A run of the place's: never beside a composition or an install, Stop
    /// reaches it, and a planner that has not answered within `previewLimit`
    /// is stopped and said.
    public func previewEdit() async {
        guard let binary, !isRunning, let arguments = form.arguments(output: plannedOutput?.path ?? "", dryRun: true) else { return }
        let job = starter.startJob(binary: binary, arguments: Argv.adding(["--output-format", "json"], to: arguments), streaming: false)
        previewJob = job
        previewTimedOut = false
        failure = nil
        let limit = previewLimit
        let timer = Task { [weak self] in
            try? await Task.sleep(for: limit)
            guard !Task.isCancelled, let self else { return }
            self.previewTimedOut = true
            await job.stop(grace: .seconds(10))
        }
        var lines: [String] = []
        var end: JobEnd?
        for await update in job.updates {
            switch update {
            case .said(let line): lines.append(line)
            case .ended(let ended): end = ended
            default: break
            }
        }
        timer.cancel()
        previewJob = nil
        if previewTimedOut {
            preview = nil
            failure = "The planner didn't answer within \(Self.said(limit)), so the preview was stopped: nothing was changed."
            return
        }
        if end?.interrupted == true {
            say?("Preview stopped: nothing was changed")
            return
        }
        if let value = JSONValue.parse(lines.joined(separator: "\n")), let project = ProjectRecord(value) {
            preview = project
            failure = nil
        } else {
            preview = nil
            failure = end?.stderrTail.lastLine ?? "the edit could not be planned"
        }
    }

    /// A time limit as the sheet says it: `5 minutes`, `90 seconds`.
    static func said(_ limit: Duration) -> String {
        let (seconds, attoseconds) = limit.components
        if attoseconds == 0, seconds >= 60, seconds % 60 == 0 {
            return seconds == 60 ? "1 minute" : "\(seconds / 60) minutes"
        }
        let value = Double(seconds) + Double(attoseconds) / 1e18
        return value == 1 ? "1 second" : "\(value.rounded() == value ? String(Int(value)) : String(value)) seconds"
    }

    /// The preview's diff against the project it edits.
    public var previewDiff: (changes: [SceneChange], compared: Bool)? {
        guard let preview, case .edit(let parent) = sheet else { return nil }
        return MediaLibrary.diff(edited: preview, parent: parent)
    }

    /// Renders the edit: the same command, without the dry run -- never
    /// beside its preview.
    public func renderEdit() {
        guard !isRunning, let output = plannedOutput?.path, let arguments = form.arguments(output: output) else { return }
        sheet = nil
        preview = nil
        start(arguments, output: output, resumable: arguments)
    }

    /// A project's diff against the one it was edited from.
    public func diff(_ project: ProjectRecord) -> (changes: [SceneChange], compared: Bool)? {
        guard project.parent != nil else { return nil }
        return MediaLibrary.diff(edited: project, parent: parent(of: project))
    }

    public func dismissFailure() {
        failure = nil
    }
}
