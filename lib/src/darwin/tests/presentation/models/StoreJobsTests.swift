import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationModels

private func recorded(_ name: String, _ ext: String = "json") -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
        .appendingPathComponent("../../business/reads/Fixtures/\(name).\(ext)")
    return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
}

/// The convert and quantize sheets' commands, pinned byte for byte and to the
/// binary's own `--help` words (recorded); the store's long runs on the jobs
/// core, their steps ticking from scripted read-backs. Nothing converts.
@Suite("StoreJobs", .timeLimit(.minutes(1)))
@MainActor
struct StoreJobsTests {
    static let snapshot = "Llama3.2-1B-Instruct/safetensors/edaba290ce7f"
    static let f16 = "Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41"

    /// Every flag the form writes is one the binary's help names.
    func pinned(_ arguments: [String], to help: String) {
        for word in arguments where word.hasPrefix("-") {
            #expect(help.contains(word), "\(word) is no flag of the binary's: \(help.prefix(80))")
        }
    }

    @Test("convert to GGUF: the CLI's defaults left out; a precision, levels, registering and the base name as chosen")
    func convertGGUF() {
        let help = recorded("models-convert-help", "txt")
        var form = ConvertForm(handle: Self.snapshot, mlx: false, bytes: 2_482_980_600)
        #expect(form.arguments == ["models", "convert", Self.snapshot, "--register"], "f16 the default, registering on")
        form.register = false
        #expect(form.arguments == ["models", "convert", Self.snapshot])
        form.precision = "bf16"
        form.levels = ["Q8_0", "Q4_K_M"]
        form.baseName = "llama-1b"
        #expect(form.arguments == ["models", "convert", Self.snapshot, "-t", "bf16", "--register-with", "Q4_K_M,Q8_0", "--base-name", "llama-1b"],
                "levels in the CLI's order; choosing one turns registering on")
        form.modelType = "llama"
        #expect(!form.arguments.contains("--model-type"), "a GGUF takes no MLX model type")
        pinned(form.arguments, to: help)
        #expect(form.steps.map(\.label) == ["Convert to GGUF BF16", "Quantize to Q4_K_M", "Quantize to Q8_0", "Register llama-1b-BF16, llama-1b-Q4KM, llama-1b-Q80"])
        #expect(form.steps.map(\.estimate) == ["about 2.3 GiB", "about 717 MiB", "about 1.2 GiB", nil], "estimates, said as such")
    }

    @Test("convert to MLX: 4bit the default, further precisions one conversion each, the model type as typed")
    func convertMLX() {
        let help = recorded("models-convert-help", "txt")
        var form = ConvertForm(handle: Self.snapshot, mlx: true, bytes: nil)
        #expect(form.arguments == ["models", "convert", Self.snapshot, "--mlx", "--register"])
        #expect(form.levelChoices == ["3bit", "6bit", "8bit", "mxfp4"], "the written precision is no further level")
        form.precision = "8bit"
        form.levels = ["4bit"]
        form.modelType = "gemma3"
        #expect(form.arguments == ["models", "convert", Self.snapshot, "--mlx", "-t", "8bit", "--register-with", "4bit", "--model-type", "gemma3"])
        pinned(form.arguments, to: help)
        #expect(form.steps.map(\.label) == ["Convert to MLX 8bit", "Convert to MLX 4bit", "Register Llama3.2-1B-Instruct-8bit, Llama3.2-1B-Instruct-4bit"])
        #expect(form.steps.allSatisfy { $0.estimate == nil }, "no size stated, no estimate")
    }

    @Test("quantize: Q4_K_M the default left out; the name follows the level until typed; the registration that follows")
    func quantizeForm() {
        let help = recorded("models-quantize-help", "txt")
        var form = QuantizeForm(handle: Self.f16, base: "Llama3.2-1B-Instruct", bytes: 2_479_596_089)
        #expect(form.arguments == ["models", "quantize", Self.f16])
        #expect(form.name == "Llama3.2-1B-Instruct-Q4KM")
        form.choose("Q8_0")
        #expect(form.arguments == ["models", "quantize", Self.f16, "-t", "Q8_0"] && form.name == "Llama3.2-1B-Instruct-Q8_0".replacingOccurrences(of: "_", with: ""))
        pinned(form.arguments, to: help)
        form.rename("small")
        form.choose("Q2_K")
        #expect(form.name == "small", "a typed name stays")
        #expect(form.registration(path: "/x/a.gguf") == ["config", "add-backend", "small", "--type", "llamacpp", "--model-path", "/x/a.gguf"])
        form.register = false
        #expect(form.registration(path: "/x/a.gguf") == nil)
        #expect(form.steps.map(\.label) == ["Quantize to Q2_K"])
        #expect(modelBaseName("Wan-AI--Wan2.2-TI2V-5B") == "Wan2.2-TI2V-5B" && modelBaseName("Gemma4-12B-Instruct") == "Gemma4-12B-Instruct")
    }

    func jobs(_ scripted: [ScriptedJob], _ runner: ScriptedRunner) -> (DownloadsViewModel, ScriptedJobs) {
        let starter = ScriptedJobs(scripted)
        let model = DownloadsViewModel(starter: starter)
        model.connect(binary: URL(fileURLWithPath: "/Users/someone/.local/bin/apogee"))
        model.reader = runner
        return (model, starter)
    }

    static func list(_ names: [String]) -> String {
        let rows = names.map { name in
            name.contains("/")
                ? #"{"backend":"(not configured)","backend_type":"-","model":"\#(name)","state":"ok"}"#
                : #"{"backend":"\#(name)","backend_type":"llamacpp","model":"\#(name).gguf","state":"ok"}"#
        }
        return #"{"object":"list","data":[\#(rows.joined(separator: ","))]}"#
    }

    @Test("a conversion runs the sheet's command; its steps tick as the sets land, registration on a good end; Show the weights")
    func convertJob() async throws {
        let job = ScriptedJob()
        let runner = ScriptedRunner()
        runner.reading(["models", "list", "--all"], Self.list([Self.snapshot]))
        let (model, starter) = jobs([job], runner)
        var opened: String?
        model.showWeights = { opened = $0 }
        var form = ConvertForm(handle: Self.snapshot, mlx: false, bytes: nil)
        form.levels = ["Q4_K_M"]
        model.convert(form)
        #expect(await eventually { starter.starts.count == 1 })
        #expect(starter.starts[0].0 == ["models", "convert", Self.snapshot, "--register-with", "Q4_K_M"] && !starter.starts[0].1)
        let download = try #require(model.downloads.first)
        #expect(download.kind == .convert && download.state(of: 0) == .running)
        runner.reading(["models", "list", "--all"], Self.list([Self.snapshot, Self.f16]))
        job.say(.said("converting Llama3.2-1B-Instruct to F16"))
        #expect(await eventually { download.state(of: 0) == .done })
        #expect(download.state(of: 1) == .running && download.state(of: 2) == .waiting)
        runner.reading(["models", "list", "--all"], Self.list([Self.snapshot, "Llama3.2-1B-Instruct-F16", "Llama3.2-1B-Instruct-Q4KM"]))
        job.end()
        #expect(await eventually { !download.isRunning && download.endedAt != nil })
        #expect((0..<3).map(download.state(of:)) == [.done, .done, .done])
        #expect(download.firstLanded == "Llama3.2-1B-Instruct-F16")
        model.showWeights?(download.firstLanded!)
        #expect(opened == "Llama3.2-1B-Instruct-F16")
    }

    @Test("Stop interrupts: what landed stays landed; the CLI's resume command runs as typed")
    func stopAndResume() async throws {
        let first = ScriptedJob()
        let second = ScriptedJob()
        let runner = ScriptedRunner()
        runner.reading(["models", "list", "--all"], Self.list([Self.snapshot]))
        let (model, starter) = jobs([first, second], runner)
        var form = ConvertForm(handle: Self.snapshot, mlx: false, bytes: nil)
        form.levels = ["Q4_K_M"]
        model.convert(form)
        #expect(await eventually { starter.starts.count == 1 })
        let download = try #require(model.downloads.first)
        runner.reading(["models", "list", "--all"], Self.list([Self.snapshot, Self.f16]))
        first.say(.said("the F16 is made; quantize it with: apogee models quantize \(Self.f16) -t Q4_K_M"))
        await model.stop(download)
        #expect(first.stops == 1)
        #expect(await eventually { download.progress.status == .stopped && download.endedAt != nil })
        #expect(download.state(of: 0) == .done, "the made set stays")
        #expect(download.progress.resumeCommand == ["models", "quantize", Self.f16, "-t", "Q4_K_M"])
        model.resume(download)
        #expect(starter.starts.last?.0 == ["models", "quantize", Self.f16, "-t", "Q4_K_M"])
        second.end()
    }

    @Test("a quantization registers what it made from the read-back, as asked; a refusal leaves the GGUF and is said")
    func quantizeJob() async throws {
        for refused in [false, true] {
            let job = ScriptedJob()
            let runner = ScriptedRunner()
            runner.reading(["models", "list", "--all"], Self.list([Self.f16]))
            runner.reading(["models", "info", "Llama3.2-1B-Instruct/gguf/8d51e6c04a27"], recorded("store-info-q4"))
            if refused {
                runner.commanding(["config", "add-backend"], .failed(exit: ChildExit(reason: .exited, status: 1), stderrTail: tail("apogee config: a backend named 'Llama3.2-1B-Instruct-Q4KM' exists")))
            }
            let (model, starter) = jobs([job], runner)
            model.quantize(QuantizeForm(handle: Self.f16, base: "Llama3.2-1B-Instruct", bytes: nil))
            #expect(await eventually { starter.starts.count == 1 })
            #expect(starter.starts[0].0 == ["models", "quantize", Self.f16])
            let download = try #require(model.downloads.first)
            runner.reading(["models", "list", "--all"], Self.list([Self.f16, "Llama3.2-1B-Instruct/gguf/8d51e6c04a27"]))
            job.end()
            #expect(await eventually { download.endedAt != nil })
            let registration = "run config add-backend Llama3.2-1B-Instruct-Q4KM --type llamacpp --model-path /Users/someone/.apogee/models/Llama3.2-1B-Instruct/gguf/8d51e6c04a27/Llama3.2-1B-Instruct-Q4KM.gguf"
            #expect(runner.calls.contains(registration), "the new file from the read-back's models info")
            if refused {
                #expect(download.outcome?.hasPrefix("The GGUF is made; registering it was refused: apogee config: a backend named 'Llama3.2-1B-Instruct-Q4KM' exists") == true)
                #expect(download.state(of: 1) == .waiting, "not registered")
            } else {
                #expect(download.state(of: 1) == .done)
            }
            #expect(download.state(of: 0) == .done)
        }
    }

    @Test("Check the files is a job: its result the CLI's last line, or its failure in its words")
    func checkJob() async throws {
        let passing = ScriptedJob()
        let failing = ScriptedJob()
        let runner = ScriptedRunner()
        runner.reading(["models", "list", "--all"], Self.list([Self.snapshot]))
        let (model, starter) = jobs([passing, failing], runner)
        model.check(Self.snapshot)
        #expect(await eventually { starter.starts.count == 1 })
        #expect(starter.starts[0].0 == ["models", "repair", Self.snapshot])
        let first = try #require(model.downloads.first)
        passing.say(.said("Llama3.2-1B-Instruct/safetensors/edaba290ce7f: 9 file(s) match the record"))
        passing.end()
        #expect(await eventually { first.endedAt != nil })
        #expect(first.outcome == "Llama3.2-1B-Instruct/safetensors/edaba290ce7f: 9 file(s) match the record")
        model.check(Self.snapshot)
        #expect(await eventually { starter.starts.count == 2 })
        let second = try #require(model.downloads.first)
        failing.end(status: 1)
        #expect(await eventually { second.endedAt != nil })
        if case .failed = second.progress.status {} else { Issue.record("a failing check is no pass") }
        #expect(second.outcome == nil)
    }

    @Test("Stored weights opens the sheets by format and hands the job on; an unquantized GGUF known by its precision or its file")
    func storeOpensTheSheets() async throws {
        let runner = ScriptedRunner()
        runner.reading(["models", "list"], recorded("store-list"))
        runner.reading(["models", "list", "--all"], recorded("store-list-all"))
        runner.reading(["models", "info", "llama3.2-1b-Q4KM"], recorded("store-info-backend"))
        runner.reading(["models", "info", "/Users/someone/.apogee/models/Llama3.2-1B-Instruct/gguf/8d51e6c04a27/Llama3.2-1B-Instruct-Q4KM.gguf"], recorded("store-info-q4"))
        runner.reading(["models", "info", Self.f16], recorded("store-info-f16"))
        runner.reading(["models", "info", Self.snapshot], recorded("store-info-snapshot"))
        runner.reading(["models", "info", "Gemma4-26B-Instruct/mlx/a88b9f386aff"], recorded("store-info-mlx"))
        let store = StoreViewModel(runner: runner)
        await store.connect(binary: URL(fileURLWithPath: "/Users/someone/.local/bin/apogee"))
        store.showingAll = true
        #expect(await eventually { store.sets.count == 6 })
        var converted: ConvertForm?
        var quantized: QuantizeForm?
        var checked: String?
        store.startConvert = { converted = $0 }
        store.startQuantize = { quantized = $0 }
        store.startCheck = { checked = $0 }
        let snapshot = try #require(store.sets.first { $0.handle == Self.snapshot })
        store.askToConvert(snapshot, mlx: true)
        #expect(store.convertForm?.mlx == true && store.convertForm?.bytes == 2_482_980_600)
        store.convert()
        #expect(converted?.arguments == ["models", "convert", Self.snapshot, "--mlx", "--register"] && store.convertForm == nil)
        let f16 = try #require(store.sets.first { $0.handle == Self.f16 })
        let q4 = try #require(store.sets.first { $0.backend != nil })
        #expect(store.isUnquantized(f16), "its file says F16")
        #expect(!store.isUnquantized(q4) && !store.isUnquantized(snapshot))
        store.askToQuantize(q4)
        #expect(store.quantizeForm == nil, "a quantized GGUF has no Quantize")
        store.askToQuantize(f16)
        #expect(store.quantizeForm?.name == "Llama3.2-1B-Instruct-Q4KM")
        store.quantize()
        #expect(quantized?.handle == Self.f16)
        store.check(snapshot)
        #expect(checked == Self.snapshot)
    }
}
