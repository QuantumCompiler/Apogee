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

    /// GUI 6h: each click starts its own read, so a slow one can answer
    /// after the next click. The documents are the branch's binary's own
    /// (`review skills show security | ai-ml --output-format json`).
    @Test("a slow skill reply is dropped: the detail is the highlighted skill's, or nothing while it is read")
    func slowSkillReplyDropped() async throws {
        let (model, runner) = await model()
        runner.reading(["review", "skills", "show", "security"], fixture("review-skill-show"))
        runner.reading(["review", "skills", "show", "ai-ml"], fixture("review-skill-show-ai-ml"))
        await model.show(skill: "ai-ml")
        #expect(model.shownSkill?.name == "ai-ml")
        let slow = runner.holding(["review", "skills", "show", "security"])
        let security = Task { await model.show(skill: "security") }
        #expect(await eventually { slow.arrived == 1 })
        #expect(model.selectedSkill == "security" && model.shownSkill == nil, "ai-ml's detail goes at the click on security")
        let answered = runner.holding(["review", "skills", "show", "ai-ml"])
        let aiML = Task { await model.show(skill: "ai-ml") }
        #expect(await eventually { answered.arrived == 1 })
        #expect(model.selectedSkill == "ai-ml")
        #expect(model.shownSkill == nil, "between the click and ai-ml's reply: nothing, never security's")
        answered.open()
        await aiML.value
        #expect(model.shownSkill?.name == "ai-ml")
        slow.open()
        await security.value
        #expect(model.selectedSkill == "ai-ml")
        #expect(model.shownSkill?.name == "ai-ml", "security's late reply never lands under ai-ml")
        #expect(model.shownSkill?.description.hasPrefix("Machine-learning code") == true)
        #expect(model.failure == nil)
    }

    @Test("a slow skill read's failure is dropped once another skill is chosen; its own still says it")
    func slowSkillFailureDropped() async throws {
        let (model, runner) = await model()
        runner.reading(["review", "skills", "show", "ai-ml"], fixture("review-skill-show-ai-ml"))
        runner.failing(["review", "skills", "show", "security"], .timedOut(after: .seconds(60), stderrTail: BoundedTail()))
        let slow = runner.holding(["review", "skills", "show", "security"])
        let security = Task { await model.show(skill: "security") }
        #expect(await eventually { slow.arrived == 1 })
        await model.show(skill: "ai-ml")
        #expect(model.shownSkill?.name == "ai-ml")
        slow.open()
        await security.value
        #expect(model.failure == nil, "security's failure is not raised over ai-ml")
        #expect(model.shownSkill?.name == "ai-ml")
        await model.show(skill: "security")
        #expect(model.shownSkill == nil && model.failure?.hasPrefix("it did not finish within") == true, "asked while chosen, it is said")
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
