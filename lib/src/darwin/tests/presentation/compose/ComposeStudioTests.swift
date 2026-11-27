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
        #expect(model.plannedArguments == ["video", "compose", "-o", out, "--voice-model", Recorded.torch, "--", Recorded.brief])
        model.choose(voiceModel: Recorded.mlx)
        #expect(model.plannedArguments?.suffix(4) == ["--voice-model", Recorded.mlx, "--", Recorded.brief])
        model.choose(voiceModel: Recorded.torch)
        model.compose(planFirst: true)
        #expect(jobs.arguments(0) == ["video", "compose", "-o", out, "--voice-model", Recorded.torch, "--dry-run", "--output-format", "stream-json", "--", Recorded.brief])
        // The binary took that command and planned (compose-named-planned.jsonl,
        // recorded with the brief first; the brief after `--` plans the same
        // stream but for its scenes' seeds and times, GUI 4g).
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
        model.installElsewhere = { "apogee train setup" }
        again.replay(
            stdout: Recorded.media("compose-refused-runtime.jsonl"), stderr: Recorded.media("compose-refused-voice-runtime.err"),
            status: 1, streaming: true)
        #expect(await eventually { toasts.contain("apogee train setup is already running: compose again once it has finished") })
        #expect(model.consent == nil)
    }

    /// GUI 6c: the one-install rule asked when the install starts -- a
    /// consent raised earlier, the rebuild a failure names, the rebuild
    /// itself beside a run on the environment.
    @Test("a consent, a rebuild offer and a rebuild each wait while another install -- or, for the rebuild, a run on the environment -- runs")
    func installsWaitWhenTheyStart() async throws {
        let folder = try Recorded.folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let unfit = "apogee train: the Python environment at /h/venv is on Python 3.14.5, and the 'video' set runs on >=3.10 -- rebuild it on a Python that fits: apogee train setup --rebuild --with video\n"
        let rebuild = ["train", "setup", "--rebuild", "--yes", "--with", "video"]
        let first = ScriptedJob()
        let install = ScriptedJob()
        let again = ScriptedJob()
        let installAgain = ScriptedJob()
        let jobs = ScriptedJobs([first, install, again, installAgain])
        let toasts = Toasts()
        let model = await refused(folder, jobs, first, toasts: toasts)
        // A consent raised while nothing installed; another install started since.
        model.installElsewhere = { "apogee train setup" }
        model.install()
        #expect(jobs.starts.count == 1, "no second train setup beside the first")
        #expect(model.consent?.arguments == ["train", "setup", "--with", "video"], "kept for later")
        #expect(toasts.said.last == "apogee train setup is already running: press Install again once it has finished")
        // It has finished: Install runs; and fails naming the rebuild while the bootstrap runs.
        model.installElsewhere = { nil }
        model.install()
        #expect(jobs.starts.count == 2)
        model.installElsewhere = { "apogee bootstrap" }
        install.replay(stdout: "", stderr: unfit, status: 1, streaming: false)
        #expect(await eventually { !model.isInstalling && toasts.said.last?.hasPrefix("apogee bootstrap is already running") == true })
        #expect(model.consent == nil, "no rebuild offered beside it")
        #expect(toasts.said.last == "apogee bootstrap is already running: once it has finished, apogee train setup --rebuild --yes --with video rebuilds the environment")
        // Later, nothing else installing: the rebuild is asked -- and waits for a speech.
        model.installElsewhere = { nil }
        model.compose()
        again.replay(
            stdout: Recorded.media("compose-refused-runtime.jsonl"), stderr: Recorded.media("compose-refused-runtime.err"),
            status: 1, streaming: true)
        #expect(await eventually { model.consent != nil })
        model.install()
        installAgain.replay(stdout: "", stderr: unfit, status: 1, streaming: false)
        #expect(await eventually { model.consent?.arguments == rebuild })
        model.usingEnvironment = { "apogee voice say" }
        model.install()
        #expect(jobs.starts.count == 4, "nothing moves the environment from under the speech")
        #expect(model.consent?.arguments == rebuild)
        #expect(toasts.said.last == "apogee voice say is using Apogee's Python environment: press Rebuild again once it has finished")
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

/// GUI 6j: the CLI's edit dry run writes nothing (its 52c), so Render runs
/// the command again and its planner plans the edit again. Over the edit's
/// dry runs recorded on a mock planner (tests/business/media/Fixtures/
/// compose-edit-*): the sheet says it, a preview for other arguments goes,
/// and the page says where the render's plan came out otherwise.
@Suite("ComposeRenderPlansAgain", .timeLimit(.minutes(1)))
@MainActor
struct ComposeRenderPlansAgainTests {
    static let instruction = "make the second scene the moon"
    static let replanning =
        "Render plans the edit again with planner: what renders can differ from this preview, and the planner is called a second time."

    /// A recorded document with its home moved to `folder`.
    static func relocated(_ name: String, to folder: URL) -> String {
        Recorded.media(name).replacingOccurrences(of: "/Users/someone/Movies/Apogee", with: folder.path)
    }

    /// A recorded dry run's project as its render saves it: done, each scene
    /// spoken and rendered, its sources and changes as the dry run wrote them.
    static func made(_ document: String) -> String {
        document.replacingOccurrences(of: #""status":"planned""#, with: #""status":"done""#)
            .replacingOccurrences(of: #""clip":null"#, with: #""clip":{"file":"/s/clip.mp4"}"#)
            .replacingOccurrences(of: #""voice":null"#, with: #""voice":{"file":"/s/voice.wav"}"#)
    }

    /// The recorded parent, made, open in the edit sheet with `instruction`
    /// and `redo`.
    func editing(_ folder: URL, jobs: ScriptedJobs, toasts: Toasts = Toasts(), instruction: String = instruction, redo: Set<Int> = [])
        throws -> ComposeViewModel
    {
        let parent = folder.appendingPathComponent("tides.mp4").path
        try Self.made(Self.relocated("compose-edit-parent.json", to: folder)).write(toFile: parent + ".json", atomically: true, encoding: .utf8)
        let model = ComposeViewModel(starter: jobs, pollInterval: .milliseconds(20))
        model.folders = { [folder] }
        model.say = { toasts.say($0) }
        model.connect(binary: Recorded.binary)
        let project = try #require(model.projects.first)
        model.edit(project)
        model.form.instruction = instruction
        model.form.redo = redo
        return model
    }

    /// Previews the sheet's edit, the dry run printing `document`.
    func preview(_ model: ComposeViewModel, _ job: ScriptedJob, printing document: String) async {
        let previewing = Task { await model.previewEdit() }
        _ = await eventually { model.previewing }
        job.replay(stdout: document, status: 0, streaming: false)
        await previewing.value
    }

    @Test("a drawn preview says Render plans again, naming plan.backend's planner; without one, the line without the name")
    func lineNamesThePlanner() async throws {
        let folder = try Recorded.folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let job = ScriptedJob()
        let model = try editing(folder, jobs: ScriptedJobs([job]))
        #expect(model.renderPlansAgain == nil, "nothing said before a preview")
        #expect(model.renderHelp == "Plans the edit and renders it, in one run")
        await preview(model, job, printing: Self.relocated("compose-edit-preview.json", to: folder))
        #expect(model.previewDiff?.changes.map(\.kind) == [.unchanged, .changed(["prompt"])])
        #expect(model.renderPlansAgain == Self.replanning)
        #expect(model.renderHelp == "Plans the edit again with planner, then renders it: it can differ from this preview")
        // An older document, naming no planner.
        let older = ScriptedJob()
        let unnamed = try editing(folder, jobs: ScriptedJobs([older]))
        await preview(unnamed, older, printing: Self.relocated("compose-edit-preview.json", to: folder)
            .replacingOccurrences(of: #""plan":{"attempts":1,"backend":"planner"},"#, with: ""))
        #expect(unnamed.previewDiff != nil)
        #expect(unnamed.renderPlansAgain == "Render plans the edit again: what renders can differ from this preview.")
    }

    @Test("a re-roll alone plans nothing: its preview stands, and no line says Render plans again")
    func redoAloneSaysNoReplan() async throws {
        let folder = try Recorded.folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let job = ScriptedJob()
        let model = try editing(folder, jobs: ScriptedJobs([job]), instruction: "", redo: [2])
        await preview(model, job, printing: Self.relocated("compose-edit-redo.json", to: folder))
        #expect(model.previewDiff?.changes.map(\.kind) == [.unchanged, .changed(["seed"])])
        #expect(model.renderPlansAgain == nil)
        #expect(model.renderHelp == "Renders the scenes chosen again on fresh seeds; nothing is planned")
    }

    @Test("a preview, then the instruction changed: the diff and the line go -- and stay gone when it is typed back")
    func instructionChangeDropsThePreview() async throws {
        let folder = try Recorded.folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let job = ScriptedJob()
        let model = try editing(folder, jobs: ScriptedJobs([job]))
        await preview(model, job, printing: Self.relocated("compose-edit-preview.json", to: folder))
        #expect(model.previewDiff != nil)
        model.form.instruction = "make the first scene the moon"
        #expect(model.preview == nil)
        #expect(model.previewDiff == nil)
        #expect(model.renderPlansAgain == nil)
        model.form.instruction = Self.instruction
        #expect(model.previewDiff == nil, "a dropped preview is planned again, never brought back")
    }

    @Test("a preview, then a re-roll toggled: the diff and the line go")
    func redoToggleDropsThePreview() async throws {
        let folder = try Recorded.folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let job = ScriptedJob()
        let model = try editing(folder, jobs: ScriptedJobs([job]))
        await preview(model, job, printing: Self.relocated("compose-edit-preview.json", to: folder))
        #expect(model.renderPlansAgain == Self.replanning)
        model.form.redo.insert(1)
        #expect(model.previewDiff == nil)
        #expect(model.renderPlansAgain == nil)
    }

    @Test("typed into while it planned: the plan lands for the earlier words, so it is dropped and said")
    func changedWhilePlanning() async throws {
        let folder = try Recorded.folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let job = ScriptedJob()
        let toasts = Toasts()
        let model = try editing(folder, jobs: ScriptedJobs([job]), toasts: toasts)
        let previewing = Task { await model.previewEdit() }
        #expect(await eventually { model.previewing })
        model.form.instruction = Self.instruction + ", at night"
        job.replay(stdout: Self.relocated("compose-edit-preview.json", to: folder), status: 0, streaming: false)
        await previewing.value
        #expect(model.previewDiff == nil)
        #expect(toasts.contain("The edit changed while it was planned"))
    }

    @Test("the sheet closed while it planned -- Cancel, or another sheet opened: the plan is dropped, and nothing is said")
    func closedWhilePlanning() async throws {
        let folder = try Recorded.folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let job = ScriptedJob()
        let toasts = Toasts()
        let model = try editing(folder, jobs: ScriptedJobs([job]), toasts: toasts)
        let previewing = Task { await model.previewEdit() }
        #expect(await eventually { model.previewing })
        model.sheet = nil
        job.replay(stdout: Self.relocated("compose-edit-preview.json", to: folder), status: 0, streaming: false)
        await previewing.value
        #expect(model.preview == nil)
        #expect(model.previewDiff == nil)
        #expect(!toasts.contain("The edit changed"), "nothing changed: the person closed the sheet")
        // Closed, then the new sheet opened before the planner answered.
        let later = ScriptedJob()
        let other = try editing(folder, jobs: ScriptedJobs([later]), toasts: toasts)
        let planning = Task { await other.previewEdit() }
        #expect(await eventually { other.previewing })
        other.sheet = nil
        other.newComposition()
        later.replay(stdout: Self.relocated("compose-edit-preview.json", to: folder), status: 0, streaming: false)
        await planning.value
        #expect(other.preview == nil)
        #expect(!toasts.contain("The edit changed"))
    }

    /// A preview, then Render, its document `rendered` written beside the
    /// output as the CLI saves it: the view-model, the output, the render's argv.
    func rendered(_ folder: URL, preview document: String, rendered: String) async throws -> (ComposeViewModel, String, [String]?) {
        let previewJob = ScriptedJob()
        let render = ScriptedJob()
        let jobs = ScriptedJobs([previewJob, render])
        let model = try editing(folder, jobs: jobs)
        await preview(model, previewJob, printing: Self.relocated(document, to: folder))
        let output = try #require(model.plannedOutput?.path)
        model.renderEdit()
        try Self.made(Self.relocated(rendered, to: folder)).write(toFile: output + ".json", atomically: true, encoding: .utf8)
        render.end()
        _ = await eventually { !model.isRunning && model.selectedProject?.output == output }
        return (model, output, jobs.arguments(1))
    }

    @Test("rendered, the plan made again came out otherwise: the page says its changes differ -- and Render's command is today's")
    func pageSaysTheRenderDiffers() async throws {
        let folder = try Recorded.folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let (model, output, argv) = try await rendered(folder, preview: "compose-edit-preview.json", rendered: "compose-edit-replanned.json")
        let parent = folder.appendingPathComponent("tides.mp4").path
        #expect(argv == ["video", "compose", "--from", parent, "--edit", Self.instruction, "-o", output], "the render's argv, unchanged")
        let project = try #require(model.selectedProject)
        #expect(project.status == "done")
        #expect(model.diff(project)?.changes.map(\.kind) == [.changed(["narration"]), .unchanged], "the page draws the render's own diff")
        #expect(model.renderDiffers(project) == "The render planned again, and its changes differ from the preview")
    }

    @Test("rendered as previewed: nothing extra is said")
    func pageSaysNothingWhenTheyMatch() async throws {
        let folder = try Recorded.folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let (model, _, _) = try await rendered(folder, preview: "compose-edit-preview.json", rendered: "compose-edit-preview.json")
        let project = try #require(model.selectedProject)
        #expect(model.renderDiffers(project) == nil)
    }

    @Test("a re-rolled scene's seed differs, its words match: nothing extra is said")
    func seedOnlyDifferenceSaysNothing() async throws {
        let folder = try Recorded.folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let previewJob = ScriptedJob()
        let render = ScriptedJob()
        let jobs = ScriptedJobs([previewJob, render])
        let model = try editing(folder, jobs: jobs, redo: [1])
        await preview(model, previewJob, printing: Self.relocated("compose-edit-reroll-preview.json", to: folder))
        #expect(model.renderPlansAgain == Self.replanning, "an instruction with a re-roll plans again")
        let output = try #require(model.plannedOutput?.path)
        model.renderEdit()
        try Self.made(Self.relocated("compose-edit-reroll-again.json", to: folder)).write(toFile: output + ".json", atomically: true, encoding: .utf8)
        render.end()
        #expect(await eventually { !model.isRunning && model.selectedProject?.output == output })
        let project = try #require(model.selectedProject)
        #expect(project.scenes.first?.seed != ProjectRecord(JSONValue.parse(Recorded.media("compose-edit-reroll-preview.json"))!)?.scenes.first?.seed)
        #expect(model.renderDiffers(project) == nil)
    }

    @Test("a render not started from the drawn preview -- none, or one dropped by a change -- gets no line")
    func renderWithoutAPreviewSaysNothing() async throws {
        let folder = try Recorded.folder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let previewJob = ScriptedJob()
        let render = ScriptedJob()
        let jobs = ScriptedJobs([previewJob, render])
        let model = try editing(folder, jobs: jobs)
        await preview(model, previewJob, printing: Self.relocated("compose-edit-preview.json", to: folder))
        model.form.instruction = "make the first scene the moon"
        let output = try #require(model.plannedOutput?.path)
        model.renderEdit()
        #expect(jobs.arguments(1)?.contains("make the first scene the moon") == true)
        try Self.made(Self.relocated("compose-edit-replanned.json", to: folder)).write(toFile: output + ".json", atomically: true, encoding: .utf8)
        render.end()
        #expect(await eventually { !model.isRunning && model.selectedProject?.output == output })
        let project = try #require(model.selectedProject)
        #expect(model.renderDiffers(project) == nil, "no preview stood beside this Render")
    }
}
