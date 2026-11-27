import BusinessJobs
import BusinessReads
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationLibrary

/// The Library's editor keeps what the person brought (GUI 6f): Close and
/// Esc ask before discarding typing, a refused symphony save can still be put
/// back on close, an agent's file can be put back after any save, and Play
/// gives `--image` to a symphony whose input takes one -- and to no other.
@Suite("Library editor keeps edits", .timeLimit(.minutes(1)))
@MainActor
struct LibraryEditorCloseTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    /// `notes-digest`'s spec as the recording read it (`symphony-show.json`
    /// is its document), and the same with an unknown key in its second
    /// stage, which `symphony-show-unknown-key.err` refuses.
    static let digest = """
        description: Summarize then check.
        input:
          description: The text to summarize.
        stages:
          - name: summarize
            role: utility
            prompt: "Summarize: {{input}}"
          - name: verify
            role: chat
            prompt: "Check this summary against the text. Summary: {{summarize}} Text: {{input}}"

        """
    static let refusedDigest = digest.replacingOccurrences(of: "    role: chat\n", with: "    role: chat\n    temprature: 0.2\n")

    /// A throwaway data directory holding the person's own files, as `config
    /// path` would name it.
    func dataHome() throws -> URL {
        let home = FileManager.default.temporaryDirectory.appendingPathComponent("library-close-\(UUID().uuidString)")
        for folder in ["config", "prompts", "schemas", "symphonies"] {
            try FileManager.default.createDirectory(at: home.appendingPathComponent(folder), withIntermediateDirectories: true)
        }
        try "Sort the issue by area.".write(to: home.appendingPathComponent("prompts/triage.txt"), atomically: true, encoding: .utf8)
        try Self.digest.write(to: home.appendingPathComponent("symphonies/notes-digest.yaml"), atomically: true, encoding: .utf8)
        return home
    }

    func agents(_ home: URL, _ runner: ScriptedRunner = ScriptedRunner()) async -> AgentsViewModel {
        runner.reading(["agents", "list"], fixture("agents-list"))
        runner.answering(["config", "path"], .done(stdout: home.appendingPathComponent("config/config.json").path + "\n"))
        let model = AgentsViewModel(runner: runner)
        await model.connect(binary: Self.binary)
        model.edit(model.agents.first { $0.name == "triage" }!, file: "prompts/triage.txt")
        return model
    }

    /// The person's own `notes-digest` open in the editor, its file in
    /// `home`; `symphonies show` reads it as recorded.
    func symphonies(_ home: URL, _ runner: ScriptedRunner) async -> SymphoniesViewModel {
        let path = home.appendingPathComponent("symphonies/notes-digest.yaml").path
        runner.reading(["symphonies", "list"], fixture("symphonies-list").replacingOccurrences(of: "/Users/someone/.apogee/symphonies/notes-digest.yaml", with: path))
        runner.reading(["symphonies", "show"], fixture("symphony-show"))
        let model = SymphoniesViewModel(runner: runner, starter: ScriptedJobs([]))
        await model.connect(binary: Self.binary)
        model.select(model.symphonies.first { $0.name == "notes-digest" }!)
        model.edit(model.current!)
        return model
    }

    /// The binary's refusal of a spec with an unknown key in its second stage,
    /// recorded from the branch's `apogee-dev` (`symphony-show-unknown-key.err`).
    func refuseShow(_ runner: ScriptedRunner) {
        let refused = refusal("symphony-show-unknown-key")
        runner.failing(["symphonies", "show"], .failed(exit: refused.exit, stderrTail: refused.tail))
    }

    // MARK: - Unsaved typing

    @Test("typed text then Close asks first and leaves the editor open; Keep editing keeps every character; Discard closes")
    func unsavedClose() async throws {
        let home = try dataHome()
        defer { try? FileManager.default.removeItem(at: home) }
        let model = await agents(home)
        model.editor?.text = "Sort the issue by area, then by urgency."
        model.requestClose()
        #expect(model.closing == .unsaved)
        #expect(model.editor?.text == "Sort the issue by area, then by urgency.", "the editor stays open, its text as typed")
        model.keepEditing()
        #expect(model.closing == nil && model.editor?.text == "Sort the issue by area, then by urgency.")
        model.requestClose()
        #expect(model.closing == .unsaved)
        model.discardAndClose()
        #expect(model.editor == nil && model.closing == nil)
        #expect(try String(contentsOf: home.appendingPathComponent("prompts/triage.txt"), encoding: .utf8) == "Sort the issue by area.", "discarding writes nothing")
    }

    @Test("a symphony's typing asks first too, and typing after a save that stood asks again")
    func unsavedSymphonyClose() async throws {
        let home = try dataHome()
        defer { try? FileManager.default.removeItem(at: home) }
        let model = await symphonies(home, ScriptedRunner())
        model.editor?.text = "description: Summarize, then check.\n"
        model.requestClose()
        #expect(model.closing == .unsaved)
        model.keepEditing()
        await model.save()
        model.editor?.text += "# and once more\n"
        model.requestClose()
        #expect(model.closing == .unsaved, "typing since the save is unsaved")
        #expect(model.editor != nil)
    }

    // MARK: - Nothing to lose

    @Test("opening then closing, or closing after a save that stood, closes at once with no prompt")
    func nothingToLose() async throws {
        let home = try dataHome()
        defer { try? FileManager.default.removeItem(at: home) }
        let agents = await agents(home)
        agents.requestClose()
        #expect(agents.editor == nil && agents.closing == nil, "opened and closed: nothing typed")

        agents.edit(agents.agents.first { $0.name == "triage" }!, file: "prompts/triage.txt")
        agents.editor?.text = "Sort by area."
        await agents.save()
        #expect(agents.editor?.changed == true, "the text differs from the original, so changed alone would prompt")
        #expect(agents.editor?.unsaved == false)
        agents.requestClose()
        #expect(agents.editor == nil && agents.closing == nil, "the save stood: nothing is lost")

        let symphonies = await symphonies(home, ScriptedRunner())
        symphonies.requestClose()
        #expect(symphonies.editor == nil && symphonies.closing == nil)
        symphonies.edit(symphonies.current!)
        symphonies.editor?.text = "description: Summarize, then check.\n"
        await symphonies.save()
        #expect(symphonies.editor?.verdict == "Saved, and apogee reads it.")
        symphonies.requestClose()
        #expect(symphonies.editor == nil && symphonies.closing == nil)
    }

    @Test("closing mid-save still closes at once: 4h's rule, with no prompt")
    func midSaveClosesAtOnce() async throws {
        let home = try dataHome()
        defer { try? FileManager.default.removeItem(at: home) }
        let runner = ScriptedRunner()
        let model = await symphonies(home, runner)
        model.editor?.text = "description: Summarize, then check.\n"
        let held = runner.holding(["symphonies", "show"])
        let saving = Task { await model.save() }
        #expect(await eventually { held.arrived == 1 })
        model.requestClose()
        #expect(model.editor == nil && model.closing == nil, "closed at once, never asked")
        held.open()
        await saving.value
        #expect(model.editor == nil, "the save's end doesn't bring it back")
    }

    // MARK: - A refused save on disk

    @Test("after a refused save, Close asks; Put it back writes the original, asks show again, and closes once it answers")
    func refusedOnDiskPutBack() async throws {
        let home = try dataHome()
        defer { try? FileManager.default.removeItem(at: home) }
        let file = home.appendingPathComponent("symphonies/notes-digest.yaml")
        let runner = ScriptedRunner()
        let model = await symphonies(home, runner)
        refuseShow(runner)
        model.editor?.text = Self.refusedDigest
        await model.save()
        #expect(model.editor?.refused == true)
        #expect(model.editor?.verdict == "apogee symphonies: /Users/someone/.apogee/symphonies/notes-digest.yaml: stages[1]: unknown key 'temprature' (accepted: name, role, play, prompt, input, schema, image, brief_tokens, answer_tokens) (exited with status 1)")
        #expect(try String(contentsOf: file, encoding: .utf8) == Self.refusedDigest, "the refused text is on disk")
        #expect(model.editor?.unsaved == false, "nothing typed is unsaved: the loss is the original")

        model.requestClose()
        #expect(model.closing == .refusedOnDisk)
        #expect(model.editor != nil)
        model.keepEditing()
        #expect(model.closing == nil && model.editor?.text == Self.refusedDigest)

        runner.clearingFailures(["symphonies", "show"])
        model.requestClose()
        let shows = runner.calls.filter { $0 == "read symphonies|show|notes-digest" }.count
        let held = runner.holding(["symphonies", "show"])
        let closing = Task { await model.restoreAndClose() }
        #expect(await eventually { held.arrived == 1 })
        #expect(try String(contentsOf: file, encoding: .utf8) == Self.digest, "the original is written back first")
        #expect(model.editor != nil, "open until apogee has answered")
        #expect(model.closing == nil)
        held.open()
        await closing.value
        #expect(model.editor == nil)
        #expect(runner.calls.filter { $0 == "read symphonies|show|notes-digest" }.count == shows + 1, "show asked again of the original")
    }

    @Test("Leave it closes with the refused text on disk, and asks nothing more of the binary")
    func refusedOnDiskLeave() async throws {
        let home = try dataHome()
        defer { try? FileManager.default.removeItem(at: home) }
        let file = home.appendingPathComponent("symphonies/notes-digest.yaml")
        let runner = ScriptedRunner()
        let model = await symphonies(home, runner)
        refuseShow(runner)
        model.editor?.text = Self.refusedDigest
        await model.save()
        model.editor?.text += "# typed since\n"
        model.requestClose()
        #expect(model.closing == .refusedOnDisk, "the file's loss is asked about before the typing's")
        #expect(model.editor?.explanation(.refusedOnDisk).hasSuffix("What you typed since the save is lost either way.") == true)
        let calls = runner.calls.count
        model.leaveAndClose()
        #expect(model.editor == nil && model.closing == nil)
        #expect(try String(contentsOf: file, encoding: .utf8) == Self.refusedDigest)
        #expect(runner.calls.count == calls)
    }

    @Test("Put it back that cannot write keeps the editor open, saying why")
    func putBackThatFailsStaysOpen() async throws {
        let home = try dataHome()
        defer { try? FileManager.default.removeItem(at: home) }
        let runner = ScriptedRunner()
        let model = await symphonies(home, runner)
        refuseShow(runner)
        model.editor?.text = Self.refusedDigest
        await model.save()
        try FileManager.default.removeItem(at: home.appendingPathComponent("symphonies"))
        model.requestClose()
        await model.restoreAndClose()
        #expect(model.editor != nil, "the file still holds the refused text, so the editor stays")
        #expect(model.editor?.verdict?.hasPrefix("Not saved: ") == true)
        model.requestClose()
        #expect(model.closing == .refusedOnDisk)
    }

    @Test("a refused save whose file holds the original after all -- put back, or never changed -- closes with no prompt")
    func refusedOriginalClosesAtOnce() async throws {
        let home = try dataHome()
        defer { try? FileManager.default.removeItem(at: home) }
        let runner = ScriptedRunner()
        let model = await symphonies(home, runner)
        refuseShow(runner)
        await model.save()
        #expect(model.editor?.refused == true, "apogee refuses the file as it was")
        model.requestClose()
        #expect(model.editor == nil && model.closing == nil, "the file holds its original: closing loses nothing")
    }

    // MARK: - The agents' restore

    @Test("an agent's file can be put back as it was after any save, and is then closed with no prompt")
    func agentsRestore() async throws {
        let home = try dataHome()
        defer { try? FileManager.default.removeItem(at: home) }
        let file = home.appendingPathComponent("prompts/triage.txt")
        let model = await agents(home)
        #expect(!model.offersRestore, "nothing saved yet: the file is as it was")
        model.editor?.text = "Sort by area."
        #expect(!model.offersRestore, "typing alone writes nothing")
        await model.save()
        #expect(model.offersRestore, "after a save that stood")
        await model.restore()
        #expect(try String(contentsOf: file, encoding: .utf8) == "Sort the issue by area.")
        #expect(model.editor?.text == "Sort the issue by area.")
        #expect(!model.offersRestore)
        model.requestClose()
        #expect(model.editor == nil && model.closing == nil)
    }

    // MARK: - The prompt's words

    @Test("the prompt says what closing loses, in plain words")
    func promptWords() {
        var editor = DefinitionEditor(title: "notes-digest", path: "/Users/someone/.apogee/symphonies/notes-digest.yaml", original: Self.digest)
        #expect(editor.closeReason == nil)
        editor.text = "typed"
        #expect(editor.closeReason == .unsaved)
        #expect(editor.question(.unsaved) == "Discard your changes to notes-digest.yaml?")
        #expect(editor.explanation(.unsaved) == "What you typed isn't saved, and closing loses it.")
        #expect(editor.question(.refusedOnDisk) == "notes-digest.yaml holds the text apogee refused")
        editor.text = Self.digest
        #expect(
            editor.explanation(.refusedOnDisk)
                == "Put it back as it was writes the text it held when you opened it, and apogee reads it again. Leave it keeps the refused text in the file, and apogee can't play it until it is fixed.")
    }

    // MARK: - Play and its image

    @Test("describe-answer plays only with an image, and then carries exactly --image; one that takes none never does")
    func playArgv() async throws {
        let first = ScriptedJob()
        let second = ScriptedJob()
        let runner = ScriptedRunner()
        runner.reading(["symphonies", "list"], fixture("symphonies-list"))
        let starter = ScriptedJobs([first, second])
        let model = SymphoniesViewModel(runner: runner, starter: starter)
        await model.connect(binary: Self.binary)
        let describe = try #require(model.symphonies.first { $0.name == "describe-answer" })
        #expect(describe.takesImage, "the recorded list says image: true")
        model.select(describe)
        model.input = "what is it"
        #expect(model.needsImage && !model.canPlay, "no image: Play is unavailable")
        model.playIt()
        #expect(starter.starts.isEmpty && !model.isPlaying, "and no job is started")

        model.image = "/Users/someone/Pictures/cat.png"
        #expect(model.imageName == "cat.png")
        #expect(!model.needsImage && model.canPlay)
        model.select(describe)
        #expect(model.image != nil, "choosing the same symphony keeps it")
        model.playIt()
        #expect(starter.starts == [["symphonies", "play", "describe-answer", "--input", "what is it", "--image", "/Users/someone/Pictures/cat.png", "--output-format", "json"]])
        first.end()
        #expect(await eventually { !model.isPlaying })

        let critique = try #require(model.symphonies.first { $0.name == "critique-revise" })
        model.select(critique)
        #expect(model.image == nil, "switching symphonies clears the image")
        model.image = "/Users/someone/Pictures/cat.png"
        #expect(!model.needsImage && model.canPlay)
        model.playIt()
        #expect(starter.starts.last == ["symphonies", "play", "critique-revise", "--input", "what is it", "--output-format", "json"], "image: false never carries --image")
        second.end()
        #expect(await eventually { !model.isPlaying })

        model.select(describe)
        model.image = "/Users/someone/Pictures/cat.png"
        model.copyName = "describe-mine"
        await model.copy()
        #expect(model.selected == "describe-mine" && model.image == nil, "the copy, selected once made, is another symphony: the image goes")
    }

    @Test("the chooser takes the CLI's list of image types")
    func imageTypes() {
        #expect(SymphoniesViewModel.imageTypes == ["png", "jpg", "gif", "webp", "bmp"])
    }
}
