import Foundation

/// What a clip or a speech runs on: the engine its model's directory says
/// (the CLI's 53l and 53m). PyTorch runs a diffusers pipeline and a voice
/// model as Kokoro publishes it; MLX runs an MLX video model (mlx-video's
/// build of Wan) and an MLX voice model (mlx-audio's build of Kokoro). Any
/// other word a later CLI writes is kept as written, never guessed at: the
/// protocol grows additively (the CLI's ADR 0006).
public enum MediaEngine: Sendable, Equatable, Hashable {
    case torch
    case mlx
    /// A word this app does not know, as the CLI wrote it.
    case other(String)

    public init(word: String) {
        switch word {
        case "torch": self = .torch
        case "mlx": self = .mlx
        default: self = .other(word)
        }
    }

    /// The word, as the CLI writes it and `--engine` takes it.
    public var word: String {
        switch self {
        case .torch: "torch"
        case .mlx: "mlx"
        case .other(let word): word
        }
    }

    /// As a person reads it: PyTorch, MLX, or the word as written.
    public var name: String {
        switch self {
        case .torch: "PyTorch"
        case .mlx: "MLX"
        case .other(let word): word
        }
    }

    /// A record's engine: the one it states, else PyTorch -- a clip's record
    /// from before the CLI's 53l, or a speech's from before 53m, ran there.
    public static func recorded(_ stated: MediaEngine?) -> MediaEngine { stated ?? .torch }

    /// The engine a store's video model runs on, by its format and its kind
    /// as `models list` states them (the CLI's 53l): a diffusers pipeline on
    /// PyTorch, an MLX directory of kind `video` on MLX; `nil` for anything
    /// `video generate` does not run.
    public static func video(format: String?, kind: String?) -> MediaEngine? {
        if format == "pipeline" { return .torch }
        if format == "mlx" && kind == "video" { return .mlx }
        return nil
    }

    /// The engine a store's voice model speaks on (the CLI's 53m): a voice
    /// model as Kokoro publishes it on PyTorch, an MLX directory of kind
    /// `voice` on MLX; `nil` for anything `voice say` does not speak with.
    public static func voice(format: String?, kind: String?) -> MediaEngine? {
        if format == "voice" { return .torch }
        if format == "mlx" && kind == "voice" { return .mlx }
        return nil
    }

    /// What ran, as a record says it: the engine, then each version its
    /// `versions` names beyond Apogee's own -- `MLX · mlx 0.31.2, mlx-video
    /// 0.1.0`. A version the record leaves empty (an MLX run's `torch`) is
    /// not one that ran.
    public func said(versions: [String: String]) -> String {
        let ran = versions.filter { $0.key != "apogee" && !$0.value.isEmpty }.sorted { $0.key < $1.key }
        guard !ran.isEmpty else { return name }
        return name + " · " + ran.map { "\($0.key.replacingOccurrences(of: "_", with: "-")) \($0.value)" }.joined(separator: ", ")
    }
}

/// One of the store's video or voice models: its store handle, and the engine
/// its directory runs on.
public struct MediaModel: Sendable, Equatable, Hashable, Identifiable {
    /// `<model>/<format>/<id>`, what `-m` takes.
    public let handle: String
    public let engine: MediaEngine

    public init(handle: String, engine: MediaEngine) {
        self.handle = handle
        self.engine = engine
    }

    public var id: String { handle }

    /// The model folder: the handle's first part.
    public var folder: String { handle.split(separator: "/").first.map(String.init) ?? handle }

    /// The models in the order a sheet offers them: each folder where the
    /// store first lists it, and PyTorch before MLX within a folder -- the
    /// CLI's `auto` picks neither, so PyTorch comes first (the GUI's 3b, the
    /// open call's default).
    public static func offered(_ models: [MediaModel]) -> [MediaModel] {
        var folders: [String] = []
        for model in models where !folders.contains(model.folder) { folders.append(model.folder) }
        func rank(_ engine: MediaEngine) -> Int {
            switch engine {
            case .torch: 0
            case .mlx: 1
            case .other: 2
            }
        }
        return models.enumerated().sorted { a, b in
            let (fa, fb) = (folders.firstIndex(of: a.element.folder)!, folders.firstIndex(of: b.element.folder)!)
            if fa != fb { return fa < fb }
            if rank(a.element.engine) != rank(b.element.engine) { return rank(a.element.engine) < rank(b.element.engine) }
            return a.offset < b.offset
        }.map(\.element)
    }

    /// Whether `model`'s folder holds models on more than one engine -- a
    /// pipeline beside its MLX conversion, Kokoro beside its MLX build. Then
    /// the engine is the person's choice, passed as `--engine`; anywhere
    /// else it is the directory's alone, shown and never passed.
    public static func choosesEngine(_ model: MediaModel, among models: [MediaModel]) -> Bool {
        Set(models.filter { $0.folder == model.folder }.map(\.engine)).count > 1
    }

    /// How a picker names it among `models`: its folder and engine
    /// (`Kokoro-82M · MLX`), or -- where its folder holds another on the same
    /// engine -- its handle and engine.
    public func label(among models: [MediaModel]) -> String {
        let twin = models.contains { $0 != self && $0.folder == folder && $0.engine == engine }
        return (twin ? handle : folder) + " · " + engine.name
    }

    /// The model a name given to `-m` names among `models`, as the CLI finds
    /// one in the store: its handle, or a model folder holding exactly one;
    /// `nil` where it names none, or a folder holding several -- which the
    /// CLI refuses, asking for one by its handle.
    public static func named(_ name: String, in models: [MediaModel]) -> MediaModel? {
        if let exact = models.first(where: { $0.handle == name }) { return exact }
        let inFolder = models.filter { $0.folder == name }
        return inFolder.count == 1 ? inFolder[0] : nil
    }
}
