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
