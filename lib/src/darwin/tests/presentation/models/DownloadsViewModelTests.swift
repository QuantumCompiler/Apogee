import BusinessJobs
import DataProtocol
import Foundation
import InfrastructureProcess
import Synchronization
import Testing

@testable import PresentationModels

/// A job a test drives by hand.
final class ScriptedJob: JobRunning, @unchecked Sendable {
    let updates: AsyncStream<JobUpdate>
    private let continuation: AsyncStream<JobUpdate>.Continuation
    private let stopped = Mutex(0)

    init() {
        (updates, continuation) = AsyncStream.makeStream(of: JobUpdate.self)
    }

    var stops: Int { stopped.withLock { $0 } }

    func say(_ update: JobUpdate) { continuation.yield(update) }

    func end(status: Int32 = 0, interrupted: Bool = false) {
        continuation.yield(.ended(JobEnd(exit: ChildExit(reason: .exited, status: status), stderrTail: BoundedTail(), interrupted: interrupted)))
        continuation.finish()
    }

    func stop(grace: Duration) async {
        stopped.withLock { $0 += 1 }
        end(status: 130, interrupted: true)
    }
}

/// Hands out scripted jobs, recording what each was started as.
final class ScriptedJobs: JobStarting, @unchecked Sendable {
    private let jobs: Mutex<[ScriptedJob]>
    private let started = Mutex<[([String], Bool)]>([])

    init(_ jobs: [ScriptedJob]) {
        self.jobs = Mutex(jobs)
    }

    var starts: [([String], Bool)] { started.withLock { $0 } }

    func startJob(binary: URL, arguments: [String], streaming: Bool, folder: URL?) -> any JobRunning {
        started.withLock { $0.append((arguments, streaming)) }
        return jobs.withLock { $0.removeFirst() }
    }
}

@MainActor
func eventually(_ condition: @MainActor () -> Bool) async -> Bool {
    let deadline = ContinuousClock.now + .seconds(5)
    while !condition() {
        if ContinuousClock.now > deadline { return false }
        try? await Task.sleep(for: .milliseconds(10))
    }
    return true
}

@Suite("DownloadsViewModel", .timeLimit(.minutes(1)))
@MainActor
struct DownloadsViewModelTests {
    func downloads(_ jobs: [ScriptedJob], streams: Bool) -> (DownloadsViewModel, ScriptedJobs) {
        let starter = ScriptedJobs(jobs)
        let model = DownloadsViewModel(starter: starter)
        model.connect(binary: URL(fileURLWithPath: "/Users/someone/.local/bin/apogee"))
        model.streams = { streams }
        return (model, starter)
    }

    @Test("the form is the command a person types: a plain pull, or full weights converted and registered at each level")
    func form() {
        let (model, _) = downloads([], streams: false)
        #expect(model.formArguments == nil)
        model.reference = "  Qwen/Qwen3-8B "
        #expect(model.formArguments == ["models", "pull", "Qwen/Qwen3-8B"])
        model.convert = true
        model.chosenLevels = ["Q8_0", "Q4_K_M"]
        #expect(model.formArguments == ["models", "pull", "Qwen/Qwen3-8B", "--safetensors", "--register-with", "Q4_K_M,Q8_0"], "levels in the CLI's order")
        model.chosenLevels = []
        #expect(model.formArguments == ["models", "pull", "Qwen/Qwen3-8B", "--safetensors", "--register"])
        model.reference = "two words"
        #expect(model.formArguments == nil)
    }

    @Test("on a 52b build the pull streams its events; stages and bytes show, and the library reads back at the end")
    func streamed() async {
        let job = ScriptedJob()
        let (model, starter) = downloads([job], streams: true)
        var reads = 0
        model.finished = { reads += 1 }
        model.reference = "org/model"
        model.convert = true
        model.start()
        #expect(model.reference == "")
        #expect(starter.starts.first?.0 == ["models", "pull", "org/model", "--safetensors", "--register-with", "Q4_K_M", "--output-format", "stream-json"])
        #expect(starter.starts.first?.1 == true)
        let download = model.downloads[0]
        job.say(.event(.stage(index: 1, count: 3, label: "pull org/model's full weights", state: "started")))
        job.say(.event(.bytes(done: 100, total: 400, file: "a.safetensors", index: 1, count: 2)))
        #expect(await eventually { download.progress.bytes?.done == 100 })
        #expect(DownloadsViewModel.now(download.progress) == "1 of 3: pull org/model's full weights")
        #expect(model.running == 1)
        job.end()
        #expect(await eventually { reads == 1 })
        #expect(download.progress.status == .done)
        #expect(model.running == 0)
        model.dismiss(download)
        #expect(model.downloads.isEmpty)
    }

    @Test("before 52b the busy line names the step where its words do, and says working where they do not")
    func fallback() async {
        let job = ScriptedJob()
        let (model, starter) = downloads([job], streams: false)
        model.reference = "tiny:latest"
        model.start()
        #expect(starter.starts.first?.0 == ["models", "pull", "tiny:latest"])
        #expect(starter.starts.first?.1 == false)
        let download = model.downloads[0]
        #expect(DownloadsViewModel.now(download.progress) == "working")
        job.say(.busy("⠋ downloading tiny:latest 34%"))
        #expect(await eventually { DownloadsViewModel.now(download.progress) == "downloading" })
        job.say(.busy("⠙ quantizing to Q4_K_M"))
        #expect(await eventually { DownloadsViewModel.now(download.progress) == "quantizing" })
        job.say(.busy("something it has never said"))
        #expect(await eventually { DownloadsViewModel.now(download.progress) == "working" }, "nothing decided on the words: it degrades")
    }

    @Test("Stop interrupts; a stopped pull runs again, or carries on by the command the CLI named")
    func stopAndResume() async {
        let first = ScriptedJob()
        let second = ScriptedJob()
        let third = ScriptedJob()
        let (model, starter) = downloads([first, second, third], streams: true)
        model.reference = "org/model"
        model.convert = true
        model.start()
        let download = model.downloads[0]
        first.say(.event(.stage(index: 2, count: 3, label: "convert to F16", state: "started")))
        await model.stop(download)
        #expect(first.stops == 1)
        #expect(await eventually { download.progress.status == .stopped })
        #expect(download.progress.stages[2]?.state == "stopped")
        model.again(download)
        #expect(starter.starts[1].0 == ["models", "pull", "org/model", "--safetensors", "--register-with", "Q4_K_M", "--output-format", "stream-json"])
        let retried = model.downloads[0]
        second.say(.event(.stage(index: 3, count: 3, label: "quantize to Q4_K_M", state: "failed")))
        second.say(.event(.notice("stopped at step 3 of 3 -- carry on with: apogee models quantize org--model/gguf/F16 Q4_K_M")))
        second.say(.event(.error("quantize failed: out of disk space")))
        second.end(status: 1)
        #expect(await eventually { retried.progress.status == .failed("quantize failed: out of disk space") })
        model.resume(retried)
        #expect(starter.starts[2].0 == ["models", "quantize", "org--model/gguf/F16", "Q4_K_M"], "quantize takes no stream: its own words, as typed")
        #expect(starter.starts[2].1 == false)
    }

    @Test("with no binary, nothing starts")
    func noBinary() {
        let model = DownloadsViewModel(starter: ScriptedJobs([]))
        model.reference = "org/model"
        #expect(!model.canStart)
        model.start()
        #expect(model.downloads.isEmpty)
    }
}
