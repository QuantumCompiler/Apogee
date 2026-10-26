import Darwin
import Foundation
import InfrastructureProcess
import Testing

/// A folder of a test's own, for the pid files its scripts write.
struct PidFolder {
    let url: URL

    init() throws {
        url = FileManager.default.temporaryDirectory
            .appendingPathComponent("process-group-\(UUID().uuidString)").resolvingSymlinksInPath()
        try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
    }

    /// Where a script writes the pid it is asked for.
    func file(_ name: String) -> URL { url.appendingPathComponent(name) }

    /// The pid a script wrote to `name`, waiting up to two seconds for it.
    func pid(_ name: String) async -> Int32? {
        for _ in 0..<40 {
            if let text = try? String(contentsOf: file(name), encoding: .utf8),
                let pid = Int32(text.trimmingCharacters(in: .whitespacesAndNewlines))
            {
                return pid
            }
            try? await Task.sleep(for: .milliseconds(50))
        }
        return nil
    }

    func remove() {
        try? FileManager.default.removeItem(at: url)
    }
}

/// Every line `child` writes, or `nil` when its stdout has not finished
/// within `limit`.
func lines(of child: any RunningChild, within limit: Duration) async -> [String]? {
    await withTaskGroup(of: [String]?.self) { group in
        group.addTask {
            var all: [String] = []
            for await line in child.lines { all.append(line) }
            return Task.isCancelled ? nil : all
        }
        group.addTask {
            try? await Task.sleep(for: limit)
            return nil
        }
        let first = await group.next() ?? nil
        group.cancelAll()
        return first
    }
}

/// How `child` ended, or `nil` when it has not within `limit`.
func ending(of child: any RunningChild, within limit: Duration) async -> ChildExit? {
    await withTaskGroup(of: ChildExit?.self) { group in
        group.addTask { await child.waitForExit() }
        group.addTask {
            try? await Task.sleep(for: limit)
            return nil
        }
        let first = await group.next() ?? nil
        group.cancelAll()
        return first
    }
}

/// Whether process `pid` is running, a zombie not counted.
func running(_ pid: Int32) -> Bool {
    kill(pid, 0) == 0
}

/// 4c: a child is a process group of its own, its signals reach the whole
/// group, and what it starts can't hold its end -- or the child itself --
/// hostage. Real `/bin/sh` scripts, the grandchild a `sleep` they start.
@Suite("Process groups", .timeLimit(.minutes(1)))
struct ProcessGroupTests {
    let spawner = ProcessSpawner()

    func shell(_ script: String, _ arguments: [String] = []) -> ChildCommand {
        ChildCommand(executable: URL(fileURLWithPath: "/bin/sh"), arguments: ["-c", script, "sh"] + arguments)
    }

    @Test("every child leads a process group of its own, and what it starts is in it")
    func ownGroup() async throws {
        let folder = try PidFolder()
        defer { folder.remove() }
        let child = try spawner.spawn(shell(#"/bin/sleep 30 & echo $! > "$1"; wait"#, [folder.file("grandchild").path]))
        let grandchild = try #require(await folder.pid("grandchild"))
        defer { kill(-child.processIdentifier, SIGKILL) }
        #expect(getpgid(child.processIdentifier) == child.processIdentifier)
        #expect(getpgid(child.processIdentifier) != getpgrp(), "never the app's own group")
        #expect(getpgid(grandchild) == child.processIdentifier)
    }

    @Test("terminate and kill reach the whole group: what the child started ends with it", arguments: ["terminate", "kill"])
    func signalsReachTheGroup(_ signal: String) async throws {
        let folder = try PidFolder()
        defer { folder.remove() }
        let child = try spawner.spawn(shell(#"/bin/sleep 30 & echo $! > "$1"; wait"#, [folder.file("grandchild").path]))
        let grandchild = try #require(await folder.pid("grandchild"))
        defer { kill(grandchild, SIGKILL) }
        if signal == "terminate" { child.terminate() } else { child.kill() }
        #expect(await gone(grandchild), "the grandchild outlived its child's \(signal)")
        #expect(await ending(of: child, within: .seconds(5)) != nil)
    }

    @Test("interrupt is Ctrl-C's at a terminal: the whole group gets it, so a foreground grandchild ends too")
    func interruptReachesTheGroup() async throws {
        let folder = try PidFolder()
        defer { folder.remove() }
        // The grandchild is in the foreground of the child's script, where
        // SIGINT is not ignored: it writes its pid, then becomes the sleep.
        let child = try spawner.spawn(
            shell(#"/bin/sh -c 'echo $$ > "$1"; exec /bin/sleep 30' sh "$1"; echo after"#, [folder.file("grandchild").path]))
        let grandchild = try #require(await folder.pid("grandchild"))
        defer { kill(grandchild, SIGKILL) }
        child.interrupt()
        #expect(await gone(grandchild), "the grandchild outlived the interrupt")
        let ended = await ending(of: child, within: .seconds(5))
        #expect(ended != nil && ended?.succeeded == false)
    }

    @Test("stdout held open by a grandchild past the child's exit still finishes, within the failsafe")
    func stdoutFailsafe() async throws {
        let folder = try PidFolder()
        defer { folder.remove() }
        let child = try spawner.spawn(
            shell(#"echo first; /bin/sleep 25 & echo $! > "$1"; echo 'last, unended' | tr -d '\n'; exit 0"#, [folder.file("grandchild").path]))
        let grandchild = try #require(await folder.pid("grandchild"))
        defer { kill(grandchild, SIGKILL) }
        let clock = ContinuousClock()
        let started = clock.now
        let said = await lines(of: child, within: .seconds(8))
        #expect(said == ["first", "last, unended"], "stdout never finished while the grandchild held it")
        #expect(clock.now - started < .seconds(6))
        #expect(await ending(of: child, within: .seconds(2)) == ChildExit(reason: .exited, status: 0))
        #expect(running(grandchild), "the grandchild still held the pipes: the failsafe ended them, not their close")
    }

    @Test("a child is released once its process has exited, though a grandchild still holds its pipes")
    func releasedWhileAGrandchildHoldsItsPipes() async throws {
        let folder = try PidFolder()
        defer { folder.remove() }
        var child: (any RunningChild)? = try spawner.spawn(
            shell(#"echo hello; /bin/sleep 25 & echo $! > "$1"; echo oops >&2; exit 0"#, [folder.file("grandchild").path]))
        weak let released = child
        let grandchild = try #require(await folder.pid("grandchild"))
        defer { kill(grandchild, SIGKILL) }
        #expect(await ending(of: child!, within: .seconds(5)) == ChildExit(reason: .exited, status: 0))
        #expect(child?.stderrTail.lastLine == "oops")
        child = nil
        for _ in 0..<40 where released != nil {
            try? await Task.sleep(for: .milliseconds(50))
        }
        #expect(released == nil, "the child's pipes' handlers held it past its process")
        #expect(running(grandchild), "the grandchild still held the pipes")
    }
}
