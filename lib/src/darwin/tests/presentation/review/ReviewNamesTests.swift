import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationReview

private func recorded(_ name: String) -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("Fixtures/\(name)")
    return ((try? String(contentsOf: url, encoding: .utf8)) ?? "").trimmingCharacters(in: .newlines)
}

private func failed(_ status: Int32, _ stderr: String) -> CommandOutcome {
    .failed(exit: ChildExit(reason: .exited, status: status), stderrTail: tail(stderr))
}

/// CLI11's refusal of a dash-led word before `--`: the verb never ran.
private let required = "name is required\nRun with --help for more information."

/// Review's refs and names reach their verbs as words (GUI M23). A config
/// keeps review targets named `-x`, `-h` and `--graph`, hand-written
/// (`Fixtures/review-targets-dashes.json`, recorded from the built CLI over
/// such a sandbox), and a person may type a ref or a target name that begins
/// with a dash: before `--` the parser refused each one. Each line before
/// `--` is scripted to answer CLI11's refusal, and after `--` the binary's
/// recorded answer.
@Suite("Review names", .timeLimit(.minutes(1)))
@MainActor
struct ReviewNamesTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func model(_ jobs: [ScriptedJob] = []) async -> (ReviewViewModel, ScriptedRunner, ScriptedJobs) {
        let runner = ScriptedRunner()
        runner.reading(["review", "targets"], recorded("review-targets-dashes.json"))
        runner.reading(["review", "skills", "list"], fixture("review-skills"))
        for name in NameVerdicts.names {
            runner.answering(["review", "targets", "delete", name], failed(106, required))
            runner.answering(["review", "targets", "add", name], failed(106, required))
            runner.failing(["review", "skills", "show", name], .failed(exit: ChildExit(reason: .exited, status: 106), stderrTail: tail(required)))
        }
        runner.answering(["review", "targets", "add", "--repo", "o/r", "--", "-h"], failed(1, recorded("review-targets-add-dash-h.err")))
        runner.answering(["review", "targets", "delete", "--", "-x"], failed(1, recorded("review-targets-delete-dash-x.err")))
        runner.failing(["review", "skills", "show", "--", "-x"], .failed(exit: ChildExit(reason: .exited, status: 1), stderrTail: tail(recorded("review-skills-show-dash-x.err"))))
        let starter = ScriptedJobs(jobs)
        let model = ReviewViewModel(runner: runner, starter: starter)
        await model.connect(binary: Self.binary)
        return (model, runner, starter)
    }

    @Test("a typed -x ref runs review mr --dry-run --output-format json -- -x, and the CLI says what a merge request is")
    func reviewDashRef() async {
        let job = ScriptedJob()
        let (model, _, starter) = await model([job])
        model.form.ref = "-x"
        model.form.skills = ["security"]
        model.review()
        #expect(starter.starts == [["review", "mr", "--skills", "security", "--dry-run", "--output-format", "json", "--", "-x"]],
                "--skills takes a -- directly after it as its own end: never last before it")
        job.end(status: 1, stderr: recorded("review-mr-dash-x.err"))
        #expect(await eventually { model.failure != nil })
        #expect(model.failure?.contains("'-x' is not a merge request") == true, "failure: \(model.failure ?? "none")")
    }

    @Test("a typed -h target is added after --, --repo last after --skills: the CLI refuses it by its name rule")
    func addDashTarget() async {
        let (model, runner, _) = await model()
        model.targetForm.name = "-h"
        model.targetForm.place = "o/r"
        await model.addTarget()
        #expect(runner.calls.contains("run review|targets|add|--repo|o/r|--|-h"))
        #expect(model.failure?.contains("review target name '-h' is not one both config formats can edit") == true, "failure: \(model.failure ?? "none")")
        model.targetForm.skills = ["security"]
        model.targetForm.forge = "gitlab"
        #expect(model.targetForm.arguments == ["review", "targets", "add", "--skills", "security", "--forge", "gitlab", "--repo", "o/r", "--", "-h"])
    }

    @Test("a listed -x target's delete and a -x skill's show reach their verbs, which answer in their own words")
    func listedTargetAndSkill() async throws {
        let (model, runner, _) = await model()
        #expect(model.targets.map(\.name).contains("-x"), "targets: \(model.targets.map(\.name))")
        await model.confirm(.deleteTarget("-x"))
        #expect(runner.calls.contains("run review|targets|delete|--|-x"))
        #expect(model.failure?.contains("review target '-x' is not one both config formats can edit") == true, "failure: \(model.failure ?? "none")")
        #expect(ReviewViewModel.deleteArguments("-x") == ["review", "targets", "delete", "--", "-x"])
        await model.show(skill: "-x")
        #expect(runner.calls.contains("read review|skills|show|--|-x"))
        #expect(model.failure?.contains("no review skill named '-x'") == true, "failure: \(model.failure ?? "none")")
    }

    @Test("every line Review builds with -x, -h or --graph reaches the verb: each recorded from the built CLI, none the parser's refusal")
    func sweep() async {
        var lines: [[String]] = []
        for name in NameVerdicts.names {
            let jobs = (0..<4).map { _ in ScriptedJob() }
            let (model, runner, starter) = await model(jobs)
            model.form.ref = name
            model.review()
            jobs[0].end(status: 1)
            #expect(await eventually { !model.isRunning })
            model.form.skills = ["security"]
            model.form.model = "mock"
            model.review()
            jobs[1].end(status: 1)
            #expect(await eventually { !model.isRunning })
            model.targetForm = .init()
            model.targetForm.name = name
            model.targetForm.place = "o/r"
            await model.addTarget()
            model.targetForm = .init()
            model.targetForm.name = name
            model.targetForm.isOwner = true
            model.targetForm.place = "o"
            model.targetForm.skills = ["security"]
            model.targetForm.forge = "gitlab"
            model.targetForm.host = "h"
            model.targetForm.post = true
            model.targetForm.drafts = true
            model.targetForm.model = "mock"
            await model.addTarget()
            await model.confirm(.deleteTarget(name))
            await model.show(skill: name)
            model.sweepForm.targets = [name]
            model.sweepDry()
            jobs[2].end(status: 1)
            #expect(await eventually { !model.isRunning })
            lines += NameVerdicts.lines(ofCalls: runner.calls) + starter.starts
        }
        let (missing, refused) = NameVerdicts.judged(lines)
        #expect(missing.isEmpty, "never recorded -- add these rows to tests/presentation/shell/Fixtures/argv-name-verdicts.txt and run scripts/refresh-argv-name-verdicts.sh <the built apogee>:\n\(missing.joined(separator: "\n"))")
        #expect(refused.isEmpty, "the parser refuses:\n\(refused.joined(separator: "\n"))")
    }
}
