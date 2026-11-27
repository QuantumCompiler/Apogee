import BusinessReads
import Foundation

/// A clip to make, as the generate sheet holds it -- and as the command a
/// person would type: `video generate`, every flag the sheet set and nothing
/// it did not.
public struct ClipForm: Sendable, Equatable {
    /// What happens: a new clip, a recorded run again, or a clip continued.
    public enum Start: Sendable, Equatable {
        case new
        /// `--from <record or clip>`: every setting not given taken from it,
        /// the seed held.
        case again(String)
        /// `--extend <clip>`: continued from its last frame.
        case extend(String)
    }

    /// `--draft` (16 steps, the same seed) or `--final` (50 steps).
    public enum Preset: String, Sendable, Equatable, CaseIterable {
        case none
        case draft
        case final
    }

    public var start: Start = .new
    public var prompt = ""
    public var image: String?
    /// A pipeline: a store handle, id or model; empty is the default video pipeline.
    public var model = ""
    public var seed: Int?
    public var duration: Double?
    /// `WIDTHxHEIGHT`.
    public var resolution: String?
    public var steps: Int?
    public var fps: Int?
    public var preset: Preset = .none
    /// Each a `.safetensors` file (or a folder holding one), its strength
    /// after a colon.
    public var loras: [String] = []
    /// No LoRAs: with `--from`, the record's cleared.
    public var noLoRAs = false
    /// `--engine`: set only where the person chose the engine -- a model
    /// folder holding a pipeline beside its MLX conversion (the GUI's 3b).
    /// Anywhere else the model's directory decides, and nothing is passed:
    /// the CLI's flag only names that engine or refuses another (its 53l).
    public var engine: MediaEngine?

    public init() {}

    /// The arguments after `apogee`, writing to `output`; `nil` while the
    /// form cannot run -- a new clip needs a prompt or a seed image. The
    /// prompt is the last word, after `--`, so the binary never reads it as
    /// a flag (`Argv`, GUI 4g). Each `--lora` comes first: it takes a `--`
    /// directly after it as the end of its list (`Argv.endOfOptionsEaters`),
    /// so `-o O --lora L -- '-dash first'` exits 109 (GUI M23).
    public func arguments(output: String) -> [String]? {
        let prompt = prompt.trimmingCharacters(in: .whitespacesAndNewlines)
        var options: [Argv.Option] = []
        if noLoRAs {
            options.append(.flag("--no-lora"))
        } else {
            options += loras.filter { !$0.isEmpty }.map { .value("--lora", $0) }
        }
        switch start {
        case .new:
            guard !prompt.isEmpty || image != nil else { return nil }
        case .again(let record):
            options.append(.value("--from", record))
        case .extend(let clip):
            options.append(.value("--extend", clip))
        }
        options.append(.value("-o", output))
        if let image { options.append(.value("--image", image)) }
        let model = model.trimmingCharacters(in: .whitespaces)
        if !model.isEmpty { options.append(.value("-m", model)) }
        if let engine { options.append(.value("--engine", engine.word)) }
        if let seed { options.append(.value("--seed", String(seed))) }
        if let duration { options.append(.value("--duration", Self.number(duration))) }
        if let resolution, !resolution.isEmpty { options.append(.value("--resolution", resolution)) }
        if let steps, preset == .none { options.append(.value("--steps", String(steps))) }
        if let fps { options.append(.value("--fps", String(fps))) }
        switch preset {
        case .none: break
        case .draft: options.append(.flag("--draft"))
        case .final: options.append(.flag("--final"))
        }
        return Argv.line(["video", "generate"], options: options, names: prompt.isEmpty ? [] : [prompt])
    }

    /// `5` for 5.0, `2.5` for 2.5.
    static func number(_ value: Double) -> String {
        value.rounded() == value ? String(Int(value)) : String(value)
    }

    /// The form for running `record` again, or continuing its clip.
    public static func from(_ record: ClipRecord, extend: Bool) -> ClipForm {
        var form = ClipForm()
        form.start = extend ? .extend(record.output) : .again(record.sidecar ?? record.output + ".json")
        return form
    }
}
