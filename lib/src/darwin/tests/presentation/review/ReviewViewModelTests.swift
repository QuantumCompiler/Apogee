import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import Testing

@testable import PresentationReview

func fixture(_ name: String, _ ext: String = "json") -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
        .appendingPathComponent("../../business/reads/Fixtures/\(name).\(ext)")
    return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
}

/// The Review place over documents recorded from the installed binary, a
/// fake `gh` answering for the forge (the CLI's own review e2e's way) and a
/// scripted mock reviewer: hermetic, no forge reached.
@Suite("ReviewViewModel", .timeLimit(.minutes(1)))
@MainActor
struct ReviewViewModelTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func model(_ jobs: [ScriptedJob] = []) async -> (ReviewViewModel, ScriptedRunner, ScriptedJobs) {
        let runner = ScriptedRunner()
        runner.reading(["review", "targets"], fixture("review-targets"))
        runner.reading(["review", "skills", "list"], fixture("review-skills"))
        runner.reading(["review", "skills", "show", "security"], fixture("review-skill-show"))
        let starter = ScriptedJobs(jobs)
        let model = ReviewViewModel(runner: runner, starter: starter)
        await model.connect(binary: Self.binary)
        return (model, runner, starter)
    }

    /// Runs the model's job to its end: the CLI's narration, then its document.
    func finish(_ job: ScriptedJob, _ document: String, status: Int32 = 0, stderr: String = "") {
        job.say(.busy("[review] hunting with security"))
        if !document.isEmpty { job.say(.said(document)) }
        job.end(status: status, stderr: stderr)
    }

    @Test("a review is a dry run first: the argv as typed, supervised, its findings read from its document")
    func dryRunFirst() async throws {
        let job = ScriptedJob()
        let (model, _, starter) = await model([job])
        model.form.ref = "acme/widget#12"
        model.form.skills = ["security"]
        model.review()
        #expect(starter.starts == [["review", "mr", "acme/widget#12", "--skills", "security", "--dry-run", "--output-format", "json"]])
        #expect(model.running == "Reviewing acme/widget#12")
        finish(job, fixture("review-run-dry"))
        #expect(await eventually { !model.isRunning })
        let run = try #require(model.run)
        #expect(run.mergeRequest == "github.com/acme/widget#12" && run.title == "Thumbnails" && run.mode == "full")
        #expect(run.findings.count == 1)
        let finding = run.findings[0]
        #expect(finding.file == "app/thumbnails.py" && finding.line == 8 && finding.severity == "high")
        #expect(finding.claim == "The file name from the request reaches a shell command.")
        #expect(finding.suggestedFix == "Pass a list, no shell.")
        #expect(run.posted == .notPosted(reason: "dry run: nothing posted"))
        #expect(!run.isSilent)
    }

    @Test("posting asks first, saying the batch -- and only the person's yes passes --yes, on the same review")
    func postConsented() async throws {
        let dry = ScriptedJob()
        let post = ScriptedJob()
        let (model, _, starter) = await model([dry, post])
        model.form.ref = "acme/widget#12"
        model.review()
        finish(dry, fixture("review-run-dry"))
        #expect(await eventually { model.run != nil && !model.isRunning })
        #expect(starter.starts.count == 1, "nothing posts without asking")
        model.askToPost()
        let run = try #require(model.run)
        #expect(model.pending == .post(run))
        #expect(ReviewViewModel.postWords(run) == "The dry run found 1 finding to post inline. Posting runs the review again with --yes and posts what that run finds, as one batch on github.com/acme/widget#12: its inline comments and a summary.")
        model.dismissPending()
        #expect(starter.starts.count == 1, "cancelled: nothing ran")
        model.askToPost()
        await model.confirm()
        #expect(starter.starts.last == ["review", "mr", "acme/widget#12", "--yes", "--output-format", "json"])
        finish(post, fixture("review-run-posted"))
        #expect(await eventually { !model.isRunning })
        #expect(model.run?.posted == .posted(comments: 1, summary: true, batched: true))
        #expect(starter.starts.filter { $0.contains("--yes") }.count == 1, "--yes appears once, on the consented run")
    }

    @Test("silence is a complete review: shown as one, and nothing to post is offered")
    func silence() async throws {
        let job = ScriptedJob()
        let (model, _, _) = await model([job])
        model.form.ref = "acme/widget#12"
        model.review()
        finish(job, fixture("review-run-silent"))
        #expect(await eventually { !model.isRunning })
        let run = try #require(model.run)
        #expect(run.isSilent)
        #expect(run.posted == .notPosted(reason: "nothing to say: silence is a complete review"))
        model.askToPost()
        #expect(model.pending == nil, "a silent review has nothing to post")
        #expect(model.failure == nil, "silence is no failure")
    }

    @Test("a forge's refusal is said verbatim, its fix with it; no document is made up")
    func forgeRefusal() async {
        let job = ScriptedJob()
        let (model, _, _) = await model([job])
        model.form.ref = "acme/widget#12"
        model.review()
        let refusal = fixture("review-no-gh", "err").trimmingCharacters(in: .newlines)
        finish(job, "", status: 1, stderr: refusal)
        #expect(await eventually { !model.isRunning })
        #expect(model.run == nil)
        #expect(model.failure == "apogee review: gh is not installed, or not on PATH (the GitHub CLI is how Apogee reaches github, under its own login) -- install the GitHub CLI (https://cli.github.com), then: gh auth login")
    }

    @Test("a run refused posting on a pipe is read as the CLI says it; the GUI never sends that run")
    func refusedDocument() throws {
        let run = try #require(ReviewRun(document: ReadDocument(text: fixture("review-run-refused"))!))
        #expect(run.posted == .notPosted(reason: "nobody to ask -- run with --yes to post, or --dry-run to review only"))
        #expect(!run.dryRun)
    }

    @Test("targets: listed with their grants; added and removed as typed, removal asked first")
    func targets() async {
        let (model, runner, _) = await model()
        #expect(model.targets.map(\.name) == ["acme-all", "widget"])
        #expect(model.targets[0].owner == "acme" && model.targets[0].post)
        #expect(model.targets[1].repo == "acme/widget" && !model.targets[1].post && model.targets[1].skills == ["security"])
        model.newTarget()
        model.targetForm.name = "gadget"
        model.targetForm.place = "acme/gadget"
        model.targetForm.skills = ["security", "concurrency"]
        #expect(model.targetForm.arguments == ["review", "targets", "add", "gadget", "--repo", "acme/gadget", "--skills", "concurrency,security"])
        model.targetForm.isOwner = true
        model.targetForm.place = "acme"
        model.targetForm.forge = "gitlab"
        model.targetForm.host = "git.example.com"
        model.targetForm.post = true
        model.targetForm.drafts = true
        model.targetForm.skills = []
        await model.addTarget()
        #expect(runner.calls.contains("run review|targets|add|gadget|--owner|acme|--forge|gitlab|--host|git.example.com|--post|--drafts"))
        model.askToDelete(target: "widget")
        #expect(!runner.calls.contains { $0.hasPrefix("run review|targets|delete") }, "asked first")
        await model.confirm()
        #expect(runner.calls.contains("run review|targets|delete|widget"))
        #expect(runner.calls.filter { $0 == "read review|targets" }.count >= 3, "read back after each change")
    }

    @Test("a sweep is a dry run unless consented; a posting sweep names where it posts, --allow-post only when chosen")
    func sweeps() async throws {
        let dry = ScriptedJob()
        let posting = ScriptedJob()
        let (model, _, starter) = await model([dry, posting])
        model.newSweep()
        model.sweepForm.targets = ["widget"]
        model.sweepForm.since = "2026-10-01"
        model.sweepDry()
        #expect(starter.starts == [["review", "sweep", "--target", "widget", "--since", "2026-10-01", "--dry-run", "--output-format", "json"]])
        dry.say(.said(fixture("review-sweep")))
        dry.end()
        #expect(await eventually { !model.isRunning })
        let sweep = try #require(model.sweep)
        #expect(sweep.dryRun && sweep.reviewed == 1 && sweep.posted == 0 && sweep.errors == 0)
        #expect(sweep.document == "/Users/someone/.apogee/review/sweeps/20261021T060925Z.json")
        let project = try #require(sweep.targets.first?.projects.first)
        #expect(project.project == "github.com/acme/widget" && project.watermarkAfter == "2026-10-18T09:00:00Z" && !project.firstSweep)
        #expect(project.reviewed.first?.findings.count == 1)
        #expect(sweep.targets.first?.grant == "none")
        // Posting: where granted -- widget grants nothing; with --allow-post, it does.
        #expect(model.postingTargets(allowPost: false).isEmpty)
        #expect(model.postingTargets(allowPost: true) == ["widget"])
        model.askToSweep(allowPost: true)
        #expect(starter.starts.count == 1, "asked first")
        await model.confirm()
        #expect(starter.starts.last == ["review", "sweep", "--target", "widget", "--since", "2026-10-01", "--allow-post", "--output-format", "json"])
        posting.end()
    }

    @Test("skills: the seven lenses listed with what they apply to; one shown whole, its prompt read only")
    func skills() async throws {
        let (model, _, _) = await model()
        #expect(model.skills.map(\.name) == ["ai-ml", "back-end", "concurrency", "embedded", "front-end", "general-correctness", "security"])
        #expect(model.skills.first?.extensions == [".ipynb"] && model.skills.first?.everywhere == false)
        await model.show(skill: "security")
        let skill = try #require(model.shownSkill)
        #expect(skill.everywhere && skill.source == "shipped")
        #expect(skill.prompt?.hasPrefix("You are reviewing one change through one lens: security.") == true)
    }
}
