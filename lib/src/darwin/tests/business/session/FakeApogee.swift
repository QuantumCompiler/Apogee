import Foundation
import InfrastructureProcess

/// A scripted fake `apogee`: a small program that speaks the machine-mode
/// protocol on its pipes, scenario by scenario, and logs every line it reads
/// -- the house's fake-driver idiom. No model, no network, no real binary.
final class FakeApogee {
    let directory: URL
    let executable: URL
    let log: URL

    enum Scenario: String {
        /// A whole turn: thinking, a question answered, a tool line, a
        /// streamed answer echoing the answer, a result with usage. Again for
        /// every `user` line; exits 0 at end of input.
        case conversation
        /// An answer streamed slowly until a `cancel` ends it; the next turn echoes.
        case slow
        /// Says something on stderr and exits 2 at the first turn.
        case dies
        /// The vocabulary grown: a field it does not know, a type it does not
        /// know, a line that is not JSON.
        case unknown
        /// Announces protocol 2.
        case protocol2
        /// Ignores end of input and SIGTERM: only SIGKILL ends it.
        case stubborn
    }

    init() throws {
        directory = FileManager.default.temporaryDirectory.appendingPathComponent("apogee-fake-\(UUID().uuidString)")
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        executable = directory.appendingPathComponent("apogee")
        log = directory.appendingPathComponent("stdin.log")
        try Self.script.write(to: executable, atomically: true, encoding: .utf8)
        try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: executable.path)
    }

    deinit { try? FileManager.default.removeItem(at: directory) }

    func command(_ scenario: Scenario) -> ChildCommand {
        ChildCommand(
            executable: executable, arguments: ["chat", "--output-format", "stream-json"],
            environment: ["FAKE_SCENARIO": scenario.rawValue, "FAKE_LOG": log.path], workingDirectory: directory)
    }

    /// Every line the fake read, in order.
    var received: [String] {
        ((try? String(contentsOf: log, encoding: .utf8)) ?? "").split(separator: "\n").map(String.init)
    }

    static let script = #"""
        #!/usr/bin/env python3
        import json, os, select, signal, sys, time

        scenario = os.environ.get("FAKE_SCENARIO", "conversation")
        log = os.environ.get("FAKE_LOG")
        buffer = b""

        def emit(event):
            sys.stdout.write(json.dumps(event) + "\n")
            sys.stdout.flush()

        def next_line(timeout=None):
            """The next line read, as JSON; "" when none came in time; None at end of input."""
            global buffer
            while b"\n" not in buffer:
                if timeout is not None and not select.select([0], [], [], timeout)[0]:
                    return ""
                chunk = os.read(0, 4096)
                if not chunk:
                    return None
                buffer += chunk
            line, buffer = buffer.split(b"\n", 1)
            text = line.decode()
            if log:
                with open(log, "a") as f:
                    f.write(text + "\n")
            return json.loads(text)

        session = {"type": "session", "protocol_version": 2 if scenario == "protocol2" else 1, "model": "fake",
                   "next_turn": 1, "capabilities": {"events": ["session"], "accepts": ["user", "answer", "attach", "hello", "cancel"],
                                                    "tools": True, "ask": True, "schema": "2026-10-13"}}
        if scenario == "unknown":
            session["future_field"] = {"nested": True}
        if os.environ.get("FAKE_CWD"):
            with open(os.environ["FAKE_CWD"], "w") as f:
                f.write(os.path.realpath(os.getcwd()))
        if scenario == "stubborn":
            signal.signal(signal.SIGTERM, signal.SIG_IGN)
        emit(session)
        sys.stderr.write("[fake] started\n")
        sys.stderr.flush()

        def stream(turn, text):
            emit({"type": "answer_start", "turn": turn})
            for i in range(0, len(text), 4):
                emit({"type": "answer_delta", "text": text[i:i + 4], "turn": turn})
            emit({"type": "answer_end", "turn": turn})
            emit({"type": "result", "text": text, "model": "fake", "finish_reason": "stop", "turn": turn,
                  "usage": {"input_tokens": 12, "output_tokens": 3}})

        turn = 0
        while True:
            line = next_line()
            if line is None:
                if scenario == "stubborn":
                    while True:
                        time.sleep(1)
                break
            if line.get("type") != "user":
                continue
            turn += 1
            if scenario == "dies":
                sys.stderr.write("loading the model\nfatal: the model went away\n")
                sys.stderr.flush()
                sys.exit(2)
            if scenario == "conversation":
                emit({"type": "thinking", "turn": turn})
                emit({"type": "thinking_delta", "text": "Which name?", "turn": turn})
                emit({"type": "question", "turn": turn, "questions": [{"header": "Name", "question": "Call it what?",
                      "multi_select": False, "options": [{"label": "todo.md", "description": "A list"}]}]})
                answer = next_line()
                if answer is None:
                    emit({"type": "error", "message": "input closed while a question waited", "turn": turn})
                    break
                emit({"type": "tool_status", "text": "[tool] write_file", "turn": turn})
                stream(turn, "You chose " + answer["text"])
            elif scenario == "slow":
                if turn > 1:
                    stream(turn, "echo: " + line["text"])
                    continue
                emit({"type": "answer_start", "turn": turn})
                said = ""
                for i in range(200):
                    chunk = "chunk%d " % i
                    said += chunk
                    emit({"type": "answer_delta", "text": chunk, "turn": turn})
                    stop = next_line(timeout=0.05)
                    if stop is None or (stop and stop.get("type") == "cancel"):
                        break
                emit({"type": "result", "text": said, "model": "fake", "finish_reason": "cancelled", "turn": turn})
            elif scenario == "unknown":
                emit({"type": "hologram", "turn": turn, "shape": "cube"})
                print("this is not json", flush=True)
                emit({"type": "answer_start", "turn": turn, "glow": True})
                emit({"type": "answer_delta", "text": "still here", "turn": turn})
                emit({"type": "answer_end", "turn": turn})
                emit({"type": "result", "text": "still here", "turn": turn})
        sys.exit(0)
        """#
}
