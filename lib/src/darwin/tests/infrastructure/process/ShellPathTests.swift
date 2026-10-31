import Darwin
import Foundation
import InfrastructureProcess
import Testing

/// A login shell scripted for one test, in a folder of its own that is also
/// its home: `/bin/sh` running `body`, with what it is asked to run in "$2"
/// (after `-ilc` in "$1"). The real login shell is never started.
struct ScriptedShell {
    let home: URL
    let url: URL

    init(_ body: String) throws {
        home = FileManager.default.temporaryDirectory
            .appendingPathComponent("shell-path-\(UUID().uuidString)").resolvingSymlinksInPath()
        try FileManager.default.createDirectory(at: home, withIntermediateDirectories: true)
        url = home.appendingPathComponent("shell")
        try ("#!/bin/sh\n" + body + "\n").write(to: url, atomically: true, encoding: .utf8)
        try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: url.path)
    }

    /// The app's environment as the Dock gives it, with this shell as `SHELL`.
    var environment: [String: String] {
        ["SHELL": url.path, "HOME": home.path, "PATH": ShellPath.launchdPath]
    }

    /// The fallback this home gives: the well-known directories ahead of launchd's.
    var fallback: String {
        "\(home.path)/.local/bin:/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"
    }

    func remove() {
        try? FileManager.default.removeItem(at: home)
    }
}

/// The `PATH` a child spawned through the real spawner over `path` sees.
func childPath(_ path: ShellPath, environment: [String: String] = [:]) async throws -> String {
    let child = try ProcessSpawner(path: path).spawn(
        ChildCommand(
            executable: URL(fileURLWithPath: "/bin/sh"), arguments: ["-c", "printf '%s\\n' \"$PATH\""],
            environment: environment))
    var lines: [String] = []
    for await line in child.lines { lines.append(line) }
    _ = await child.waitForExit()
    return lines.joined(separator: "\n")
}

/// Whether process `pid` is gone, waiting up to two seconds for it.
func gone(_ pid: Int32) async -> Bool {
    for _ in 0..<40 {
        if kill(pid, 0) == -1 && errno == ESRCH { return true }
        try? await Task.sleep(for: .milliseconds(50))
    }
    return false
}

@Suite("ShellPath", .timeLimit(.minutes(1)))
struct ShellPathTests {
    /// The user's case (2026-10-22): `claude` and `codex` in ~/.local/bin,
    /// `gemini` in /opt/homebrew/bin, `ollama` in /usr/local/bin -- and a
    /// profile that greets, and says goodbye.
    static let usersCase = """
        [ "$1" = "-ilc" ] || { echo "asked as $1" >&2; exit 64; }
        echo "Last login: Thu Oct 22 09:12:01 on ttys001"
        echo "/not/the/path:/printed/by/a/profile"
        printf 'apogee-path-but-not-the-marker'
        PATH="$HOME/.local/bin:/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"
        export PATH
        eval "$2"
        echo "goodbye from .zlogout"
        """

    @Test("a profile that prints before and after the markers still yields exactly the PATH")
    func printingProfile() async throws {
        let shell = try ScriptedShell(Self.usersCase)
        defer { shell.remove() }
        let resolver = ShellPath(environment: shell.environment)
        resolver.begin()
        let answer = await resolver.answer()
        #expect(answer.source == .shell)
        #expect(answer.reason == nil)
        #expect(answer.shell == shell.url.path)
        #expect(answer.path == "\(shell.home.path)/.local/bin:/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin")
        #expect(resolver.current == answer)
    }

    /// 4c, the fourth defect: a profile that prints its environment prints
    /// the marker's variable, value and all, before the real output.
    @Test("a profile that prints its environment -- the marker's variable among it -- still yields exactly the PATH", arguments: ["export -p", "env", "set"])
    func profilePrintsItsEnvironment(_ dump: String) async throws {
        let shell = try ScriptedShell(
            """
            [ "$1" = "-ilc" ] || { echo "asked as $1" >&2; exit 64; }
            echo "Last login: Thu Oct 22 09:12:01 on ttys001"
            \(dump)
            PATH="$HOME/.local/bin:/opt/homebrew/bin:/usr/bin:/bin"
            export PATH
            eval "$2"
            \(dump)
            """)
        defer { shell.remove() }
        let resolver = ShellPath(environment: shell.environment)
        resolver.begin()
        let answer = await resolver.answer()
        #expect(answer.source == .shell)
        #expect(answer.path == "\(shell.home.path)/.local/bin:/opt/homebrew/bin:/usr/bin:/bin")
    }

    @Test("a child spawned through the real spawner carries the shell's PATH under its own variables")
    func childCarriesTheAnswer() async throws {
        let shell = try ScriptedShell(Self.usersCase)
        defer { shell.remove() }
        let resolver = ShellPath(environment: shell.environment)
        resolver.begin()
        let answer = await resolver.answer()
        #expect(try await childPath(resolver) == answer.path)
        #expect(try await childPath(resolver).hasPrefix("\(shell.home.path)/.local/bin:"))
        let child = try ProcessSpawner(path: resolver).spawn(
            ChildCommand(
                executable: URL(fileURLWithPath: "/bin/sh"), arguments: ["-c", "printf '%s|%s\\n' \"$APOGEE_HOME\" \"$PATH\""],
                environment: ["APOGEE_HOME": "/tmp/sandbox"]))
        var lines: [String] = []
        for await line in child.lines { lines.append(line) }
        #expect(lines == ["/tmp/sandbox|\(answer.path)"])
    }

    @Test("a command that sets its own PATH keeps it")
    func commandsOwnPathWins() async throws {
        let shell = try ScriptedShell(Self.usersCase)
        defer { shell.remove() }
        let resolver = ShellPath(environment: shell.environment)
        resolver.begin()
        #expect(await resolver.answer().source == .shell)
        #expect(try await childPath(resolver, environment: ["PATH": "/only/mine:/bin"]) == "/only/mine:/bin")
    }

    @Test("a shell that never answers is cut off at the deadline, its whole group killed, and children get the fallback")
    func neverAnswers() async throws {
        let shell = try ScriptedShell(
            """
            echo $$ > "$HOME/shell.pid"
            sleep 30 &
            echo $! > "$HOME/background.pid"
            sleep 30
            """)
        defer { shell.remove() }
        // Short, but long enough for a loaded machine to start the script.
        let resolver = ShellPath(environment: shell.environment, deadline: .seconds(2))
        let clock = ContinuousClock()
        let started = clock.now
        resolver.begin()
        let answer = await resolver.answer()
        #expect(clock.now - started < .seconds(5), "cut off at its deadline, not the shell's thirty seconds")
        #expect(answer == ShellPath.Resolution(
            path: shell.fallback, source: .fallback, shell: shell.url.path, reason: "didn't answer within 2 seconds"))
        let shellPID = try #require(Int32(String(contentsOf: shell.home.appendingPathComponent("shell.pid"), encoding: .utf8).trimmingCharacters(in: .whitespacesAndNewlines)))
        let backgroundPID = try #require(Int32(String(contentsOf: shell.home.appendingPathComponent("background.pid"), encoding: .utf8).trimmingCharacters(in: .whitespacesAndNewlines)))
        #expect(await gone(shellPID))
        #expect(await gone(backgroundPID))
        #expect(try await childPath(resolver) == shell.fallback)
    }

    @Test("a shell that exits non-zero gives the fallback, and says so")
    func exitsNonZero() async throws {
        let shell = try ScriptedShell(
            """
            echo "zsh: parse error near ')' in .zshrc" >&2
            echo "half a greeting"
            exit 1
            """)
        defer { shell.remove() }
        let resolver = ShellPath(environment: shell.environment)
        resolver.begin()
        #expect(await resolver.answer() == ShellPath.Resolution(
            path: shell.fallback, source: .fallback, shell: shell.url.path, reason: "exited with status 1"))
        #expect(try await childPath(resolver) == shell.fallback)
    }

    @Test("no shell at all gives the fallback at once")
    func noShell() async throws {
        let shell = try ScriptedShell("exit 0")
        defer { shell.remove() }
        var environment = shell.environment
        environment["SHELL"] = shell.home.appendingPathComponent("no-such-shell").path
        let resolver = ShellPath(environment: environment)
        let clock = ContinuousClock()
        let started = clock.now
        resolver.begin()
        let answer = await resolver.answer()
        #expect(clock.now - started < .seconds(2))
        #expect(answer.source == .fallback)
        #expect(answer.reason == "isn't there")
        #expect(answer.path == shell.fallback)
    }

    @Test("a shell that exits cleanly without printing its PATH gives the fallback")
    func noAnswer() async throws {
        let shell = try ScriptedShell("echo 'a profile that ends the shell early'; exit 0")
        defer { shell.remove() }
        let resolver = ShellPath(environment: shell.environment)
        resolver.begin()
        let answer = await resolver.answer()
        #expect(answer.source == .fallback)
        #expect(answer.reason == "didn't print its PATH")
    }

    @Test("its input is closed: a profile that reads it gets the end at once")
    func inputClosed() async throws {
        let shell = try ScriptedShell(
            """
            cat > "$HOME/read.txt"
            PATH=/custom/bin:/usr/bin:/bin
            export PATH
            eval "$2"
            """)
        defer { shell.remove() }
        let resolver = ShellPath(environment: shell.environment)
        resolver.begin()
        // Answered at all means the read ended at once: an open input would
        // have held it to the deadline, and the fallback.
        #expect(await resolver.answer() == ShellPath.Resolution(path: "/custom/bin:/usr/bin:/bin", source: .shell, shell: shell.url.path))
        #expect(try String(contentsOf: shell.home.appendingPathComponent("read.txt"), encoding: .utf8).isEmpty)
    }

    @Test("a child spawned before the shell answers gets the fallback, never waiting; one after, the answer")
    func childBeforeTheAnswer() async throws {
        let shell = try ScriptedShell(
            """
            while [ ! -f "$HOME/go" ]; do sleep 0.05; done
            PATH="$HOME/.local/bin:/usr/bin:/bin"
            export PATH
            eval "$2"
            """)
        defer { shell.remove() }
        let resolver = ShellPath(environment: shell.environment)
        resolver.begin()
        #expect(resolver.current == resolver.fallback)
        #expect(try await childPath(resolver) == shell.fallback)
        FileManager.default.createFile(atPath: shell.home.appendingPathComponent("go").path, contents: Data())
        let answer = await resolver.answer()
        #expect(answer.source == .shell)
        #expect(try await childPath(resolver) == "\(shell.home.path)/.local/bin:/usr/bin:/bin")
    }

    @Test("the shell is asked once, however often it is begun")
    func askedOnce() async throws {
        let shell = try ScriptedShell(
            """
            echo asked >> "$HOME/asks.log"
            eval "$2"
            """)
        defer { shell.remove() }
        let resolver = ShellPath(environment: shell.environment)
        resolver.begin()
        resolver.begin()
        _ = await resolver.answer()
        resolver.begin()
        _ = await resolver.answer()
        let asks = try String(contentsOf: shell.home.appendingPathComponent("asks.log"), encoding: .utf8)
        #expect(asks == "asked\n")
    }

    @Test("never begun, it never asks: children and the answer are the fallback")
    func neverBegun() async throws {
        let shell = try ScriptedShell(
            """
            echo asked >> "$HOME/asks.log"
            eval "$2"
            """)
        defer { shell.remove() }
        let resolver = ShellPath(environment: shell.environment)
        #expect(await resolver.answer() == resolver.fallback)
        #expect(resolver.current == resolver.fallback)
        #expect(resolver.fallback.reason == "hasn't answered yet")
        #expect(!FileManager.default.fileExists(atPath: shell.home.appendingPathComponent("asks.log").path))
    }

    @Test("the fallback is launchd's PATH behind the well-known directories, ~ expanded, each once, in order")
    func fallbackShape() {
        let resolver = ShellPath(environment: [
            "HOME": "/Users/someone", "PATH": "/usr/bin:/bin:/usr/local/bin:/usr/sbin:/sbin:/usr/bin",
        ])
        #expect(resolver.fallback.path == "/Users/someone/.local/bin:/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin")
        #expect(resolver.fallback.directories.first == "/Users/someone/.local/bin")
        #expect(resolver.fallback.source == .fallback)
    }

    @Test("with no SHELL it asks /bin/zsh, and with no PATH the fallback starts from launchd's")
    func defaults() {
        let resolver = ShellPath(environment: ["HOME": "/Users/someone"])
        #expect(resolver.shell == "/bin/zsh")
        #expect(resolver.deadline == .seconds(5))
        #expect(resolver.fallback.path == "/Users/someone/.local/bin:/opt/homebrew/bin:/usr/local/bin:\(ShellPath.launchdPath)")
    }
}
