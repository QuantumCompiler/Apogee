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
    /// The voice model the store holds (its backend's name), and its voices.
    public private(set) var voiceModel: String?
    public private(set) var voices: [VoiceGroup] = []
    public var sheetOpen = false
    public var text = ""
    public var voice = ""
    public var speed = 1.0
    public private(set) var run: JobProgress?
    public private(set) var consent: [String]?
    public private(set) var consentSaid = ""
    public private(set) var installing: JobProgress?
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

    /// The store's voice model (`models list`'s voice row) and its voices
    /// (`models info <it>`'s `voices` field, one language a line).
    private func readVoices() async {
        guard let binary, let list = await runner.read(binary: binary, ["models", "list"]).document else { return }
        voiceModel = ModelRow.list(from: list).first { $0.format == "voice" }?.name
        guard let voiceModel, let info = await runner.read(binary: binary, ["models", "info", voiceModel]).document else {
            voices = []
            return
        }
        voices = Self.voices(in: ModelInfo(document: info).field("voices") ?? "")
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

    /// `voice say <text> -o <wav>`, with the voice and speed when chosen.
    public var plannedArguments: [String]? {
        let text = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty else { return nil }
        var arguments = ["voice", "say", text, "-o", plannedOutput.path]
        if !voice.isEmpty { arguments += ["--voice", voice] }
        if speed != 1 { arguments += ["--speed", String(format: "%g", speed)] }
        return arguments
    }

    public var isRunning: Bool { job != nil }

    public func speak() {
        guard let arguments = plannedArguments else { return }
        try? FileManager.default.createDirectory(at: outputFolder, withIntermediateDirectories: true)
        sheetOpen = false
        start(arguments)
    }

    private func start(_ arguments: [String]) {
        guard let binary, job == nil else { return }
        let streaming = streams()
        run = JobProgress()
        let job = starter.startJob(
            binary: binary, arguments: arguments + (streaming ? ["--output-format", "stream-json"] : []), streaming: streaming)
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
                if let install = MediaLibrary.installCommand(in: message + "\n" + (run?.stderr ?? "")) {
                    consent = install
                    consentSaid = message
                    retry = arguments
                }
            default:
                break
            }
        }
    }

    public func stop() async {
        await job?.stop(grace: .seconds(10))
    }

    public func dismissRun() {
        if job == nil { run = nil }
    }

    /// Yes: the voice runtime installed, then the refused run again.
    public func install() {
        guard let consent, let binary else { return }
        self.consent = nil
        installing = JobProgress()
        let job = starter.startJob(binary: binary, arguments: consent, streaming: false)
        Task {
            for await update in job.updates { installing?.apply(update) }
            let done = installing?.status == .done
            installing = done ? nil : installing
            if done, let retry {
                self.retry = nil
                start(retry)
            }
        }
    }

    /// No: nothing installed.
    public func decline() {
        let command = consent.map { (["apogee"] + $0).joined(separator: " ") } ?? "apogee train setup --with voice"
        consent = nil
        retry = nil
        say?("Not installed. \(command) installs it whenever you want it")
    }
}
