import BusinessJobs
import BusinessMedia
import DataPreferences
import DataProtocol
import Foundation
import InfrastructureProcess
import Synchronization
import Testing

@testable import PresentationVideo

@Suite("VideoViewModel", .timeLimit(.minutes(1)))
@MainActor
struct VideoViewModelTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func home() throws -> URL {
        let home = FileManager.default.temporaryDirectory.appendingPathComponent("video-home-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: home.appendingPathComponent("Movies/Apogee"), withIntermediateDirectories: true)
        return home
    }

    @Test("a clip is generated into a free name in ~/Movies/Apogee, supervised, and the library read back")
    func generate() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let job = ScriptedJob()
        let jobs = ScriptedJobs([job])
        let model = VideoViewModel(starter: jobs, preferences: Preferences(store: MemoryStore()), home: home)
        model.connect(binary: Self.binary)
        #expect(model.clips.isEmpty)
        model.newClip()
        model.form.prompt = "A lighthouse at dusk"
        let out = home.appendingPathComponent("Movies/Apogee/a-lighthouse-at-dusk.mp4").path
        #expect(model.plannedArguments == ["video", "generate", "A lighthouse at dusk", "-o", out])
        model.generate()
        #expect(!model.sheetOpen)
        #expect(jobs.starts.first?.arguments == ["video", "generate", "A lighthouse at dusk", "-o", out])
        #expect(model.isRunning)
        job.say(.busy("step 12/50"))
        #expect(await eventually { model.run?.busy == "step 12/50" })
        // The CLI writes the clip and its record when whole.
        try Data("mp4".utf8).write(to: URL(fileURLWithPath: out))
        try #"{"object":"video.generation","status":"done","output":"\#(out)","sidecar":"\#(out).json","model":{"name":"wan"},"prompt":"A lighthouse at dusk","mode":"t2v","settings":{},"created_at":"2026-10-20T10:00:00Z"}"#.write(toFile: out + ".json", atomically: true, encoding: .utf8)
        job.end()
        #expect(await eventually { !model.isRunning })
        #expect(model.clips.map(\.output) == [out])
        #expect(model.selected == out)
    }

    @Test("on a 52b build the run streams its steps")
    func streams() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let job = ScriptedJob()
        let jobs = ScriptedJobs([job])
        let model = VideoViewModel(starter: jobs, preferences: Preferences(store: MemoryStore()), home: home)
        model.streams = { true }
        model.connect(binary: Self.binary)
        model.form.prompt = "waves"
        model.generate()
        #expect(jobs.starts.first?.arguments.suffix(2) == ["--output-format", "stream-json"])
        #expect(jobs.starts.first?.streaming == true)
        job.say(.event(.step(step: 3, steps: 50, scene: nil)))
        #expect(await eventually { model.run?.step?.done == 3 })
        await model.stop()
        #expect(await eventually { model.run?.status == .stopped })
    }

    @Test("a run refused for its runtime asks before installing; yes installs then runs again, no installs nothing")
    func consent() async throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let refused = ScriptedJob()
        let install = ScriptedJob()
        let retried = ScriptedJob()
        let jobs = ScriptedJobs([refused, install, retried])
        let model = VideoViewModel(starter: jobs, preferences: Preferences(store: MemoryStore()), home: home)
        model.connect(binary: Self.binary)
        model.form.prompt = "waves"
        model.generate()
        var tail = BoundedTail()
        tail.append(Data("apogee video: the video runtime is not installed -- run: apogee train setup --with video\n".utf8))
        refused.endWith(tail: tail)
        #expect(await eventually { model.consent != nil })
        #expect(model.consent?.arguments == ["train", "setup", "--with", "video"])
        #expect(jobs.starts.count == 1, "nothing installed before the person's yes")
        model.install()
        #expect(jobs.starts[1].arguments == ["train", "setup", "--with", "video"])
        install.end()
        #expect(await eventually { jobs.starts.count == 3 })
        #expect(jobs.starts[2].arguments == jobs.starts[0].arguments, "the refused run, run again")
        retried.end(status: 1)

        #expect(VideoViewModel.installCommand(in: "no runtime -- apogee train setup --with voice.") == ["train", "setup", "--with", "voice"])
        #expect(VideoViewModel.installCommand(in: "a failure of another kind") == nil)
    }

    @Test("run again and continue are --from the record and --extend the clip")
    func iterate() throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let model = VideoViewModel(starter: ScriptedJobs([]), preferences: Preferences(store: MemoryStore()), home: home)
        let clip = try #require(ClipRecord(JSONValue.parse(#"{"object":"video.generation","output":"/m/light.mp4","sidecar":"/m/light.mp4.json","prompt":"p"}"#)!))
        model.newClip(from: clip)
        #expect(model.form.start == .again("/m/light.mp4.json"))
        #expect(model.plannedArguments?.prefix(4) == ["video", "generate", "--from", "/m/light.mp4.json"])
        #expect(model.plannedOutput.lastPathComponent == "light.mp4")
        model.newClip(from: clip, extend: true)
        #expect(model.form.start == .extend("/m/light.mp4"))
    }

    @Test("a folder the person adds is read too, and remembered")
    func folders() throws {
        let home = try home()
        defer { try? FileManager.default.removeItem(at: home) }
        let store = MemoryStore()
        let model = VideoViewModel(starter: ScriptedJobs([]), preferences: Preferences(store: store), home: home)
        let extra = home.appendingPathComponent("Elsewhere")
        model.add(folder: extra)
        model.add(folder: extra)
        #expect(model.folders.map(\.lastPathComponent) == ["Apogee", "Elsewhere"])
        #expect(Preferences(store: store).videoFolders == [extra.path])
        model.remove(folder: extra)
        #expect(model.folders.count == 1)
    }
}
