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
}
#endif
