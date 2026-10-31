import BusinessReads
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
/// own chain -- convert, each further level, the registrations -- in the one
/// shape the CLI takes it. With `--mlx`, `--register-with` names every
/// precision to convert, `-t` refused beside it (the CLI's M16); a GGUF
/// registers and quantizes from the F16 alone, so another precision is made
/// on its own (M2: the shapes held to the binary's recorded verdicts).
///
/// An MLX conversion runs the converter the set's `models info` names
/// (`data.mlx_conversion`, the CLI's 53f): towers kept through mlx-vlm, or
/// with **Text only** (`--text-only`) mlx-lm's build of the language model
/// alone -- passed always where only that is offered. What a conversion makes
/// that no backend runs -- a drafter, a video or voice model -- takes none of
/// `--register`, `--register-with`, `--text-only` or `--model-type`, which the
/// CLI refuses there (53i, 53k, 53m).
public struct ConvertForm: Equatable, Sendable {
    /// The set, by handle: SafeTensors, or a voice model (to MLX).
    public let handle: String
    /// `--mlx`: an MLX model for Apple silicon; else a GGUF for llama.cpp.
    public let mlx: Bool
    /// What an MLX conversion of the set runs, as its `models info` states
    /// it; `nil` where it does not say (a binary before 53f), when the sheet
    /// reads as it did then.
    public let conversion: ModelInfo.MLXConversion?
    /// The CLI's own `towers:` line, where its `models info` has one.
    public let towersSaid: String?
    /// `--text-only`: mlx-lm's build of the language model alone, the towers
    /// left out. Only where the CLI offers it; always where that is all it
    /// offers.
    public var textOnly = false {
        didSet { if textOnlyOnly || !textOnlyOffered { textOnly = textOnlyOnly } }
    }
    /// `-t`: the precision written; `nil` leaves the CLI's default
    /// (`defaultPrecision`).
    /// A GGUF at another precision is made on its own: no registering, no
    /// levels.
    public var precision: String? {
        didSet { if alone { levels = []; register = false } }
    }
    /// `--register-with`: further levels, each its own backend. A GGUF's start
    /// from the F16.
    public var levels: Set<String> = [] {
        didSet { if !mlx && !levels.isEmpty && alone { precision = nil } }
    }
    /// `--register`: register what is made -- a GGUF's being the F16.
    public var register = true {
        didSet { if !mlx && register && alone { precision = nil } }
    }
    /// `--base-name`: what each backend's name begins with; empty is the model's name.
    public var baseName = ""
    /// `--model-type` (MLX): the type mlx-lm should take the weights as.
    public var modelType = ""
    /// The set's stated bytes, for the estimates.
    public let bytes: Int?

    /// A conversion as the sheet built it before the CLI said which
    /// converter runs (and as it still does where its `models info` does not).
    public init(handle: String, mlx: Bool, bytes: Int?) {
        self.init(handle: handle, mlx: mlx, bytes: bytes, conversion: nil, towersSaid: nil)
    }

    public init(handle: String, mlx: Bool, bytes: Int?, conversion: ModelInfo.MLXConversion?, towersSaid: String?) {
        self.handle = handle
        self.mlx = mlx
        self.bytes = bytes
        self.conversion = mlx ? conversion : nil
        self.towersSaid = mlx ? towersSaid : nil
        textOnly = textOnlyOnly
        register = registers
    }

    public static let ggufPrecisions = ["f16", "bf16", "f32", "q8_0", "auto"]
    public static let mlxPrecisions = ["4bit", "3bit", "6bit", "8bit", "mxfp4"]
    public static let ggufLevels = ["Q2_K", "Q3_K_M", "Q4_K_M", "Q5_K_M", "Q6_K", "Q8_0"]

    // MARK: - Which converter runs

    /// The converter `models convert --mlx` runs: what the CLI states, or
    /// mlx-lm where `--text-only` asks for its build; `nil` where the CLI
    /// does not say.
    public var converter: String? {
        guard mlx, let conversion else { return nil }
        return textOnly ? "mlx-lm" : conversion.converter
    }

    /// The towers its configuration declares, as the CLI names them.
    public var towers: [String] { conversion?.towers ?? [] }

    /// Whether **Text only** is offered: a set with towers whose language
    /// model mlx-lm builds alone (`data.mlx_conversion.text_only`).
    public var textOnlyOffered: Bool { mlx && !towers.isEmpty && conversion?.textOnly == true }

    /// Whether mlx-lm's text-only build is all the CLI offers -- no converter
    /// for a plain `--mlx`, which it refuses naming `--text-only` (53f).
    public var textOnlyOnly: Bool { textOnlyOffered && conversion?.converter == nil }

    /// Whether what the conversion makes is a backend's to run, so it may
    /// register, convert to further levels and take a `--model-type`: a
    /// language model's build (mlx-lm, or Text only), a vision model's
    /// (mlx-vlm keeping its towers), an embedding model's (an entry the
    /// embedding role names, 53h). A drafter (mlx-vlm with no towers, 53i), a
    /// video model (mlx-video, 53k), a voice model (mlx-audio, 53m) and a
    /// converter this app does not know take none of those flags.
    public var registers: Bool {
        guard mlx, let conversion else { return true }
        switch converter {
        case "mlx-lm"?, "mlx-embeddings"?: return true
        case "mlx-vlm"?: return !conversion.towers.isEmpty
        default: return false
        }
    }

    /// The precision the CLI writes when none is named: f16 for a GGUF; bf16
    /// for a drafter and a voice model, 4bit for every other MLX conversion.
    public var defaultPrecision: String {
        guard mlx else { return "f16" }
        return isDrafter || converter == "mlx-audio" ? "bf16" : "4bit"
    }

    /// A draft head's conversion: mlx-vlm with no tower to keep (53i).
    private var isDrafter: Bool { conversion?.converter == "mlx-vlm" && towers.isEmpty }

    /// The precisions the sheet offers, the default first: mlx-video writes 4
    /// or 8 bits alone of the app's MLX precisions (53k).
    public var precisions: [String] {
        guard mlx else { return Self.ggufPrecisions }
        if converter == "mlx-video" { return ["4bit", "8bit"] }
        return defaultPrecision == "4bit" ? Self.mlxPrecisions : [defaultPrecision] + Self.mlxPrecisions
    }
    /// The further levels the sheet offers: GGUF quantizations, or the MLX
    /// precisions besides the one written -- none where nothing registers.
    public var levelChoices: [String] {
        guard mlx else { return Self.ggufLevels }
        return registers ? precisions.filter { $0 != written } : []
    }
    /// What `-t` writes, the CLI's default when none is chosen.
    public var written: String { precision ?? defaultPrecision }

    /// Which converter runs, as the sheet says it.
    public var converterNote: String {
        guard mlx else {
            return "A GGUF, made by llama.cpp's converter on this Mac and kept beside these weights in the model's gguf/ folder. Quantized copies are made from it."
        }
        let towers = self.towers.joined(separator: ", ")
        if textOnly {
            return "An MLX model of its language model alone, made by mlx-lm's converter on this Mac and kept in the model's mlx/ folder. Its towers (\(towers)) are left out, so it reads no images."
        }
        switch converter {
        case "mlx-vlm"? where !self.towers.isEmpty:
            return "An MLX vision model, made by mlx-vlm's converter on this Mac with its towers kept (\(towers)), and kept in the model's mlx/ folder."
        case "mlx-vlm"?:
            return "An MLX drafter, made by mlx-vlm's converter on this Mac. It runs beside its base model as that backend's draft, so nothing is registered."
        case "mlx-embeddings"?:
            return "An MLX embedding model, made by mlx-embeddings on this Mac. Registered, it is an entry the embedding role can name; it answers no chat."
        case "mlx-video"?:
            return "An MLX video model, made by mlx-video's converter on this Mac. Video generate runs it and no backend does, so nothing is registered."
        case "mlx-audio"?:
            return "An MLX voice model, made by mlx-audio on this Mac. Voice say speaks with it and no backend does, so nothing is registered."
        case let other? where other != "mlx-lm":
            return "An MLX model, made by \(other) on this Mac and kept in the model's mlx/ folder."
        default:
            return "An MLX model, made by mlx-lm's converter on this Mac and kept beside these weights in the model's mlx/ folder."
        }
    }

    /// A GGUF at a precision other than f16: the CLI registers and quantizes
    /// from the F16 alone ("--register makes the F16 -- the source every
    /// quantization starts from"), so this one is made by itself.
    public var alone: Bool { !mlx && written != "f16" }

    /// Why a GGUF at another precision registers nothing and makes no levels.
    public var aloneNote: String? {
        alone ? "Registering and quantizing start from the F16, so a \(written) GGUF is made on its own. Choose f16 to register it or quantize from it." : nil
    }

    /// The command, exactly as the sheet shows it.
    public var arguments: [String] {
        var arguments = ["models", "convert", handle]
        if mlx { arguments.append("--mlx") }
        if mlx && textOnly && textOnlyOffered { arguments.append("--text-only") }
        let chosen = levelChoices.filter(levels.contains)
        if mlx && !chosen.isEmpty {
            // Every precision listed, the written one first: the CLI converts
            // and names them in this order.
            arguments += ["--register-with", ([written] + chosen).joined(separator: ",")]
        } else {
            if let precision { arguments += ["-t", precision] }
            if !chosen.isEmpty {
                arguments += ["--register-with", chosen.joined(separator: ",")]
            } else if register && registers {
                arguments.append("--register")
            }
        }
        let base = baseName.trimmingCharacters(in: .whitespaces)
        if !base.isEmpty && registers && (register || !chosen.isEmpty) { arguments += ["--base-name", base] }
        let type = modelType.trimmingCharacters(in: .whitespaces)
        if mlx && registers && !type.isEmpty { arguments += ["--model-type", type] }
        return arguments
    }

    /// The steps the command takes, in its order, each with its estimate.
    public var steps: [StoreStep] {
        let chosen = levelChoices.filter(levels.contains)
        let alone = textOnly ? ", its language model alone" : ""
        let first = mlx ? "Convert to MLX \(written)\(alone)" : "Convert to GGUF \(written.uppercased())"
        var steps = [StoreStep(label: first, estimate: Estimate.size(written, from: bytes), makesSet: true)]
        for level in chosen {
            steps.append(StoreStep(label: mlx ? "Convert to MLX \(level)\(alone)" : "Quantize to \(level)", estimate: Estimate.size(level, from: bytes), makesSet: true))
        }
        if registers && (register || !chosen.isEmpty) {
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
