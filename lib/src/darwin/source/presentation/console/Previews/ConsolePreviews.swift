#if DEBUG
import BusinessJobs
import Foundation

/// The console's view-model for its previews (ADR 0004): commands run as
/// preview jobs saying what the binary said, recorded; nothing starts the
/// binary.
@MainActor
enum ConsolePreview {
    /// Tab's candidates after `models li`, as
    /// tests/presentation/console/ConsoleViewModelTests.swift holds them.
    static let runner = PreviewRunner(commands: [(["__complete", "models"], "list\nlist-all\n")])

    /// The console, each run it starts saying `said` (and ending there when
    /// it `finishes`).
    static func console(saying said: [JobUpdate] = [], finishes: Bool = false) -> ConsoleViewModel {
        let console = ConsoleViewModel(runner: runner, starter: PreviewJobs(said: said, finishes: finishes))
        console.connect(binary: previewBinary)
        return console
    }

    /// `config scan`: its report on stdout, line by line, then a good end.
    static let scan: [JobUpdate] =
        ConsoleDocuments.configScan.split(separator: "\n", omittingEmptySubsequences: false).map { .said(String($0)) }
        + [previewEnd(status: 0)]

    /// The symphony the Library plays, `critique-revise`, at its first stage:
    /// the stage line its stderr says, the busy line while it runs.
    static let playing: [JobUpdate] =
        ConsoleDocuments.symphonyPlayErr.split(separator: "\n").prefix(1).map { .busy(String($0)) }

    /// A review on a Mac without the GitHub CLI: its stderr, and an exit of 1
    /// (tests/presentation/review/ReviewViewModelTests.swift's `forgeRefusal`).
    static let refused: [JobUpdate] = [previewEnd(status: 1, stderr: ConsoleDocuments.reviewNoGh)]
}
#endif
