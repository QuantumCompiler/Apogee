import BusinessSession
import DataProtocol
import Foundation
import PresentationSession
import Testing

/// 1g: the session's states and the input's reach -- each over a scripted
/// session announcing what its build accepts.
@Suite("SessionStates", .timeLimit(.minutes(1)))
@MainActor
struct SessionStatesTests {
    static let old = #"{"type":"session","protocol_version":1,"model":"local","next_turn":1,"capabilities":{"events":[],"accepts":["user","answer","attach","hello","cancel"],"tools":true,"ask":true,"schema":"2026-10-13"}}"#
    static let new = #"{"type":"session","protocol_version":1,"model":"local","next_turn":1,"capabilities":{"events":[],"accepts":["user","answer","attach","hello","cancel","detach","set","compact"],"tools":true,"ask":true,"schema":"2026-10-20"}}"#

    func ready(_ line: String, launch: ChatLaunch = ChatLaunch(), sessions: [ScriptedSession]? = nil) async -> (SessionViewModel, ScriptedSession, ScriptedStarter) {
        let scripted = sessions ?? [ScriptedSession()]
        let starter = ScriptedStarter(sessions: scripted)
        let model = SessionViewModel(starter: starter, launch: launch)
        model.start()
        scripted[0].emit(line)
        _ = await eventually { model.transcript.status == .ready }
        return (model, scripted[0], starter)
    }

    @Test("attach is the protocol's line and a chip; detach and compact only where the build takes them")
    func attachDetachCompact() async {
        let (older, oldChild, _) = await ready(Self.old)
        older.attach("report.pdf")
        older.attach("report.pdf")
        #expect(older.attachments == ["report.pdf"])
        #expect(await eventually { oldChild.calls == ["attach: report.pdf", "attach: report.pdf"] })
        #expect(!older.canDetach && !older.canCompact && !older.canSet)
        older.detach("report.pdf")
        older.compact()
        #expect(older.attachments == ["report.pdf"], "no detach before 52d: the chip stays, and offers none")
        try? await Task.sleep(for: .milliseconds(50))
        #expect(oldChild.calls.count == 2)

        let (newer, newChild, _) = await ready(Self.new)
        newer.attach("src")
        newer.detach("src")
        newer.compact()
        #expect(newer.attachments.isEmpty)
        #expect(await eventually { newChild.calls == ["attach: src", "detach: src", "compact"] })
    }

    @Test("a settings change is the set line where the build takes it, said by the CLI from the next turn")
    func settingsBySet() async {
        let (model, child, starter) = await ready(Self.new)
        await model.apply(RunSettings(model: "claude", think: "off"))
        #expect(await eventually { child.calls == [#"set: {"type":"set","model":"claude","think":"off"}"#] })
        #expect(model.settings == RunSettings(model: "claude", think: "off"))
        #expect(starter.openedCount == 1, "no restart")
        await model.apply(RunSettings())
        #expect(child.calls.count == 1, "no change, no line")
    }

    @Test("on an older build a settings change restarts the session with the flags, resuming it, and says so")
    func settingsByRestart() async {
        let first = ScriptedSession()
        let second = ScriptedSession()
        let (model, _, starter) = await ready(Self.old, launch: ChatLaunch(resume: "c1"), sessions: [first, second])
        await model.apply(RunSettings(model: "claude", effort: "high"))
        #expect(first.calls == ["stop"])
        #expect(await eventually { starter.openedCount == 2 })
        #expect(starter.launches.last == ChatLaunch(resume: "c1", settings: RunSettings(model: "claude", effort: "high")))
        #expect(model.transcript.items.contains {
            $0.content == .notice("restarting with model claude, effort high -- this apogee changes settings by restarting the session; the conversation carries on, and a local model loads again")
        })
        second.emit(Self.old)
        #expect(await eventually { model.transcript.status == .ready })
    }

    @Test("Try again sends the last turn as typed, and leaves the draft alone")
    func retry() async {
        let (model, child, _) = await ready(Self.new)
        model.draft = "summarize report.pdf"
        model.submit()
        child.emit(#"{"type":"error","message":"llama.cpp: failed to allocate 7.9 GB","turn":1}"#)
        #expect(await eventually { model.transcript.status == .ready })
        model.draft = "half typed"
        model.retry()
        #expect(await eventually { child.calls == ["user: summarize report.pdf", "user: summarize report.pdf"] })
        #expect(model.draft == "half typed")
    }

    @Test("the context meter is the last turn's input tokens against the window; unknown when either is")
    func meter() async {
        let (model, child, _) = await ready(Self.new)
        #expect(model.meter.text == "unknown")
        model.contextWindow = 8192
        #expect(model.meter.text == "not reported", "a window, and no turn yet")
        model.draft = "hi"
        model.submit()
        child.emit(#"{"type":"result","text":"hello","turn":1,"usage":{"input_tokens":6800,"output_tokens":20}}"#)
        #expect(await eventually { model.meter.percent == 83 })
        #expect(model.meter.warns)
        #expect(model.meter.text == "83% · 6,800 of 8,192 tokens")
        #expect(ContextMeter(used: 100, window: 1000).warns == false)
        #expect(ContextMeter(used: 9000, window: 8192).percent == 100)
        #expect(ContextMeter(used: 5, window: 0).percent == nil)
    }

    @Test("slash commands offer only what the build and the moment can do, and each maps to its act")
    func slash() async {
        let (model, child, _) = await ready(Self.old)
        var acts: [SessionAction] = []
        model.onAction = { acts.append($0) }
        model.draft = "/"
        #expect(model.slashSuggestions.map(\.name) == ["/new", "/private", "/model", "/suite", "/attach", "/inspector"],
            "no compact or detach before 52d, nothing to stop, nothing saved to rename or delete; /suite in any chat (GUI 4i)")
        model.draft = "/pr"
        #expect(model.slashSuggestions.map(\.name) == ["/private"])
        model.run(model.slashSuggestions[0])
        #expect(acts == [.newPrivateSession])
        #expect(model.draft == "")
        model.draft = "/model"
        model.run(model.slashSuggestions[0])
        #expect(model.presenting == .settings)
        model.draft = "/model now"
        #expect(model.slashSuggestions.isEmpty, "a space ends the command word")
        model.draft = "go"
        model.submit()
        child.emit(#"{"type":"answer_start","turn":1}"#)
        _ = await eventually { model.canCancel }
        model.draft = "/st"
        model.run(model.slashSuggestions[0])
        #expect(await eventually { child.calls.last == "cancel" })
    }

    @Test("a private session says what is kept: nothing, or deleted on close")
    func privateStates() async {
        let (written, _, _) = await ready(Self.new, launch: ChatLaunch(privately: true))
        #expect(written.privateMode == .nothingWritten)
        #expect(SessionViewModel.privateWords(.nothingWritten) == "Nothing of this session is written: no transcript, no recall in later sessions.")
        let (fallback, _, _) = await ready(Self.old, launch: ChatLaunch(noRecall: true))
        #expect(fallback.privateMode == .deletedOnClose)
        #expect(SessionViewModel.privateWords(.deletedOnClose) == "Never recalled in later sessions, and deleted when you close it. This apogee writes it until then.")
        let (plain, _, _) = await ready(Self.new)
        #expect(plain.privateMode == nil)
    }

    @Test("@ offers the session folder's paths, and completes the one picked")
    func mentions() async throws {
        let folder = FileManager.default.temporaryDirectory.appendingPathComponent("at-\(UUID().uuidString)")
        defer { try? FileManager.default.removeItem(at: folder) }
        try FileManager.default.createDirectory(at: folder.appendingPathComponent("src"), withIntermediateDirectories: true)
        try Data().write(to: folder.appendingPathComponent("report.pdf"))
        let (model, _, _) = await ready(Self.new)
        model.folder = folder
        model.draft = "read @re"
        #expect(model.mentionSuggestions == ["report.pdf"])
        model.complete(mention: "report.pdf")
        #expect(model.draft == "read @report.pdf ")
        model.draft = "in @s"
        model.complete(mention: "src/")
        #expect(model.draft == "in @src/", "a folder keeps the mention open")
    }

    @Test("recalled chats are named and the sources numbered, each its own row")
    func recallAndSources() {
        var transcript = Transcript()
        transcript.apply(.event(EventDecoder.decode(line: #"{"type":"memory","chats":1,"decisions":2,"turn":1,"sessions":[{"id":"c7","name":"asyncio timeouts in the scraper"}]}"#)))
        transcript.apply(.event(EventDecoder.decode(line: #"{"type":"sources","turn":1,"sources":[{"file":"report.pdf","pages":[41,42],"score":0.8,"retriever":"vector"}]}"#)))
        guard case .recalled(let recall) = transcript.items[0].content, case .sources(let sources) = transcript.items[1].content else {
            Issue.record("rows: \(transcript.items)")
            return
        }
        #expect(recall.summary == "recalled 1 earlier chat and 2 decisions")
        #expect(recall.chats.map(\.id) == ["c7"])
        #expect(sources.map(\.place) == ["pages 41–42"])
    }
}
