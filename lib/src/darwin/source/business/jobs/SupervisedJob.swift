import DataProtocol
import Foundation
import InfrastructureProcess

/// A long run of the binary, supervised: the one place every long child is
/// held -- downloads, clips, compositions, speeches.
///
/// It spawns `apogee <arguments>`, reads its stdout -- the 52b stream's
/// events, or, on a build before it, the lines it prints for people -- and
/// samples the last line of its stderr for the busy line a person would see
/// (shown, never decided on). Stop is the interrupt a person's Ctrl-C is;
/// how it ended is the exit and the stream's own last word.
public actor SupervisedJob {
    /// What the job says, in order, `.ended` last.
    public nonisolated let updates: AsyncStream<JobUpdate>
    private let continuation: AsyncStream<JobUpdate>.Continuation
    private let child: (any RunningChild)?
    private let streaming: Bool
    private var interrupted = false
    private var ended = false

    /// Spawns `command`. `streaming` says the arguments ask for the 52b
    /// stream, so stdout lines are events; otherwise they are prose.
    public static func start(spawner: any ChildSpawner, command: ChildCommand, streaming: Bool) -> SupervisedJob {
        do {
            let child = try spawner.spawn(command)
            child.closeInput()
            let job = SupervisedJob(child: child, streaming: streaming)
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
        self.streaming = streaming
    }

    private func pump() async {
        guard let child else { return }
        for await line in child.lines {
            continuation.yield(streaming ? .event(JobEvent.decode(line: line)) : .said(line))
        }
        let exit = await child.waitForExit()
        ended = true
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

    /// Stops it as Ctrl-C does: an interrupt; terminated if it has not gone
    /// within `grace`, killed two seconds after that.
    public func stop(grace: Duration = .seconds(10)) async {
        guard let child, !ended else { return }
        interrupted = true
        child.interrupt()
        if await ends(within: grace) { return }
        child.terminate()
        if await ends(within: .seconds(2)) { return }
        child.kill()
    }

    private func ends(within duration: Duration) async -> Bool {
        let deadline = ContinuousClock.now + duration
        while !ended && ContinuousClock.now < deadline {
            try? await Task.sleep(for: .milliseconds(20))
        }
        return ended
    }
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
}

extension SupervisedJob: JobRunning {}

/// The app's job starter: real processes, the user's home as the folder.
public struct JobStarter: JobStarting {
    private let spawner: any ChildSpawner
    private let folder: URL?

    /// Over real processes.
    public init() {
        self.init(spawner: ProcessSpawner(), folder: nil)
    }

    public init(spawner: any ChildSpawner, folder: URL?) {
        self.spawner = spawner
        self.folder = folder
    }

    public func startJob(binary: URL, arguments: [String], streaming: Bool, folder: URL?) -> any JobRunning {
        SupervisedJob.start(
            spawner: spawner,
            command: ChildCommand(
                executable: binary, arguments: arguments,
                workingDirectory: folder ?? self.folder ?? FileManager.default.homeDirectoryForCurrentUser),
            streaming: streaming)
    }
}
