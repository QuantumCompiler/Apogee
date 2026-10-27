import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationGraph

/// GUI M15, Graph's side: on a binary dated 2026-10-26 or later Dedupe…'s dry
/// run is `graph dedupe --dry-run`'s document -- the admin route's body, byte
/// for byte -- its groups drawn and Merge offered only when it names any; an
/// older binary, or one that refuses the flag, reads the text as before. The
/// merge itself stays a supervised command.
@Suite("Graph: Dedupe… from its document (GUI M15)", .timeLimit(.minutes(1)))
@MainActor
struct DedupeDocumentTests {
    func connected(_ runner: ScriptedRunner, vocabulary: String?, jobs: ScriptedJobs = ScriptedJobs([])) async -> GraphViewModel {
        runner.saying(["__complete", "graph", "stats"], "demo\n")
        runner.reading(["graph", "stats"], fixture("stats"))
        let model = GraphViewModel(runner: runner, starter: jobs)
        model.vocabulary = { vocabulary }
        await model.connect(binary: GraphActsTests.binary)
        return model
    }

    @Test("an M31 binary's dry run is its document: the groups, Merge offered, the merge supervised as the command it always was")
    func groupsFromTheBody() async throws {
        let runner = ScriptedRunner()
        runner.reading(["graph", "dedupe"], printedText("graph-dedupe-dry-run-groups.json"))
        let job = ScriptedJob()
        let jobs = ScriptedJobs([job])
        let model = await connected(runner, vocabulary: "2026-10-26", jobs: jobs)
        await model.planDedupe()
        #expect(runner.calls.last == "read graph dedupe demo --dry-run", "--dry-run asked: with the flag the read never merges")
        #expect(!runner.calls.contains { $0.hasPrefix("run graph dedupe") }, "the document answered: never both")
        guard case .found(let plan) = model.dedupe else {
            Issue.record("not the document's plan: \(String(describing: model.dedupe))")
            return
        }
        #expect(plan.groups.map(\.kept) == ["K8s", "Scheduler"] && plan.mergedNodes == 4)
        #expect(model.dedupeMerges)
        model.confirmDedupe()
        #expect(jobs.starts.first?.arguments == ["graph", "dedupe", "demo"], "the merge: never --output-format, never --dry-run")
        #expect(model.dedupe == .merging)
        for line in printedText("graph-dedupe-merged-groups.txt").split(separator: "\n") { job.say(.said(String(line))) }
        job.end()
        #expect(await eventually { if case .merged = model.dedupe { true } else { false } })
        #expect(model.dedupe == .merged(printedText("graph-dedupe-merged-groups.txt").trimmingCharacters(in: .newlines)))
    }

    @Test("a body naming no group offers nothing to merge, and a Merge pressed anyway runs nothing")
    func nothingInTheBody() async {
        let runner = ScriptedRunner()
        runner.reading(["graph", "dedupe"], printedText("graph-dedupe-dry-run.json"))
        let jobs = ScriptedJobs([])
        let model = await connected(runner, vocabulary: "2026-10-26", jobs: jobs)
        await model.planDedupe()
        guard case .found(let plan) = model.dedupe else {
            Issue.record("not the document's plan: \(String(describing: model.dedupe))")
            return
        }
        #expect(plan.groups.isEmpty && plan.codeEntities == 9)
        #expect(!model.dedupeMerges)
        model.confirmDedupe()
        #expect(jobs.starts.isEmpty)
    }

    @Test("an older binary's dry run is read as today: the command's text")
    func olderBinary() async {
        let runner = ScriptedRunner()
        runner.saying(["graph", "dedupe"], printedText("graph-dedupe-dry-run-groups.txt"))
        let model = await connected(runner, vocabulary: "2026-10-25")
        await model.planDedupe()
        #expect(runner.calls.last == "run graph dedupe demo --dry-run" && !runner.calls.contains { $0.hasPrefix("read graph dedupe") })
        #expect(model.dedupe == .planned(printedText("graph-dedupe-dry-run-groups.txt").trimmingCharacters(in: .newlines)))
        #expect(model.dedupeMerges, "its text names the merges")
    }

    @Test("a binary dated M31 that refuses the flag falls back to the text; one that hasn't said is read as text")
    func refusedFallsBack() async {
        let runner = ScriptedRunner()
        var tail = BoundedTail()
        tail.append(Data(printedText("graph-dedupe-json-refused-older.err").utf8))
        runner.reading(["graph", "dedupe"], .failed(exit: ChildExit(reason: .exited, status: 109), stderrTail: tail))
        runner.saying(["graph", "dedupe"], printedText("graph-dedupe-dry-run.txt"))
        let model = await connected(runner, vocabulary: "2026-10-26")
        await model.planDedupe()
        #expect(runner.calls.suffix(2) == ["read graph dedupe demo --dry-run", "run graph dedupe demo --dry-run"])
        #expect(model.dedupe == .planned(printedText("graph-dedupe-dry-run.txt").trimmingCharacters(in: .newlines)))
        let unsaid = await connected(ScriptedRunner(), vocabulary: nil)
        await unsaid.planDedupe()
        #expect(unsaid.dedupe == .planned(""), "a binary that hasn't said is asked as text")
    }
}
