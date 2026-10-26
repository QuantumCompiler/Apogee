import BusinessJobs
import BusinessMedia
import BusinessReads
import DataPreferences
import Foundation
import Observation

/// The Voice place: the speeches -- the records in `~/Music/Apogee` and the
/// folders the person adds -- the say sheet with the voice model's own
/// voices, a run supervised, and playback that follows the record's own
/// sentence timings.
///
/// SwiftUI-free (ADR 0002). The voices are the model's, from its `models
/// info` -- never a list of the app's; the timings are the document's, never
/// measured again.
@MainActor
@Observable
public final class VoiceViewModel {
    /// The voices of one language, as the model lists them.
    public struct VoiceGroup: Equatable, Sendable, Identifiable {
        public let language: String
        public let voices: [String]
        public var id: String { language }
    }

    public private(set) var speeches: [SpeechRecord] = []
    public private(set) var skipped: [MediaLibrary.Skipped] = []
    public private(set) var selected: String?
    /// The store's voice models, as `models list` states them, in the order
    /// the sheet offers them (the CLI's 53m): Kokoro as hexgrad publishes it
    /// on PyTorch, its MLX build on MLX -- PyTorch first.
    public private(set) var models: [MediaModel] = []
    /// The voice model the sheet speaks with, by its store handle -- the
    /// store's one, or the one picked when it holds more -- and its voices.
    public private(set) var voiceModel: String?
    public private(set) var voices: [VoiceGroup] = []
    public var sheetOpen = false
    public var text = ""
    public var voice = ""
    public var speed = 1.0
    public private(set) var run: JobProgress?
    /// The install a refused speech named, waiting on the person's yes.
    public private(set) var consent: InstallConsent?
    /// The install the person said yes to, as it runs and, ended, until
    /// dismissed: what the list draws as its card (GUI 4f).
    public private(set) var installing: RuntimeInstall?
    /// Whether an install another place started runs now (the composition
    /// reads the children registry): a second `train setup` into the one
    /// environment is never offered beside it.
    public var installElsewhere: @MainActor () -> Bool = { false }
    /// Where playback is, in seconds: the player's clock, set by the view.
    public var position: Double = 0
    public var say: (@MainActor (String) -> Void)?
    public var streams: @MainActor () -> Bool = { false }

    private let runner: any CommandRunning
    private let starter: any JobStarting
    private let preferences: Preferences
    private let home: URL
    private var binary: URL?
    private var job: (any JobRunning)?
    private var installJob: (any JobRunning)?
    /// The refused speech, spoken again once its install is in -- and only then.
    private var retry: [String]?

    public init(runner: any CommandRunning, starter: any JobStarting, preferences: Preferences, home: URL) {
        self.runner = runner
        self.starter = starter
        self.preferences = preferences
        self.home = home
    }

    public func connect(binary: URL?) async {
        self.binary = binary
        await refresh()
    }

    public func appeared() async {
        await refresh()
    }

    // MARK: - The library

    public var outputFolder: URL { home.appendingPathComponent("Music/Apogee") }

    public var folders: [URL] { [outputFolder] + preferences.voiceFolders.map { URL(fileURLWithPath: $0) } }

    /// The records, read again; and the voice model's voices.
    public func refresh() async {
        let scan = MediaLibrary.speeches(in: folders)
        speeches = scan.records
        skipped = scan.skipped
        if let selected, !speeches.contains(where: { $0.output == selected }) { self.selected = nil }
        await readVoices()
    }

    /// The store's voice models (`models list`'s voice rows, and since the
    /// CLI's 53m its MLX voice models) and the picked one's voices -- read as
    /// the compose sheet reads them (`MediaModel.readVoiceModels`).
    private func readVoices() async {
        guard let binary, let read = await MediaModel.readVoiceModels(runner, binary: binary) else { return }
        models = read
        if voiceModel == nil || !models.contains(where: { $0.handle == voiceModel }) { voiceModel = models.first?.handle }
        await readVoices(of: voiceModel)
    }

    /// A voice model's voices: `models info <it>`'s `voices` field, one
    /// language a line -- an MLX build's own presets as the CLI lists them.
    private func readVoices(of handle: String?) async {
        guard let binary, let handle, let info = await runner.read(binary: binary, ["models", "info", handle]).document else {
            voices = []
            return
        }
        guard handle == voiceModel else { return }
        voices = Self.voices(in: ModelInfo(document: info).field("voices") ?? "")
        if !voice.isEmpty && !voices.contains(where: { $0.voices.contains(voice) }) { voice = "" }
    }

    /// Picks the voice model the sheet speaks with, and reads its voices: a
    /// voice the new one does not carry goes back to the default.
    public func choose(model handle: String) async {
        guard models.contains(where: { $0.handle == handle }), handle != voiceModel else { return }
        voiceModel = handle
        await readVoices(of: handle)
    }

    /// The picked model, as the store lists it.
    public var chosen: MediaModel? { voiceModel.flatMap { handle in models.first { $0.handle == handle } } }

    /// How the picker names a model among the store's.
    public func label(_ model: MediaModel) -> String { model.label(among: models) }

    /// `--engine`, where the person chose it: the picked model's folder holds
    /// Kokoro beside its MLX build. Anywhere else the directory decides and
    /// nothing is passed (the CLI's flag only names that engine, its 53m).
    public var chosenEngine: MediaEngine? {
        guard models.count > 1, let chosen, MediaModel.choosesEngine(chosen, among: models) else { return nil }
        return chosen.engine
    }

    /// The sheet's engine line: what the speech will be spoken on, as the
    /// model's directory says; `nil` with no voice model.
    public var engineLine: String? {
        guard let chosen else { return nil }
        var line = "Speaks on " + Self.speaksOn(chosen.engine) + "."
        if let engine = chosenEngine {
            line += " \(chosen.folder) holds it and its MLX build, so the engine is yours: --engine \(engine.word)."
        }
        return line
    }

    /// An engine and what speaks on it there, in the CLI's words (`on MLX
    /// (mlx-audio)`).
    static func speaksOn(_ engine: MediaEngine) -> String {
        switch engine {
        case .torch: "PyTorch, through Kokoro: the voice model as Kokoro publishes it"
        case .mlx: "MLX, through mlx-audio: an MLX voice model"
        case .other(let word): word
        }
    }

    /// `American English: af_heart, af_bella` lines, as the model lists them.
    static func voices(in text: String) -> [VoiceGroup] {
        text.split(whereSeparator: \.isNewline).compactMap { line in
            guard let colon = line.firstIndex(of: ":") else { return nil }
            let names = line[line.index(after: colon)...].split(separator: ",").map { $0.trimmingCharacters(in: .whitespaces) }
                .filter { !$0.isEmpty }
            return names.isEmpty ? nil : VoiceGroup(language: line[..<colon].trimmingCharacters(in: .whitespaces), voices: names)
        }
    }

    public func add(folder: URL) {
        var kept = preferences.voiceFolders
        guard folder != outputFolder, !kept.contains(folder.path) else { return }
        kept.append(folder.path)
        preferences.voiceFolders = kept
        Task { await refresh() }
    }

    public func select(_ speech: SpeechRecord) {
        selected = speech.output
        position = 0
    }

    public var selectedSpeech: SpeechRecord? { selected.flatMap { s in speeches.first { $0.output == s } } }

    /// The sentence being spoken now, by the record's own timings.
    public var currentSentence: Int? { selectedSpeech?.sentence(at: position) }

    /// Where a sentence starts: what clicking it seeks to.
    public func start(of sentence: Int) -> Double? {
        selectedSpeech.flatMap { $0.sentences.indices.contains(sentence) ? $0.sentences[sentence].offset : nil }
    }

    // MARK: - Saying

    public var plannedOutput: URL {
        let stem = MediaLibrary.stem(text, words: 4)
        return MediaLibrary.freePath(in: outputFolder, base: stem.isEmpty ? "speech" : stem, ext: "wav")
    }

    /// `voice say -o <wav> -- <text>`, with the voice and speed when chosen
    /// -- and where the store holds more than one voice model, the one picked
    /// (`-m`, by its handle), which the CLI asks for rather than guess (its
    /// 53m); with one, nothing is named, as before. The text is the last
    /// word, after `--`, so the binary never reads it as a flag (`Argv`,
    /// GUI 4g).
    public var plannedArguments: [String]? {
        let text = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty else { return nil }
        var arguments = ["voice", "say", "-o", plannedOutput.path]
        if models.count > 1, let voiceModel {
            arguments += ["-m", voiceModel]
            if let engine = chosenEngine { arguments += ["--engine", engine.word] }
        }
        if !voice.isEmpty { arguments += ["--voice", voice] }
        if speed != 1 { arguments += ["--speed", String(format: "%g", speed)] }
        return arguments + Argv.text(text)
    }

    /// Whether the place runs something now: a speech, or the install one
    /// needs.
    public var isRunning: Bool { job != nil || installJob != nil }

    /// Whether the install the person said yes to runs now.
    public var isInstalling: Bool { installJob != nil }

    /// Says the sheet's text, supervised -- never beside an install, whose
    /// runtime the speech may need.
    public func speak() {
        guard let arguments = plannedArguments else { return }
        if let installing, isInstalling {
            say?("Installing first: the speech can be spoken once \(installing.command) has finished")
            return
        }
        try? FileManager.default.createDirectory(at: outputFolder, withIntermediateDirectories: true)
        sheetOpen = false
        start(arguments)
    }

    private func start(_ arguments: [String]) {
        guard let binary, job == nil, installJob == nil else { return }
        let streaming = streams()
        run = JobProgress()
        let job = starter.startJob(
            binary: binary, arguments: streaming ? Argv.adding(["--output-format", "stream-json"], to: arguments) : arguments,
            streaming: streaming)
        self.job = job
        Task {
            for await update in job.updates { run?.apply(update) }
            self.job = nil
            await refresh()
            switch run?.status {
            case .done:
                if let output = arguments.firstIndex(of: "-o").map({ arguments[$0 + 1] }) {
                    selected = output
                    say?("Spoke \((output as NSString).lastPathComponent)")
                }
            case .failed(let message):
                // Refused for its runtime -- named on stderr alone, the
                // stream silent -- the app asks, never assumes.
                guard let asked = InstallConsent(failure: message, stderr: run?.stderr ?? "") else { break }
                if installElsewhere() {
                    say?("\(asked.command) is already running: say it again once it has finished")
                } else {
                    consent = asked
                    retry = arguments
                }
            default:
                break
            }
        }
    }

    /// Stops what runs as Ctrl-C does: the speech -- nothing is written -- or
    /// the install, which then runs nothing again.
    public func stop() async {
        if let job {
            await job.stop(grace: .seconds(10))
        } else {
            await installJob?.stop(grace: .seconds(10))
        }
    }

    public func dismissRun() {
        if job == nil { run = nil }
    }

    /// Yes: the voice runtime's install, supervised -- its card, Stop -- and
    /// once it is in, the refused speech again. A failed or stopped install
    /// is said, and runs nothing again.
    public func install() {
        guard let consent, let binary, job == nil, installJob == nil else { return }
        self.consent = nil
        let retry = retry
        self.retry = nil
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
                say?("Installed: \(installing.command)" + (retry == nil ? "" : " -- speaking it again"))
                if let retry { start(retry) }
            case .failed:
                say?("Not installed: \(installing.failure ?? installing.command)")
            case .stopped:
                say?("The install was stopped before it finished: \(installing.command) runs it again")
            case .running:
                break
            }
        }
    }

    /// No: nothing installed.
    public func decline() {
        let command = consent?.command ?? "apogee train setup --with voice"
        consent = nil
        retry = nil
        say?("Not installed. \(command) installs it whenever you want it")
    }

    /// The ended install's card, put away -- once its own end is said; a
    /// running one stays.
    public func dismissInstall() {
        guard installing?.phase != .running else { return }
        installing = nil
    }
}
