import BusinessJobs
import BusinessReads
import Foundation
import Testing

@testable import PresentationKnowledge

/// The documents recorded from the installed binary (tests/business/reads/Fixtures).
func fixture(_ name: String, _ ext: String = "json") -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
        .appendingPathComponent("../../business/reads/Fixtures/\(name).\(ext)")
    return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
}

@Suite("RecordsViewModel", .timeLimit(.minutes(1)))
@MainActor
struct RecordsViewModelTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func records(_ jobs: [ScriptedJob] = []) async -> (RecordsViewModel, ScriptedRunner, ScriptedJobs) {
        let runner = ScriptedRunner()
        runner.reading(["knowledge", "list"], fixture("knowledge-list"))
        runner.answering(["knowledge"], .done(stdout: "done\n"))
        runner.answering(["knowledge", "query"], .done(stdout: fixture("knowledge-query")))
        let starter = ScriptedJobs(jobs)
        let model = RecordsViewModel(runner: runner, starter: starter)
        await model.connect(binary: Self.binary)
        return (model, runner, starter)
    }

    @Test("the records are knowledge list's, newest first; a branch narrows the list")
    func list() async {
        let (model, _, _) = await records()
        #expect(model.listing == .read && model.records.count == 3)
        model.branch = "rejected"
        #expect(model.shown.map(\.discipline) == ["ux"])
        model.branch = "superseded"
        #expect(model.emptyText == "No superseded records.")
    }

    @Test("a question is knowledge query --json over the chosen branch or every one; scores stay on the retriever's scale")
    func ask() async {
        let (model, runner, _) = await records()
        model.question = "why was the cancel button removed"
        await model.search()
        #expect(runner.calls.last == "run knowledge|query|--status||--json|--|why was the cancel button removed")
        #expect(model.query?.retriever == "lexical")
        #expect(model.query.map { RecordsViewModel.score($0.matches[0]) } == "0.596")
        model.branch = "shipped"
        await model.search()
        #expect(runner.calls.last == "run knowledge|query|--status|shipped|--json|--|why was the cancel button removed")
        model.select(model.query!.matches[0].record)
        #expect(model.selectedRecord?.discipline == "ux")
    }

    @Test("link, move and delete are their verbs, each read back; the export writes where the person chose")
    func changes() async {
        let (model, runner, _) = await records()
        let record = model.records[0]
        await model.link(record, to: "  PROJ-7 ")
        #expect(runner.calls.contains("run knowledge|link|\(record.id)|PROJ-7"))
        await model.move(record, to: "superseded")
        #expect(runner.calls.contains("run knowledge|status|\(record.id)|superseded"))
        await model.move(record, to: "shipped")
        #expect(!runner.calls.contains("run knowledge|status|\(record.id)|shipped"), "already shipped: nothing runs")
        model.pendingDelete = record
        await model.confirmDelete()
        #expect(runner.calls.contains("run knowledge|delete|\(record.id)"))
        await model.export(to: URL(fileURLWithPath: "/Users/someone/knowledge.md"), markdown: true, anonymize: true)
        #expect(runner.calls.contains("run knowledge|export|--format|markdown|--anonymize|-o|/Users/someone/knowledge.md"))
        #expect(runner.calls.filter { $0 == "read knowledge|list" }.count >= 5, "every change read back")
    }

    @Test("a capture is the clerk's run, supervised: its command as typed, overrides only where set")
    func capture() async {
        var form = RecordsViewModel.CaptureForm()
        #expect(form.arguments == nil)
        form.text = "Ada: drop it? Bob: yes"
        #expect(form.arguments == ["knowledge", "capture", "--", "Ada: drop it? Bob: yes"])
        form.status = "rejected"
        form.discipline = "ux"
        form.link = "PROJ-9"
        #expect(form.arguments == ["knowledge", "capture", "--status", "rejected", "--discipline", "ux", "--link", "PROJ-9", "--", "Ada: drop it? Bob: yes"])
        let job = ScriptedJob()
        let (model, _, starter) = await records([job])
        var said: [String] = []
        model.say = { said.append($0) }
        model.newCapture()
        model.captureForm.text = "Ada: drop it? Bob: yes"
        model.capture()
        #expect(starter.starts == [["knowledge", "capture", "--", "Ada: drop it? Bob: yes"]] && model.isRunning && !model.capturing)
        job.say(.said("Captured kr-20261021T045944Z-1e2354"))
        job.end()
        #expect(await eventually { !model.isRunning })
        #expect(said == ["Captured kr-20261021T045944Z-1e2354"])
    }
}

@Suite("CollectionsViewModel", .timeLimit(.minutes(1)))
@MainActor
struct CollectionsViewModelTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func collections(_ jobs: [ScriptedJob] = []) async -> (CollectionsViewModel, ScriptedRunner, ScriptedJobs) {
        let runner = ScriptedRunner()
        runner.reading(["embed", "list"], fixture("collections-list"))
        runner.reading(["embed", "info"], fixture("collection-info"))
        runner.answering(["embed", "query"], .done(stdout: fixture("collection-query", "txt")))
        runner.answering(["embed", "delete"], .done(stdout: "removed 1 chunk(s) from 'solar.md'\n"))
        let starter = ScriptedJobs(jobs)
        let model = CollectionsViewModel(runner: runner, starter: starter)
        await model.connect(binary: Self.binary)
        return (model, runner, starter)
    }

    @Test("the collections are embed list's; one chosen reads its info, a text-only index said so")
    func list() async {
        let (model, runner, _) = await collections()
        #expect(model.collections.map(\.name) == ["knowledge", "notes"])
        await model.select(model.collections[1])
        #expect(runner.calls.contains("read embed|info|notes"))
        #expect(model.info?.sources.count == 5)
        #expect(model.info.map(CollectionsViewModel.vectors) == "none -- a text index only")
    }

    @Test("a search is embed query, its lines as printed -- the retriever and the scores the CLI's")
    func search() async {
        let (model, runner, _) = await collections()
        await model.select(model.collections[1])
        model.question = "how does a heat pump work"
        await model.search()
        #expect(runner.calls.contains("run embed|query|notes|--|how does a heat pump work"))
        #expect(model.results?.hasPrefix("0.000  [lexical]  heat-pumps.md#0") == true)
    }

    @Test("a whole collection's delete passes --yes only after the person's word; a source's, as the CLI takes it")
    func deletes() async {
        let (model, runner, _) = await collections()
        model.pendingDelete = .init(collection: "notes", source: "solar.md")
        await model.confirmDelete()
        #expect(runner.calls.contains("run embed|delete|notes|--source|solar.md"))
        model.pendingDelete = .init(collection: "notes", source: nil)
        await model.confirmDelete()
        #expect(runner.calls.contains("run embed|delete|notes|--yes"))
    }

    @Test("an ingest is its command, supervised; the collection is chosen and read when it ends")
    func ingest() async {
        var form = CollectionsViewModel.IngestForm()
        form.collection = "notes"
        form.path = "/Users/someone/notes"
        #expect(form.arguments == ["embed", "ingest", "notes", "/Users/someone/notes"])
        form.retriever = "vector"
        form.graph = true
        #expect(form.arguments == ["embed", "ingest", "notes", "/Users/someone/notes", "--retriever", "vector", "--graph"])
        let job = ScriptedJob()
        let (model, runner, starter) = await collections([job])
        model.newIngest(into: "notes")
        model.ingestForm.path = "/Users/someone/notes"
        model.ingest()
        #expect(starter.starts == [["embed", "ingest", "notes", "/Users/someone/notes"]])
        job.say(.busy("[ingest] solar.md (file 2/3)"))
        job.end()
        #expect(await eventually { !model.isRunning && model.selected == "notes" })
        #expect(runner.calls.contains("read embed|info|notes"))
    }
}
