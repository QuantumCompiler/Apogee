import BusinessJobs
import BusinessMedia
import DataPreferences
import Foundation
import Observation

/// The Video place's clips: the library -- the records in `~/Movies/Apogee`
/// and the folders the person adds -- a clip's page, the generate sheet, the
/// run supervised, and the one-time install consent.
///
/// SwiftUI-free (ADR 0002). The library is records, not a database: re-read
/// on coming into view and after every run. Every run is the command a person
/// would type; the runtime is installed only on an explicit yes.
@MainActor
@Observable
public final class VideoViewModel {
    public enum Tab: String, CaseIterable, Identifiable, Sendable {
        case clips
        case compositions

        public var id: String { rawValue }
        public var title: String { self == .clips ? "Clips" : "Compositions" }
    }

    /// The install the CLI said a run needs, waiting on the person's yes.
    public struct Consent: Equatable, Sendable {
        /// What it runs: `train setup --with video`, or the rebuild.
        public let arguments: [String]
        /// The CLI's own words for why.
        public let said: String
        /// It replaces the environment rather than adding to it (M19).
        public var rebuilds: Bool { MediaLibrary.rebuilds(arguments) }
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
    public private(set) var consent: Consent?
    public private(set) var installing: JobProgress?
    public var say: (@MainActor (String) -> Void)?
    /// Whether the build streams a run's steps (the CLI's 52b).
    public var streams: @MainActor () -> Bool = { false }

    private let starter: any JobStarting
    private let preferences: Preferences
    private let home: URL
    private var binary: URL?
    private var job: (any JobRunning)?
    private var retryAfterInstall: [String]?

    public init(starter: any JobStarting, preferences: Preferences, home: URL) {
        self.starter = starter
        self.preferences = preferences
        self.home = home
    }

    public func connect(binary: URL?) {
        self.binary = binary
        refresh()
    }

    // MARK: - The library

    /// `~/Movies/Apogee`: where clips are made, and the first folder read.
    public var outputFolder: URL { home.appendingPathComponent("Movies/Apogee") }

    /// Every folder the library reads.
    public var folders: [URL] { [outputFolder] + preferences.videoFolders.map { URL(fileURLWithPath: $0) } }

    public func appeared() {
        refresh()
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
        sheetOpen = true
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

    public var isRunning: Bool { job != nil }

    /// Generates: the sheet's command, supervised.
    public func generate() {
        guard let arguments = plannedArguments else { return }
        try? FileManager.default.createDirectory(at: outputFolder, withIntermediateDirectories: true)
        sheetOpen = false
        start(arguments)
    }

    private func start(_ arguments: [String]) {
        guard let binary, job == nil else { return }
        let streaming = streams()
        let full = arguments + (streaming ? ["--output-format", "stream-json"] : [])
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
            // command that installs it: the app asks, never assumes.
            if let install = Self.installCommand(in: message + "\n" + run.stderr) {
                consent = Consent(arguments: install, said: message)
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

    /// Stops the run as Ctrl-C does: nothing is written, the output untouched.
    public func stop() async {
        await job?.stop(grace: .seconds(10))
    }

    public func dismissRun() {
        guard job == nil else { return }
        run = nil
        runArguments = nil
    }

    // MARK: - Consent

    /// The person said yes: install, then run what was refused again.
    public func install() {
        guard let consent, let binary else { return }
        self.consent = nil
        installing = JobProgress()
        let job = starter.startJob(binary: binary, arguments: consent.arguments, streaming: false)
        Task {
            for await update in job.updates { installing?.apply(update) }
            let done = installing?.status == .done
            installing = done ? nil : installing
            if done, let retry = retryAfterInstall {
                retryAfterInstall = nil
                start(retry)
            } else if case .failed(let message) = installing?.status,
                      let next = Self.installCommand(in: message + "\n" + (installing?.stderr ?? "")),
                      next != consent.arguments {
                // An install the environment's Python cannot take names its
                // rebuild (M19): asked next, the refused run kept for after.
                installing = nil
                self.consent = Consent(arguments: next, said: message)
            }
        }
    }

    /// The person said no: nothing is installed, and the run stays refused.
    public func decline() {
        defer {
            consent = nil
            retryAfterInstall = nil
        }
        say?("Not installed. apogee \(consent?.arguments.joined(separator: " ") ?? "train setup") installs it whenever you want it")
    }

    public func dismissInstall() {
        installing = nil
    }
}
