import DataProtocol
import Foundation
import InfrastructureProcess
import Synchronization

/// A long run of the binary, supervised: the one place every long child is
/// held -- downloads, clips, compositions, speeches.
///
/// It spawns `apogee <arguments>`, reads its stdout -- the 52b stream's
/// events, or, on a build before it, the lines it prints for people -- and
/// samples the last line of its stderr for the busy line a person would see
/// (shown, never decided on). Stop is the interrupt a person's Ctrl-C is,
/// to the run's whole process group; how it ended is the exit and the
/// stream's own last word. Started with a registry, it is listed there from
/// its spawn to its end (GUI 4c).
public actor SupervisedJob {
    /// What the job says, in order, `.ended` last.
    public nonisolated let updates: AsyncStream<JobUpdate>
    /// The child's process id, when it started.
    public nonisolated let processIdentifier: Int32?
    private let continuation: AsyncStream<JobUpdate>.Continuation
    private let child: (any RunningChild)?
    private let streaming: Bool
    private var interrupted = false
    private var ended = false
    /// Its place in the children registry, from its spawn until its end.
    private nonisolated let registration = Mutex<ChildRegistry.Registration?>(nil)

    /// Spawns `command`. `streaming` says the arguments ask for the 52b
    /// stream, so stdout lines are events; otherwise they are prose.
    public static func start(spawner: any ChildSpawner, command: ChildCommand, streaming: Bool) -> SupervisedJob {
        start(spawner: spawner, command: command, streaming: streaming, registry: nil)
    }

    /// Spawns `command`, listed in `registry` -- under the command as typed
    /// (`ChildRegistry.label(for:)`) -- until it ends.
    public static func start(
        spawner: any ChildSpawner, command: ChildCommand, streaming: Bool, registry: ChildRegistry?
    ) -> SupervisedJob {
        do {
            let child = try spawner.spawn(command)
            child.closeInput()
            let job = SupervisedJob(child: child, streaming: streaming)
            if let registry {
                let registered = registry.register(
                    kind: .job, label: ChildRegistry.label(for: command.arguments),
                    processIdentifier: child.processIdentifier
                ) { [weak job] grace in await job?.stop(grace: grace) }
                job.registration.withLock { $0 = registered }
            }
            Task { await job.pump() }
            Task { await job.sample() }
            return job
        } catch {
            let job = SupervisedJob(child: nil, streaming: streaming)
            job.continuation.yield(.ended(JobEnd(exit: nil, stderrTail: BoundedTail(), interrupted: false, detail: "\(error)")))
            job.continuation.finish()
            return job
        }
    }

    private init(child: (any RunningChild)?, streaming: Bool) {
        (updates, continuation) = AsyncStream.makeStream(of: JobUpdate.self)
        self.child = child
        processIdentifier = child?.processIdentifier
        self.streaming = streaming
    }

    private func pump() async {
        guard let child else { return }
        for await line in child.lines {
            continuation.yield(streaming ? .event(JobEvent.decode(line: line)) : .said(line))
        }
        let exit = await child.waitForExit()
        ended = true
        // Off the monitor before any screen hears the end.
        registration.withLock { registration in
            registration?.leave()
            registration = nil
        }
        continuation.yield(.ended(JobEnd(exit: exit, stderrTail: child.stderrTail, interrupted: interrupted)))
        continuation.finish()
    }

    /// The stderr's last line, each time it changes: the busy line.
    private func sample() async {
        guard let child else { return }
        var last: String?
        while !ended {
            let now = child.stderrTail.lastLine
            if let now, now != last {
                last = now
                continuation.yield(.busy(now))
            }
            try? await Task.sleep(for: .milliseconds(250))
        }
    }

    /// Stops it as Ctrl-C does: an interrupt, to its whole process group;
    /// terminated if it -- or anything it started -- has not gone within
    /// `grace`, killed two seconds after that. Its end comes within the
    /// spawner's failsafe of the run's own exit, whatever its group holds
    /// open; a part of the group that outlives the run (a background job,
    /// which ignores the interrupt) still ends with it.
    public func stop(grace: Duration = .seconds(10)) async {
        guard let child, !ended else { return }
        interrupted = true
        child.interrupt()
        if await settles(within: grace) { return }
        child.terminate()
        if await settles(within: .seconds(2)) { return }
        child.kill()
    }

    /// Whether it has ended and nothing of its group still runs, waiting up
    /// to `duration` for both.
    private func settles(within duration: Duration) async -> Bool {
        let deadline = ContinuousClock.now + duration
        while !settled && ContinuousClock.now < deadline {
            try? await Task.sleep(for: .milliseconds(20))
        }
        return settled
    }

    private var settled: Bool { ended && !(child?.groupRunning ?? false) }
}

/// What a supervised job says.
public enum JobUpdate: Sendable, Equatable {
    /// An event of the 52b stream.
    case event(JobEvent)
    /// A line the verb printed for people, on a build before the stream.
    case said(String)
    /// stderr's last line changed: the busy line, for showing only.
    case busy(String)
    /// It ended; always last.
    case ended(JobEnd)
}

/// How a job ended.
public struct JobEnd: Sendable, Equatable {
    /// `nil` when it never started.
    public let exit: ChildExit?
    public let stderrTail: BoundedTail
    /// Whether it was stopped by its Stop.
    public let interrupted: Bool
    /// Why it never started.
    public let detail: String?

    public init(exit: ChildExit?, stderrTail: BoundedTail, interrupted: Bool, detail: String? = nil) {
        self.exit = exit
        self.stderrTail = stderrTail
        self.interrupted = interrupted
        self.detail = detail
    }

    public var succeeded: Bool { exit?.succeeded ?? false }
}

/// Where jobs come from: the app's starts real children; a test's is
/// scripted.
public protocol JobStarting: Sendable {
    /// `folder` is the run's working directory -- the user's home when `nil`.
    func startJob(binary: URL, arguments: [String], streaming: Bool, folder: URL?) -> any JobRunning
}

extension JobStarting {
    public func startJob(binary: URL, arguments: [String], streaming: Bool) -> any JobRunning {
        startJob(binary: binary, arguments: arguments, streaming: streaming, folder: nil)
    }
}

/// A job as a screen sees it.
public protocol JobRunning: AnyObject, Sendable {
    var updates: AsyncStream<JobUpdate> { get }
    func stop(grace: Duration) async
    /// Its child's process id, when it started.
    var processIdentifier: Int32? { get }
}

extension JobRunning {
    public var processIdentifier: Int32? { nil }
}

extension SupervisedJob: JobRunning {}

/// The app's job starter: real processes, the user's home as the folder,
/// every run listed in a children registry until it ends -- the app's own
/// unless given another.
public struct JobStarter: JobStarting {
    private let spawner: any ChildSpawner
    private let folder: URL?
    private let registry: ChildRegistry

    /// Over real processes, listed in the app's registry.
    public init() {
        self.init(spawner: ProcessSpawner(), folder: nil, registry: .app)
    }

    /// Over real processes, listed in `registry`.
    public init(registry: ChildRegistry) {
        self.init(spawner: ProcessSpawner(), folder: nil, registry: registry)
    }

    public init(spawner: any ChildSpawner, folder: URL?) {
        self.init(spawner: spawner, folder: folder, registry: .app)
    }

    public init(spawner: any ChildSpawner, folder: URL?, registry: ChildRegistry) {
        self.spawner = spawner
        self.folder = folder
        self.registry = registry
    }

    public func startJob(binary: URL, arguments: [String], streaming: Bool, folder: URL?) -> any JobRunning {
        SupervisedJob.start(
            spawner: spawner,
            command: ChildCommand(
                executable: binary, arguments: arguments,
                workingDirectory: folder ?? self.folder ?? FileManager.default.homeDirectoryForCurrentUser),
            streaming: streaming, registry: registry)
    }
}
