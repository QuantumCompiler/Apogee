import BusinessJobs
import BusinessReads
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationReview

/// Review's lists do what they say (GUI 4h): a failed read of the targets or
/// the skills is said where they are drawn, no apogee is an empty state
/// rather than an alert each visit, and a target can pin its lenses.
@Suite("Review correctness", .timeLimit(.minutes(1)))
@MainActor
struct ReviewCorrectnessTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func model() async -> (ReviewViewModel, ScriptedRunner) {
        let runner = ScriptedRunner()
        runner.reading(["review", "targets"], fixture("review-targets"))
        runner.reading(["review", "skills", "list"], fixture("review-skills"))
        let model = ReviewViewModel(runner: runner, starter: ScriptedJobs([]))
        await model.connect(binary: Self.binary)
        return (model, runner)
    }

    @Test("with no apogee each tab says so, and no visit raises an alert")
    func missingBinaryIsEmpty() async {
        let runner = ScriptedRunner()
        let model = ReviewViewModel(runner: runner, starter: ScriptedJobs([]))
        #expect(model.targetsText == nil, "not told yet: nothing to say")
        await model.connect(binary: nil)
        #expect(model.failure == nil)
        for _ in 0..<3 { await model.appeared() }
        #expect(model.failure == nil, "every visit: no alert")
        let missing = "Apogee isn't installed, so there is nothing to review with."
        #expect(model.targetsText == missing && model.skillsText == missing)
        #expect(runner.calls.isEmpty)
    }

    @Test("a failed targets read is said where the targets are, the ones read before kept")
    func targetsFailureSaid() async {
        let (model, runner) = await model()
        #expect(model.targets.count == 2 && model.targetsText == nil)
        let bad = tail(fixture("review-targets-bad-config", "err").trimmingCharacters(in: .newlines))
        runner.failing(["review", "targets"], .failed(exit: ChildExit(reason: .exited, status: 1), stderrTail: bad))
        await model.appeared()
        #expect(model.targets.count == 2, "the targets already read stay, as Records and Tasks keep theirs")
        #expect(model.targetsText == "The targets could not be read: apogee review: /Users/someone/.apogee/config/config.json: not valid JSON: line 4, column 5: expected ',' or '}' after a member (exited with status 1)")
        #expect(model.skillsText == nil, "the skills read fine")
    }

    @Test("a failed skills read is said where the skills are, the lenses read before kept")
    func skillsFailureSaid() async {
        let (model, runner) = await model()
        let lenses = model.skills.count
        #expect(lenses >= 7 && model.skillsText == nil)
        runner.failing(["review", "skills", "list"], .timedOut(after: .seconds(60), stderrTail: BoundedTail()))
        await model.appeared()
        #expect(model.skills.count == lenses)
        #expect(model.skillsText?.hasPrefix("The review skills could not be read: it did not finish within") == true)
    }

    @Test("a target pins its lenses: the sheet offers the skills read, and the ones ticked are its --skills")
    func targetPinsLenses() async {
        let (model, _) = await model()
        model.newTarget()
        #expect(model.lensChoices == model.skills.map(\.name))
        #expect(ReviewViewModel.lensesLabel(model.targetForm.skills, none: "each change's files") == "Lenses: as each change's files say")
        model.targetForm.name = "gadget"
        model.targetForm.place = "acme/gadget"
        model.targetForm.skills.insert("security")
        model.targetForm.skills.insert("concurrency")
        #expect(ReviewViewModel.lensesLabel(model.targetForm.skills, none: "each change's files") == "Lenses: concurrency, security")
        #expect(model.targetForm.arguments == ["review", "targets", "add", "gadget", "--repo", "acme/gadget", "--skills", "concurrency,security"])
        #expect(ReviewViewModel.lensesLabel(model.form.skills, none: "the change's files") == "Lenses: as the change's files say", "the review's own menu, the same words")
    }
}
