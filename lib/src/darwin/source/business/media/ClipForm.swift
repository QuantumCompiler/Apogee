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
    /// a flag (`Argv`, GUI 4g).
    public func arguments(output: String) -> [String]? {
        let prompt = prompt.trimmingCharacters(in: .whitespacesAndNewlines)
        var arguments = ["video", "generate"]
        switch start {
        case .new:
            guard !prompt.isEmpty || image != nil else { return nil }
        case .again(let record):
            arguments += ["--from", record]
        case .extend(let clip):
            arguments += ["--extend", clip]
        }
        arguments += ["-o", output]
        if let image { arguments += ["--image", image] }
        let model = model.trimmingCharacters(in: .whitespaces)
        if !model.isEmpty { arguments += ["-m", model] }
        if let engine { arguments += ["--engine", engine.word] }
        if let seed { arguments += ["--seed", String(seed)] }
        if let duration { arguments += ["--duration", Self.number(duration)] }
        if let resolution, !resolution.isEmpty { arguments += ["--resolution", resolution] }
        if let steps, preset == .none { arguments += ["--steps", String(steps)] }
        if let fps { arguments += ["--fps", String(fps)] }
        switch preset {
        case .none: break
        case .draft: arguments.append("--draft")
        case .final: arguments.append("--final")
        }
        if noLoRAs {
            arguments.append("--no-lora")
        } else {
            for lora in loras where !lora.isEmpty { arguments += ["--lora", lora] }
        }
        return prompt.isEmpty ? arguments : arguments + Argv.text(prompt)
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
