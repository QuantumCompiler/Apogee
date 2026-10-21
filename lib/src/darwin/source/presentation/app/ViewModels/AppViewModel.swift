import BusinessJobs
import BusinessReads
import BusinessSession
import DataPreferences
import Foundation
import Observation
import PresentationCompose
import PresentationGraph
import PresentationKnowledge
import PresentationModels
import PresentationConsole
import PresentationMonitor
import PresentationSessions
import PresentationSettings
import PresentationShell
import PresentationSuites
import PresentationTasks
import PresentationVideo
import PresentationVoice

/// The application's state: its name, the build it is, the window around its
/// screens, and the session library.
///
/// SwiftUI-free (ADR 0002): it reads the bundle's information dictionary as a
/// plain value, and builds the view-models over the client core and the app's
/// preferences -- the composition the views bind to, never reach past.
@MainActor
@Observable
final class AppViewModel {
    let title = "Apogee"
    let versionLine: String
    let shell: ShellViewModel
    let sessions: SessionLibraryViewModel
    let models: ModelsViewModel
    let downloads: DownloadsViewModel
    let tasks: TasksViewModel
    let monitor: MonitorViewModel
    let console: ConsoleViewModel
    let suites: SuitesViewModel
    let graph: GraphViewModel
    let records: RecordsViewModel
    let collections: CollectionsViewModel
    let video: VideoViewModel
    let compose: ComposeViewModel
    let voice: VoiceViewModel
    let settings: SettingsViewModel

    init(
        info: [String: Any]?, preferences: Preferences, starter: (any ChatStarting)? = nil,
        runner: (any CommandRunning)? = nil, jobs: (any JobStarting)? = nil
    ) {
        let version = (info?["CFBundleShortVersionString"] as? String).flatMap { $0.isEmpty ? nil : $0 }
        // Only the release build (scripts/build.sh) stamps the version; a
        // build from Xcode leaves it empty.
        versionLine = version.map { "Version \($0)" } ?? "Development build"
        let shell = ShellViewModel(preferences: preferences)
        let sessions = SessionLibraryViewModel(
            starter: starter ?? ApogeeClient(identity: ClientIdentity(version: version ?? "development")),
            runner: runner ?? ReadRunner(), preferences: preferences)
        let models = ModelsViewModel(runner: runner ?? ReadRunner())
        let downloads = DownloadsViewModel(starter: jobs ?? JobStarter())
        downloads.streams = { [weak sessions] in sessions?.streamsJobs ?? false }
        downloads.finished = { [weak models] in Task { await models?.refresh() } }
        let tasks = TasksViewModel(runner: runner ?? ReadRunner(), starter: jobs ?? JobStarter())
        tasks.say = { [weak shell] text in shell?.say(text) }
        let console = ConsoleViewModel(runner: runner ?? ReadRunner(), starter: jobs ?? JobStarter())
        let monitor = MonitorViewModel(runner: runner ?? ReadRunner())
        monitor.liveChildren = { [weak sessions, weak downloads, weak tasks] in
            var children: [(label: String, pid: Int32)] = []
            if let session = sessions?.current, let pid = session.processIdentifier {
                children.append(("Session: \(session.transcript.model ?? "starting")", pid))
            }
            return children + (downloads?.children ?? []) + (tasks?.children ?? [])
        }
        let suites = SuitesViewModel(runner: runner ?? ReadRunner())
        suites.say = { [weak shell] text in shell?.say(text) }
        let graph = GraphViewModel(runner: runner ?? ReadRunner(), starter: jobs ?? JobStarter())
        graph.say = { [weak shell] text in shell?.say(text) }
        let records = RecordsViewModel(runner: runner ?? ReadRunner(), starter: jobs ?? JobStarter())
        records.say = { [weak shell] text in shell?.say(text) }
        let collections = CollectionsViewModel(runner: runner ?? ReadRunner(), starter: jobs ?? JobStarter())
        collections.say = { [weak shell] text in shell?.say(text) }
        let video = VideoViewModel(
            starter: jobs ?? JobStarter(), preferences: preferences, home: FileManager.default.homeDirectoryForCurrentUser)
        video.say = { [weak shell] text in shell?.say(text) }
        video.streams = { [weak sessions] in sessions?.streamsJobs ?? false }
        let compose = ComposeViewModel(starter: jobs ?? JobStarter())
        compose.say = { [weak shell] text in shell?.say(text) }
        compose.streams = { [weak sessions] in sessions?.streamsJobs ?? false }
        compose.plansAlone = { [weak sessions] in sessions?.hasTrack52 ?? false }
        compose.folders = { [weak video] in video?.folders ?? [] }
        let voice = VoiceViewModel(
            runner: runner ?? ReadRunner(), starter: jobs ?? JobStarter(), preferences: preferences,
            home: FileManager.default.homeDirectoryForCurrentUser)
        voice.say = { [weak shell] text in shell?.say(text) }
        voice.streams = { [weak sessions] in sessions?.streamsJobs ?? false }
        let settings = SettingsViewModel(runner: runner ?? ReadRunner(), starter: jobs ?? JobStarter(), preferences: preferences)
        settings.say = { [weak shell] text in shell?.say(text) }
        settings.hasTrack52 = { [weak sessions] in sessions?.hasTrack52 ?? false }
        sessions.say = { [weak shell] text in shell?.say(text) }
        models.say = { [weak shell] text in shell?.say(text) }
        models.useInSession = { [weak shell, weak sessions] backend in
            shell?.go(.sessions)
            Task { await sessions?.newSession(on: backend) }
        }
        // Sessions in view or not: the library keeps its one child to the
        // session on screen. A place coming into view reads again.
        shell.routeChanged = { [weak sessions, weak models, weak tasks, weak suites, weak graph, weak video, weak compose, weak voice, weak records, weak collections] destination in
            Task {
                if destination == .sessions { await sessions?.cameIntoView() } else { await sessions?.leftView() }
                if destination == .models { await models?.appeared() }
                if destination == .tasks { await tasks?.appeared() }
                if destination == .suites { await suites?.appeared() }
                if destination == .graph { await graph?.appeared() }
                if destination == .knowledge {
                    await records?.appeared()
                    await collections?.appeared()
                }
                if destination == .voice { await voice?.appeared() }
                if destination == .video {
                    video?.appeared()
                    compose?.appeared()
                }
            }
        }
        self.shell = shell
        self.sessions = sessions
        self.models = models
        self.downloads = downloads
        self.tasks = tasks
        self.monitor = monitor
        self.console = console
        self.suites = suites
        self.graph = graph
        self.records = records
        self.collections = collections
        self.video = video
        self.compose = compose
        self.voice = voice
        self.settings = settings
    }

    convenience init(bundle: Bundle) {
        self.init(info: bundle.infoDictionary, preferences: .standard())
    }

    /// The app started: the library reads and opens what is in view.
    func start() async {
        await sessions.start(inView: shell.route == .sessions)
        await models.connect(binary: sessions.binaryURL)
        downloads.connect(binary: sessions.binaryURL)
        await tasks.connect(binary: sessions.binaryURL)
        monitor.connect(binary: sessions.binaryURL)
        console.connect(binary: sessions.binaryURL)
        await suites.connect(binary: sessions.binaryURL)
        await graph.connect(binary: sessions.binaryURL)
        await records.connect(binary: sessions.binaryURL)
        await collections.connect(binary: sessions.binaryURL)
        video.connect(binary: sessions.binaryURL)
        compose.connect(binary: sessions.binaryURL)
        await voice.connect(binary: sessions.binaryURL)
        await settings.connect(binary: sessions.binaryURL, version: sessions.binaryVersion)
    }
}
