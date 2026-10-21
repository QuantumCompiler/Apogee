import Foundation

/// One step a store job takes, as its sheet lists it: what it makes, and
/// about how big -- an estimate, said as one, never used to decide anything.
public struct StoreStep: Equatable, Sendable, Identifiable {
    public let label: String
    /// `about 2.3 GiB`, from bits per weight over the set's stated bytes.
    public let estimate: String?
    /// Whether the step makes a set (it ticks as one lands); a registration
    /// ticks when the run ends well.
    public let makesSet: Bool
    public var id: String { label }

    public init(label: String, estimate: String?, makesSet: Bool) {
        self.label = label
        self.estimate = estimate
        self.makesSet = makesSet
    }
}

/// The estimates' arithmetic: bits per weight, against the 16 a SafeTensors
/// set or an F16 GGUF holds. Approximate by nature, and labelled so.
enum Estimate {
    static let bitsPerWeight: [String: Double] = [
        "f16": 16, "bf16": 16, "f32": 32, "q8_0": 8.5, "auto": 16,
        "F16": 16, "BF16": 16, "F32": 32, "Q8_0": 8.5, "Q6_K": 6.56, "Q5_K_M": 5.69, "Q4_K_M": 4.85, "Q3_K_M": 3.91, "Q2_K": 2.63,
        "4bit": 4.5, "3bit": 3.5, "6bit": 6.5, "8bit": 8.5, "mxfp4": 4.25,
    ]

    /// About how big `precision` makes weights that take `bytes` at 16 bits.
    static func size(_ precision: String, from bytes: Int?) -> String? {
        guard let bytes, let bits = bitsPerWeight[precision] else { return nil }
        return "about " + StoreViewModel.size(Int(Double(bytes) * bits / 16))
    }
}

/// `models convert`, as the convert sheet builds it: one command, the CLI's
/// own chain -- convert, each further level, the registrations.
public struct ConvertForm: Equatable, Sendable {
    /// The SafeTensors set, by handle.
    public let handle: String
    /// `--mlx`: an MLX model for Apple silicon; else a GGUF for llama.cpp.
    public let mlx: Bool
    /// `-t`: the precision written; `nil` leaves the CLI's default (f16, or 4bit).
    public var precision: String?
    /// `--register-with`: further levels, each its own backend.
    public var levels: Set<String> = []
    /// `--register`: register what is made.
    public var register = true
    /// `--base-name`: what each backend's name begins with; empty is the model's name.
    public var baseName = ""
    /// `--model-type` (MLX): the type mlx-lm should take the weights as.
    public var modelType = ""
    /// The set's stated bytes, for the estimates.
    public let bytes: Int?

    public init(handle: String, mlx: Bool, bytes: Int?) {
        self.handle = handle
        self.mlx = mlx
        self.bytes = bytes
    }

    public static let ggufPrecisions = ["f16", "bf16", "f32", "q8_0", "auto"]
    public static let mlxPrecisions = ["4bit", "3bit", "6bit", "8bit", "mxfp4"]
    public static let ggufLevels = ["Q2_K", "Q3_K_M", "Q4_K_M", "Q5_K_M", "Q6_K", "Q8_0"]

    public var precisions: [String] { mlx ? Self.mlxPrecisions : Self.ggufPrecisions }
    /// The further levels the sheet offers: GGUF quantizations, or the MLX
    /// precisions besides the one written.
    public var levelChoices: [String] { mlx ? Self.mlxPrecisions.filter { $0 != written } : Self.ggufLevels }
    /// What `-t` writes, the CLI's default when none is chosen.
    public var written: String { precision ?? (mlx ? "4bit" : "f16") }

    /// The command, exactly as the sheet shows it.
    public var arguments: [String] {
        var arguments = ["models", "convert", handle]
        if mlx { arguments.append("--mlx") }
        if let precision { arguments += ["-t", precision] }
        let chosen = levelChoices.filter(levels.contains)
        if !chosen.isEmpty {
            arguments += ["--register-with", chosen.joined(separator: ",")]
        } else if register {
            arguments.append("--register")
        }
        let base = baseName.trimmingCharacters(in: .whitespaces)
        if !base.isEmpty && (register || !chosen.isEmpty) { arguments += ["--base-name", base] }
        let type = modelType.trimmingCharacters(in: .whitespaces)
        if mlx && !type.isEmpty { arguments += ["--model-type", type] }
        return arguments
    }

    /// The steps the command takes, in its order, each with its estimate.
    public var steps: [StoreStep] {
        let chosen = levelChoices.filter(levels.contains)
        let first = mlx ? "Convert to MLX \(written)" : "Convert to GGUF \(written.uppercased())"
        var steps = [StoreStep(label: first, estimate: Estimate.size(written, from: bytes), makesSet: true)]
        for level in chosen {
            steps.append(StoreStep(label: mlx ? "Convert to MLX \(level)" : "Quantize to \(level)", estimate: Estimate.size(level, from: bytes), makesSet: true))
        }
        if register || !chosen.isEmpty {
            steps.append(StoreStep(label: "Register " + names.joined(separator: ", "), estimate: nil, makesSet: false))
        }
        return steps
    }

    /// The backends' names, by the CLI's naming: the base, then the level
    /// without its underscores (`-F16`, `-Q4KM`), or MLX's precision (`-4bit`).
    public var names: [String] {
        let base = baseName.trimmingCharacters(in: .whitespaces).isEmpty
            ? modelBaseName(handle.split(separator: "/").first.map(String.init) ?? handle)
            : baseName.trimmingCharacters(in: .whitespaces)
        let made = [mlx ? written : written.uppercased()] + levelChoices.filter(levels.contains)
        return made.map { base + "-" + $0.replacingOccurrences(of: "_", with: "") }
    }
}

/// `models quantize`, as the quantize sheet builds it -- and, since the verb
/// takes no `--register`, the `config add-backend` that follows when asked.
public struct QuantizeForm: Equatable, Sendable {
    /// The unquantized GGUF, by handle.
    public let handle: String
    /// `-t`; Q4_K_M is the CLI's default and is left out.
    public var level = "Q4_K_M"
    public var register = true
    /// The backend's name: `<base>-<level>` by the CLI's naming, unless typed.
    public var name: String
    public let bytes: Int?
    /// The model's base name, for the default.
    public let base: String
    private var named = false

    public init(handle: String, base: String, bytes: Int?) {
        self.handle = handle
        self.base = base
        self.bytes = bytes
        name = Self.name(base: base, level: "Q4_K_M")
    }

    public static let levels = ["Q2_K", "Q3_K_M", "Q4_K_M", "Q5_K_M", "Q6_K", "Q8_0"]

    /// `<base>-<level>`, the level without its underscores: `Llama3.2-1B-Instruct-Q4KM`.
    public static func name(base: String, level: String) -> String {
        base + "-" + level.replacingOccurrences(of: "_", with: "")
    }

    /// Moves the level, and the name with it until the person has typed one.
    public mutating func choose(_ level: String) {
        self.level = level
        if !named { name = Self.name(base: base, level: level) }
    }

    public mutating func rename(_ name: String) {
        self.name = name
        named = true
    }

    public var arguments: [String] {
        ["models", "quantize", handle] + (level == "Q4_K_M" ? [] : ["-t", level])
    }

    /// The registration that follows, its file read from the store after.
    public func registration(path: String) -> [String]? {
        let name = name.trimmingCharacters(in: .whitespaces)
        guard register, !name.isEmpty else { return nil }
        return ["config", "add-backend", name, "--type", "llamacpp", "--model-path", path]
    }

    public var steps: [StoreStep] {
        [StoreStep(label: "Quantize to \(level)", estimate: Estimate.size(level, from: bytes), makesSet: true)]
            + (register ? [StoreStep(label: "Register \(name)", estimate: nil, makesSet: false)] : [])
    }
}

/// The base a model's backends are named from: the repository part of an
/// `owner--repo` folder, else the folder as it is (the CLI's
/// `model_base_name`).
public func modelBaseName(_ folder: String) -> String {
    if let range = folder.range(of: "--") { return String(folder[range.upperBound...]) }
    return folder
}
