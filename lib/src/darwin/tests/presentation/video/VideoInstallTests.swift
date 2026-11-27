import BusinessJobs
import BusinessMedia
import DataPreferences
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationVideo

/// The consented install, supervised (GUI 4f), over the binary's own words
/// recorded over a sandbox (tests/business/media/Fixtures): `video generate`
/// refused for its runtime -- a 52b stream saying only its `job` line, the
/// refusal on stderr -- and `train setup --with video` failing or done.
@Suite("VideoInstall", .timeLimit(.minutes(1)))
@MainActor
struct VideoInstallTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let install = ["train", "setup", "--with", "video"]

    func home() throws -> URL {
        let home = FileManager.default.temporaryDirectory.appendingPathComponent("video-install-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: home.appendingPathComponent("Movies/Apogee"), withIntermediateDirectories: true)
        return home
    }

    /// A clip refused for its runtime as the binary refuses one, the consent
    /// standing; `jobs` hands out `refused` first.
    func refused(_ jobs: ScriptedJobs, _ refused: ScriptedJob, home: URL, toasts: Toasts) async -> VideoViewModel {
        let model = VideoViewModel(starter: jobs, preferences: Preferences(store: MemoryStore()), home: home)
        model.say = { toasts.say($0) }
        model.streams = { true }
        model.connect(binary: Self.binary)
        model.newClip()
        model.form.prompt = "a heron lands on a pier at dusk"
        model.generate()
        refused.replay(
            stdout: recordedFixture("business/media/Fixtures/generate-refused-runtime.jsonl"),
            stderr: recordedFixture("business/media/Fixtures/generate-refused-runtime.err"), status: 1, streaming: true)
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
        #expect(model.consent?.arguments == Self.install)
        #expect(model.consent?.said == "apogee video generate: torch and diffusers are not installed in Apogee's Python environment -- apogee train setup --with video")
        #expect(jobs.starts.count == 1, "nothing installed before the person's yes")
    }

    @Test("while it installs the place is busy: a second generate starts nothing and asks nothing again")
    func secondGenerateWaits() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let install = ScriptedJob()
        let first = ScriptedJob()
        let jobs = ScriptedJobs([first, install, ScriptedJob()])
        let toasts = Toasts()
        let model = await refused(jobs, first, home: home, toasts: toasts)
        model.install()
        #expect(jobs.starts.map(\.arguments).last == Self.install)
        #expect(model.isRunning, "an install is a run of the place's")
        model.newClip()
        model.form.prompt = "a lighthouse at dusk"
        model.generate()
        #expect(jobs.starts.count == 2, "no second run beside the install")
        #expect(model.consent == nil, "and no second ask")
        install.end()
    }

    @Test("Stop reaches the install: it ends stopped, said, and the refused clip is not run")
    func stopReachesTheInstall() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let install = ScriptedJob()
        let first = ScriptedJob()
        let jobs = ScriptedJobs([first, install, ScriptedJob()])
        let toasts = Toasts()
        let model = await refused(jobs, first, home: home, toasts: toasts)
        model.install()
        await model.stop()
        #expect(install.stopped)
        #expect(await eventually { !model.isRunning && toasts.contain("stopped") })
        #expect(jobs.starts.count == 2, "a stopped install runs nothing again")
    }

    @Test("a failed install is said in the CLI's words, and the refused clip is not run")
    func failureSaid() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let install = ScriptedJob()
        let first = ScriptedJob()
        let jobs = ScriptedJobs([first, install, ScriptedJob()])
        let toasts = Toasts()
        let model = await refused(jobs, first, home: home, toasts: toasts)
        model.install()
        install.replay(
            stdout: recordedFixture("business/media/Fixtures/install-video-failed.txt"),
            stderr: recordedFixture("business/media/Fixtures/install-video-failed.err"), status: 1, streaming: false)
        #expect(await eventually { toasts.contain("pip install for the 'video' set failed") })
        #expect(jobs.starts.count == 2, "a failed install runs nothing again")
    }

    @Test("installed, the refused clip runs again -- the same command, once")
    func installedRunsAgain() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let install = ScriptedJob()
        let again = ScriptedJob()
        let first = ScriptedJob()
        let jobs = ScriptedJobs([first, install, again])
        let model = await refused(jobs, first, home: home, toasts: Toasts())
        model.install()
        install.replay(stdout: recordedFixture("business/media/Fixtures/install-video-done.txt"), status: 0, streaming: false)
        #expect(await eventually { jobs.starts.count == 3 })
        #expect(jobs.arguments(2) == jobs.arguments(0))
        again.replay(
            stdout: recordedFixture("business/media/Fixtures/generate-refused-runtime.jsonl"),
            stderr: recordedFixture("business/media/Fixtures/generate-refused-runtime.err"), status: 1, streaming: true)
        #expect(await eventually { model.consent != nil && !model.isRunning }, "refused again, it asks again")
        #expect(jobs.starts.count == 3, "and runs nothing of its own")
    }

    @Test("the install as its card draws it: running with its line, installed, then put away -- a running one never")
    func drawnAndDismissed() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let first = ScriptedJob()
        let install = ScriptedJob()
        let jobs = ScriptedJobs([first, install, ScriptedJob()])
        let model = await refused(jobs, first, home: home, toasts: Toasts())
        model.install()
        #expect(model.installing?.phase == .running)
        #expect(model.installing?.headline == "Installing the video set")
        #expect(model.isInstalling)
        let lines = recordedFixture("business/media/Fixtures/install-video-done.txt").split(separator: "\n").map(String.init)
        install.say(.said(lines[0]))
        install.say(.said(lines[1]))
        #expect(await eventually { model.installing?.detail?.hasPrefix("installing the 'video' set") == true })
        model.dismissInstall()
        #expect(model.installing != nil, "a running install's card stays")
        install.replay(stdout: lines.dropFirst(2).joined(separator: "\n"), status: 0, streaming: false)
        #expect(await eventually { model.installing?.phase == .installed && !model.isInstalling })
        model.dismissInstall()
        #expect(model.installing == nil)
    }

    @Test("an install running elsewhere -- another place's, or the bootstrap -- is never offered beside: said, nothing asked")
    func installElsewhere() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let first = ScriptedJob()
        let jobs = ScriptedJobs([first])
        let toasts = Toasts()
        let model = VideoViewModel(starter: jobs, preferences: Preferences(store: MemoryStore()), home: home)
        model.say = { toasts.say($0) }
        model.streams = { true }
        model.installElsewhere = { "apogee bootstrap" }
        model.connect(binary: Self.binary)
        model.newClip()
        model.form.prompt = "a heron lands on a pier at dusk"
        model.generate()
        first.replay(
            stdout: recordedFixture("business/media/Fixtures/generate-refused-runtime.jsonl"),
            stderr: recordedFixture("business/media/Fixtures/generate-refused-runtime.err"), status: 1, streaming: true)
        #expect(await eventually { toasts.contain("apogee bootstrap is already running: generate the clip again once it has finished") })
        #expect(model.consent == nil)
    }

    @Test("Not now installs nothing and says the command for later")
    func declined() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let first = ScriptedJob()
        let jobs = ScriptedJobs([first])
        let toasts = Toasts()
        let model = await refused(jobs, first, home: home, toasts: toasts)
        model.decline()
        #expect(model.consent == nil)
        #expect(toasts.contain("Not installed. apogee train setup --with video installs it whenever you want it"))
        #expect(jobs.starts.count == 1)
    }

    // MARK: - GUI 6c: asked when an install starts, not only when offered

    /// The M19 refusal a failed install names its rebuild in.
    static let unfit = "apogee train: the Python environment at /h/venv is on Python 3.14.5, and the 'video' set runs on >=3.10 -- rebuild it on a Python that fits: apogee train setup --rebuild --with video\n"
    static let rebuild = ["train", "setup", "--rebuild", "--yes", "--with", "video"]

    @Test("a consent raised earlier starts nothing while another install runs: the running command said, the consent kept for later")
    func consentWaitsForAnotherInstall() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let first = ScriptedJob()
        let install = ScriptedJob()
        let jobs = ScriptedJobs([first, install, ScriptedJob()])
        let toasts = Toasts()
        let model = await refused(jobs, first, home: home, toasts: toasts)
        model.installElsewhere = { "apogee train setup" }
        model.install()
        #expect(jobs.starts.count == 1, "no second train setup beside the first")
        #expect(model.consent?.arguments == Self.install)
        #expect(toasts.said.last == "apogee train setup is already running: press Install again once it has finished")
        model.installElsewhere = { nil }
        model.install()
        #expect(jobs.arguments(1) == Self.install)
        install.end()
    }

    @Test("a failed install's rebuild is not offered while another install runs; a rebuild waits for a run on the environment")
    func rebuildWaits() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let first = ScriptedJob()
        let install = ScriptedJob()
        let again = ScriptedJob()
        let installAgain = ScriptedJob()
        let jobs = ScriptedJobs([first, install, again, installAgain])
        let toasts = Toasts()
        let model = await refused(jobs, first, home: home, toasts: toasts)
        model.install()
        model.installElsewhere = { "apogee train setup" }
        install.replay(stdout: "", stderr: Self.unfit, status: 1, streaming: false)
        #expect(await eventually { !model.isInstalling && !toasts.said.isEmpty })
        #expect(model.consent == nil, "no rebuild offered beside another install")
        #expect(toasts.said.last == "apogee train setup is already running: once it has finished, apogee train setup --rebuild --yes --with video rebuilds the environment")
        #expect(model.installing?.phase == .failed)
        // Once it has finished: the clip again, refused, its install asked
        // and failing as before -- now its rebuild is asked.
        model.installElsewhere = { nil }
        model.generate()
        #expect(jobs.starts.count == 3)
        again.replay(
            stdout: recordedFixture("business/media/Fixtures/generate-refused-runtime.jsonl"),
            stderr: recordedFixture("business/media/Fixtures/generate-refused-runtime.err"), status: 1, streaming: true)
        #expect(await eventually { model.consent?.arguments == Self.install })
        model.install()
        installAgain.replay(stdout: "", stderr: Self.unfit, status: 1, streaming: false)
        #expect(await eventually { model.consent?.arguments == Self.rebuild })
        model.usingEnvironment = { "apogee voice say" }
        model.install()
        #expect(jobs.starts.count == 4, "nothing moves the environment from under the speech")
        #expect(model.consent?.arguments == Self.rebuild)
        #expect(toasts.said.last == "apogee voice say is using Apogee's Python environment: press Rebuild again once it has finished")
    }
}
