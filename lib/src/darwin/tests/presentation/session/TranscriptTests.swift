import BusinessSession
import DataProtocol
import InfrastructureProcess
import PresentationSession
import Testing

/// The transcript is a pure function of the session's events and the user's
/// acts: every test here is a scripted sequence and what it folds to.
@Suite("Transcript")
struct TranscriptTests {
    /// What each item is, in a word or two, for comparing whole transcripts.
    func shape(_ transcript: Transcript) -> [String] {
        transcript.items.map { item in
            switch item.content {
            case .user(let text): "user: \(text)"
            case .activity(let a): "activity\(a.live ? " live" : ""): \(a.reasoning)|\(a.lines.joined(separator: ","))"
            case .answer(let a): "answer\(a.finished ? "" : " streaming"): \(a.text)"
            case .notice(let text): "notice: \(text)"
            case .recalled(let recall): "recalled: \(recall.chats.map(\.name).joined(separator: ","))"
            case .sources(let sources): "sources: \(sources.map(\.file).joined(separator: ","))"
            case .question(let card): "question: \(card.answers?.joined(separator: ",") ?? "waiting")"
            case .permission(let card): "permission: \(card.answer ?? "waiting")"
            case .error(let message): "error: \(message)"
            case .stopped: "stopped"
            case .todo(let items): "todo: \(items.map(\.text).joined(separator: ","))"
            case .unknown(let type): "unknown: \(type)"
            case .ended(let ending): "ended: \(ending.message)"
            }
        }
    }

    func lastID(_ transcript: Transcript) -> Int { transcript.items.last!.id }

    @Test("the mock-actor conversation folds to the rows a person should see, each state from its event")
    func mockActor() {
        var transcript = Transcript()
        #expect(transcript.status == .connecting)
        var statuses: [Transcript.Status] = []
        for line in mockActorLines {
            transcript.apply(decoded(line))
            statuses.append(transcript.status)
            let type = JSONValue.parse(line)?["type"]?.stringValue
            if type == "session" {
                #expect(transcript.status == .ready)
                #expect(transcript.model == "actor")
                #expect(transcript.userSent("Write me a note.") == 1)
            }
            if type == "question" {
                #expect(transcript.status == .waiting(turn: 1))
                let card = transcript.items.last!
                if case .permission = card.content {
                    transcript.answered(card.id, with: ["yes"])
                } else {
                    transcript.answered(card.id, with: ["todo.md"])
                }
                #expect(transcript.status == .working(turn: 1))
            }
        }
        #expect(shape(transcript) == [
            "user: Write me a note.",
            "activity: They want a note written. I should ask what to call it.|",
            "question: todo.md",
            "activity: |",
            "permission: yes",
            "activity: |[tool] write_file",
            "answer: I wrote todo.md with one item: buy milk.",
        ])
        #expect(statuses.contains(.thinking(turn: 1)))
        #expect(statuses.contains(.answering(turn: 1)))
        #expect(transcript.status == .ready)
        #expect(transcript.acceptsInput)
    }

    @Test("a resumed conversation says its earlier turns are kept but not shown; a new one says nothing")
    func resumedSaysSo() {
        var resumed = Transcript()
        resumed.apply(.event(EventDecoder.decode(line: #"{"type":"session","protocol_version":1,"model":"m","next_turn":4}"#)))
        #expect(shape(resumed) == [
            "notice: resumed: 3 earlier turns of this conversation aren't shown here -- apogee keeps them, and the model still has them"
        ])
        #expect(resumed.status == .ready)
        var one = Transcript()
        one.apply(.event(EventDecoder.decode(line: #"{"type":"session","protocol_version":1,"model":"m","next_turn":2}"#)))
        #expect(shape(one) == ["notice: resumed: 1 earlier turn of this conversation isn't shown here -- apogee keeps it, and the model still has it"])
        var fresh = Transcript()
        fresh.apply(.event(EventDecoder.decode(line: #"{"type":"session","protocol_version":1,"model":"m","next_turn":1}"#)))
        #expect(fresh.items.isEmpty)
    }

    @Test("the answer shown is exactly its deltas joined")
    func deltasJoined() {
        var transcript = Transcript()
        transcript.apply(decoded(#"{"type":"session","protocol_version":1,"model":"m"}"#))
        transcript.userSent("hi")
        for line in [
            #"{"type":"answer_start","turn":1}"#, #"{"type":"answer_delta","text":"caf","turn":1}"#,
            #"{"type":"answer_delta","text":"é ok","turn":1}"#, #"{"type":"answer_end","turn":1}"#,
            #"{"type":"result","text":"café ok","turn":1,"usage":{"input_tokens":4,"output_tokens":2}}"#,
        ] {
            transcript.apply(decoded(line))
        }
        #expect(shape(transcript) == ["user: hi", "answer: café ok"])
        guard case .answer(let answer) = transcript.items.last?.content else { return }
        #expect(answer.usage == Usage(inputTokens: 4, outputTokens: 2))
    }

    @Test("a result with no deltas before it still shows its answer")
    func droppedDeltas() {
        var transcript = Transcript()
        transcript.apply(decoded(#"{"type":"session","protocol_version":1,"model":"m"}"#))
        transcript.userSent("hi")
        transcript.apply(decoded(#"{"type":"result","text":"whole","turn":1}"#))
        #expect(shape(transcript) == ["user: hi", "answer: whole"])
    }

    @Test("a cancel mid-stream: stopping until the result says so, the partial answer kept, the turn marked")
    func cancelled() {
        var transcript = Transcript()
        transcript.apply(decoded(#"{"type":"session","protocol_version":1,"model":"m"}"#))
        transcript.userSent("long story")
        transcript.apply(decoded(#"{"type":"answer_start","turn":1}"#))
        transcript.apply(decoded(#"{"type":"answer_delta","text":"Once ","turn":1}"#))
        #expect(transcript.turnInFlight)
        transcript.cancelling()
        #expect(transcript.status == .stopping(turn: 1))
        transcript.apply(decoded(#"{"type":"answer_delta","text":"upon","turn":1}"#))
        #expect(transcript.status == .stopping(turn: 1))
        #expect(!transcript.turnInFlight)
        transcript.apply(decoded(#"{"type":"result","text":"Once upon","finish_reason":"cancelled","turn":1}"#))
        #expect(shape(transcript) == ["user: long story", "answer: Once upon", "stopped"])
        #expect(transcript.status == .ready)
    }

    @Test("a failed turn shows the CLI's words exactly, and the input is the user's again")
    func error() {
        var transcript = Transcript()
        transcript.apply(decoded(#"{"type":"session","protocol_version":1,"model":"m"}"#))
        transcript.userSent("hi")
        transcript.apply(decoded(#"{"type":"thinking","turn":1}"#))
        transcript.apply(decoded(#"{"type":"error","message":"anthropic: 529 overloaded -- try again in a minute","turn":1}"#))
        #expect(shape(transcript) == ["user: hi", "activity: |", "error: anthropic: 529 overloaded -- try again in a minute"])
        #expect(transcript.status == .ready)
    }

    @Test("what this client does not know is a quiet row, never dropped")
    func unknownRows() {
        var transcript = Transcript()
        transcript.apply(decoded(#"{"type":"session","protocol_version":1,"model":"m"}"#))
        transcript.apply(decoded(#"{"type":"hologram","turn":1}"#))
        transcript.apply(decoded("not json at all"))
        transcript.apply(decoded(#"{"type":"task_round","task_id":"t"}"#))
        #expect(shape(transcript) == ["unknown: hologram", "unknown: a line that is not JSON", "unknown: task_round"])
    }

    @Test("a session that fails ends with its message and the stderr behind it, and takes no more input")
    func endedFailed() {
        var transcript = Transcript()
        transcript.apply(decoded(#"{"type":"session","protocol_version":1,"model":"m"}"#))
        var tail = BoundedTail()
        tail.append(.init("loading\nfatal: the model went away\n".utf8))
        transcript.apply(.ended(SessionEnd(reason: .failed, exit: ChildExit(reason: .exited, status: 2), stderrTail: tail)))
        #expect(shape(transcript) == ["ended: apogee exited with status 2: fatal: the model went away"])
        guard case .ended(let ending) = transcript.items.last?.content else { return }
        #expect(ending.details == "loading\nfatal: the model went away\n")
        #expect(transcript.status == .ended)
        #expect(!transcript.acceptsInput)
    }

    @Test("a binary that will not start says why")
    func couldNotStart() {
        var transcript = Transcript()
        transcript.apply(.ended(SessionEnd(reason: .couldNotStart, exit: nil, stderrTail: BoundedTail(), detail: "permission denied")))
        #expect(shape(transcript) == ["ended: apogee could not be started: permission denied"])
    }

    @Test("the plan is drawn in place, and what was recalled is said once, quietly")
    func todoAndNotices() {
        var transcript = Transcript()
        transcript.apply(decoded(#"{"type":"session","protocol_version":1,"model":"m"}"#))
        transcript.userSent("plan it")
        transcript.apply(decoded(#"{"type":"memory","chats":2,"decisions":1,"turn":1}"#))
        transcript.apply(decoded(#"{"type":"todo","items":[{"text":"a","state":"active"}],"turn":1}"#))
        transcript.apply(decoded(#"{"type":"notice","text":"context budget: 2 earlier exchanges not sent","turn":1}"#))
        transcript.apply(decoded(#"{"type":"todo","items":[{"text":"a","state":"done"},{"text":"b","state":"active"}],"turn":1}"#))
        #expect(shape(transcript) == [
            "user: plan it", "notice: recalled 2 earlier chats and 1 decision", "todo: a,b",
            "notice: context budget: 2 earlier exchanges not sent",
        ])
    }

    @Test("a question asked while a cancel is on its way leaves the turn stopping")
    func questionWhileStopping() {
        var transcript = Transcript()
        transcript.apply(decoded(#"{"type":"session","protocol_version":1,"model":"m"}"#))
        transcript.userSent("go")
        transcript.apply(decoded(#"{"type":"thinking","turn":1}"#))
        transcript.cancelling()
        transcript.apply(decoded(#"{"type":"question","turn":1,"questions":[{"question":"?"}]}"#))
        #expect(transcript.status == .stopping(turn: 1))
    }
}
