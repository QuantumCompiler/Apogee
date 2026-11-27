import BusinessJobs
import BusinessReads
import Foundation
import InfrastructureProcess
import PresentationKnowledge
import Testing

/// A run recorded from the built CLI (`tests/business/jobs/Fixtures/<name>.out`,
/// `.err`, `.exit`), replayed on a scripted job: each stdout line said, then
/// its end with its stderr as the tail.
func replay(_ name: String, on job: ScriptedJob) {
    let folder = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("../../business/jobs/Fixtures")
    func read(_ ext: String) -> String {
        (try? String(contentsOf: folder.appendingPathComponent("\(name).\(ext)"), encoding: .utf8)) ?? ""
    }
    for line in read("out").split(separator: "\n") { job.say(.said(String(line))) }
    job.end(status: Int32(read("exit").trimmingCharacters(in: .whitespacesAndNewlines)) ?? -1, stderr: read("err").trimmingCharacters(in: .newlines))
}

/// A recorded run's stderr lines.
func errorLines(_ name: String) -> [String] {
    let file = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("../../business/jobs/Fixtures/\(name).err")
    return ((try? String(contentsOf: file, encoding: .utf8)) ?? "").split(separator: "\n").map(String.init)
}

/// A document recorded beside those runs (`tests/business/jobs/Fixtures`).
func jobsFixture(_ name: String) -> String {
    let file = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("../../business/jobs/Fixtures/\(name)")
    return (try? String(contentsOf: file, encoding: .utf8)) ?? ""
}

/// GUI M22: an ingest's notes reach the collection's page, word for word; the
/// toast stays the outcome line.
@Suite("Collections' job notes", .timeLimit(.minutes(1)))
@MainActor
struct CollectionsNotesTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func collections(_ jobs: [ScriptedJob]) async -> (CollectionsViewModel, ScriptedRunner, ScriptedJobs) {
        let runner = ScriptedRunner()
        runner.reading(["embed", "list"], jobsFixture("embed-list-my-notes.json"))
        runner.reading(["embed", "info", "--", "my notes"], jobsFixture("embed-info-my-notes.json"))
        runner.reading(["embed", "info", "--", "notes"], fixture("collection-info"))
        let starter = ScriptedJobs(jobs)
        let model = CollectionsViewModel(runner: runner, starter: starter)
        await model.connect(binary: Self.binary)
        return (model, runner, starter)
    }

    func ingest(_ model: CollectionsViewModel, into collection: String) {
        model.newIngest()
        model.ingestForm.collection = collection
        model.ingestForm.path = "/Users/someone/docs/a note.txt"
        model.ingest()
    }

    @Test("an ingest the config can't register toasts its outcome, and its page keeps the CLI's note word for word", arguments: [
        ("embed-ingest-unregistrable", "my notes"), ("embed-ingest-no-config", "notes"),
    ])
    func noted(_ name: String, _ collection: String) async {
        let job = ScriptedJob()
        let (model, runner, _) = await collections([job])
        var said: [String] = []
        model.say = { said.append($0) }
        ingest(model, into: collection)
        replay(name, on: job)
        // The toast follows the read-back: wait for it, not only the end.
        #expect(await eventually { !model.isRunning && runner.calls.contains("read embed|info|--|\(collection)") && !said.isEmpty })
        #expect(said == ["1 file(s), 1 chunk(s)"])
        #expect(model.selected == collection)
        #expect(model.doneNotes == CollectionsViewModel.DoneNotes(collection: collection, notes: JobNotes(lines: errorLines(name))))
        #expect(model.doneNotes?.notes.lines == errorLines(name))
    }

    @Test("an ingest that said nothing on stderr leaves no card")
    func quiet() async {
        let job = ScriptedJob()
        let (model, _, _) = await collections([job])
        ingest(model, into: "notes")
        replay("embed-ingest-registered", on: job)
        #expect(await eventually { !model.isRunning && model.selected == "notes" })
        #expect(model.doneNotes == nil)
    }

    @Test("the card goes with Dismiss")
    func dismissed() async {
        let job = ScriptedJob()
        let (model, _, _) = await collections([job])
        ingest(model, into: "my notes")
        replay("embed-ingest-unregistrable", on: job)
        #expect(await eventually { model.doneNotes != nil })
        model.dismissNotes()
        #expect(model.doneNotes == nil)
    }

    @Test("the card goes when the next ingest starts")
    func nextIngest() async {
        let first = ScriptedJob()
        let second = ScriptedJob()
        let (model, _, _) = await collections([first, second])
        ingest(model, into: "my notes")
        replay("embed-ingest-unregistrable", on: first)
        #expect(await eventually { model.doneNotes != nil && !model.isRunning })
        ingest(model, into: "notes")
        #expect(model.isRunning)
        #expect(model.doneNotes == nil)
    }

    @Test("the card belongs to its collection: choosing another drops it, choosing the same keeps it")
    func anotherCollection() async {
        let job = ScriptedJob()
        let (model, _, _) = await collections([job])
        ingest(model, into: "my notes")
        replay("embed-ingest-unregistrable", on: job)
        #expect(await eventually { model.doneNotes != nil && !model.isRunning })
        let rows = model.collections
        await model.select(rows.first { $0.name == "my notes" }!)
        #expect(model.doneNotes != nil, "the same collection chosen again keeps its card")
        await model.select(rows.first { $0.name == "notes" }!)
        #expect(model.doneNotes == nil)
    }

    @Test("a refresh that drops the collection drops its card")
    func refreshDrops() async {
        let job = ScriptedJob()
        let (model, runner, _) = await collections([job])
        ingest(model, into: "my notes")
        replay("embed-ingest-unregistrable", on: job)
        #expect(await eventually { model.doneNotes != nil && !model.isRunning })
        runner.reading(["embed", "list"], fixture("collections-list"))
        await model.refresh()
        #expect(model.selected == nil)
        #expect(model.doneNotes == nil)
    }

    @Test("a failed ingest says its last line in the alert, and draws no card")
    func failed() async {
        let job = ScriptedJob()
        let (model, _, _) = await collections([job])
        ingest(model, into: "notes")
        replay("embed-ingest-no-config-graph", on: job)
        #expect(await eventually { !model.isRunning && model.failure != nil })
        #expect(model.failure == errorLines("embed-ingest-no-config-graph").last)
        #expect(model.doneNotes == nil)
    }
}

/// GUI M22: a capture's or a reindex's notes reach Records, word for word;
/// the toast stays the outcome line.
@Suite("Records' job notes", .timeLimit(.minutes(1)))
@MainActor
struct RecordsNotesTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func records(_ jobs: [ScriptedJob]) async -> RecordsViewModel {
        let runner = ScriptedRunner()
        runner.reading(["knowledge", "list"], fixture("knowledge-list"))
        let model = RecordsViewModel(runner: runner, starter: ScriptedJobs(jobs))
        await model.connect(binary: Self.binary)
        return model
    }

    @Test("a capture the CLI validated, or could not register, toasts its record and keeps the note", arguments: [
        "knowledge-capture-validated", "knowledge-capture-unregistrable",
    ])
    func captured(_ name: String) async {
        let job = ScriptedJob()
        let model = await records([job])
        var said: [String] = []
        model.say = { said.append($0) }
        model.newCapture()
        model.captureForm.text = "Ada: testers kept mistaking the cancel button for back, so we removed it. Shipped in PROJ-42."
        model.capture()
        replay(name, on: job)
        // The toast follows the read-back: wait for it, not only the end.
        #expect(await eventually { !model.isRunning && !said.isEmpty })
        #expect(said.count == 1 && said[0].hasPrefix("Captured kr-"))
        #expect(model.doneNotes?.notes.lines == errorLines(name))
        #expect(model.doneNotes?.title == "What the capture said")
    }

    @Test("a reindex's notes are kept under its own title")
    func reindexed() async {
        let job = ScriptedJob()
        let model = await records([job])
        model.reindex()
        job.say(.said("reindexed 3 record(s)"))
        job.end(stderr: "apogee knowledge: a note about the reindex")
        #expect(await eventually { !model.isRunning && model.doneNotes != nil })
        #expect(model.doneNotes == RecordsViewModel.DoneNotes(title: "What the reindex said", notes: JobNotes(lines: ["apogee knowledge: a note about the reindex"])))
    }

    @Test("the card goes when the next run starts, and with Dismiss")
    func cleared() async {
        let first = ScriptedJob()
        let second = ScriptedJob()
        let model = await records([first, second])
        model.newCapture()
        model.captureForm.text = "Ada: drop it"
        model.capture()
        replay("knowledge-capture-validated", on: first)
        #expect(await eventually { !model.isRunning && model.doneNotes != nil })
        model.reindex()
        #expect(model.isRunning)
        #expect(model.doneNotes == nil, "the next run's start clears the last one's notes")
        second.end(stderr: "apogee knowledge: again")
        #expect(await eventually { !model.isRunning && model.doneNotes != nil })
        model.dismissNotes()
        #expect(model.doneNotes == nil)
    }

    @Test("a capture with nothing on stderr leaves no card, and a failed one says its line in the alert")
    func quietAndFailed() async {
        let quiet = ScriptedJob()
        let failing = ScriptedJob()
        let model = await records([quiet, failing])
        model.reindex()
        quiet.say(.said("reindexed 3 record(s)"))
        quiet.end()
        #expect(await eventually { !model.isRunning })
        #expect(model.doneNotes == nil)
        model.reindex()
        failing.end(status: 1, stderr: "apogee knowledge: a note first\napogee knowledge: reindex failed")
        #expect(await eventually { !model.isRunning && model.failure != nil })
        #expect(model.failure == "apogee knowledge: reindex failed")
        #expect(model.doneNotes == nil)
    }
}
