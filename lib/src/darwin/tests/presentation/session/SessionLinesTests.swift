import BusinessReads
import BusinessSession
import DataProtocol
import Foundation
import InfrastructureProcess
import PresentationSession
import Testing

/// 4b: the lines the window sends and what it shows of them stay honest --
/// each over the branch binary's own recorded streams and documents.
@Suite("The session's lines stay honest", .timeLimit(.minutes(1)))
@MainActor
struct SessionLinesTests {
    static func stream(_ name: String) -> [String] { fixtureStream(name) }

    static func document(_ name: String) -> ReadDocument { fixtureDocument(name) }

    static func type(_ line: String) -> String? { JSONValue.parse(line)?["type"]?.stringValue }

    /// A session over one scripted child, started; returns once its first
    /// line -- the recorded `session` event -- has made it ready.
    func ready(
        _ session: String, launch: ChatLaunch = ChatLaunch(), reader: (any CommandRunning)? = nil,
        children: [ScriptedSession]? = nil
    ) async -> (SessionViewModel, ScriptedSession, ScriptedStarter) {
        let scripted = children ?? [ScriptedSession()]
        let starter = ScriptedStarter(sessions: scripted)
        let model = SessionViewModel(starter: starter, launch: launch, reader: reader)
        model.start()
        scripted[0].emit(session)
        _ = await eventually { model.transcript.status == .ready }
        return (model, scripted[0], starter)
    }

    /// Emits `lines` in order, up to (not including) the first of `type`.
    func emit(_ lines: ArraySlice<String>, to child: ScriptedSession, until type: String? = nil) -> ArraySlice<String> {
        var rest = lines
        while let line = rest.first, Self.type(line) != type || type == nil {
            child.emit(line)
            rest = rest.dropFirst()
        }
        return rest
    }

    /// The card that waits on the person, by its id.
    func standing(_ model: SessionViewModel) -> Int? { model.transcript.standingCard }

    // MARK: - Finding 1: lines wait for the turn's end

    @Test("the recorded stream: a set sent while the question waits draws no notice -- the CLI dropped it")
    func theCLIDropsASetMidQuestion() {
        let lines = Self.stream("question-then-set")
        #expect(lines.contains { Self.type($0) == "question" })
        #expect(!lines.contains { Self.type($0) == "notice" }, "52d's rule, as the branch binary keeps it")
        #expect(Self.stream("set-between-turns").last.flatMap(Self.type) == "notice")
    }

    @Test("a setting made while a question waits is held -- not sent, not shown applied -- and sent once the turn ends")
    func setHeldUntilTheTurnEnds() async {
        let lines = Self.stream("set-between-turns")
        let (model, child, _) = await ready(lines[0])
        model.draft = "Write me a note."
        model.submit()
        var rest = emit(lines.dropFirst(), to: child, until: "question")
        child.emit(rest.removeFirst())
        #expect(await eventually { standing(model) != nil })
        await model.apply(RunSettings(temperature: 0.9))
        try? await Task.sleep(for: .milliseconds(50))
        #expect(child.calls == ["user: Write me a note."], "nothing but the answer reaches a question")
        #expect(model.settings == RunSettings(), "not shown applied: the session has not taken it")
        #expect(model.held == [.set(RunSettings(temperature: 0.9))], "held, and said so")
        #expect(model.held.map(\.words) == ["temperature 0.9"])
        #expect(model.intendedSettings == RunSettings(temperature: 0.9), "what the sheet opens on again")
        #expect(!model.takesLinesNow)
        model.answer(standing(model)!, with: ["todo.md"])
        rest = emit(rest, to: child, until: "notice")
        #expect(await eventually { model.transcript.status == .ready })
        #expect(await eventually { child.calls.count == 3 })
        #expect(child.calls == ["user: Write me a note.", "answer: todo.md", #"set: {"type":"set","temperature":0.9}"#])
        #expect(model.settings == RunSettings(temperature: 0.9))
        #expect(model.held.isEmpty && model.takesLinesNow)
        child.emit(rest.removeFirst())
        #expect(await eventually { model.transcript.items.last?.content == .notice("temperature: 0.9 -- from the next turn") })
    }

    @Test("a detach and an attach made mid-turn are held too, the chips as they were, and sent in order once the turn ends")
    func detachAndAttachHeld() async {
        let lines = Self.stream("set-between-turns")
        let (model, child, _) = await ready(lines[0])
        model.attach("src")
        #expect(await eventually { child.calls == ["attach: src"] })
        model.draft = "Write me a note."
        model.submit()
        var rest = emit(lines.dropFirst(), to: child, until: "question")
        child.emit(rest.removeFirst())
        #expect(await eventually { standing(model) != nil })
        model.detach("src")
        model.attach("report.pdf")
        try? await Task.sleep(for: .milliseconds(50))
        #expect(child.calls == ["attach: src", "user: Write me a note."])
        #expect(model.attachments == ["src"], "the chip stays until the detach is sent")
        #expect(model.held == [.detach("src"), .attach("report.pdf")])
        #expect(model.held.map(\.words) == ["detach src", "attach report.pdf"])
        model.answer(standing(model)!, with: ["todo.md"])
        rest = emit(rest, to: child, until: "notice")
        #expect(await eventually { child.calls.count == 5 })
        #expect(child.calls == ["attach: src", "user: Write me a note.", "answer: todo.md", "detach: src", "attach: report.pdf"])
        #expect(model.attachments == ["report.pdf"])
    }

    // MARK: - Finding 2: a relaunch carries what the session runs on

    @Test("a session launched on -m plain, switched to quiet by set, pauses and resumes on quiet -- never the stale flag")
    func relaunchCarriesTheSwitch() async {
        let lines = Self.stream("set-model")
        let id = JSONValue.parse(lines[0])?["chat_id"]?.stringValue
        let second = ScriptedSession()
        let (model, child, starter) = await ready(
            lines[0], launch: ChatLaunch(settings: RunSettings(model: "plain")), children: [ScriptedSession(), second])
        model.identify(id!)
        model.draft = "Which port does the dev server use?"
        model.submit()
        var rest = emit(lines.dropFirst(), to: child, until: "notice")
        #expect(await eventually { model.transcript.status == .ready })
        await model.apply(RunSettings(model: "quiet"))
        #expect(await eventually { child.calls.last == #"set: {"type":"set","model":"quiet"}"# })
        child.emit(rest.removeFirst())
        #expect(await eventually { model.transcript.items.last?.content == .notice("switched to quiet -- from the next turn") })
        await model.pause()
        model.resume()
        #expect(await eventually { starter.openedCount == 2 })
        #expect(starter.launches.last == ChatLaunch(resume: id, settings: RunSettings(model: "quiet")))
        #expect(starter.launches.last?.arguments.contains("plain") == false)
        second.emit(Self.stream("resumed-switched")[0])
        #expect(await eventually { model.transcript.status == .ready })
        #expect(model.transcript.model == "quiet")
    }

    @Test("a value the CLI refuses by name is put back: neither shown applied nor carried by a relaunch")
    func refusedValuePutBack() async {
        let lines = Self.stream("set-refused")
        let id = JSONValue.parse(lines[0])?["chat_id"]?.stringValue
        let (model, child, starter) = await ready(lines[0], children: [ScriptedSession(), ScriptedSession()])
        model.identify(id!)
        await model.apply(RunSettings(model: "nosuch", temperature: 0.9))
        #expect(await eventually { child.calls == [#"set: {"type":"set","model":"nosuch","temperature":0.9}"#] })
        #expect(model.settings == RunSettings(model: "nosuch", temperature: 0.9), "sent between turns: taken, unless refused")
        for line in lines.dropFirst() { child.emit(line) }
        #expect(await eventually { model.transcript.items.count == 2 })
        #expect(shape(model.transcript) == ["notice: set model: no backend named 'nosuch'", "notice: temperature: 0.9 -- from the next turn"])
        #expect(model.settings == RunSettings(temperature: 0.9), "the refused model put back; the temperature taken")
        await model.pause()
        model.resume()
        #expect(await eventually { starter.openedCount == 2 })
        #expect(starter.launches.last == ChatLaunch(resume: id, settings: RunSettings(temperature: 0.9)))
        #expect(starter.launches.last.map { Array($0.arguments.suffix(2)) } == ["-t", "0.9"], "the session's temperature, at launch")
    }

    @Test("on a build before set, a temperature restarts the session with -t, the conversation resumed")
    func temperatureByRestart() async {
        let older = SessionStatesTests.old
        let second = ScriptedSession()
        let (model, child, starter) = await ready(older, launch: ChatLaunch(resume: "c1"), children: [ScriptedSession(), second])
        #expect(!model.canSet)
        await model.apply(RunSettings(temperature: 0.4))
        #expect(child.calls == ["stop"])
        #expect(await eventually { starter.openedCount == 2 })
        #expect(starter.launches.last?.arguments.suffix(4) == ["--resume", "c1", "-t", "0.4"])
        #expect(model.settings == RunSettings(temperature: 0.4))
    }

    // MARK: - Finding 3: a dead card takes no answer

    @Test("a question left standing by a stopped turn takes no answer: no stray answer line is written")
    func deadCardTakesNoAnswer() async {
        let lines = Self.stream("question-cancelled")
        let (model, child, _) = await ready(lines[0])
        model.draft = "Write me a note."
        model.submit()
        var rest = emit(lines.dropFirst(), to: child, until: "question")
        child.emit(rest.removeFirst())
        #expect(await eventually { standing(model) != nil })
        let card = standing(model)!
        model.cancel()
        _ = emit(rest, to: child)
        #expect(await eventually { model.transcript.status == .ready })
        model.answer(card, with: ["todo.md"])
        try? await Task.sleep(for: .milliseconds(50))
        #expect(child.calls == ["user: Write me a note.", "cancel"])
        #expect(standing(model) == nil, "drawn as no longer answerable")
    }

    // MARK: - Finding 6: a resumed chat's attachments are chips

    @Test("a resumed chat's standing attachments are chips from chats show's names, and /detach reaches them")
    func resumedChips() async throws {
        let shown = Self.document("chats-show-attached.json")
        let chat = try #require(SavedChat(document: shown))
        let lines = Self.stream("resumed-detach")
        let (model, child, _) = await ready(
            lines[0], launch: ChatLaunch(resume: chat.id), reader: ScriptedReader(.document(shown)))
        #expect(model.attachments == ["Documents/report.md"])
        let detach = try #require(SlashCommand.all.first { $0.act == .detachAll })
        #expect(model.available(detach))
        model.run(detach)
        #expect(await eventually { child.calls == ["detach: Documents/report.md"] })
        child.emit(lines[1])
        #expect(await eventually { model.transcript.items.last?.content == .notice("detached Documents/report.md") })
        #expect(model.attachments.isEmpty)
    }

    // MARK: - Finding 7: what the read could not carry over is drawn

    /// Whether `transcript` says `words` above the divider, among the turns
    /// read back.
    func saidAmongTheEarlierTurns(_ words: String, in transcript: Transcript) -> Bool {
        let shapes = shape(transcript)
        guard let said = shapes.firstIndex(where: { $0.contains(words) }),
            let divider = shapes.firstIndex(where: { $0.hasPrefix("divider") })
        else { return false }
        return said < divider
    }

    @Test("55b: chats show's warnings are drawn among the earlier turns, and the resume's own notice after them")
    func warningsDrawn() async throws {
        let shown = Self.document("chats-show-warnings.json")
        let chat = try #require(SavedChat(document: shown))
        let lines = Self.stream("resumed-warnings")
        let (model, child, _) = await ready(
            lines[0], launch: ChatLaunch(resume: chat.id), reader: ScriptedReader(.document(shown)))
        child.emit(lines[1])
        #expect(await eventually { model.transcript.items.last?.content == .notice("resume: a message could not be read and was skipped") })
        #expect(saidAmongTheEarlierTurns("a message could not be read and was skipped", in: model.transcript))
        #expect(shape(model.transcript) == [
            "warning: a message could not be read and was skipped",
            "user: Which port does the dev server use?",
            "answer: The dev server listens on port 8080.\n\nNothing else: the launch is on 2026-11-12.",
            "divider: earlier in this conversation",
            "notice: resume: a message could not be read and was skipped",
        ], "the read's word heads the turns it could not read whole; the resume's own follows the divider")
    }

    @Test("an older binary's chats show says its warning on stderr alone: the read's stderr tail is drawn among the earlier turns")
    func olderWarningsFromStderr() async throws {
        let shown = recorded("business/reads/Fixtures/chats-show-warnings-older.json")
        let said = recorded("business/reads/Fixtures/chats-show-warnings-older.err")
        let chat = try #require(SavedChat(document: ReadDocument(text: shown)!))
        let reader = ReadRunner(spawner: OneRead(stdout: shown, stderr: said))
        let older = Self.stream("resumed-warnings-older")
        #expect(older.count == 1, "the resume says nothing: before 55b its warning went to stderr alone")
        let (model, _, _) = await ready(older[0], launch: ChatLaunch(resume: chat.id), reader: reader)
        #expect(saidAmongTheEarlierTurns("apogee chats: a message could not be read and was skipped", in: model.transcript))
        #expect(shape(model.transcript).first == "warning: apogee chats: a message could not be read and was skipped")
        #expect(shape(model.transcript).count == 4, "the warning, the turn and its answer, the divider: no notice")
    }
}

/// A one-shot child that prints `stdout`, says `stderr` and exits 0: a read
/// that succeeds while saying something on stderr.
struct OneRead: ChildSpawner {
    let stdout: String
    let stderr: String

    func spawn(_ command: ChildCommand) throws -> any RunningChild {
        PrintedChild(stdout: stdout, stderr: stderr)
    }
}

final class PrintedChild: RunningChild, @unchecked Sendable {
    let lines: AsyncStream<String>
    let stderrTail: BoundedTail
    var processIdentifier: Int32 { 0 }

    init(stdout: String, stderr: String) {
        let (lines, continuation) = AsyncStream.makeStream(of: String.self)
        for line in stdout.split(separator: "\n", omittingEmptySubsequences: false) where !line.isEmpty {
            continuation.yield(String(line))
        }
        continuation.finish()
        self.lines = lines
        var tail = BoundedTail()
        tail.append(Data(stderr.utf8))
        stderrTail = tail
    }

    func write(_ data: Data) throws {}
    func closeInput() {}
    func interrupt() {}
    func terminate() {}
    func kill() {}
    func waitForExit() async -> ChildExit { ChildExit(reason: .exited, status: 0) }
}
