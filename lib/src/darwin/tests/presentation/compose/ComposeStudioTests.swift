import BusinessJobs
import BusinessMedia
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationCompose

/// Compose, finished (GUI 4f), over the binary's own words recorded over a
/// sandbox: its store's documents (tests/business/reads/Fixtures/compose-*,
/// check-captions-*) and its runs' streams with the stderr they left
/// (tests/business/media/Fixtures/compose-*, install-video-*).
@MainActor
enum Recorded {
    static let torch = "hexgrad--Kokoro-82M/voice/cccccccccccc"
    static let mlx = "hexgrad--Kokoro-82M/mlx/ffffffffffff"
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    /// The brief every recording composed.
    static let brief = "the tides, twice a day"

    static func media(_ name: String) -> String { recordedFixture("business/media/Fixtures/\(name)") }
    static func read(_ name: String) -> String { recordedFixture("business/reads/Fixtures/\(name)") }

    /// A fresh folder of compositions.
    static func folder() throws -> URL {
        let folder = FileManager.default.temporaryDirectory.appendingPathComponent("compose-studio-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        return folder
    }

    /// The store and check the recordings ran over: `voices` voice models
    /// (one or two), and `check`'s document.
    static func store(voices: Int = 2, check: String = "check-captions-skipped.json") -> ScriptedRunner {
        let runner = ScriptedRunner()
        runner.reading(["models", "list"], read(voices == 1 ? "compose-voice-list.json" : "compose-voices-list.json"))
        runner.reading(["check"], read(check))
        return runner
    }

    /// The view-model over `folder`, its store read, the sheet open on the
    /// recordings' brief.
    static func composing(
        _ folder: URL, jobs: ScriptedJobs, runner: ScriptedRunner = store(), toasts: Toasts = Toasts(),
        plansAlone: Bool = true, previewLimit: Duration = .seconds(300)
    ) async -> ComposeViewModel {
        let model = ComposeViewModel(starter: jobs, pollInterval: .milliseconds(20), previewLimit: previewLimit)
        model.folders = { [folder] }
        model.reader = runner
        model.streams = { true }
        model.plansAlone = { plansAlone }
        model.say = { toasts.say($0) }
        model.connect(binary: binary)
        await model.readStore()
        model.newComposition()
        model.form.brief = brief
        return model
    }

    /// The first line of a recorded stream that is an `error` event: its message.
    static func error(in stream: String) -> String? {
        stream.split(separator: "\n").lazy.compactMap { line -> String? in
            if case .error(let message) = JobEvent.decode(line: String(line)) { message } else { nil }
        }.first
    }
}

@Suite("ComposeVoiceModel", .timeLimit(.minutes(1)))
@MainActor
struct ComposeVoiceModelTests {
    @Test("two voice models: both offered, PyTorch first, the one picked named with --voice-model -- the bare command the binary refuses never run")
    func twoVoiceModels() async throws {
        let folder = try Recorded.folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let job = ScriptedJob()
        let jobs = ScriptedJobs([job])
        let toasts = Toasts()
        let model = await Recorded.composing(folder, jobs: jobs, toasts: toasts)
        #expect(model.voiceModels.map(\.handle) == [Recorded.torch, Recorded.mlx])
        #expect(model.voiceModel == Recorded.torch, "PyTorch first, as the say sheet picks")
        // The binary's refusal of the bare command names exactly the two the picker offers.
        let refusal = try #require(Recorded.error(in: Recorded.media("compose-refused-two.jsonl")))
        #expect(refusal.hasPrefix("the store holds 2 voice models"))
        #expect(model.voiceModels.allSatisfy { refusal.contains($0.handle) })
        let out = folder.appendingPathComponent("the-tides-twice-a-day.mp4").path
        #expect(model.plannedArguments == ["video", "compose", Recorded.brief, "-o", out, "--voice-model", Recorded.torch])
        model.choose(voiceModel: Recorded.mlx)
        #expect(model.plannedArguments?.suffix(2) == ["--voice-model", Recorded.mlx])
        model.choose(voiceModel: Recorded.torch)
        model.compose(planFirst: true)
        #expect(jobs.arguments(0) == ["video", "compose", Recorded.brief, "-o", out, "--voice-model", Recorded.torch, "--dry-run", "--output-format", "stream-json"])
        // The binary took that command and planned (compose-named-planned.jsonl).
        job.replay(stdout: Recorded.media("compose-named-planned.jsonl"), status: 0, streaming: true)
        #expect(await eventually { toasts.contain("Planned the-tides-twice-a-day.mp4") })
    }

    @Test("one voice model: nothing named, the command as before (the Voice place's rule)")
    func oneVoiceModel() async throws {
        let folder = try Recorded.folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let model = await Recorded.composing(folder, jobs: ScriptedJobs([]), runner: Recorded.store(voices: 1))
        #expect(model.voiceModels.map(\.handle) == [Recorded.torch])
        #expect(model.plannedArguments?.contains("--voice-model") == false)
    }
}

@Suite("ComposeConsent", .timeLimit(.minutes(1)))
@MainActor
struct ComposeConsentTests {
    /// A composition refused for its video runtime as the binary refuses one.
    func refused(_ folder: URL, _ jobs: ScriptedJobs, _ first: ScriptedJob, toasts: Toasts = Toasts()) async -> ComposeViewModel {
        let model = await Recorded.composing(folder, jobs: jobs, toasts: toasts)
        model.compose()
        first.replay(
            stdout: Recorded.media("compose-refused-runtime.jsonl"), stderr: Recorded.media("compose-refused-runtime.err"),
            status: 1, streaming: true)
        _ = await eventually { !model.isRunning }
        return model
    }

    @Test("refused for its runtime, compose asks to install -- the stderr's words, never an OK-only alert")
    func refusalAsks() async throws {
        let folder = try Recorded.folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let first = ScriptedJob()
        let jobs = ScriptedJobs([first])
        let model = await refused(folder, jobs, first)
        #expect(model.failure == nil, "the refusal asks; it is not an alert")
        #expect(model.consent?.arguments == ["train", "setup", "--with", "video"])
        #expect(model.consent?.said == "apogee video compose: torch and diffusers are not installed in Apogee's Python environment -- apogee train setup --with video")
        #expect(jobs.starts.count == 1, "nothing installed before the person's yes")
    }

    @Test("installed, the refused composition runs again; refused then for its voice runtime, it asks for that set")
    func bothRuntimes() async throws {
        let folder = try Recorded.folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let first = ScriptedJob()
        let install = ScriptedJob()
        let again = ScriptedJob()
        let jobs = ScriptedJobs([first, install, again])
        let model = await refused(folder, jobs, first)
        model.install()
        #expect(jobs.arguments(1) == ["train", "setup", "--with", "video"])
        #expect(model.isRunning)
        install.replay(stdout: Recorded.media("install-video-done.txt"), status: 0, streaming: false)
        #expect(await eventually { jobs.starts.count == 3 })
        #expect(jobs.arguments(2) == jobs.arguments(0), "the refused composition, run again")
        again.replay(
            stdout: Recorded.media("compose-refused-runtime.jsonl"), stderr: Recorded.media("compose-refused-voice-runtime.err"),
            status: 1, streaming: true)
        #expect(await eventually { model.consent?.arguments == ["train", "setup", "--with", "voice"] })
        #expect(model.failure == nil)
    }

    @Test("while it installs, a second composition starts nothing; Stop reaches the install; a failure is said")
    func supervised() async throws {
        let folder = try Recorded.folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let first = ScriptedJob()
        let install = ScriptedJob()
        let second = ScriptedJob()
        let failing = ScriptedJob()
        let jobs = ScriptedJobs([first, install, second, failing])
        let toasts = Toasts()
        let model = await refused(folder, jobs, first, toasts: toasts)
        model.install()
        model.newComposition()
        model.form.brief = Recorded.brief
        model.compose()
        #expect(jobs.starts.count == 2, "no composition beside the install")
        await model.stop()
        #expect(install.stopped)
        #expect(await eventually { !model.isRunning && toasts.contain("stopped") })
        #expect(jobs.starts.count == 2, "a stopped install runs nothing again")
        // Refused again, asked again, and this time the install fails.
        model.compose()
        second.replay(
            stdout: Recorded.media("compose-refused-runtime.jsonl"), stderr: Recorded.media("compose-refused-runtime.err"),
            status: 1, streaming: true)
        #expect(await eventually { model.consent != nil })
        model.install()
        failing.replay(
            stdout: Recorded.media("install-video-failed.txt"), stderr: Recorded.media("install-video-failed.err"), status: 1,
            streaming: false)
        #expect(await eventually { toasts.contain("pip install for the 'video' set failed") })
        #expect(jobs.starts.count == 4, "a failed install runs nothing again")
    }

    @Test("the install's card: running, installed, put away; an install elsewhere is never offered beside")
    func drawnAndElsewhere() async throws {
        let folder = try Recorded.folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let first = ScriptedJob()
        let install = ScriptedJob()
        let again = ScriptedJob()
        let jobs = ScriptedJobs([first, install, again])
        let toasts = Toasts()
        let model = await refused(folder, jobs, first, toasts: toasts)
        model.install()
        #expect(model.installing?.headline == "Installing the video set")
        model.dismissInstall()
        #expect(model.installing != nil, "a running install's card stays")
        install.replay(stdout: Recorded.media("install-video-done.txt"), status: 0, streaming: false)
        #expect(await eventually { toasts.contain("Installed: apogee train setup --with video -- composing again") })
        #expect(model.installing?.phase == .installed)
        model.dismissInstall()
        #expect(model.installing == nil)
        // Refused again, for the voice set, while another place installs.
        model.installElsewhere = { true }
        again.replay(
            stdout: Recorded.media("compose-refused-runtime.jsonl"), stderr: Recorded.media("compose-refused-voice-runtime.err"),
            status: 1, streaming: true)
        #expect(await eventually { toasts.contain("apogee train setup --with voice is already running") })
        #expect(model.consent == nil)
    }

    @Test("a refusal naming no set to install -- no Python environment at all -- stays the alert, in the CLI's words")
    func otherRefusals() async throws {
        let folder = try Recorded.folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let first = ScriptedJob()
        let model = await Recorded.composing(folder, jobs: ScriptedJobs([first]))
        model.compose()
        // Recorded with the environment moved aside: its stream is the same
        // one `job` line as the runtime's refusal, its stderr its own.
        first.replay(
            stdout: Recorded.media("compose-refused-runtime.jsonl"), stderr: Recorded.media("compose-refused-no-env.err"),
            status: 1, streaming: true)
        #expect(await eventually { model.failure != nil })
        #expect(model.failure?.hasPrefix("apogee video compose: the Python environment has not been created yet. Run 'apogee train setup' first") == true)
        #expect(model.consent == nil, "plain train setup names no set: nothing to ask")
    }
}

@Suite("ComposePlan", .timeLimit(.minutes(1)))
@MainActor
struct ComposePlanTests {
    @Test("plan first says the project was planned -- never composed -- and leaves it planned, to be rendered")
    func planFirstSaysPlanned() async throws {
        let folder = try Recorded.folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let job = ScriptedJob()
        let toasts = Toasts()
        let model = await Recorded.composing(folder, jobs: ScriptedJobs([job]), runner: Recorded.store(voices: 1), toasts: toasts)
        let out = folder.appendingPathComponent("the-tides-twice-a-day.mp4").path
        model.compose(planFirst: true)
        // The dry run keeps its plan beside the video, as the binary did.
        try Recorded.media("compose-planned-project.json")
            .replacingOccurrences(of: "/Users/someone/Movies/Apogee/tides", with: (out as NSString).deletingPathExtension)
            .write(toFile: out + ".json", atomically: true, encoding: .utf8)
        job.replay(stdout: Recorded.media("compose-planned.jsonl"), status: 0, streaming: true)
        #expect(await eventually { !model.isRunning && !toasts.said.isEmpty })
        #expect(toasts.said.allSatisfy { !$0.contains("Composed") }, "a plan is not a composition")
        #expect(toasts.contain("Planned the-tides-twice-a-day.mp4"))
        #expect(model.selectedProject?.status == "planned", "its page offers Render it")
    }

    @Test("a composition rendered says it was composed")
    func renderSaysComposed() async throws {
        let folder = try Recorded.folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let job = ScriptedJob()
        let toasts = Toasts()
        let model = await Recorded.composing(folder, jobs: ScriptedJobs([job]), runner: Recorded.store(voices: 1), toasts: toasts)
        model.compose()
        job.end()
        #expect(await eventually { toasts.contain("Composed the-tides-twice-a-day.mp4") })
    }
}

@Suite("ComposePreview", .timeLimit(.minutes(1)))
@MainActor
struct ComposePreviewTests {
    /// A made composition, open in the edit sheet with an instruction.
    func editing(_ folder: URL, jobs: ScriptedJobs, toasts: Toasts = Toasts(), limit: Duration = .seconds(300)) async throws
        -> ComposeViewModel
    {
        let parent = folder.appendingPathComponent("tides.mp4").path
        try ComposeViewModelTests().projectJSON(parent, status: "done", made: 3)
            .write(toFile: parent + ".json", atomically: true, encoding: .utf8)
        let model = await Recorded.composing(folder, jobs: jobs, runner: Recorded.store(voices: 1), toasts: toasts, previewLimit: limit)
        model.sheet = nil
        let project = try #require(model.projects.first)
        model.edit(project)
        model.form.instruction = "make scene 2 brighter"
        return model
    }

    @Test("Stop reaches the preview's dry run: it ends, nothing changed, the sheet free again")
    func stopReachesThePreview() async throws {
        let folder = try Recorded.folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let preview = ScriptedJob()
        let toasts = Toasts()
        let model = try await editing(folder, jobs: ScriptedJobs([preview]), toasts: toasts)
        let previewing = Task { await model.previewEdit() }
        #expect(await eventually { model.previewing })
        #expect(model.isRunning, "a preview is a run of the place's")
        await model.stop()
        await previewing.value
        #expect(preview.stopped)
        #expect(!model.previewing)
        #expect(model.failure == nil, "a stop is no failure")
    }

    @Test("a planner that never answers is stopped at the preview's time limit, and said")
    func bounded() async throws {
        let folder = try Recorded.folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let preview = ScriptedJob()
        let model = try await editing(folder, jobs: ScriptedJobs([preview]), limit: .milliseconds(200))
        await model.previewEdit()
        #expect(preview.stopped, "stopped at its limit")
        #expect(!model.previewing)
        #expect(model.failure?.contains("didn't answer within") == true)
    }

    @Test("a preview never runs beside a render, nor a render beside a preview")
    func neverBeside() async throws {
        let folder = try Recorded.folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let render = ScriptedJob()
        let preview = ScriptedJob()
        let jobs = ScriptedJobs([render, preview])
        let model = try await editing(folder, jobs: jobs)
        let project = try #require(model.projects.first)
        model.resume(project)
        #expect(jobs.starts.count == 1)
        await model.previewEdit()
        #expect(jobs.starts.count == 1, "no preview beside the render")
        render.end()
        #expect(await eventually { !model.isRunning })
        model.edit(project)
        model.form.instruction = "make scene 2 brighter"
        let previewing = Task { await model.previewEdit() }
        #expect(await eventually { model.previewing })
        model.renderEdit()
        model.resume(project)
        #expect(jobs.starts.count == 2, "no render beside the preview")
        preview.end()
        await previewing.value
    }
}

@Suite("ComposeLibrary", .timeLimit(.minutes(1)))
@MainActor
struct ComposeLibraryTests {
    @Test("a project passed over is listed with why: not JSON, or its video not where it says")
    func skippedListed() async throws {
        let folder = try Recorded.folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let tides = folder.appendingPathComponent("tides.mp4").path
        try ComposeViewModelTests().projectJSON(tides, status: "done", made: 3)
            .write(toFile: tides + ".json", atomically: true, encoding: .utf8)
        try Data("{\"object\":\"video.project\",".utf8).write(to: folder.appendingPathComponent("cut-short.mp4.json"))
        // A project moved here from another folder: it names where it was.
        let moved = "/Users/someone/Elsewhere/moved.mp4"
        try ComposeViewModelTests().projectJSON(moved, status: "done", made: 3)
            .write(to: folder.appendingPathComponent("moved.mp4.json"), atomically: true, encoding: .utf8)
        let model = ComposeViewModel(starter: ScriptedJobs([]))
        model.folders = { [folder] }
        model.connect(binary: Recorded.binary)
        #expect(model.projects.map(\.output) == [tides])
        #expect(model.skipped.map { ($0.file as NSString).lastPathComponent } == ["cut-short.mp4.json", "moved.mp4.json"])
        #expect(model.skipped.map(\.reason) == ["not a JSON document", "its file is gone: \(moved).json"])
    }
}

@Suite("ComposeCaptions", .timeLimit(.minutes(1)))
@MainActor
struct ComposeCaptionsTests {
    @Test("this ffmpeg without libass: the sheet says, before the run, the captions are kept as an .srt -- and what brings libass")
    func keptAsSRT() async throws {
        let folder = try Recorded.folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let model = await Recorded.composing(folder, jobs: ScriptedJobs([]))
        #expect(model.captionsLine == "Captions will be kept beside the video as the-tides-twice-a-day.srt, not burned in: no subtitles filter (this ffmpeg was built without libass).")
        #expect(model.captionsRemedy == "brew install ffmpeg-full && brew unlink ffmpeg; brew link --force ffmpeg-full")
        model.form.captions = false
        #expect(model.captionsLine == nil, "no captions, nothing to say of them")
    }

    @Test("this ffmpeg with libass: the captions burn in, said before the run")
    func burnedIn() async throws {
        let folder = try Recorded.folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let model = await Recorded.composing(folder, jobs: ScriptedJobs([]), runner: Recorded.store(check: "check-captions-ok.json"))
        #expect(model.captionsLine == "Captions will be burned into the frames: this ffmpeg has libass.")
        #expect(model.captionsRemedy == nil)
    }

    @Test("a check with no captions row -- before the CLI's 52e -- says the page will tell")
    func olderCheck() async throws {
        let folder = try Recorded.folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let runner = Recorded.store()
        var document = try #require(JSONSerialization.jsonObject(with: Data(Recorded.read("check-captions-skipped.json").utf8)) as? [String: Any])
        document["rows"] = (document["rows"] as? [[String: Any]] ?? []).filter { $0["name"] as? String != "ffmpeg captions" }
        runner.reading(["check"], String(decoding: try JSONSerialization.data(withJSONObject: document), as: UTF8.self))
        let model = await Recorded.composing(folder, jobs: ScriptedJobs([]), runner: runner)
        #expect(model.captionsLine == "This apogee's check doesn't say whether its ffmpeg burns captions in: the composition's page says once it renders.")
    }

    @Test("the free name passes over a taken .srt, which the composer would never write over")
    func freeOfAnSRT() async throws {
        let folder = try Recorded.folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        try Data("1\n".utf8).write(to: folder.appendingPathComponent("the-tides-twice-a-day.srt"))
        let model = await Recorded.composing(folder, jobs: ScriptedJobs([]))
        #expect(model.plannedOutput?.lastPathComponent == "the-tides-twice-a-day-2.mp4")
        model.form.captions = false
        #expect(model.plannedOutput?.lastPathComponent == "the-tides-twice-a-day.mp4", "no captions, no .srt to keep")
    }
}
