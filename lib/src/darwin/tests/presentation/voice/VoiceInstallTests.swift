import BusinessJobs
import BusinessMedia
import DataPreferences
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationVoice

/// The consented install, supervised (GUI 4f), over the binary's own words
/// recorded over a sandbox (tests/business/media/Fixtures): `voice say`
/// refused for its runtime -- a 52b stream saying only its `job` line, the
/// refusal on stderr -- and `train setup --with voice` failing or done.
@Suite("VoiceInstall", .timeLimit(.minutes(1)))
@MainActor
struct VoiceInstallTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let install = ["train", "setup", "--with", "voice"]

    func home() throws -> URL {
        let home = FileManager.default.temporaryDirectory.appendingPathComponent("voice-install-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: home.appendingPathComponent("Music/Apogee"), withIntermediateDirectories: true)
        return home
    }

    /// The store the refusal was recorded over: one voice model.
    func store() -> ScriptedRunner {
        let runner = ScriptedRunner()
        runner.reading(["models", "list"], recordedFixture("business/reads/Fixtures/compose-voice-list.json"))
        return runner
    }

    /// A speech refused for its runtime as the binary refuses one, the
    /// consent standing; `jobs` hands out `refused` first.
    func refused(_ jobs: ScriptedJobs, _ refused: ScriptedJob, home: URL, toasts: Toasts) async -> VoiceViewModel {
        let model = VoiceViewModel(runner: store(), starter: jobs, preferences: Preferences(store: MemoryStore()), home: home)
        model.say = { toasts.say($0) }
        model.streams = { true }
        await model.connect(binary: Self.binary)
        model.text = "Hello there. How are you?"
        model.speak()
        refused.replay(
            stdout: recordedFixture("business/media/Fixtures/voice-refused-runtime.jsonl"),
            stderr: recordedFixture("business/media/Fixtures/voice-refused-runtime.err"), status: 1, streaming: true)
        _ = await eventually { model.consent != nil }
        return model
    }

    @Test("the binary's refusal -- on stderr alone, its stream silent -- names the install, asked before anything runs")
    func refusalAsks() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let first = ScriptedJob()
        let jobs = ScriptedJobs([first])
        let model = await refused(jobs, first, home: home, toasts: Toasts())
        #expect(model.consent != nil)
        #expect(jobs.starts.count == 1, "nothing installed before the person's yes")
    }

    @Test("while it installs the place is busy: a second speech starts nothing and asks nothing again")
    func secondSpeechWaits() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let first = ScriptedJob()
        let install = ScriptedJob()
        let jobs = ScriptedJobs([first, install, ScriptedJob()])
        let model = await refused(jobs, first, home: home, toasts: Toasts())
        model.install()
        #expect(jobs.starts.map(\.arguments).last == Self.install)
        #expect(model.isRunning, "an install is a run of the place's")
        model.text = "Another one."
        model.speak()
        #expect(jobs.starts.count == 2, "no second run beside the install")
        #expect(model.consent == nil, "and no second ask")
        install.end()
    }

    @Test("Stop reaches the install: it ends stopped, said, and the refused speech is not run")
    func stopReachesTheInstall() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let first = ScriptedJob()
        let install = ScriptedJob()
        let jobs = ScriptedJobs([first, install, ScriptedJob()])
        let toasts = Toasts()
        let model = await refused(jobs, first, home: home, toasts: toasts)
        model.install()
        await model.stop()
        #expect(install.stopped)
        #expect(await eventually { !model.isRunning && toasts.contain("stopped") })
        #expect(jobs.starts.count == 2, "a stopped install runs nothing again")
    }

    @Test("a failed install is said in the CLI's words, and the refused speech is not run")
    func failureSaid() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let first = ScriptedJob()
        let install = ScriptedJob()
        let jobs = ScriptedJobs([first, install, ScriptedJob()])
        let toasts = Toasts()
        let model = await refused(jobs, first, home: home, toasts: toasts)
        model.install()
        install.replay(
            stdout: recordedFixture("business/media/Fixtures/install-voice-failed.txt"),
            stderr: recordedFixture("business/media/Fixtures/install-voice-failed.err"), status: 1, streaming: false)
        #expect(await eventually { toasts.contain("pip install for the 'voice' set failed") })
        #expect(jobs.starts.count == 2, "a failed install runs nothing again")
    }

    @Test("installed, the refused speech runs again -- the same command, once")
    func installedRunsAgain() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let first = ScriptedJob()
        let install = ScriptedJob()
        let again = ScriptedJob()
        let jobs = ScriptedJobs([first, install, again])
        let model = await refused(jobs, first, home: home, toasts: Toasts())
        model.install()
        install.replay(stdout: recordedFixture("business/media/Fixtures/install-voice-done.txt"), status: 0, streaming: false)
        #expect(await eventually { jobs.starts.count == 3 })
        #expect(jobs.arguments(2) == jobs.arguments(0))
        again.replay(
            stdout: recordedFixture("business/media/Fixtures/voice-refused-runtime.jsonl"),
            stderr: recordedFixture("business/media/Fixtures/voice-refused-runtime.err"), status: 1, streaming: true)
        #expect(await eventually { model.consent != nil && !model.isRunning }, "refused again, it asks again")
        #expect(jobs.starts.count == 3, "and runs nothing of its own")
    }

    @Test("the install as its card draws it: running with its line, failed in the CLI's words, then put away")
    func drawnAndDismissed() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let first = ScriptedJob()
        let install = ScriptedJob()
        let jobs = ScriptedJobs([first, install])
        let model = await refused(jobs, first, home: home, toasts: Toasts())
        #expect(model.consent?.arguments == Self.install)
        #expect(model.consent?.said == "apogee voice say: torch and kokoro are not installed in Apogee's Python environment -- apogee train setup --with voice")
        model.install()
        #expect(model.installing?.headline == "Installing the voice set")
        model.dismissInstall()
        #expect(model.installing != nil, "a running install's card stays")
        install.replay(
            stdout: recordedFixture("business/media/Fixtures/install-voice-failed.txt"),
            stderr: recordedFixture("business/media/Fixtures/install-voice-failed.err"), status: 1, streaming: false)
        #expect(await eventually { model.installing?.phase == .failed && !model.isInstalling })
        #expect(model.installing?.failure?.hasPrefix("apogee train: pip install for the 'voice' set failed (exit code 1):") == true)
        model.dismissInstall()
        #expect(model.installing == nil)
    }

    @Test("an install running elsewhere is never offered beside: said, nothing asked")
    func installElsewhere() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let first = ScriptedJob()
        let jobs = ScriptedJobs([first])
        let toasts = Toasts()
        let model = VoiceViewModel(runner: store(), starter: jobs, preferences: Preferences(store: MemoryStore()), home: home)
        model.say = { toasts.say($0) }
        model.streams = { true }
        model.installElsewhere = { true }
        await model.connect(binary: Self.binary)
        model.text = "Hello there. How are you?"
        model.speak()
        first.replay(
            stdout: recordedFixture("business/media/Fixtures/voice-refused-runtime.jsonl"),
            stderr: recordedFixture("business/media/Fixtures/voice-refused-runtime.err"), status: 1, streaming: true)
        #expect(await eventually { toasts.contain("apogee train setup --with voice is already running") })
        #expect(model.consent == nil)
    }
}
