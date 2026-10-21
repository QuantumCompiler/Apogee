#if DEBUG
import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation

/// The Review place's view-model for its previews, over the recorded
/// documents (ADR 0004): nothing starts the binary, and no forge is reached.
@MainActor
enum ReviewPreview {
    static let runner = PreviewRunner(reads: [
        (["review", "targets"], ReviewDocuments.targets),
        (["review", "skills", "list"], ReviewDocuments.skills),
        (["review", "skills", "show", "security"], ReviewDocuments.skillShow),
    ])

    /// No sweep targets yet: review-targets.json, its two targets sliced out.
    static let noTargets = PreviewRunner(reads: [
        (["review", "targets"], #"{"data":[],"object":"list"}"#),
        (["review", "skills", "list"], ReviewDocuments.skills),
    ])

    /// The reviewer's narration, as tests/presentation/review's `finish` has
    /// the CLI say it.
    static let narration: JobUpdate = .busy("[review] hunting with security")

    /// A review run to its end: the narration, then its document.
    static func reviewed(_ document: String) -> PreviewJobs {
        PreviewJobs(said: [narration, .said(document)], finishes: true)
    }

    /// A review still running, its narration said.
    static let reviewing = PreviewJobs(said: [narration])

    /// A review the forge refused: review-no-gh.err, its narration the busy
    /// line and its last line the CLI's last words.
    static let refusedByForge = PreviewJobs(
        said: ReviewDocuments.noGh.split(whereSeparator: \.isNewline).prefix(1).map { .busy(String($0)) },
        finishes: true, status: 1, stderr: ReviewDocuments.noGh)

    /// A dry sweep run to its end: its document.
    static let swept = PreviewJobs(said: [.said(ReviewDocuments.sweep)], finishes: true)

    static func review(_ starter: PreviewJobs = PreviewJobs(), runner: PreviewRunner = runner) -> ReviewViewModel {
        ReviewViewModel(runner: runner, starter: starter)
    }

    /// Connected, then the tests' merge request reviewed through the security
    /// lens, a dry run.
    static func reviewing(_ model: ReviewViewModel) async {
        await model.connect(binary: previewBinary)
        model.form.ref = "acme/widget#12"
        model.form.skills = ["security"]
        model.review()
    }

    /// Connected, then a dry sweep of the tests' target since their date.
    static func sweeping(_ model: ReviewViewModel) async {
        await model.connect(binary: previewBinary)
        model.newSweep()
        model.sweepForm.targets = ["widget"]
        model.sweepForm.since = "2026-10-01"
        model.sweepDry()
    }

    /// Waits, a few seconds at most, for the run going to end.
    static func settled(_ model: ReviewViewModel) async {
        for _ in 0..<250 {
            if !model.isRunning { return }
            try? await Task.sleep(for: .milliseconds(20))
        }
    }

    /// A run's document, as the place reads it.
    static func run(_ document: String) -> ReviewRun? {
        ReadDocument(text: document).flatMap(ReviewRun.init(document:))
    }

    /// The dry sweep's document, as the place reads it.
    static var sweep: ReviewSweep? {
        ReadDocument(text: ReviewDocuments.sweep).flatMap(ReviewSweep.init(document:))
    }
}
#endif
