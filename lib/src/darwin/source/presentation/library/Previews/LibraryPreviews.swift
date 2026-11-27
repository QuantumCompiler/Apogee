#if DEBUG
import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation

/// The Library's view-models for its previews, over the recorded documents
/// (ADR 0004): nothing starts the binary.
@MainActor
enum LibraryPreview {
    static let runner = PreviewRunner(
        reads: [
            (["agents", "list"], LibraryDocuments.agentsList),
            (["symphonies", "list"], LibraryDocuments.symphoniesList),
            (["mcp", "list"], LibraryDocuments.mcpList),
            (["mcp", "presets"], LibraryDocuments.mcpPresets),
        ],
        commands: [
            (["config", "path"], "/Users/someone/.apogee/config/config.json\n"),
            // GUI 4k's recording: a server removed by its config entry.
            (["config", "delete-mcp-server"], LibraryDocuments.mcpDelete),
        ])

    /// `critique-revise` played on a mock writer: its stage lines as the CLI
    /// says them (tests/business/reads/Fixtures/symphony-play.err), then its
    /// document.
    static let play: [JobUpdate] = [
        .busy("stage 1/2 critique — asking utility (writer): Below is a draft. Work out what it is meant to do -- from… · 0.0 s"),
        .said(LibraryDocuments.symphonyPlay),
    ]

    /// The prompt `apogee agents create triage --description "Sort incoming
    /// issues by area"` scaffolds, as the binary wrote it.
    static let triagePrompt = """
        You are triage -- Sort incoming issues by area.

        Describe the task this agent performs and exactly how it should respond. This
        file is the system prompt loaded for "apogee analyze --agent triage"; edit it
        to shape the agent's behavior, then run the agent to try it.

        A report that finds nothing notable is a complete, valid result.
        """

    /// The agents `agents list` names, as rows.
    static var agentRows: [AgentEntry] { AgentEntry.list(from: ReadDocument(text: LibraryDocuments.agentsList)!) }

    /// The same reads over a config hand-edited into invalid JSON (a missing
    /// comma): `agents list`, `symphonies list` and `mcp list` refuse it, each
    /// in its recorded words, while `mcp presets` reads no config.
    static let badConfig = PreviewRunner(
        reads: [(["mcp", "presets"], LibraryDocuments.mcpPresets)],
        commands: [(["config", "path"], "/Users/someone/.apogee/config/config.json\n")],
        refusals: [
            (["agents", "list"], LibraryDocuments.agentsListBadConfig),
            (["symphonies", "list"], LibraryDocuments.symphoniesListBadConfig),
            (["mcp", "list"], LibraryDocuments.mcpListBadConfig),
        ])

    static func agents(runner: PreviewRunner = LibraryPreview.runner) -> AgentsViewModel { AgentsViewModel(runner: runner) }

    static func symphonies(playing: Bool = false, runner: PreviewRunner = LibraryPreview.runner) -> SymphoniesViewModel {
        SymphoniesViewModel(runner: runner, starter: PreviewJobs(said: play, finishes: !playing))
    }

    static func mcp(runner: PreviewRunner = LibraryPreview.runner) -> MCPViewModel { MCPViewModel(runner: runner) }

    /// A line typed into the triage prompt.
    static let triageTyped = triagePrompt + "\nFile each issue under one area.\n"

    /// The person's own triage prompt open in the agents' editor, as `edit`
    /// opens it: with a line typed; saved, as a save that stood leaves it;
    /// mid-save; or asked about on Close, the typing unsaved (GUI 6f).
    static func agentEditor(typed: Bool = false, saved: Bool = false, saving: Bool = false, closing: Bool = false) -> AgentsViewModel {
        let model = agents()
        var editor = DefinitionEditor(title: "triage · prompts/triage.txt", path: "/Users/someone/.apogee/prompts/triage.txt", original: triagePrompt)
        if typed || saved || saving { editor.text = triageTyped }
        if saved {
            editor.wrote(triageTyped)
            editor.endSave(of: triageTyped, verdict: "Saved. Apogee checks an agent's prompt and schema when it runs -- its first run says what is wrong, if anything.", refused: false)
        }
        if saving {
            editor.wrote(triageTyped)
            editor.saving = true
        }
        model.editor = editor
        if closing { model.requestClose() }
        return model
    }

    /// `notes-digest`'s spec as the recording read it (its document is
    /// tests/business/reads/Fixtures/symphony-show.json), and the same with
    /// the unknown key `symphony-show-unknown-key.err` refuses.
    static let notesDigest = """
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
    static let notesDigestRefused = notesDigest.replacingOccurrences(of: "    role: chat\n", with: "    role: chat\n    temprature: 0.2\n")

    /// The person's own `notes-digest` after a save apogee refused -- the
    /// refused text in its file, in the binary's recorded words -- and, when
    /// `closing`, Close asked about (GUI 6f).
    static func refusedSymphony(closing: Bool = false) -> SymphoniesViewModel {
        let model = symphonies()
        var editor = DefinitionEditor(title: "notes-digest", path: "/Users/someone/.apogee/symphonies/notes-digest.yaml", original: notesDigest, text: notesDigestRefused)
        editor.wrote(notesDigestRefused)
        editor.endSave(of: notesDigestRefused, verdict: LibraryDocuments.symphonyShowUnknownKey + " (exited with status 1)", refused: true)
        model.editor = editor
        if closing { model.requestClose() }
        return model
    }
}
#endif
