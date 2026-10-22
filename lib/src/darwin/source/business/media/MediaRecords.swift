import DataProtocol
import Foundation

/// A clip's record: the `video.generation` document a run writes beside its
/// clip as `<clip>.json` (the CLI's 49c-49e).
public struct ClipRecord: Sendable, Equatable, Identifiable {
    public struct LoRA: Sendable, Equatable {
        public let path: String
        public let scale: Double?

        public init(path: String, scale: Double?) {
            self.path = path
            self.scale = scale
        }
    }

    public let status: String
    /// The clip, absolute.
    public let output: String
    public let sidecar: String?
    public let model: String
    public let prompt: String
    public let image: String?
    /// `t2v` or `i2v`.
    public let mode: String
    public let seed: Int?
    /// `given`, `held` or `generated`.
    public let seedSource: String?
    public let width: Int?
    public let height: Int?
    public let duration: Double?
    public let fps: Int?
    public let steps: Int?
    public let codec: String?
    /// How long the run took.
    public let seconds: Double?
    public let createdAt: String?
    /// `from` (a record run again) or `extend` (a clip continued), and the
    /// clip it came from; `nil` for a run of its own.
    public let parentMove: String?
    public let parentClip: String?
    /// `draft`, `final`, or `nil`.
    public let preset: String?
    public let loras: [LoRA]
    public let error: String?
    /// The engine the record says it ran on (the CLI's 53l), `nil` in a
    /// record from before there were two -- which ran on PyTorch (`engine`).
    public let recordedEngine: MediaEngine?
    /// The versions that ran, as the record names them (`torch`,
    /// `diffusers`; `mlx`, `mlx_video`; `apogee`), each as written.
    public let versions: [String: String]

    public var id: String { output }

    /// What the clip ran on: the record's engine, else PyTorch.
    public var engine: MediaEngine { MediaEngine.recorded(recordedEngine) }

    public init?(_ value: JSONValue) {
        guard value["object"]?.stringValue == "video.generation", let output = value["output"]?.stringValue else { return nil }
        func number(_ v: JSONValue?) -> Double? { if case .number(let n)? = v { n } else { nil } }
        let settings = value["settings"]
        status = value["status"]?.stringValue ?? ""
        self.output = output
        sidecar = value["sidecar"]?.stringValue
        model = value["model"]?["name"]?.stringValue ?? ""
        prompt = value["prompt"]?.stringValue ?? ""
        image = value["image"]?.stringValue
        mode = value["mode"]?.stringValue ?? ""
        seed = value["seed"]?.intValue
        seedSource = value["seed_source"]?.stringValue
        width = settings?["width"]?.intValue
        height = settings?["height"]?.intValue
        duration = number(settings?["duration"])
        fps = settings?["fps"]?.intValue
        steps = settings?["steps"]?.intValue
        codec = value["codec"]?.stringValue
        seconds = number(value["seconds"])
        createdAt = value["created_at"]?.stringValue
        parentMove = value["parent"]?["move"]?.stringValue
        parentClip = value["parent"]?["clip"]?.stringValue
        preset = value["preset"]?.stringValue
        loras = (value["loras"]?.arrayValue ?? []).compactMap { l in
            l["path"]?.stringValue.map { LoRA(path: $0, scale: number(l["scale"])) }
        }
        error = value["error"]?.stringValue
        recordedEngine = value["engine"]?.stringValue.map(MediaEngine.init(word:))
        versions = (value["versions"]?.objectValue ?? [:]).compactMapValues(\.stringValue)
    }

    public var name: String { (output as NSString).lastPathComponent }
}

/// A speech's record: the `voice.speech` document `voice say` writes beside
/// its WAV (the CLI's 50a).
public struct SpeechRecord: Sendable, Equatable, Identifiable {
    public struct Sentence: Sendable, Equatable {
        public let text: String
        /// Where it starts in the file, and how long it lasts, in seconds --
        /// counted from the samples spoken, never estimated.
        public let offset: Double
        public let seconds: Double
    }

    public let status: String
    public let output: String
    public let model: String
    public let voice: String
    public let language: String?
    public let speed: Double?
    public let text: String
    public let sentences: [Sentence]
    public let seconds: Double?
    public let createdAt: String?
    public let error: String?
    /// The engine the record says it spoke on (the CLI's 53m), `nil` in a
    /// record from before there were two -- which spoke on PyTorch (`engine`).
    public let recordedEngine: MediaEngine?
    /// The versions that ran, as the record names them (`torch`, `kokoro`;
    /// `mlx`, `mlx_audio`; `apogee`), each as written.
    public let versions: [String: String]

    public var id: String { output }

    /// What the speech was spoken on: the record's engine, else PyTorch.
    public var engine: MediaEngine { MediaEngine.recorded(recordedEngine) }

    public init?(_ value: JSONValue) {
        guard value["object"]?.stringValue == "voice.speech", let output = value["output"]?.stringValue else { return nil }
        func number(_ v: JSONValue?) -> Double? { if case .number(let n)? = v { n } else { nil } }
        status = value["status"]?.stringValue ?? ""
        self.output = output
        model = value["model"]?["name"]?.stringValue ?? ""
        voice = value["voice"]?.stringValue ?? ""
        language = value["language"]?.stringValue
        speed = number(value["speed"])
        text = value["text"]?.stringValue ?? ""
        // Each segment's sentences, their offsets laid onto the file's own:
        // a segment's offset, then the sentence's within it.
        var sentences: [Sentence] = []
        for segment in value["segments"]?.arrayValue ?? [] {
            let base = number(segment["offset"]) ?? 0
            for sentence in segment["sentences"]?.arrayValue ?? [] {
                guard let text = sentence["text"]?.stringValue, let seconds = number(sentence["seconds"]) else { continue }
                sentences.append(Sentence(text: text, offset: base + (number(sentence["offset"]) ?? 0), seconds: seconds))
            }
        }
        self.sentences = sentences
        seconds = number(value["seconds"])
        createdAt = value["created_at"]?.stringValue
        error = value["error"]?.stringValue
        recordedEngine = value["engine"]?.stringValue.map(MediaEngine.init(word:))
        versions = (value["versions"]?.objectValue ?? [:]).compactMapValues(\.stringValue)
    }

    public var name: String { (output as NSString).lastPathComponent }

    /// The sentence being spoken at `time`, by the document's own timings.
    public func sentence(at time: Double) -> Int? {
        sentences.lastIndex { $0.offset <= time + 1e-6 && time < $0.offset + $0.seconds + 1e-6 }
    }
}

/// The media library: the records found in folders -- `~/Movies/Apogee`,
/// `~/Music/Apogee`, and any the person adds. The folders are the truth; the
/// app keeps no database of its own.
public enum MediaLibrary {
    /// A sidecar that was passed over, and why.
    public struct Skipped: Sendable, Equatable {
        public let file: String
        public let reason: String
    }

    /// Every record of `object` in `folders` (a shallow walk for `*.json`),
    /// newest first, each whose output is still there; and those skipped.
    public static func scan<Record>(
        _ folders: [URL], object: String, read: (JSONValue) -> Record?, output: (Record) -> String,
        created: (Record) -> String?
    ) -> (records: [Record], skipped: [Skipped]) {
        var records: [Record] = []
        var skipped: [Skipped] = []
        let manager = FileManager.default
        for folder in folders {
            guard let names = try? manager.contentsOfDirectory(atPath: folder.path) else { continue }
            for name in names.sorted() where name.hasSuffix(".json") {
                let path = folder.appendingPathComponent(name).path
                guard let data = manager.contents(atPath: path), let text = String(data: data, encoding: .utf8),
                    let value = JSONValue.parse(text)
                else {
                    skipped.append(Skipped(file: path, reason: "not a JSON document"))
                    continue
                }
                guard value["object"]?.stringValue == object else { continue }
                guard let record = read(value) else {
                    skipped.append(Skipped(file: path, reason: "a \(object) record without its output"))
                    continue
                }
                guard manager.fileExists(atPath: output(record)) else {
                    skipped.append(Skipped(file: path, reason: "its file is gone: \(output(record))"))
                    continue
                }
                records.append(record)
            }
        }
        records.sort { (created($0) ?? "") > (created($1) ?? "") }
        return (records, skipped)
    }

    public static func clips(in folders: [URL]) -> (records: [ClipRecord], skipped: [Skipped]) {
        scan(folders, object: "video.generation", read: ClipRecord.init, output: \.output, created: \.createdAt)
    }

    public static func speeches(in folders: [URL]) -> (records: [SpeechRecord], skipped: [Skipped]) {
        scan(folders, object: "voice.speech", read: SpeechRecord.init, output: \.output, created: \.createdAt)
    }

    /// A path in `folder` for `base`.`ext` that names nothing yet: a taken
    /// name gets a number (`lighthouse-2.mp4`) -- nothing is ever written over.
    public static func freePath(in folder: URL, base: String, ext: String) -> URL {
        let clean = base.replacingOccurrences(of: "/", with: "-").trimmingCharacters(in: .whitespaces)
        let stem = clean.isEmpty ? "untitled" : clean
        var candidate = folder.appendingPathComponent("\(stem).\(ext)")
        var n = 2
        while FileManager.default.fileExists(atPath: candidate.path)
            || FileManager.default.fileExists(atPath: candidate.path + ".json")
        {
            candidate = folder.appendingPathComponent("\(stem)-\(n).\(ext)")
            n += 1
        }
        return candidate
    }

    /// A file-name stem from a prompt's first words: `a lighthouse at dusk` is
    /// `a-lighthouse-at-dusk`.
    public static func stem(_ prompt: String, words: Int = 5) -> String {
        let kept = prompt.lowercased().split { !$0.isLetter && !$0.isNumber }.prefix(words)
        return kept.joined(separator: "-")
    }

    /// The command that installs a media runtime -- `train setup --with
    /// video` or `voice` -- when a refusal's words name it: what the app asks
    /// the person before running, never runs on its own.
    public static func installCommand(in text: String) -> [String]? {
        guard let range = text.range(of: "apogee train setup") else { return nil }
        let words = text[range.lowerBound...].split(whereSeparator: { $0 == " " || $0.isNewline }).map(String.init)
        guard words.count >= 5, words[3] == "--with" else { return nil }
        let with = words[4].trimmingCharacters(in: CharacterSet(charactersIn: "'\"`.,;)"))
        return ["train", "setup", "--with", with]
    }

    /// The parent chain of `record`: each clip it came from, back to the first.
    public static func chain(of record: ClipRecord, in records: [ClipRecord]) -> [ClipRecord] {
        var chain: [ClipRecord] = []
        var seen: Set<String> = [record.output]
        var parent = record.parentClip
        while let clip = parent, !seen.contains(clip), let found = records.first(where: { $0.output == clip }) {
            chain.append(found)
            seen.insert(clip)
            parent = found.parentClip
        }
        return chain
    }
}
