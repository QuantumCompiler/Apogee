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

/// The CLI's pinned stream of a chained pull that finished
/// (tests/business/jobs/Fixtures/pull-chain-done-handmade.jsonl, hand-made
/// from `model_chain_test.cpp`; a real chain needs real weights).
private func chainStream() -> [JobUpdate] {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
        .appendingPathComponent("../../business/jobs/Fixtures/pull-chain-done-handmade.jsonl")
    let text = (try? String(contentsOf: url, encoding: .utf8)) ?? ""
    return text.split(whereSeparator: \.isNewline).map { .event(JobEvent.decode(line: String($0))) }
}

/// A store job knows what it made and keeps who it is (GUI 4e), over a store
/// recorded from the branch's binary: two tiny F16 GGUFs, Llama's and
/// Gemma's, each quantized for real by the binary's llama.cpp, and what it
/// printed doing so (`quantize-*` in tests/business/reads/Fixtures).
@Suite("JobIdentity", .timeLimit(.minutes(1)))
@MainActor
struct JobIdentityTests {
    static let llamaF16 = "Llama3.2-1B-Instruct/gguf/dfed3eebdfdb"
    static let gemmaF16 = "Gemma3-1B-Instruct/gguf/cce9d2f99787"
    static let llamaQ4 = "Llama3.2-1B-Instruct/gguf/5703795782eb"
    static let gemmaQ4 = "Gemma3-1B-Instruct/gguf/09f55af982ed"
    static let llamaPath = "/Users/someone/.apogee/models/Llama3.2-1B-Instruct/gguf/5703795782eb/Llama3.2-1B-Instruct-Q4_K_M.gguf"
    static let gemmaPath = "/Users/someone/.apogee/models/Gemma3-1B-Instruct/gguf/09f55af982ed/Gemma3-1B-Instruct-Q4_K_M.gguf"
    static let llamaRegistration = "run config add-backend Llama3.2-1B-Instruct-Q4KM --type llamacpp --model-path \(llamaPath)"
    static let gemmaRegistration = "run config add-backend Gemma3-1B-Instruct-Q4KM --type llamacpp --model-path \(gemmaPath)"

    func jobs(_ scripted: [ScriptedJob], _ runner: ScriptedRunner) -> (DownloadsViewModel, ScriptedJobs) {
        let starter = ScriptedJobs(scripted)
        let model = DownloadsViewModel(starter: starter)
        model.connect(binary: URL(fileURLWithPath: "/Users/someone/.local/bin/apogee"))
        model.reader = runner
        return (model, starter)
    }

    /// The store before either quantization, the sets each one's path and
    /// handle name, and the registrations, as the binary answered them.
    func quantizeRunner() -> ScriptedRunner {
        let runner = ScriptedRunner()
        runner.reading(["models", "list", "--all"], recorded("quantize-list-before"))
        runner.reading(["models", "info", Self.llamaPath], recorded("quantize-info-llama-path"))
        runner.reading(["models", "info", Self.gemmaPath], recorded("quantize-info-gemma-path"))
        runner.reading(["models", "info", Self.llamaQ4], recorded("quantize-info-llama"))
        runner.reading(["models", "info", Self.gemmaQ4], recorded("quantize-info-gemma"))
        runner.commanding(["config", "add-backend", "Llama3.2-1B-Instruct-Q4KM"], .done(stdout: recorded("quantize-add-backend-llama", "txt")))
        runner.commanding(["config", "add-backend", "Gemma3-1B-Instruct-Q4KM"], .done(stdout: recorded("quantize-add-backend-gemma", "txt")))
        return runner
    }

    static let llamaForm = QuantizeForm(handle: llamaF16, base: "Llama3.2-1B-Instruct", bytes: nil)
    static let gemmaForm = QuantizeForm(handle: gemmaF16, base: "Gemma3-1B-Instruct", bytes: nil)

    @Test("two quantizations at once: each registers the GGUF its own run made, whatever landed first")
    func concurrentQuantizesRegisterTheirOwn() async throws {
        let llamaJob = ScriptedJob()
        let gemmaJob = ScriptedJob()
        let runner = quantizeRunner()
        let (model, starter) = jobs([llamaJob, gemmaJob], runner)
        model.quantize(Self.llamaForm)
        #expect(await eventually { starter.starts.count == 1 })
        model.quantize(Self.gemmaForm)
        #expect(await eventually { starter.starts.count == 2 })
        #expect(starter.starts.map(\.0) == [["models", "quantize", Self.llamaF16], ["models", "quantize", Self.gemmaF16]])
        let llama = try #require(model.downloads.first { $0.reference == Self.llamaF16 })
        let gemma = try #require(model.downloads.first { $0.reference == Self.gemmaF16 })

        // Both GGUFs land while both run: Gemma's sorts first in the store.
        runner.reading(["models", "list", "--all"], recorded("quantize-list-both-landed"))
        gemmaJob.print(recorded("quantize-gemma", "txt"))
        llamaJob.print(recorded("quantize-llama", "txt"))
        llamaJob.end()
        #expect(await eventually { llama.endedAt != nil })
        #expect(runner.calls.contains(Self.llamaRegistration), "Llama's name on Llama's GGUF")
        #expect(!runner.calls.contains { $0.hasPrefix("run config add-backend Llama3.2-1B-Instruct-Q4KM") && $0 != Self.llamaRegistration },
                "never another job's GGUF under its name")
        gemmaJob.end()
        #expect(await eventually { gemma.endedAt != nil })
        #expect(runner.calls.contains(Self.gemmaRegistration))
        #expect(runner.calls.filter { $0.hasPrefix("run config add-backend") }.count == 2)
        #expect(llama.outcome == "added backend 'Llama3.2-1B-Instruct-Q4KM' to /Users/someone/.apogee/config/config.json")
        #expect(llama.state(of: 1) == .done && gemma.state(of: 1) == .done)
        #expect(llama.firstLanded == Self.llamaQ4 && gemma.firstLanded == Self.gemmaQ4, "Show the weights opens its own set")
    }

    @Test("the store unread before a run: the step fails, said, and nothing runs; Run again runs it, registered")
    func failedFirstReadFailsTheStep() async throws {
        let job = ScriptedJob()
        let runner = quantizeRunner()
        runner.reading(["models", "list", "--all"], outcome: .couldNotStart("apogee: the store is not readable"))
        let (model, starter) = jobs([job], runner)
        model.quantize(Self.llamaForm)
        let download = try #require(model.downloads.first)
        #expect(await eventually { download.endedAt != nil }, "it ends, failed")
        #expect(starter.starts.isEmpty, "nothing runs over an unread store")
        guard case .failed(let why) = download.progress.status else {
            Issue.record("not failed: \(download.progress.status)")
            return
        }
        #expect(why.contains("apogee models list --all") && why.contains("apogee: the store is not readable"), "said, in the read's words: \(why)")
        #expect(download.state(of: 0) == .waiting && download.state(of: 1) == .waiting)
        #expect(!runner.calls.contains { $0.hasPrefix("run config add-backend") })

        runner.reading(["models", "list", "--all"], recorded("quantize-list-before"))
        model.again(download)
        #expect(await eventually { starter.starts.count == 1 })
        let retried = try #require(model.downloads.first)
        #expect(retried.kind == .quantize && retried.registerAfter == Self.llamaForm)
        runner.reading(["models", "list", "--all"], recorded("quantize-list-both-landed"))
        job.print(recorded("quantize-llama", "txt"))
        job.end()
        #expect(await eventually { retried.endedAt != nil })
        #expect(runner.calls.contains(Self.llamaRegistration))
    }

    @Test("Run again on a failed quantization is the quantization: its steps, its registration, its end")
    func againOnAFailedQuantizeRegisters() async throws {
        let first = ScriptedJob()
        let second = ScriptedJob()
        let runner = quantizeRunner()
        let (model, starter) = jobs([first, second], runner)
        var clock = Date(timeIntervalSince1970: 1_000_000)
        model.now = { clock }
        model.quantize(Self.llamaForm)
        #expect(await eventually { starter.starts.count == 1 })
        let failed = try #require(model.downloads.first)
        // The store not writable, as a full disk fails it: nothing is made.
        first.end(status: 1, stderr: recorded("quantize-llama-failed", "err"))
        #expect(await eventually { failed.endedAt != nil })
        #expect(failed.progress.status == .failed("llama.cpp said: llama_model_quantize: failed to quantize: ios_base::clear: unspecified iostream_category error"))
        #expect(failed.progress.resumeCommand == nil, "a quantization names no resume: Run again")
        #expect(!runner.calls.contains { $0.hasPrefix("run config add-backend") })

        model.again(failed)
        #expect(await eventually { starter.starts.count == 2 })
        #expect(starter.starts[1].0 == ["models", "quantize", Self.llamaF16] && !starter.starts[1].1)
        let retried = try #require(model.downloads.first)
        #expect(retried !== failed)
        #expect(retried.kind == .quantize, "the same kind, not a pull")
        try #require(retried.steps == failed.steps, "its steps, as the sheet listed them")
        #expect(retried.steps.map(\.label) == ["Quantize to Q4_K_M", "Register Llama3.2-1B-Instruct-Q4KM"])
        #expect(retried.registerAfter == Self.llamaForm)
        runner.reading(["models", "list", "--all"], recorded("quantize-list-both-landed"))
        second.print(recorded("quantize-llama", "txt"))
        clock += 42
        second.end()
        #expect(await eventually { retried.endedAt != nil })
        #expect(runner.calls.contains(Self.llamaRegistration), "re-registered")
        #expect(retried.state(of: 0) == .done && retried.state(of: 1) == .done)
        #expect(retried.outcome == "added backend 'Llama3.2-1B-Instruct-Q4KM' to /Users/someone/.apogee/config/config.json")
        let ran = model.elapsed(retried)
        clock += 3600
        #expect(model.elapsed(retried) == ran, "ended: its time stops (\(ran))")
    }

    @Test("a quantization the CLI finds made already names its GGUF too: that one is registered, and its step done")
    func alreadyQuantizedRegisters() async throws {
        let job = ScriptedJob()
        let runner = quantizeRunner()
        // The Q4_K_M is in the store before the run: nothing lands.
        runner.reading(["models", "list", "--all"], recorded("quantize-list-both-landed"))
        let (model, starter) = jobs([job], runner)
        model.quantize(Self.llamaForm)
        #expect(await eventually { starter.starts.count == 1 })
        let download = try #require(model.downloads.first)
        job.print(recorded("quantize-llama-again", "txt"))
        job.end()
        #expect(await eventually { download.endedAt != nil })
        #expect(download.landed.isEmpty, "nothing new in the store")
        #expect(runner.calls.contains(Self.llamaRegistration), "the GGUF its run named, under \"already quantized:\"")
        #expect(download.made == Self.llamaQ4)
        #expect(download.state(of: 0) == .done && download.state(of: 1) == .done)
    }

    @Test("a stopped conversion's named resume is the conversion again: its steps, what it made kept, the store followed")
    func resumeKeepsTheStoreJob() async throws {
        let snapshot = "Llama3.2-1B-Instruct/safetensors/edaba290ce7f"
        let f16 = "Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41"
        let first = ScriptedJob()
        let second = ScriptedJob()
        let runner = ScriptedRunner()
        runner.reading(["models", "list", "--all"], StoreJobsTests.list([snapshot]))
        let (model, starter) = jobs([first, second], runner)
        var form = ConvertForm(handle: snapshot, mlx: false, bytes: nil)
        form.levels = ["Q4_K_M"]
        model.convert(form)
        #expect(await eventually { starter.starts.count == 1 })
        let stopped = try #require(model.downloads.first)
        runner.reading(["models", "list", "--all"], StoreJobsTests.list([snapshot, f16]))
        first.say(.said("converting /Users/someone/.apogee/models/\(snapshot) to a GGUF (F16)"))
        #expect(await eventually { stopped.state(of: 0) == .done })
        // Stopped at its quantization: the chain's own words (run_chain).
        first.say(.said("stopped at [2/3] quantize to Q4_K_M -- what the stages before it made is kept."))
        first.say(.said("resume with:"))
        first.say(.said("  apogee models convert \(snapshot) --register-with Q4_K_M"))
        first.end(status: 130, interrupted: true)
        #expect(await eventually { stopped.endedAt != nil && stopped.progress.status == .stopped })
        let command = ["models", "convert", snapshot, "--register-with", "Q4_K_M"]
        #expect(stopped.progress.resumeCommand == command)

        model.resume(stopped)
        #expect(await eventually { starter.starts.count == 2 })
        #expect(starter.starts[1].0 == command && !starter.starts[1].1)
        let resumed = try #require(model.downloads.first)
        #expect(resumed !== stopped)
        #expect(resumed.kind == .convert)
        try #require(resumed.steps == stopped.steps, "its steps, as the sheet listed them")
        #expect(resumed.state(of: 0) == .done, "the F16 the first run made stays made")
        #expect(resumed.state(of: 1) == .running)
        runner.reading(["models", "list", "--all"], StoreJobsTests.list([snapshot, "Llama3.2-1B-Instruct-F16", "Llama3.2-1B-Instruct-Q4KM"]))
        second.say(.said("registered Llama3.2-1B-Instruct-F16"))
        second.say(.said("registered Llama3.2-1B-Instruct-Q4KM"))
        second.end()
        #expect(await eventually { resumed.endedAt != nil }, "followed to its end")
        #expect((0..<3).map(resumed.state(of:)) == [.done, .done, .done])
    }

    /// A conversion of the Llama snapshot to an F16 and a Q4_K_M, registered,
    /// started beside Gemma's quantization, asked to register: the store as
    /// both find it before they run.
    func convertBesideAQuantization() async throws -> (DownloadsViewModel, ScriptedRunner, ScriptedJob, ScriptedJob, DownloadsViewModel.Download, DownloadsViewModel.Download) {
        let conversionJob = ScriptedJob()
        let quantizationJob = ScriptedJob()
        let runner = quantizeRunner()
        runner.reading(["models", "list", "--all"], StoreJobsTests.list([Self.snapshot, Self.gemmaF16]))
        let (model, starter) = jobs([conversionJob, quantizationJob], runner)
        var form = ConvertForm(handle: Self.snapshot, mlx: false, bytes: nil)
        form.levels = ["Q4_K_M"]
        model.convert(form)
        #expect(await eventually { starter.starts.count == 1 })
        model.quantize(Self.gemmaForm)
        #expect(await eventually { starter.starts.count == 2 })
        let conversion = try #require(model.downloads.first { $0.kind == .convert })
        let quantization = try #require(model.downloads.first { $0.kind == .quantize })
        // The conversion makes its F16.
        runner.reading(["models", "list", "--all"], StoreJobsTests.list([Self.snapshot, Self.f16, Self.gemmaF16]))
        conversionJob.say(.said("converting /Users/someone/.apogee/models/\(Self.snapshot) to a GGUF (F16)"))
        #expect(await eventually { conversion.state(of: 0) == .done })
        #expect(conversion.state(of: 1) == .running)
        // Meanwhile Gemma's quantization lands and the app registers it.
        runner.reading(["models", "list", "--all"], StoreJobsTests.list([Self.snapshot, Self.f16, Self.gemmaF16, "Gemma3-1B-Instruct-Q4KM"]))
        quantizationJob.print(recorded("quantize-gemma", "txt"))
        quantizationJob.end()
        #expect(await eventually { quantization.endedAt != nil })
        #expect(runner.calls.contains(Self.gemmaRegistration) && quantization.state(of: 1) == .done)
        return (model, runner, conversionJob, quantizationJob, conversion, quantization)
    }

    static let snapshot = "Llama3.2-1B-Instruct/safetensors/edaba290ce7f"
    static let f16 = "Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41"

    @Test("a conversion's steps tick on its own work: a backend another job registered meanwhile ticks none of them")
    func convertTicksOnlyOnItsOwn() async throws {
        let (_, _, conversionJob, _, conversion, _) = try await convertBesideAQuantization()
        // Stopped at its quantization: the store holds its F16, and Gemma's backend.
        conversionJob.end(status: 130, interrupted: true)
        #expect(await eventually { conversion.endedAt != nil })
        #expect(conversion.landed == [Self.f16], "its own set; Gemma's backend is not its work")
        #expect(conversion.state(of: 0) == .done)
        #expect(conversion.state(of: 1) == .waiting, "Quantize to Q4_K_M never made")
    }

    @Test("a conversion's own registrations count, as its run names them; another's never, nor for Show the weights")
    func convertCountsItsOwnRegistrations() async throws {
        let (_, runner, conversionJob, _, conversion, _) = try await convertBesideAQuantization()
        // It quantizes and registers both, in the chain's words (`register_backends`):
        // each set now listed as its backend.
        runner.reading(
            ["models", "list", "--all"],
            StoreJobsTests.list([Self.snapshot, Self.gemmaF16, "Gemma3-1B-Instruct-Q4KM", "Llama3.2-1B-Instruct-F16", "Llama3.2-1B-Instruct-Q4KM"]))
        conversionJob.say(.said("registered Llama3.2-1B-Instruct-F16"))
        conversionJob.say(.said("registered Llama3.2-1B-Instruct-Q4KM"))
        conversionJob.end()
        #expect(await eventually { conversion.endedAt != nil })
        #expect(conversion.landed == ["Llama3.2-1B-Instruct-F16", "Llama3.2-1B-Instruct-Q4KM"], "its two, not Gemma's")
        #expect((0..<3).map(conversion.state(of:)) == [.done, .done, .done])
        #expect(conversion.firstLanded == "Llama3.2-1B-Instruct-F16", "Show the weights opens its own")
    }

    @Test("a chained pull that finished offers no resume, though it named one on the way")
    func doneChainOffersNoResume() async throws {
        let job = ScriptedJob()
        let starter = ScriptedJobs([job])
        let model = DownloadsViewModel(starter: starter)
        model.connect(binary: URL(fileURLWithPath: "/Users/someone/.local/bin/apogee"))
        model.streams = { true }
        model.reference = "org/model"
        model.convert = true
        model.start()
        let download = try #require(model.downloads.first)
        chainStream().forEach(job.say)
        job.end()
        #expect(await eventually { download.progress.status == .done })
        #expect(download.progress.orderedStages.count == 4)
        #expect(download.progress.notices.contains("  apogee models convert org--model/safetensors/a --register-with Q4_K_M"))
        #expect(download.progress.resumeCommand == nil, "done: nothing to resume")
        model.resume(download)
        #expect(starter.starts.count == 1, "a finished chain starts nothing again")
        #expect(await eventually { download.endedAt != nil }, "a pull's end is kept too")
    }
}
