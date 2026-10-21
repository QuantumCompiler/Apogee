import DataProtocol
import Foundation

/// A composition's project: the `video.project` document the composer keeps
/// beside its video as `<video>.json`, saved after every step -- the one
/// source of a composition's progress (the CLI's 50c, 50d and 52c).
public struct ProjectRecord: Sendable, Equatable, Identifiable {
    public struct Scene: Sendable, Equatable, Identifiable {
        public let index: Int
        public let narration: String
        public let visual: String
        /// The plan's length for it.
        public let seconds: Double?
        public let asset: String?
        public let seed: Int?
        /// Its narration's WAV, once spoken; its clip, once rendered.
        public let voiceFile: String?
        public let clipFile: String?
        /// Since 52c, in an edited project: the parent's scene it came from
        /// (`new` for none), what changed, and whether it moved.
        public let from: String?
        public let changes: [String]?
        public let moved: Bool

        public var id: Int { index }

        /// Spoken and rendered: nothing of it left to make.
        public var isMade: Bool { voiceFile != nil && (clipFile != nil || asset != nil) }
    }

    public struct Edit: Sendable, Equatable {
        public let instruction: String?
        public let redo: [Int]
        public let from: String?
    }

    /// `rendering`, `done`, `failed`, `cancelled`; `planned` for a dry run's.
    public let status: String
    public let brief: String
    public let title: String
    public let output: String
    public let scenes: [Scene]
    public let captionsFile: String?
    public let captionsBurned: Bool?
    public let parent: String?
    public let edits: [Edit]
    /// Since 52c: the parent's scenes an edit left out.
    public let removed: [Int]?
    public let error: String?
    public let created: String?
    public let music: String?
    public let voice: String?

    public var id: String { output }

    public init?(_ value: JSONValue) {
        guard value["object"]?.stringValue == "video.project", let output = value["output"]?.stringValue else { return nil }
        func number(_ v: JSONValue?) -> Double? { if case .number(let n)? = v { n } else { nil } }
        status = value["status"]?.stringValue ?? ""
        brief = value["brief"]?.stringValue ?? ""
        title = value["title"]?.stringValue ?? ""
        self.output = output
        scenes = (value["scenes"]?.arrayValue ?? []).compactMap { s in
            guard let index = s["index"]?.intValue else { return nil }
            var from: String?
            if let text = s["from"]?.stringValue { from = text } else if let n = s["from"]?.intValue { from = String(n) }
            return Scene(
                index: index, narration: s["narration"]?.stringValue ?? "", visual: s["visual"]?.stringValue ?? "",
                seconds: number(s["seconds"]), asset: s["asset"]?.stringValue, seed: s["seed"]?.intValue,
                voiceFile: s["voice"]?["file"]?.stringValue, clipFile: s["clip"]?["file"]?.stringValue,
                from: from, changes: s["changes"]?.arrayValue?.compactMap(\.stringValue), moved: s["moved"]?.boolValue ?? false)
        }
        captionsFile = value["captions"]?["file"]?.stringValue
        captionsBurned = value["captions"]?["burned"]?.boolValue
        parent = value["parent"]?.stringValue
        edits = (value["edits"]?.arrayValue ?? []).map { e in
            Edit(
                instruction: e["instruction"]?.stringValue, redo: (e["redo"]?.arrayValue ?? []).compactMap(\.intValue),
                from: e["from"]?.stringValue)
        }
        removed = value["removed"]?.arrayValue?.compactMap(\.intValue)
        error = value["error"]?.stringValue
        created = value["created"]?.stringValue
        music = value["settings"]?["music"]?.stringValue
        voice = value["settings"]?["voice"]?.stringValue
    }

    public var name: String { (output as NSString).lastPathComponent }
    public var made: Int { scenes.filter(\.isMade).count }
    /// Whether the edit's own comparison is in the document (52c).
    public var hasDiff: Bool { scenes.contains { $0.changes != nil } || removed != nil }

    /// Reads the project kept beside `video`, when there is one.
    public static func beside(_ video: String) -> ProjectRecord? {
        guard let data = FileManager.default.contents(atPath: video + ".json"),
            let value = JSONValue.parse(String(decoding: data, as: UTF8.self))
        else { return nil }
        return ProjectRecord(value)
    }
}

/// One scene's line in an edit's diff.
public struct SceneChange: Sendable, Equatable, Identifiable {
    public enum Kind: Sendable, Equatable {
        case unchanged
        case changed([String])
        case added
        case removed
    }

    public let index: Int
    public let narration: String
    public let kind: Kind
    public let moved: Bool
    public var id: String { "\(index)-\(narration.prefix(12))" }
}

extension MediaLibrary {
    /// Every composition in `folders`, newest first.
    public static func projects(in folders: [URL]) -> (records: [ProjectRecord], skipped: [Skipped]) {
        scan(folders, object: "video.project", read: ProjectRecord.init, output: { $0.output + ".json" }, created: \.created)
    }

    /// An edit's diff: from the edited project's own comparison where it
    /// carries one (52c), else by comparing the two documents scene by scene
    /// in order -- said as a comparison, since the scenes do not name their
    /// source (the design's G13 fallback).
    public static func diff(edited: ProjectRecord, parent: ProjectRecord?) -> (changes: [SceneChange], compared: Bool) {
        if edited.hasDiff {
            var changes = edited.scenes.map { scene -> SceneChange in
                let kind: SceneChange.Kind =
                    scene.from == "new" ? .added : (scene.changes ?? []).isEmpty ? .unchanged : .changed(scene.changes ?? [])
                return SceneChange(index: scene.index, narration: scene.narration, kind: kind, moved: scene.moved)
            }
            for index in edited.removed ?? [] {
                let narration = parent?.scenes.first { $0.index == index }?.narration ?? ""
                changes.append(SceneChange(index: index, narration: narration, kind: .removed, moved: false))
            }
            return (changes, false)
        }
        let before = parent?.scenes ?? []
        var changes: [SceneChange] = []
        for scene in edited.scenes {
            guard let old = before.first(where: { $0.index == scene.index }) else {
                changes.append(SceneChange(index: scene.index, narration: scene.narration, kind: .added, moved: false))
                continue
            }
            var what: [String] = []
            if old.narration != scene.narration { what.append("narration") }
            if old.visual != scene.visual { what.append("prompt") }
            if old.seconds != scene.seconds { what.append("duration") }
            if old.asset != scene.asset { what.append("asset") }
            if old.seed != scene.seed { what.append("seed") }
            changes.append(SceneChange(index: scene.index, narration: scene.narration, kind: what.isEmpty ? .unchanged : .changed(what), moved: false))
        }
        for old in before where !edited.scenes.contains(where: { $0.index == old.index }) {
            changes.append(SceneChange(index: old.index, narration: old.narration, kind: .removed, moved: false))
        }
        return (changes, true)
    }
}

/// A composition to make or to edit, as the command a person would type.
public struct ComposeForm: Sendable, Equatable {
    public var brief = ""
    public var assets: String?
    public var music: String?
    public var captions = true
    public var voice = ""
    public var model = ""
    /// Editing: the composed video (or its project), the instruction, the
    /// scenes to render again on fresh seeds.
    public var from: String?
    public var instruction = ""
    public var redo: Set<Int> = []

    public init() {}

    /// `video compose …` writing `output`; `dryRun` plans (or, editing,
    /// says the diff) and writes nothing. `nil` while it cannot run.
    public func arguments(output: String, dryRun: Bool = false) -> [String]? {
        var arguments = ["video", "compose"]
        if let from {
            let instruction = instruction.trimmingCharacters(in: .whitespacesAndNewlines)
            guard !instruction.isEmpty || !redo.isEmpty else { return nil }
            arguments += ["--from", from]
            if !instruction.isEmpty { arguments += ["--edit", instruction] }
            for scene in redo.sorted() { arguments += ["--redo-scene", String(scene)] }
        } else {
            let brief = brief.trimmingCharacters(in: .whitespacesAndNewlines)
            guard !brief.isEmpty else { return nil }
            arguments.append(brief)
        }
        arguments += ["-o", output]
        if from == nil {
            if let assets { arguments += ["--assets", assets] }
            if let music { arguments += ["--music", music] }
            if !captions { arguments.append("--no-captions") }
            let voice = voice.trimmingCharacters(in: .whitespaces)
            if !voice.isEmpty { arguments += ["--voice", voice] }
            let model = model.trimmingCharacters(in: .whitespaces)
            if !model.isEmpty { arguments += ["-m", model] }
        }
        if dryRun { arguments.append("--dry-run") }
        return arguments
    }
}
