import BusinessJobs
import BusinessMedia
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
@MainActor
@Observable
public final class ComposeViewModel {
    /// What the sheet is doing.
    public enum Sheet: Equatable, Sendable {
        case new
        case edit(ProjectRecord)
    }

    public private(set) var projects: [ProjectRecord] = []
    public private(set) var selected: String?
    public var sheet: Sheet?
    public var form = ComposeForm()
    /// The edit's preview: the edited project a dry run printed, and its diff.
    public private(set) var preview: ProjectRecord?
    public private(set) var previewing = false
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

    private let starter: any JobStarting
    private let pollInterval: Duration
    private var binary: URL?
    private var job: (any JobRunning)?
    /// The arguments of each composition's last run, to resume it exactly.
    private var commands: [String: [String]] = [:]

    public init(starter: any JobStarting, pollInterval: Duration = .seconds(1)) {
        self.starter = starter
        self.pollInterval = pollInterval
    }

    public func connect(binary: URL?) {
        self.binary = binary
        refresh()
    }

    public func appeared() {
        refresh()
    }

    /// The projects, read again from the folders.
    public func refresh() {
        projects = MediaLibrary.projects(in: folders()).records
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
    }

    /// Where the sheet's composition is written: a free name in the first folder.
    public var plannedOutput: URL? {
        guard let folder = outputFolder else { return nil }
        let base: String
        switch sheet {
        case .edit(let project):
            base = (project.name as NSString).deletingPathExtension + "-edit"
        default:
            base = MediaLibrary.stem(form.brief).isEmpty ? "composition" : MediaLibrary.stem(form.brief)
        }
        return MediaLibrary.freePath(in: folder, base: base, ext: "mp4")
    }

    public var plannedArguments: [String]? {
        plannedOutput.flatMap { form.arguments(output: $0.path) }
    }

    public var isRunning: Bool { job != nil }

    /// Composes the sheet's brief: planned and rendered in one run with
    /// Stop -- or, where the build plans alone (52c), planned first and shown,
    /// then rendered by the same command.
    public func compose(planFirst: Bool = false) {
        guard let arguments = plannedArguments, let output = plannedOutput?.path else { return }
        if let folder = outputFolder { try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true) }
        sheet = nil
        start(planFirst && plansAlone() ? arguments + ["--dry-run"] : arguments, output: output, resumable: arguments)
    }

    /// Renders a planned composition, or resumes a stopped one: its last
    /// command again, which renders only what the project lacks.
    public func resume(_ project: ProjectRecord) {
        let arguments = commands[project.output] ?? Self.resumeArguments(project)
        start(arguments, output: project.output, resumable: arguments)
    }

    /// For a project whose command the app did not run: its brief and output
    /// -- the composer reads the rest from the project it keeps.
    static func resumeArguments(_ project: ProjectRecord) -> [String] {
        ["video", "compose", project.brief, "-o", project.output]
    }

    private func start(_ arguments: [String], output: String, resumable: [String]) {
        guard let binary, job == nil else { return }
        let streaming = streams()
        let full = arguments + (streaming ? ["--output-format", "stream-json"] : [])
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
            switch run?.status {
            case .done: say?("Composed \((output as NSString).lastPathComponent)")
            case .stopped: say?("Stopped: the finished scenes are kept")
            case .failed(let message): failure = message
            default: break
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

    /// Stops the run as Ctrl-C does: every finished scene stays.
    public func stop() async {
        await job?.stop(grace: .seconds(10))
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
    public func previewEdit() async {
        guard let binary, let arguments = form.arguments(output: plannedOutput?.path ?? "", dryRun: true) else { return }
        previewing = true
        defer { previewing = false }
        let job = starter.startJob(binary: binary, arguments: arguments + ["--output-format", "json"], streaming: false)
        var lines: [String] = []
        var end: JobEnd?
        for await update in job.updates {
            switch update {
            case .said(let line): lines.append(line)
            case .ended(let ended): end = ended
            default: break
            }
        }
        if let value = JSONValue.parse(lines.joined(separator: "\n")), let project = ProjectRecord(value) {
            preview = project
            failure = nil
        } else {
            preview = nil
            failure = end?.stderrTail.lastLine ?? "the edit could not be planned"
        }
    }

    /// The preview's diff against the project it edits.
    public var previewDiff: (changes: [SceneChange], compared: Bool)? {
        guard let preview, case .edit(let parent) = sheet else { return nil }
        return MediaLibrary.diff(edited: preview, parent: parent)
    }

    /// Renders the edit: the same command, without the dry run.
    public func renderEdit() {
        guard let output = plannedOutput?.path, let arguments = form.arguments(output: output) else { return }
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
