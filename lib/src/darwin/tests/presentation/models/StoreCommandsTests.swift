import BusinessReads
import DataProtocol
import Foundation
import Testing

@testable import PresentationModels

private let fixtures = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("Fixtures")

private func fixture(_ name: String) -> String {
    (try? String(contentsOf: fixtures.appendingPathComponent(name), encoding: .utf8)) ?? ""
}

/// A set's `models info`, recorded over the routes the CLI's 53f-53m decide
/// (`tests/business/reads/Fixtures/store-info-route-*.json`).
private func routeInfo(_ route: String) -> ModelInfo {
    let url = fixtures.appendingPathComponent("../../../business/reads/Fixtures/store-info-route-\(route).json")
    return ModelInfo(document: ReadDocument(text: (try? String(contentsOf: url, encoding: .utf8)) ?? "{}")!)
}

/// The store's commands held to the binary's own verdicts (M2): each command
/// the convert and quantize sheets can build, and the renames, recorded from a
/// real binary by `scripts/refresh-store-commands-fixture.sh` -- never to its
/// `--help`, which names the flags and not the rules between them. A command
/// the binary refuses fails here, and so does one it was never asked.
@Suite("StoreCommands")
@MainActor
struct StoreCommandsTests {
    /// The sets the recorded commands name: none exists, so the binary judges
    /// the flags and converts nothing.
    static let snapshot = "Nothing-Here/safetensors/000000000000"
    static let gguf = "Nothing-Here/gguf/000000000000"
    static let binary = URL(fileURLWithPath: "/usr/local/bin/apogee")

    /// `command` -> `taken` or `refused: <the binary's first line>`.
    static let verdicts: [String: String] = {
        var out: [String: String] = [:]
        for line in fixture("store-command-verdicts.txt").split(separator: "\n") {
            let parts = line.split(separator: "\t", maxSplits: 1).map(String.init)
            if parts.count == 2 { out[parts[1]] = parts[0] }
        }
        return out
    }()

    /// What an MLX conversion runs, as each route's recorded `models info`
    /// states it (the CLI's 53f-53m) -- and nothing, a binary before 53f.
    static let routes = ["mlx-lm", "mlx-vlm", "text-only", "drafter", "mlx-embeddings", "mlx-video", "mlx-audio"]
    static let conversions: [ModelInfo.MLXConversion?] = [nil] + routes.map { routeInfo($0).data?.mlxConversion }

    /// The convert sheet's states, made as a person makes them: for MLX, the
    /// converter the set's document names and Text only; then a precision,
    /// then further levels, then Register, then a base name, then -- for MLX
    /// -- a model type.
    static func convertStates(of handle: String = snapshot, over conversions: [ModelInfo.MLXConversion?] = conversions) -> [ConvertForm] {
        var forms: [ConvertForm] = []
        for mlx in [false, true] {
            for conversion in mlx ? conversions : [nil] {
                for textOnly in mlx ? [false, true] : [false] {
                    var opened = ConvertForm(handle: handle, mlx: mlx, bytes: nil, conversion: conversion, towersSaid: nil)
                    opened.textOnly = textOnly
                    let precisions: [String?] = [nil] + opened.precisions.dropFirst().map { $0 }
                    for precision in precisions {
                        for count in 0...2 {
                            for register in [true, false] {
                                for base in ["", "llama-1b"] {
                                    for type in mlx ? ["", "gemma3"] : [""] {
                                        var form = opened
                                        form.precision = precision
                                        form.levels = Set(form.levelChoices.prefix(count))
                                        form.register = register
                                        form.baseName = base
                                        form.modelType = type
                                        forms.append(form)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        return forms
    }

    /// Each command's verdict: those never recorded, and those refused.
    static func judged(_ commands: [[String]]) -> (missing: [String], refused: [String]) {
        var missing: [String] = []
        var refused: [String] = []
        for line in Set(commands.map { $0.joined(separator: " ") }).sorted() {
            switch verdicts[line] {
            case nil: missing.append("?\t" + line)
            case "taken": continue
            case let verdict?: refused.append(line + " -- " + verdict)
            }
        }
        return (missing, refused)
    }

    @Test("every command the convert sheet can build is one the binary takes")
    func convertCommandsTaken() {
        #expect(!Self.verdicts.isEmpty, "no recorded verdicts: run scripts/refresh-store-commands-fixture.sh")
        #expect(Self.conversions.dropFirst().allSatisfy { $0 != nil }, "every route's document states its conversion")
        let (missing, refused) = Self.judged(Self.convertStates().map(\.arguments))
        #expect(missing.isEmpty, "never recorded -- add these lines to Fixtures/store-command-verdicts.txt and run scripts/refresh-store-commands-fixture.sh:\n\(missing.joined(separator: "\n"))")
        #expect(refused.isEmpty, "the binary refuses:\n\(refused.joined(separator: "\n"))")
    }

    @Test("where the CLI refuses registering what a conversion makes -- a draft head, Wan's original layout, a voice model -- the binary's recorded refusals stand, and the sheet builds none of them")
    func routesNoBackendRuns() {
        var verdicts: [String: String] = [:]
        for line in fixture("store-route-verdicts.txt").split(separator: "\n") {
            let parts = line.split(separator: "\t", maxSplits: 1).map(String.init)
            if parts.count == 2 { verdicts[parts[1]] = parts[0] }
        }
        #expect(verdicts.count == 12, "recorded: \(verdicts.keys.sorted())")
        let sets = [
            ("drafter", "Gemma4-E4B-Assistant/safetensors/b1c2d3e4f5a6"), ("mlx-video", "Wan-AI--Wan2.2-TI2V-5B/safetensors/6ef9b00b8b83"),
            ("mlx-audio", "Kokoro-82M/voice/5f3a2c91d0e4"),
        ]
        for (route, handle) in sets {
            for flags in ["--register", "--register-with 8bit,4bit", "--text-only", "--model-type gemma4"] {
                let command = "models convert \(handle) --mlx \(flags)"
                #expect(verdicts[command]?.hasPrefix("refused: apogee models: '\(handle)' is ") == true, "\(command): \(verdicts[command] ?? "not recorded")")
            }
            let conversion = routeInfo(route).data?.mlxConversion
            #expect(conversion != nil)
            let built = Self.convertStates(of: handle, over: [conversion]).filter(\.mlx).map(\.arguments)
            for arguments in built {
                #expect(arguments.allSatisfy { !["--register", "--register-with", "--text-only", "--model-type", "--base-name"].contains($0) }, "\(arguments)")
                #expect(verdicts[arguments.joined(separator: " ")] == nil)
            }
        }
    }

    @Test("every command the quantize sheet can build is one the binary takes")
    func quantizeCommandsTaken() {
        let forms = QuantizeForm.levels.map { level in
            var form = QuantizeForm(handle: Self.gguf, base: "Nothing-Here", bytes: nil)
            form.choose(level)
            return form
        }
        let (missing, refused) = Self.judged(forms.map(\.arguments))
        #expect(missing.isEmpty, "never recorded -- add these lines to Fixtures/store-command-verdicts.txt and run scripts/refresh-store-commands-fixture.sh:\n\(missing.joined(separator: "\n"))")
        #expect(refused.isEmpty, "the binary refuses:\n\(refused.joined(separator: "\n"))")
    }

    @Test("the user's MLX case: 8bit then 4bit names both, with no -t; the command they were refused stays refused")
    func userMLXCase() {
        var form = ConvertForm(handle: Self.snapshot, mlx: true, bytes: nil)
        form.precision = "8bit"
        form.levels = ["4bit"]
        #expect(form.arguments == ["models", "convert", Self.snapshot, "--mlx", "--register-with", "8bit,4bit"])
        #expect(Self.verdicts[form.arguments.joined(separator: " ")] == "taken")
        #expect(form.steps.map(\.label) == ["Convert to MLX 8bit", "Convert to MLX 4bit", "Register Nothing-Here-8bit, Nothing-Here-4bit"], "the steps and names unchanged")
        let before = "models convert \(Self.snapshot) --mlx -t 8bit --register-with 4bit"
        #expect(Self.verdicts[before]?.hasPrefix("refused: apogee models: with --mlx, --register-with names every precision to convert") == true,
                "the user's refusal, recorded: \(Self.verdicts[before] ?? "not recorded")")
    }

    @Test("a GGUF at another precision is made on its own, said; Register or a level puts f16 back")
    func ggufAlone() {
        var form = ConvertForm(handle: Self.snapshot, mlx: false, bytes: nil)
        form.precision = "bf16"
        #expect(!form.register && form.levels.isEmpty && form.alone)
        #expect(form.aloneNote?.contains("start from the F16") == true)
        #expect(form.arguments == ["models", "convert", Self.snapshot, "-t", "bf16"])
        form.register = true
        #expect(form.precision == nil && !form.alone, "Register makes the F16")
        form.precision = "q8_0"
        form.levels = ["Q4_K_M"]
        #expect(form.precision == nil, "a level quantizes from the F16")
        #expect(form.arguments == ["models", "convert", Self.snapshot, "--register-with", "Q4_K_M"])
        let refused = "models convert \(Self.snapshot) -t bf16 --register"
        #expect(Self.verdicts[refused]?.hasPrefix("refused: apogee models: --register makes the F16") == true, "recorded: \(Self.verdicts[refused] ?? "not recorded")")
    }

    @Test("the user's rename: an MLX backend renames as a backend; a family the binary completes renames as a model")
    func userRename() async throws {
        let runner = ScriptedRunner()
        runner.reading(["models", "list"], fixture("models-rename-list.json"))
        runner.reading(["models", "status"], #"{"fields":[]}"#)
        runner.commanding(["__complete", "models", "rename"], .done(stdout: fixture("models-rename-complete.txt")))
        let model = ModelsViewModel(runner: runner)
        await model.connect(binary: Self.binary)
        #expect(model.modelNames == ["GPT-OSS-20B", "qwen3-8b"], "the binary's own names")

        let mlx = try #require(model.rows.first { $0.backend == "GPT-OSS-20B-mixed_4_8" }, "rows: \(model.rows.map(\.backend)), listing: \(model.listing)")
        #expect(mlx.quant == "mixed 4/8-bit (group 32)", "the level as the binary lists it")
        #expect(!model.renamesAsModel(mlx), "the binary completes no model by this name")
        model.askToRename(mlx, .model)
        #expect(model.renaming == nil, "no model rename offered")
        model.askToRename(mlx, .backend)
        await model.rename(try #require(model.renaming), to: "gpt-oss-20b-mixed")
        let backend = "config rename-backend GPT-OSS-20B-mixed_4_8 gpt-oss-20b-mixed"
        #expect(runner.calls.contains("run " + backend))
        #expect(Self.verdicts[backend] == "taken")
        let before = "models rename GPT-OSS-20B-mixed_4_8 gpt-oss-20b-mixed"
        #expect(!runner.calls.contains("run " + before))
        #expect(Self.verdicts[before]?.hasPrefix("refused: apogee models: no model named 'GPT-OSS-20B-mixed_4_8'") == true,
                "the user's refusal, recorded: \(Self.verdicts[before] ?? "not recorded")")

        // The recorded sandbox's GGUF is a placeholder whose header states no
        // level, so its family comes from the listing ModelsViewModelTests
        // holds (`qwen3-8b-Q4KM`, Q4_K_M) -- the command it builds is the
        // binary's to judge.
        let listed = ScriptedRunner()
        listed.reading(["models", "list"], ModelsViewModelTests.list)
        listed.reading(["models", "status"], ModelsViewModelTests.status)
        listed.commanding(["__complete", "models", "rename"], .done(stdout: fixture("models-rename-complete.txt")))
        let library = ModelsViewModel(runner: listed)
        await library.connect(binary: Self.binary)
        let family = try #require(library.rows.first { $0.backend == "qwen3-8b-Q4KM" })
        #expect(library.renamesAsModel(family), "the binary completes qwen3-8b")
        library.askToRename(family, .model)
        await library.rename(try #require(library.renaming), to: "qwen3-8b-chat")
        let whole = "models rename qwen3-8b qwen3-8b-chat"
        #expect(listed.calls.contains("run " + whole))
        #expect(Self.verdicts[whole] == "taken")
    }
}
