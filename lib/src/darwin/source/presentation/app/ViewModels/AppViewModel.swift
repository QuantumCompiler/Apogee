import BusinessJobs
import BusinessReads
import BusinessSession
import DataPreferences
import Foundation
import Observation
import PresentationGraph
import PresentationModels
import PresentationMonitor
import PresentationSessions
import PresentationShell
import PresentationSuites
import PresentationTasks

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
    let suites: SuitesViewModel
    let graph: GraphViewModel

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
        sessions.say = { [weak shell] text in shell?.say(text) }
        models.say = { [weak shell] text in shell?.say(text) }
        models.useInSession = { [weak shell, weak sessions] backend in
            shell?.go(.sessions)
            Task { await sessions?.newSession(on: backend) }
        }
        // Sessions in view or not: the library keeps its one child to the
        // session on screen. A place coming into view reads again.
        shell.routeChanged = { [weak sessions, weak models, weak tasks, weak suites, weak graph] destination in
            Task {
                if destination == .sessions { await sessions?.cameIntoView() } else { await sessions?.leftView() }
                if destination == .models { await models?.appeared() }
                if destination == .tasks { await tasks?.appeared() }
                if destination == .suites { await suites?.appeared() }
                if destination == .graph { await graph?.appeared() }
            }
        }
        self.shell = shell
        self.sessions = sessions
        self.models = models
        self.downloads = downloads
        self.tasks = tasks
        self.monitor = monitor
        self.suites = suites
        self.graph = graph
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
        await suites.connect(binary: sessions.binaryURL)
        await graph.connect(binary: sessions.binaryURL)
    }
}
