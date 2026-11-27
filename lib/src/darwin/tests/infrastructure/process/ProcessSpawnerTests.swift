import Foundation
import InfrastructureProcess
import Testing

/// Real children -- `/bin/sh`, `/bin/cat` -- through the real spawner.
@Suite("ProcessSpawner", .timeLimit(.minutes(1)))
struct ProcessSpawnerTests {
    let spawner = ProcessSpawner()

    func shell(_ script: String, environment: [String: String] = [:]) -> ChildCommand {
        ChildCommand(executable: URL(fileURLWithPath: "/bin/sh"), arguments: ["-c", script], environment: environment)
    }

    func lines(of child: any RunningChild) async -> [String] {
        var lines: [String] = []
        for await line in child.lines { lines.append(line) }
        return lines
    }

    @Test("stdout arrives as lines, stderr as a tail apart from it, and the exit as it happened")
    func linesTailAndExit() async throws {
        let child = try spawner.spawn(shell("printf 'a\\nb\\n'; printf 'oops\\n' >&2; printf 'c'; exit 3"))
        #expect(await lines(of: child) == ["a", "b", "c"])
        #expect(await child.waitForExit() == ChildExit(reason: .exited, status: 3))
        #expect(child.stderrTail.lastLine == "oops")
    }

    @Test("a line written in two pieces arrives once, whole")
    func splitWrites() async throws {
        let child = try spawner.spawn(shell("printf 'ab'; sleep 0.2; printf 'c\\n'"))
        #expect(await lines(of: child) == ["abc"])
    }

    @Test("what is written reaches the child, and closing its input ends it")
    func inputAndClose() async throws {
        let child = try spawner.spawn(ChildCommand(executable: URL(fileURLWithPath: "/bin/cat")))
        try child.write(Data("hello\n".utf8))
        child.closeInput()
        #expect(await lines(of: child) == ["hello"])
        #expect(await child.waitForExit().succeeded)
        #expect(throws: ChildError.inputClosed) { try child.write(Data("again\n".utf8)) }
    }

    @Test("a write to a child that has exited is an error, never a signal that ends the app")
    func writeToADeadChild() async throws {
        let child = try spawner.spawn(shell("exit 0"))
        _ = await child.waitForExit()
        #expect(throws: ChildError.inputClosed) {
            for _ in 0..<64 { try child.write(Data(String(repeating: "x", count: 4096).utf8)) }
        }
    }

    @Test("interrupt is Ctrl-C's signal: a child that does not catch it ends by it")
    func interrupt() async throws {
        let child = try spawner.spawn(ChildCommand(executable: URL(fileURLWithPath: "/bin/sleep"), arguments: ["30"]))
        child.interrupt()
        #expect(await child.waitForExit() == ChildExit(reason: .signalled, status: SIGINT))
    }

    @Test("terminate ends a child that would run on, and says it was a signal")
    func terminate() async throws {
        let child = try spawner.spawn(ChildCommand(executable: URL(fileURLWithPath: "/bin/sleep"), arguments: ["30"]))
        child.terminate()
        #expect(await child.waitForExit() == ChildExit(reason: .signalled, status: SIGTERM))
    }

    @Test("the command's variables are laid over the inherited environment, and its folder is its working directory")
    func environmentAndFolder() async throws {
        let folder = FileManager.default.temporaryDirectory.resolvingSymlinksInPath()
        var command = shell("echo \"$APOGEE_HOME\"; pwd -P", environment: ["APOGEE_HOME": "/tmp/sandbox"])
        command.workingDirectory = folder
        let child = try spawner.spawn(command)
        let output = await lines(of: child)
        #expect(output.first == "/tmp/sandbox")
        #expect(output.last.map { URL(fileURLWithPath: $0).standardizedFileURL } == folder.standardizedFileURL)
    }

    @Test("a program that does not exist is refused at spawn, naming it")
    func couldNotStart() {
        #expect(throws: ChildError.self) {
            try spawner.spawn(ChildCommand(executable: URL(fileURLWithPath: "/nonexistent/apogee")))
        }
    }

    @Test("a loud stderr is kept to its last bytes")
    func boundedStderr() async throws {
        let child = try ProcessSpawner(stderrLimit: 1024).spawn(
            shell("i=0; while [ $i -lt 400 ]; do echo \"noise $i\" >&2; i=$((i+1)); done; echo 'last words' >&2"))
        _ = await lines(of: child)
        _ = await child.waitForExit()
        #expect(child.stderrTail.text.utf8.count <= 1024)
        #expect(child.stderrTail.lastLine == "last words")
    }
}

/// GUI M22: a child that asks for it has its stderr framed into lines as
/// well as kept as the tail; one that does not is as it always was.
@Suite("ProcessSpawner's stderr lines", .timeLimit(.minutes(1)))
struct ProcessSpawnerErrorLinesTests {
    func shell(_ script: String, framed: Bool, _ arguments: [String] = []) -> ChildCommand {
        ChildCommand(executable: URL(fileURLWithPath: "/bin/sh"), arguments: ["-c", script, "sh"] + arguments, framesErrors: framed)
    }

    func all(_ stream: AsyncStream<String>) async -> [String] {
        var lines: [String] = []
        for await line in stream { lines.append(line) }
        return lines
    }

    @Test("a child that does not ask has no stderr lines, only its tail")
    func notAsked() async throws {
        let child = try ProcessSpawner().spawn(shell("printf 'apogee x: said\\n' >&2", framed: false))
        #expect(child.errorLines == nil)
        _ = await child.waitForExit()
        #expect(child.stderrTail.lastLine == "apogee x: said")
    }

    @Test("asked, stderr comes as whole lines across writes, a CRLF trimmed, the last unended line said, the stream finished at the exit, the tail unchanged")
    func asked() async throws {
        let script = "printf 'apogee x: one\\r\\n' >&2; printf 'two' >&2; sleep 0.2; printf ' halves\\nlast' >&2; exit 0"
        let child = try ProcessSpawner().spawn(shell(script, framed: true))
        let stream = try #require(child.errorLines)
        #expect(await all(stream) == ["apogee x: one", "two halves", "last"])
        #expect(await child.waitForExit().succeeded)
        #expect(child.stderrTail.text == "apogee x: one\r\ntwo halves\nlast")
        #expect(!child.stderrTail.cut)
    }

    @Test("by the time the exit is answered, the lines are all said and their stream is finished")
    func linesBeforeExit() async throws {
        let child = try ProcessSpawner().spawn(shell("printf 'apogee x: one\\napogee x: two' >&2; exit 0", framed: true))
        let stream = try #require(child.errorLines)
        #expect(await child.waitForExit().succeeded)
        // Finished already: collecting it ends at once, never waiting on more.
        let drained = await withTaskGroup(of: [String]?.self) { group in
            group.addTask { await self.all(stream) }
            group.addTask {
                try? await Task.sleep(for: .milliseconds(500))
                return nil
            }
            let first = await group.next() ?? nil
            group.cancelAll()
            return first
        }
        #expect(drained == ["apogee x: one", "apogee x: two"])
    }

    @Test("each line is capped, so a runaway line is cut, never kept whole")
    func capped() async throws {
        let child = try ProcessSpawner(errorLineLimit: 16).spawn(
            shell("printf 'apogee x: %0100d\\nnext\\n' 0 >&2", framed: true))
        let stream = try #require(child.errorLines)
        #expect(await all(stream) == ["apogee x: 000000", "next"])
    }

    @Test("stderr held open by a grandchild past the child's exit still finishes its lines, within the failsafe")
    func failsafe() async throws {
        let folder = try PidFolder()
        defer { folder.remove() }
        let child = try ProcessSpawner().spawn(
            shell(#"echo 'apogee x: before' >&2; /bin/sleep 25 & echo $! > "$1"; printf 'apogee x: unended' >&2; exit 0"#, framed: true,
                [folder.file("grandchild").path]))
        let grandchild = try #require(await folder.pid("grandchild"))
        defer { kill(grandchild, SIGKILL) }
        let stream = try #require(child.errorLines)
        let clock = ContinuousClock()
        let started = clock.now
        #expect(await all(stream) == ["apogee x: before", "apogee x: unended"])
        #expect(clock.now - started < .seconds(6))
        #expect(await ending(of: child, within: .seconds(2)) == ChildExit(reason: .exited, status: 0))
    }
}

@Suite("CapturedRun", .timeLimit(.minutes(1)))
struct CapturedRunTests {
    let spawner = ProcessSpawner()

    @Test("a one-shot run comes back whole: stdout, exit and stderr")
    func whole() async throws {
        let run = try await spawner.capture(
            ChildCommand(
                executable: URL(fileURLWithPath: "/bin/sh"),
                arguments: ["-c", "echo '{\"a\":'; echo '1}'; echo warn >&2"]),
            timeout: .seconds(10))
        #expect(run.stdout == "{\"a\":\n1}")
        #expect(run.exit.succeeded)
        #expect(run.stderrTail.lastLine == "warn")
        #expect(!run.timedOut)
        #expect(!run.stdoutTruncated)
    }

    @Test("a run past its time is stopped and says so")
    func timeout() async throws {
        let run = try await spawner.capture(
            ChildCommand(executable: URL(fileURLWithPath: "/bin/sleep"), arguments: ["30"]),
            timeout: .milliseconds(300))
        #expect(run.timedOut)
        #expect(!run.exit.succeeded)
    }

    @Test("stdout past the limit is cut, the child stopped, and the cut said")
    func limit() async throws {
        let run = try await spawner.capture(
            ChildCommand(executable: URL(fileURLWithPath: "/usr/bin/yes")), timeout: .seconds(10),
            stdoutLimit: 1000)
        #expect(run.stdoutTruncated)
        #expect(run.stdout.utf8.count <= 1000)
    }
}
