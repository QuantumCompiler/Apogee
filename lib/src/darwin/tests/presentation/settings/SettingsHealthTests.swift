import BusinessJobs
import DataPreferences
import Foundation
import InfrastructureProcess
import Synchronization
import Testing

@testable import PresentationSettings

/// A recorded fixture, by its path under tests/.
private func recorded(_ path: String) -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("../../\(path)")
    return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
}

/// Settings' Health over `check`'s own document (GUI 4f), recorded from the
/// branch's binary over a sandbox whose ffmpeg lacks libass
/// (tests/business/reads/Fixtures/check-captions-skipped.json): a skipped row
/// that carries a remedy is drawn with it -- the captions' `ffmpeg-full`
/// among them -- after what fails and warns.
@Suite("SettingsHealth", .timeLimit(.minutes(1)))
@MainActor
struct SettingsHealthTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func read(_ check: String) async -> SettingsViewModel {
        let runner = RecordingRunner()
        runner.reading(["check"], check)
        let model = SettingsViewModel(runner: runner, starter: NoJobs(), preferences: .init(store: MemoryStore()))
        await model.connect(binary: Self.binary, version: "0.2.0")
        return model
    }

    @Test("skipped rows with a remedy are drawn, with it, after the failing and warning ones; nothing that passes is")
    func skippedWithTheirRemedy() async throws {
        let model = await read(recorded("business/reads/Fixtures/check-captions-skipped.json"))
        let captions = try #require(model.healthRows.first { $0.name == "ffmpeg captions" })
        #expect(captions.status == "skipped")
        #expect(captions.remedy == "brew install ffmpeg-full && brew unlink ffmpeg; brew link --force ffmpeg-full")
        let skipped = model.checks.filter { $0.status == "skipped" && $0.remedy != nil }
        #expect(skipped.count == 9)
        #expect(model.healthRows.map(\.status) == ["warn", "warn"] + Array(repeating: "skipped", count: 9))
        #expect(!model.healthRows.contains { $0.status == "ok" })
    }

    @Test("a skipped row with no remedy says nothing to do, so it is not drawn")
    func skippedWithoutARemedy() async throws {
        let check = #"{"rows":[{"section":"Video","name":"ffmpeg captions","status":"skipped","detail":"could not ask ffmpeg for its filters -- it did not answer"},{"section":"Runtimes","name":"llama-server","status":"fail","detail":"not installed","remedy":"apogee check --fix"}],"ok":false,"failures":1,"warnings":0}"#
        let model = await read(check)
        #expect(model.healthRows.map(\.name) == ["llama-server"])
    }

    // MARK: - GUI 6c: a fix always gives its buttons back

    /// Settings over a scripted runner and `jobs`, its toasts kept, read.
    func connected(_ jobs: any JobStarting, toasts: SettingsToasts) async -> SettingsViewModel {
        let runner = RecordingRunner()
        runner.reading(["check"], #"{"rows":[{"section":"Runtimes","name":"llama-server","status":"fail","detail":"not installed","remedy":"apogee check --fix"}],"ok":false,"failures":1,"warnings":0}"#)
        let model = SettingsViewModel(runner: runner, starter: jobs, preferences: .init(store: MemoryStore()))
        model.say = { toasts.say($0) }
        await model.connect(binary: Self.binary, version: "0.2.0")
        return model
    }

    /// The second sweep's finding (2026-10-31): a failed fix kept `fixing`,
    /// both buttons disabled until relaunch, and toasted "stopped".
    @Test("a bootstrap that exits non-zero gives both buttons back, says it failed and why, and the next fix runs")
    func failedFixRecovers() async throws {
        let first = HeldJob()
        let second = HeldJob()
        let jobs = HeldJobs([first, second])
        let toasts = SettingsToasts()
        let model = await connected(jobs, toasts: toasts)
        model.fix(["bootstrap"])
        #expect(model.fixing != nil && model.canStopFix)
        // A binary before bootstrap, refusing the word as its parser does.
        first.fail(stderr: "The following argument was not expected: bootstrap\nRun with --help for more information.\n", status: 109)
        #expect(await settingsEventually { model.fixing == nil })
        #expect(model.failure == "Run with --help for more information.", "the CLI's last word, the fold's, kept as the red line")
        #expect(toasts.said.last == "bootstrap failed: Run with --help for more information.")
        #expect(!model.canStopFix)
        model.fix(["check", "--fix"])
        #expect(jobs.starts == [["bootstrap"], ["check", "--fix"]], "a second fix starts")
        second.finish()
        #expect(await settingsEventually { model.fixing == nil && toasts.said.last == "check --fix finished" })
    }

    @Test("Stop on a running fix ends it, said stopped, and gives the buttons back")
    func stopEndsTheFix() async throws {
        let running = HeldJob()
        let jobs = HeldJobs([running])
        let toasts = SettingsToasts()
        let model = await connected(jobs, toasts: toasts)
        model.fix(["check", "--fix"])
        await model.stopFix()
        #expect(running.stoppedWith == .seconds(10), "the media install cards' grace")
        #expect(await settingsEventually { model.fixing == nil })
        #expect(toasts.said.last == "check --fix stopped")
        #expect(model.failure == nil, "a stop is no failure")
    }

    @Test("bootstrap waits for an install running elsewhere, said by its command; check --fix installs nothing and runs")
    func bootstrapAsksTheRegistry() async throws {
        let jobs = HeldJobs([HeldJob()])
        let toasts = SettingsToasts()
        let model = await connected(jobs, toasts: toasts)
        model.installElsewhere = { "apogee train setup" }
        model.fix(["bootstrap"])
        #expect(jobs.starts.isEmpty && model.fixing == nil)
        #expect(toasts.said.last == "apogee train setup is already running: run bootstrap again once it has finished")
        model.fix(["check", "--fix"])
        #expect(jobs.starts == [["check", "--fix"]])
    }
}

/// What Settings said, in order.
@MainActor
final class SettingsToasts {
    private(set) var said: [String] = []
    func say(_ text: String) { said.append(text) }
}

/// A fix a test ends by hand: done, failed with its stderr, or stopped.
final class HeldJob: JobRunning, @unchecked Sendable {
    let updates: AsyncStream<JobUpdate>
    private let continuation: AsyncStream<JobUpdate>.Continuation
    private let grace = Mutex<Duration?>(nil)

    /// The grace its Stop was given, once asked.
    var stoppedWith: Duration? { grace.withLock { $0 } }

    init() {
        (updates, continuation) = AsyncStream.makeStream(of: JobUpdate.self)
    }

    func finish() {
        continuation.yield(.ended(JobEnd(exit: ChildExit(reason: .exited, status: 0), stderrTail: BoundedTail(), interrupted: false)))
        continuation.finish()
    }

    func fail(stderr: String, status: Int32) {
        var tail = BoundedTail()
        tail.append(Data(stderr.utf8))
        continuation.yield(.ended(JobEnd(exit: ChildExit(reason: .exited, status: status), stderrTail: tail, interrupted: false)))
        continuation.finish()
    }

    func stop(grace: Duration) async {
        self.grace.withLock { $0 = grace }
        continuation.yield(.ended(JobEnd(exit: ChildExit(reason: .exited, status: 130), stderrTail: BoundedTail(), interrupted: true)))
        continuation.finish()
    }
}

/// Hands out held jobs in order, recording each start's arguments.
final class HeldJobs: JobStarting, @unchecked Sendable {
    private let jobs: Mutex<[HeldJob]>
    private let started = Mutex<[[String]]>([])
    var starts: [[String]] { started.withLock { $0 } }

    init(_ jobs: [HeldJob]) {
        self.jobs = Mutex(jobs)
    }

    func startJob(binary: URL, arguments: [String], streaming: Bool, folder: URL?) -> any JobRunning {
        started.withLock { $0.append(arguments) }
        return jobs.withLock { $0.removeFirst() }
    }
}

@MainActor
func settingsEventually(_ condition: @MainActor () -> Bool) async -> Bool {
    let deadline = ContinuousClock.now + .seconds(5)
    while !condition() {
        if ContinuousClock.now > deadline { return false }
        try? await Task.sleep(for: .milliseconds(10))
    }
    return true
}
