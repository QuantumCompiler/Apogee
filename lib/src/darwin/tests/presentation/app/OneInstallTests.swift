import BusinessJobs
import DataPreferences
import Foundation
import Synchronization
import Testing

@testable import PresentationApp

/// Records each run's argv; every run ends at once.
private final class StartedJobs: JobStarting, @unchecked Sendable {
    private let started = Mutex<[[String]]>([])
    var starts: [[String]] { started.withLock { $0 } }

    func startJob(binary: URL, arguments: [String], streaming: Bool, folder: URL?) -> any JobRunning {
        started.withLock { $0.append(arguments) }
        return Ended()
    }

    private final class Ended: JobRunning, @unchecked Sendable {
        let updates = AsyncStream<JobUpdate> { $0.finish() }
        func stop(grace: Duration) async {}
    }
}

/// GUI 6c: one install into the one environment at a time (4f's rule),
/// asked when an install, a rebuild or Settings' bootstrap starts -- the
/// registry's two predicates, and every place handed them.
@Suite("One install at a time (GUI 6c)", .timeLimit(.minutes(1)))
@MainActor
struct OneInstallTests {
    /// Lists `arguments` in `registry` as a supervised job does: under its
    /// label, the command as typed.
    func running(_ arguments: [String], in registry: ChildRegistry) -> ChildRegistry.Registration {
        registry.register(kind: .job, label: ChildRegistry.label(for: arguments), processIdentifier: 4242) { _ in }
    }

    @Test("an install is a train setup or the bootstrap; a run on the environment is a clip, a composition, a speech or training -- each said by its label")
    func predicates() {
        let registry = ChildRegistry()
        #expect(AppViewModel.installing(in: registry) == nil && AppViewModel.usingEnvironment(in: registry) == nil)
        let pull = running(["models", "pull", "org/model"], in: registry)
        let session = registry.register(kind: .session, label: "Session: actor", processIdentifier: 1) { _ in }
        #expect(AppViewModel.installing(in: registry) == nil && AppViewModel.usingEnvironment(in: registry) == nil, "a pull and a session are neither")
        let clip = running(["video", "generate", "-o", "/Users/someone/Movies/Apogee/a.mp4", "--output-format", "stream-json", "--", "a heron"], in: registry)
        #expect(AppViewModel.usingEnvironment(in: registry) == "apogee video generate a heron", "the label as the monitor shows it")
        #expect(AppViewModel.installing(in: registry) == nil, "a clip installs nothing")
        clip.leave()
        for arguments in [["video", "compose", "-o", "/x.mp4", "--", "a brief"], ["voice", "say", "-o", "/x.wav", "--", "hi"], ["train", "run", "--dataset", "d"]] {
            let run = running(arguments, in: registry)
            #expect(AppViewModel.usingEnvironment(in: registry)?.hasPrefix("apogee " + arguments.prefix(2).joined(separator: " ")) == true, "\(arguments)")
            run.leave()
        }
        let install = running(["train", "setup", "--with", "video"], in: registry)
        #expect(AppViewModel.installing(in: registry) == "apogee train setup")
        install.leave()
        let rebuild = running(["train", "setup", "--rebuild", "--yes", "--with", "voice"], in: registry)
        #expect(AppViewModel.installing(in: registry) == "apogee train setup", "a rebuild is an install too")
        rebuild.leave()
        let bootstrap = running(["bootstrap"], in: registry)
        #expect(AppViewModel.installing(in: registry) == "apogee bootstrap")
        bootstrap.leave()
        #expect(AppViewModel.installing(in: registry) == nil, "released when it ends")
        pull.leave()
        session.leave()
    }

    @Test("Video, Compose, Voice and Settings are each handed the registry's install; the media places its run on the environment")
    func everyPlaceAsks() {
        let registry = ChildRegistry()
        let model = AppViewModel(info: nil, preferences: Preferences(store: MemoryStore()), runner: AppRunner(), jobs: StartedJobs(), children: registry)
        #expect(model.video.installElsewhere() == nil && model.compose.installElsewhere() == nil)
        #expect(model.voice.installElsewhere() == nil && model.settings.installElsewhere() == nil)
        let install = running(["train", "setup", "--with", "video"], in: registry)
        #expect(model.video.installElsewhere() == "apogee train setup")
        #expect(model.compose.installElsewhere() == "apogee train setup")
        #expect(model.voice.installElsewhere() == "apogee train setup")
        #expect(model.settings.installElsewhere() == "apogee train setup")
        install.leave()
        let speech = running(["voice", "say", "-o", "/x.wav", "--", "hi"], in: registry)
        #expect(model.video.usingEnvironment()?.hasPrefix("apogee voice say") == true)
        #expect(model.compose.usingEnvironment()?.hasPrefix("apogee voice say") == true)
        #expect(model.voice.usingEnvironment()?.hasPrefix("apogee voice say") == true)
        speech.leave()
    }

    @Test("Settings' bootstrap with an install in the registry starts nothing and says what runs; check --fix still runs")
    func bootstrapWaits() async {
        let registry = ChildRegistry()
        let jobs = StartedJobs()
        let model = AppViewModel(info: nil, preferences: Preferences(store: MemoryStore()), runner: AppRunner(), jobs: jobs, children: registry)
        await model.settings.connect(binary: URL(fileURLWithPath: "/Users/someone/.local/bin/apogee"), version: "0.2.0")
        let install = running(["train", "setup", "--with", "voice"], in: registry)
        model.settings.fix(["bootstrap"])
        #expect(jobs.starts.isEmpty)
        #expect(model.shell.toast?.text == "apogee train setup is already running: run bootstrap again once it has finished")
        model.settings.fix(["check", "--fix"])
        #expect(jobs.starts == [["check", "--fix"]], "check --fix installs nothing")
        install.leave()
    }
}
