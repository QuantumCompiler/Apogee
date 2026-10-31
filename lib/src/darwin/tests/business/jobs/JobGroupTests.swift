import BusinessJobs
import Darwin
import Foundation
import InfrastructureProcess
import Testing

/// The pid a fake run wrote to `file`, waiting up to two seconds for it.
func writtenPID(_ file: URL) async -> Int32? {
    for _ in 0..<40 {
        if let text = try? String(contentsOf: file, encoding: .utf8),
            let pid = Int32(text.trimmingCharacters(in: .whitespacesAndNewlines))
        {
            return pid
        }
        try? await Task.sleep(for: .milliseconds(50))
    }
    return nil
}

/// Whether process `pid` is gone, waiting up to `limit` for it.
func gone(_ pid: Int32, within limit: Duration = .seconds(2)) async -> Bool {
    let deadline = ContinuousClock.now + limit
    while ContinuousClock.now < deadline {
        if kill(pid, 0) == -1 && errno == ESRCH { return true }
        try? await Task.sleep(for: .milliseconds(50))
    }
    return kill(pid, 0) == -1 && errno == ESRCH
}

/// A job's updates to its end, Stop asked once its first event is out; `nil`
/// when no end came within `limit` of the Stop.
func stoppedOnce(_ job: any JobRunning, grace: Duration, within limit: Duration) async -> (updates: [JobUpdate], took: Duration)? {
    let clock = ContinuousClock()
    let stopped = StopMark()
    return await withTaskGroup(of: (updates: [JobUpdate], took: Duration)?.self) { group in
        group.addTask {
            var updates: [JobUpdate] = []
            for await update in job.updates {
                updates.append(update)
                if case .event = update, stopped.mark(clock.now) {
                    Task { await job.stop(grace: grace) }
                }
            }
            guard !Task.isCancelled, case .ended = updates.last, let at = stopped.at else { return nil }
            return (updates, clock.now - at)
        }
        group.addTask {
            while stopped.at == nil { try? await Task.sleep(for: .milliseconds(20)) }
            try? await Task.sleep(for: limit)
            return nil
        }
        let first = await group.next() ?? nil
        group.cancelAll()
        return first
    }
}

/// When Stop was asked, set once.
final class StopMark: @unchecked Sendable {
    private let lock = NSLock()
    private var instant: ContinuousClock.Instant?

    /// Marks `now` the first time; whether this was it.
    func mark(_ now: ContinuousClock.Instant) -> Bool {
        lock.withLock {
            guard instant == nil else { return false }
            instant = now
            return true
        }
    }

    var at: ContinuousClock.Instant? { lock.withLock { instant } }
}

/// 4c, the first and second defects: a stopped job always ends, though what
/// it started holds its stdout, and nothing of it outlives its end.
@Suite("Jobs and their grandchildren", .timeLimit(.minutes(1)))
struct JobGroupTests {
    /// A run that starts a grandchild sharing its pipes -- SIGINT ignored, as
    /// a shell's background job's is -- says its first event, then works
    /// until it is stopped.
    static let holdsStdout = #"""
        /bin/sleep 25 &
        echo $! > "$(dirname "$0")/grandchild.pid"
        echo '{"type":"job","verb":"video generate"}'
        while true; do /bin/sleep 0.05; done
        """#

    @Test("Stop ends a job whose grandchild holds its stdout, within the failsafe, and the grandchild ends with it")
    func stopEndsTheJob() async throws {
        let fake = try FakeRun()
        let binary = try fake.binary(Self.holdsStdout)
        var job: (any JobRunning)? = JobStarter().startJob(binary: binary, arguments: ["video", "generate"], streaming: true)
        weak let released = job
        let grandchild = try #require(await writtenPID(fake.directory.appendingPathComponent("grandchild.pid")))
        defer { kill(grandchild, SIGKILL) }
        let stopped = await stoppedOnce(job!, grace: .seconds(1), within: .seconds(8))
        let run = try #require(stopped, "the job never ended after Stop: its grandchild held stdout")
        #expect(run.took < .seconds(6), "ended \(run.took) after Stop")
        guard case .ended(let end) = run.updates.last else { return }
        #expect(end.interrupted)
        #expect(await gone(grandchild, within: .seconds(4)), "the grandchild outlived the stopped job")
        job = nil
        for _ in 0..<40 where released != nil {
            try? await Task.sleep(for: .milliseconds(50))
        }
        #expect(released == nil, "the job outlived its end")
    }
}

/// 4c, the fifth defect's core: every supervised run is listed from its spawn
/// to its end, by the supervisor, under the command it ran.
@Suite("The children registry", .timeLimit(.minutes(1)))
struct ChildRegistryTests {
    @Test("a job is listed from its spawn, under the command it ran and its pid, and leaves before its end is said")
    func listedUntilItEnds() async throws {
        let fake = try FakeRun()
        let binary = try fake.binary(#"echo 'pulling'; while true; do /bin/sleep 0.05; done"#)
        let registry = ChildRegistry()
        let job = JobStarter(spawner: ProcessSpawner(), folder: fake.directory, registry: registry)
            .startJob(binary: binary, arguments: ["models", "pull", "org/model", "--output-format", "stream-json"], streaming: false)
        let listed = try #require(registry.children.first)
        #expect(registry.children.count == 1, "listed at its start, not once it first speaks")
        #expect(listed.kind == .job)
        #expect(listed.label == "apogee models pull org/model")
        #expect(listed.processIdentifier == job.processIdentifier)
        var stillListedAtTheEnd: Bool?
        for await update in job.updates {
            if case .said = update { Task { await job.stop(grace: .seconds(2)) } }
            if case .ended = update { stillListedAtTheEnd = !registry.children.isEmpty }
        }
        #expect(stillListedAtTheEnd == false, "a screen hearing the end never finds the job still listed")
    }

    @Test("a run that never starts is never listed")
    func neverStarted() async {
        let registry = ChildRegistry()
        let job = JobStarter(spawner: ProcessSpawner(), folder: nil, registry: registry)
            .startJob(binary: URL(fileURLWithPath: "/nonexistent/apogee"), arguments: ["video", "generate"], streaming: true)
        for await _ in job.updates {}
        #expect(registry.children.isEmpty)
    }

    @Test("stopping a kind stops each of its children by its own Stop, all at once, and leaves the others")
    func stopsAKind() async throws {
        let fake = try FakeRun()
        let binary = try fake.binary(#"trap 'exit 130' INT; while true; do /bin/sleep 0.05; done"#)
        let registry = ChildRegistry()
        let starter = JobStarter(spawner: ProcessSpawner(), folder: fake.directory, registry: registry)
        let jobs = [
            starter.startJob(binary: binary, arguments: ["video", "generate", "a fox"], streaming: false),
            starter.startJob(binary: binary, arguments: ["voice", "say", "hello"], streaming: false),
        ]
        let session = registry.register(kind: .session, label: "Session: actor", processIdentifier: 1) { _ in }
        #expect(registry.children.map(\.label) == ["apogee video generate a fox", "apogee voice say hello", "Session: actor"])
        let clock = ContinuousClock()
        let started = clock.now
        #expect(await registry.stop(.job, grace: .seconds(5)))
        #expect(clock.now - started < .seconds(5), "interrupted, never waited out")
        #expect(registry.children.map(\.label) == ["Session: actor"])
        for job in jobs {
            var last: JobUpdate?
            for await update in job.updates { last = update }
            guard case .ended(let end) = last else {
                Issue.record("no end")
                continue
            }
            #expect(end.interrupted)
        }
        session.relabel("Session: helper")
        #expect(registry.children.map(\.label) == ["Session: helper"])
        session.leave()
        session.leave()
        #expect(registry.children.isEmpty)
    }

    @Test("a job's label is the command as typed, up to its first option, never longer than its limit")
    func labels() {
        #expect(ChildRegistry.label(for: ["models", "pull", "org/model", "--output-format", "stream-json"]) == "apogee models pull org/model")
        #expect(ChildRegistry.label(for: ["video", "generate", "A lighthouse at dusk", "-o", "/Users/someone/Movies/Apogee/a.mp4"])
            == "apogee video generate A lighthouse at dusk")
        #expect(ChildRegistry.label(for: ["check", "--fix"]) == "apogee check")
        #expect(ChildRegistry.label(for: ["--dev", "models", "list"]) == "apogee --dev models list", "a root option first: the first words")
        #expect(ChildRegistry.label(for: ["train", "run", "Llama3.2-1B-Instruct/safetensors/edaba290ce7f", "--dataset", "heat-notes"])
            == "apogee train run Llama3.2-1B-Instruct/safetensors/edaba290ce7f", "a store handle whole")
        let long = ChildRegistry.label(for: ["video", "generate", String(repeating: "a fox at dusk, ", count: 20), "-o", "/tmp/a.mp4"])
        #expect(long.count == ChildRegistry.labelLimit && long.hasSuffix("…") && long.hasPrefix("apogee video generate a fox at dusk, "))
    }

    @Test("a person's text after -- is still named: the command words, then the text, never the options between (GUI 4g)")
    func labelsWithText() {
        #expect(ChildRegistry.label(for: ["video", "generate", "-o", "/Users/someone/Movies/Apogee/a.mp4", "--", "A lighthouse at dusk"])
            == "apogee video generate A lighthouse at dusk")
        #expect(ChildRegistry.label(for: ["task", "run", "--rounds", "8", "--tools", "--output-format", "stream-json", "--", "-x fix the parser"])
            == "apogee task run -x fix the parser", "a dash-leading text is the text")
        #expect(ChildRegistry.label(for: ["voice", "say", "-o", "/tmp/a.wav", "--"]) == "apogee voice say", "nothing after it")
        let long = ChildRegistry.label(for: ["video", "compose", "-o", "/tmp/a.mp4", "--", String(repeating: "a fox at dusk, ", count: 20)])
        #expect(long.count == ChildRegistry.labelLimit && long.hasPrefix("apogee video compose a fox at dusk, "))
    }
}
