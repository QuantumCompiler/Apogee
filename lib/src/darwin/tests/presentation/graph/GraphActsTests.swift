import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationGraph

/// A text the branch's binary printed over a sandbox home, the home written
/// `/Users/someone` (tests/business/reads/Fixtures); `-handmade` ones are
/// held to `graph.cpp`'s own lines and said so.
func printedText(_ name: String) -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
        .appendingPathComponent("../../business/reads/Fixtures/\(name)")
    return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
}

/// The binary's refusal: exit 1, its recorded stderr the tail.
func refusedWith(_ name: String) -> CommandOutcome {
    var tail = BoundedTail()
    tail.append(Data(printedText(name).utf8))
    return .failed(exit: ChildExit(reason: .exited, status: 1), stderrTail: tail)
}

/// GUI 4k, the fourth gap: a graph exported and its report saved to the
/// file the person picked, deduped dry run first, and deleted -- each asked,
/// each through the binary's own verb, read back.
@Suite("Graph: export, report, dedupe, delete (GUI 4k)", .timeLimit(.minutes(1)))
@MainActor
struct GraphActsTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func connected(_ runner: ScriptedRunner, jobs: ScriptedJobs = ScriptedJobs([])) async -> GraphViewModel {
        runner.saying(["__complete", "graph", "stats"], "demo\n")
        runner.reading(["graph", "stats"], fixture("stats"))
        let model = GraphViewModel(runner: runner, starter: jobs)
        await model.connect(binary: Self.binary)
        return model
    }

    @Test("each export is graph export <format> --graph <graph> -o <the picked file>, the CLI's sentence said; the panel offers the CLI's own name")
    func exports() async {
        let runner = ScriptedRunner()
        runner.saying(["graph", "export", "html"], printedText("graph-export-html.txt"))
        runner.saying(["graph", "export", "graphml"], printedText("graph-export-graphml.txt"))
        runner.saying(["graph", "export", "mermaid"], printedText("graph-export-mermaid.txt"))
        var said: [String] = []
        let model = await connected(runner)
        model.say = { said.append($0) }
        #expect(model.exportName(.html) == "demo.html" && model.exportName(.graphml) == "demo.graphml" && model.exportName(.mermaid) == "demo.mmd")
        for format in GraphViewModel.ExportFormat.allCases {
            await model.export(format, to: URL(fileURLWithPath: "/Users/someone/Documents/demo.\(format.fileExtension)"))
        }
        #expect(runner.calls.filter { $0.hasPrefix("run graph export") } == [
            "run graph export html --graph demo -o /Users/someone/Documents/demo.html",
            "run graph export graphml --graph demo -o /Users/someone/Documents/demo.graphml",
            "run graph export mermaid --graph demo -o /Users/someone/Documents/demo.mmd",
        ])
        #expect(said == [
            "Exported graph \"demo\" to /Users/someone/Documents/demo.html: 9 entities and 12 relations drawn, 41 KB.",
            "Exported graph \"demo\" to /Users/someone/Documents/demo.graphml: every entity (10) and relation (13), uncapped, 8 KB.",
            "Exported graph \"demo\" to /Users/someone/Documents/demo.mmd: the call flow of 3 functions and 2 calls.",
        ])
        #expect(model.problem == nil)
    }

    @Test("the report is graph report --graph <graph> --out <the picked file>")
    func reportSaved() async {
        let runner = ScriptedRunner()
        runner.saying(["graph", "report", "--graph"], printedText("graph-report-out.txt"))
        var said: [String] = []
        let model = await connected(runner)
        model.say = { said.append($0) }
        #expect(model.reportName == "demo-report.md")
        await model.saveReport(to: URL(fileURLWithPath: "/Users/someone/Documents/demo-report.md"))
        #expect(runner.calls.contains("run graph report --graph demo --out /Users/someone/Documents/demo-report.md"))
        #expect(said == ["Wrote the report on graph \"demo\" to /Users/someone/Documents/demo-report.md (3 KB)."])
    }

    @Test("an export the CLI refuses is said in its words, apart from the graph's reads")
    func exportRefused() async {
        let runner = ScriptedRunner()
        runner.refusing(["graph", "export"], refusedWith("graph-export-deleted.err"))
        let model = await connected(runner)
        await model.export(.html, to: URL(fileURLWithPath: "/Users/someone/Documents/demo.html"))
        #expect(model.problem?.hasPrefix("apogee graph: graph 'spare' has not been built yet") == true)
        #expect(model.emptyText != model.problem, "the place still draws the graph")
    }

    @Test("dedupe's dry run comes first and changes nothing; with nothing to merge, nothing is offered")
    func dedupeNothing() async {
        let runner = ScriptedRunner()
        runner.saying(["graph", "dedupe"], printedText("graph-dedupe-dry-run.txt"))
        let jobs = ScriptedJobs([])
        let model = await connected(runner, jobs: jobs)
        await model.planDedupe()
        #expect(runner.calls.last == "run graph dedupe demo --dry-run")
        #expect(model.dedupe == .planned(printedText("graph-dedupe-dry-run.txt").trimmingCharacters(in: .newlines)))
        #expect(!model.dedupeMerges)
        model.confirmDedupe()
        #expect(jobs.starts.isEmpty, "nothing to merge: nothing runs")
        model.dismissDedupe()
        #expect(model.dedupe == nil)
    }

    @Test("a dry run naming merges offers them; the merge runs supervised on the person's word, read back, its lines kept")
    func dedupeMerges() async {
        let runner = ScriptedRunner()
        runner.saying(["graph", "dedupe"], printedText("graph-dedupe-dry-run-merges-handmade.txt"))
        let job = ScriptedJob()
        let jobs = ScriptedJobs([job])
        let model = await connected(runner, jobs: jobs)
        await model.planDedupe()
        #expect(model.dedupeMerges)
        #expect(jobs.starts.isEmpty, "the dry run alone, so far")
        model.confirmDedupe()
        #expect(jobs.starts.first?.arguments == ["graph", "dedupe", "demo"])
        #expect(model.dedupe == .merging)
        for line in printedText("graph-dedupe-merged-handmade.txt").split(separator: "\n") { job.say(.said(String(line))) }
        job.end()
        #expect(await eventually { if case .merged = model.dedupe { true } else { false } })
        #expect(model.dedupe == .merged(printedText("graph-dedupe-merged-handmade.txt").trimmingCharacters(in: .newlines)))
        #expect(runner.calls.filter { $0 == "read graph stats demo" }.count == 2, "read back")
    }

    @Test("delete asks first, runs graph delete <graph>, reads back -- an emptied named graph says Rebuild makes it again")
    func deleteAsked() async {
        let runner = ScriptedRunner()
        runner.saying(["graph", "delete"], printedText("graph-delete.txt"))
        var said: [String] = []
        let model = await connected(runner)
        model.say = { said.append($0) }
        model.question = "runner fetch"
        runner.reading(["graph", "query"], fixture("query"))
        await model.search()
        model.askToDelete()
        #expect(model.pendingDelete == "demo")
        #expect(!runner.calls.contains { $0.hasPrefix("run graph delete") })
        runner.reading(["graph", "stats"], #"{"built":false,"collections":[],"graph":"demo","kind":"named","object":"graph.stats","sources":["/Users/someone/code/ingest"]}"#)
        await model.confirmDelete()
        #expect(runner.calls.contains("run graph delete demo"))
        #expect(model.result == nil && model.pendingDelete == nil)
        #expect(said.first?.hasPrefix("Deleted graph \"spare\": 10 node(s), 13 edge(s)") == true, "the CLI's own sentence")
        #expect(model.stats?.built == false)
        #expect(model.emptyText == "demo has no graph built. Rebuild parses its sources again -- no model, no key, no network.")
    }
}
