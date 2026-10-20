import BusinessSession
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

/// Reads a session's events one at a time, as a window would.
struct EventReader {
    private var iterator: AsyncStream<SessionEvent>.Iterator
    private(set) var seen: [SessionEvent] = []

    init(_ session: MachineSession) {
        iterator = session.events.makeAsyncIterator()
    }

    /// Events up to and including the first that `stop` accepts.
    mutating func until(_ stop: (SessionEvent) -> Bool) async -> [SessionEvent] {
        var read: [SessionEvent] = []
        while let event = await iterator.next() {
            read.append(event)
            seen.append(event)
            if stop(event) { break }
        }
        return read
    }

    mutating func untilKind(_ match: (ProtocolEvent.Kind) -> Bool) async -> [SessionEvent] {
        await until { event in
            switch event {
            case .event(let protocolEvent): match(protocolEvent.kind)
            case .ended: true
            }
        }
    }

    mutating func untilEnded() async -> SessionEnd? {
        let read = await until { if case .ended = $0 { true } else { false } }
        if case .ended(let end) = read.last { return end }
        return nil
    }
}

func kinds(_ events: [SessionEvent]) -> [String] {
    events.map { event in
        switch event {
        case .event(let e):
            switch e.kind {
            case .answerDelta: "answer_delta"
            case .unknown(let type): "unknown:\(type)"
            case .unreadable: "unreadable"
            default: e.type
            }
        case .ended(let end): "ended:\(end.reason)"
        }
    }
}

let hello = DriverLine.hello(clientName: "Apogee", clientVersion: "0.2.0", wants: ["tools"])

@Suite("MachineSession", .timeLimit(.minutes(1)))
struct MachineSessionTests {
    @Test("a whole conversation: hello first, the events in order, the question answered, a clean end")
    func conversation() async throws {
        let fake = try FakeApogee()
        let session = MachineSession.start(spawner: ProcessSpawner(), command: fake.command(.conversation), hello: hello)
        var reader = EventReader(session)

        _ = await reader.untilKind { if case .session = $0 { true } else { false } }
        #expect(await session.tracker.phase == .idle)
        #expect(await session.info?.model == "fake")

        let turn = try await session.send(user: "Write me a note.")
        #expect(turn == 1)
        #expect(await session.tracker.phase == .running(turn: 1))

        let asked = await reader.untilKind { if case .question = $0 { true } else { false } }
        #expect(kinds(asked) == ["thinking", "thinking_delta", "question"])
        #expect(await session.tracker.phase == .asking(turn: 1, unanswered: 1))

        try await session.answer("todo.md")
        #expect(await session.tracker.phase == .running(turn: 1))
        let answered = await reader.untilKind { if case .result = $0 { true } else { false } }
        #expect(kinds(answered) == ["tool_status", "answer_start"] + Array(repeating: "answer_delta", count: 5) + ["answer_end", "result"])
        let streamed = answered.compactMap { event -> String? in
            if case .event(let e) = event, case .answerDelta(let text) = e.kind { text } else { nil }
        }.joined()
        guard case .event(let last) = answered.last, case .result(let result) = last.kind else {
            Issue.record("no result")
            return
        }
        #expect(streamed == "You chose todo.md")
        #expect(result.text == streamed)
        #expect(result.usage == Usage(inputTokens: 12, outputTokens: 3))
        #expect(await session.tracker.phase == .idle)

        await session.close()
        let end = await reader.untilEnded()
        #expect(end?.reason == .finished)
        #expect(end?.exit?.succeeded == true)
        #expect(await session.tracker.phase == .over)
        // stderr never reaches the events; it is kept as the tail.
        #expect(end?.stderrTail.lastLine == "[fake] started")

        #expect(fake.received == [
            #"{"type":"hello","client":{"name":"Apogee","version":"0.2.0"},"wants":["tools"]}"#,
            #"{"type":"user","text":"Write me a note."}"#,
            #"{"type":"answer","text":"todo.md"}"#,
        ])
    }

    @Test("cancel reaches a turn in flight through the protocol, and the session goes on")
    func cancelMidStream() async throws {
        let fake = try FakeApogee()
        let session = MachineSession.start(spawner: ProcessSpawner(), command: fake.command(.slow), hello: nil)
        var reader = EventReader(session)
        _ = await reader.untilKind { if case .session = $0 { true } else { false } }
        try await session.send(user: "Tell me a long story.")
        _ = await reader.untilKind { if case .answerDelta = $0 { true } else { false } }
        #expect(await session.tracker.turnInFlight)

        try await session.cancel()
        let rest = await reader.untilKind { if case .result = $0 { true } else { false } }
        guard case .event(let last) = rest.last, case .result(let result) = last.kind else {
            Issue.record("the cancelled turn did not end in a result: \(kinds(rest))")
            return
        }
        #expect(result.wasCancelled)
        #expect(result.text.hasPrefix("chunk0 "))
        #expect(await session.tracker.phase == .idle)

        // The same child answers the next turn: nothing was terminated.
        let next = try await session.send(user: "and now?")
        #expect(next == 2)
        let echoed = await reader.untilKind { if case .result = $0 { true } else { false } }
        guard case .event(let again) = echoed.last, case .result(let second) = again.kind else {
            Issue.record("no second result")
            return
        }
        #expect(second.text == "echo: and now?")
        #expect(fake.received.contains(#"{"type":"cancel"}"#))

        await session.close()
        #expect(await reader.untilEnded()?.reason == .finished)
    }

    @Test("a child that dies mid-turn ends the session failed, its stderr tail riding the failure")
    func dyingChild() async throws {
        let fake = try FakeApogee()
        let session = MachineSession.start(spawner: ProcessSpawner(), command: fake.command(.dies), hello: hello)
        var reader = EventReader(session)
        try await session.send(user: "hello?")
        let end = try #require(await reader.untilEnded())
        #expect(end.reason == .failed)
        #expect(end.exit == ChildExit(reason: .exited, status: 2))
        #expect(end.stderrTail.lastLine == "fatal: the model went away")
        #expect(end.message == "apogee exited with status 2: fatal: the model went away")
        await #expect(throws: SessionError.notRunning) { try await session.send(user: "again") }
    }

    @Test("unknown events, unknown fields and an unreadable line flow through, and the turn completes")
    func tolerance() async throws {
        let fake = try FakeApogee()
        let session = MachineSession.start(spawner: ProcessSpawner(), command: fake.command(.unknown), hello: nil)
        var reader = EventReader(session)
        let opened = await reader.untilKind { if case .session = $0 { true } else { false } }
        guard case .event(let sessionEvent) = opened.last else {
            Issue.record("no session event")
            return
        }
        #expect(sessionEvent.fields["future_field"] == .object(["nested": .bool(true)]))

        try await session.send(user: "anything new?")
        let turn = await reader.untilKind { if case .result = $0 { true } else { false } }
        #expect(kinds(turn) == ["unknown:hologram", "unreadable", "answer_start", "answer_delta", "answer_end", "result"])
        if case .event(let start) = turn[2] {
            #expect(start.fields["glow"] == .bool(true))
        }
        await session.close()
        #expect(await reader.untilEnded()?.reason == .finished)
    }

    @Test("a stream announcing another protocol version is refused, never misread")
    func otherProtocol() async throws {
        let fake = try FakeApogee()
        let session = MachineSession.start(spawner: ProcessSpawner(), command: fake.command(.protocol2), hello: nil)
        var reader = EventReader(session)
        let all = await reader.until { _ in false }
        #expect(kinds(all) == ["ended:unsupportedProtocol(2)"])
        guard case .ended(let end) = all.last else { return }
        #expect(end.message.contains("protocol 2"))
    }

    @Test("stop closes the input first, terminates only after the grace, and kills what will not go")
    func stopEscalates() async throws {
        let fake = try FakeApogee()
        let session = MachineSession.start(spawner: ProcessSpawner(), command: fake.command(.stubborn), hello: nil)
        var reader = EventReader(session)
        _ = await reader.untilKind { if case .session = $0 { true } else { false } }
        let started = ContinuousClock.now
        await session.stop(grace: .milliseconds(300))
        let end = try #require(await reader.untilEnded())
        #expect(end.reason == .stopped)
        #expect(end.exit == ChildExit(reason: .signalled, status: SIGKILL))
        #expect(ContinuousClock.now - started >= .milliseconds(300))
    }

    @Test("a session whose binary will not start ends at once, saying why")
    func couldNotStart() async throws {
        let session = MachineSession.start(
            spawner: ProcessSpawner(), command: ChildCommand(executable: URL(fileURLWithPath: "/nonexistent/apogee")),
            hello: hello)
        var reader = EventReader(session)
        let end = try #require(await reader.untilEnded())
        #expect(end.reason == .couldNotStart)
        #expect(end.exit == nil)
        #expect(end.message.hasPrefix("apogee could not be started: /nonexistent/apogee could not be started"))
        #expect(reader.seen.count == 1)
    }
}
