import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

/// A fake `apogee` read: a shell script that prints what each case needs.
final class FakeRead {
    let directory: URL

    init() throws {
        directory = FileManager.default.temporaryDirectory.appendingPathComponent("apogee-read-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
    }

    deinit { try? FileManager.default.removeItem(at: directory) }

    func binary(_ script: String) throws -> URL {
        let file = directory.appendingPathComponent("apogee")
        try ("#!/bin/sh\n" + script + "\n").write(to: file, atomically: true, encoding: .utf8)
        try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: file.path)
        return file
    }
}

@Suite("ReadRunner", .timeLimit(.minutes(1)))
struct ReadRunnerTests {
    @Test("a read's document comes back decoded, asked for with --output-format json")
    func document() async throws {
        let fake = try FakeRead()
        let binary = try fake.binary(#"printf '{"object":"list","data":[{"id":"a"}],"total":1,"argv":"%s"}\n' "$*""#)
        let outcome = await ReadRunner().run(binary: binary, ["chats", "list"])
        guard case .document(let document) = outcome else {
            Issue.record("no document: \(outcome)")
            return
        }
        #expect(document.object == "list")
        #expect(document.entries?.first?["id"]?.stringValue == "a")
        #expect(document.total == 1)
        #expect(document.value["argv"]?.stringValue == "chats list --output-format json")
    }

    @Test("a read that fails says why in the CLI's own words")
    func failed() async throws {
        let fake = try FakeRead()
        let binary = try fake.binary("echo 'error: no chat named x' >&2; exit 3")
        let outcome = await ReadRunner().run(binary: binary, ["chats", "show", "x"])
        guard case .failed(let exit, let tail) = outcome else {
            Issue.record("did not fail: \(outcome)")
            return
        }
        #expect(exit == ChildExit(reason: .exited, status: 3))
        #expect(tail.lastLine == "error: no chat named x")
    }

    @Test("stdout that is not one JSON document is said, not decoded")
    func undecodable() async throws {
        let fake = try FakeRead()
        let outcome = await ReadRunner().run(binary: try fake.binary("echo 'Models:'; echo '  none'"), ["models"])
        guard case .undecodable(let stdout, _) = outcome else {
            Issue.record("decoded: \(outcome)")
            return
        }
        #expect(stdout == "Models:\n  none")
    }

    @Test("a read that succeeds keeps what it said on stderr beside its document, asked; a plain read drops it")
    func stderrBesideTheDocument() async throws {
        let fake = try FakeRead()
        let binary = try fake.binary(#"echo "apogee chats: a message could not be read and was skipped" >&2; echo '  ' >&2; echo '{"object":"chat","turns":[]}'"#)
        let kept = await ReadRunner().runKeepingStderr(binary: binary, ["chats", "show", "c1"])
        #expect(kept.outcome.document?.object == "chat")
        #expect(kept.stderrLines == ["apogee chats: a message could not be read and was skipped"], "blank lines are none")
        let read = await ReadRunner().readKeepingStderr(binary: binary, ["chats", "show", "c1"])
        #expect(read == kept, "the runner's read is the same")
        #expect(await ReadRunner().run(binary: binary, ["chats", "show", "c1"]) == kept.outcome)
        let failed = await ReadRunner().runKeepingStderr(binary: try fake.binary("echo 'no chat named c9' >&2; exit 1"), ["chats", "show", "c9"])
        #expect(failed.outcome.failure == "no chat named c9 (exited with status 1)")
        #expect(failed.stderrLines == ["no chat named c9"])
    }

    @Test("a read past its time is stopped, and one that cannot start says so")
    func timeoutAndCouldNotStart() async throws {
        let fake = try FakeRead()
        let slow = await ReadRunner(timeout: .milliseconds(300)).run(binary: try fake.binary("sleep 30"), ["check"])
        guard case .timedOut = slow else {
            Issue.record("not timed out: \(slow)")
            return
        }
        let missing = await ReadRunner().run(binary: URL(fileURLWithPath: "/nonexistent/apogee"), ["check"])
        guard case .couldNotStart(let reason) = missing else {
            Issue.record("started: \(missing)")
            return
        }
        #expect(reason.contains("/nonexistent/apogee"))
    }
}

@Suite("CommandRunning", .timeLimit(.minutes(1)))
struct CommandRunningTests {
    @Test("a command runs exactly as typed -- no --output-format added -- and says what it printed")
    func done() async throws {
        let fake = try FakeRead()
        let binary = try fake.binary(#"echo "renamed: $*""#)
        let outcome = await ReadRunner().command(binary: binary, ["chats", "title", "c1", "Plans for the week"])
        #expect(outcome == .done(stdout: "renamed: chats title c1 Plans for the week"))
        #expect(outcome.succeeded && outcome.failure == nil)
    }

    @Test("input goes to the command's standard input and nowhere else")
    func input() async throws {
        let fake = try FakeRead()
        let binary = try fake.binary(#"read line; echo "got ${#line} characters for $*""#)
        let outcome = await ReadRunner().command(binary: binary, ["auth", "add", "anthropic", "--stdin"], input: Data("sk-secret\n".utf8))
        #expect(outcome == .done(stdout: "got 9 characters for auth add anthropic --stdin"))
    }

    @Test("a report's document stands whatever the exit: check fails and says why in it")
    func report() async throws {
        let fake = try FakeRead()
        let binary = try fake.binary(#"echo '{"rows":[{"name":"a","status":"fail"}],"ok":false,"failures":1}'; exit 1"#)
        let outcome = await ReadRunner().report(binary: binary, ["check"])
        #expect(outcome.document?.value["failures"]?.intValue == 1)
        let read = await ReadRunner().read(binary: binary, ["check"])
        #expect(read.document == nil, "a read drops it: the exit decides")
    }

    @Test("a refusal is its stderr's last line, word for word, with how it exited")
    func refused() async throws {
        let fake = try FakeRead()
        let binary = try fake.binary("echo \"apogee chats: no chat named 'nope'\" >&2; exit 1")
        let outcome = await ReadRunner().command(binary: binary, ["chats", "delete", "nope"])
        #expect(!outcome.succeeded)
        #expect(outcome.failure == "apogee chats: no chat named 'nope' (exited with status 1)")
        let read = await ReadRunner().read(binary: binary, ["chats", "list"])
        #expect(read.document == nil)
        #expect(read.failure == "apogee chats: no chat named 'nope' (exited with status 1)")
    }
}

/// A person's text is never an option (GUI 4g): a read whose words carry it
/// after `--` gets its format flag before the `--`, where the binary still
/// reads it as the read's -- after it, the flag would be taken as text too.
@Suite("A read's format flag before --", .timeLimit(.minutes(1)))
struct ReadFormatBeforeTextTests {
    /// A fake read that answers with the words it was given, one per `|`.
    static let echoing = #"words=$(printf '%s|' "$@"); printf '{"object":"echo","argv":"%s"}\n' "$words""#

    @Test("a read, a read keeping its stderr and a report each put --output-format json before the --, the text after it untouched")
    func formatBeforeTheText() async throws {
        let fake = try FakeRead()
        let binary = try fake.binary(Self.echoing)
        let words = ["graph", "query", "--graph", "demo", "--", "--output-format text"]
        let read = await ReadRunner().read(binary: binary, words)
        #expect(read.document?.value["argv"]?.stringValue == "graph|query|--graph|demo|--output-format|json|--|--output-format text|")
        let kept = await ReadRunner().readKeepingStderr(binary: binary, words)
        #expect(kept.outcome.document?.value["argv"]?.stringValue == "graph|query|--graph|demo|--output-format|json|--|--output-format text|")
        let report = await ReadRunner().report(binary: binary, ["graph", "explain", "--graph", "demo", "--", "-[Foo bar]"])
        #expect(report.document?.value["argv"]?.stringValue == "graph|explain|--graph|demo|--output-format|json|--|-[Foo bar]|")
    }

    @Test("a read with no --: the flag at the end, as before")
    func formatAtTheEnd() async throws {
        let fake = try FakeRead()
        let read = await ReadRunner().read(binary: try fake.binary(Self.echoing), ["chats", "list"])
        #expect(read.document?.value["argv"]?.stringValue == "chats|list|--output-format|json|")
    }
}
