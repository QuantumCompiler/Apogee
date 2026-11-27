import BusinessJobs
import Foundation
import InfrastructureProcess
import Testing

/// A recorded run of the built CLI (`apogee 0.2.2`, `b83a533c`), each in a
/// sandbox home written `/Users/someone` (`Fixtures/<name>.out`, `.err`,
/// `.exit`): what it printed on stdout and on stderr, and how it exited.
struct Recording {
    let name: String
    let out: String
    let err: String
    let exit: Int32

    init(_ name: String) throws {
        self.name = name
        let folder = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("Fixtures")
        out = try String(contentsOf: folder.appendingPathComponent("\(name).out"), encoding: .utf8)
        err = try String(contentsOf: folder.appendingPathComponent("\(name).err"), encoding: .utf8)
        let status = try String(contentsOf: folder.appendingPathComponent("\(name).exit"), encoding: .utf8)
        exit = try #require(Int32(status.trimmingCharacters(in: .whitespacesAndNewlines)))
    }

    /// stdout's lines, as a run prints them for people.
    var said: [String] { out.split(separator: "\n", omittingEmptySubsequences: false).map(String.init).dropLastEmpty() }

    /// stderr's lines.
    var errorLines: [String] { err.split(separator: "\n", omittingEmptySubsequences: false).map(String.init).dropLastEmpty() }

    /// stderr as the 8 KiB tail a child keeps of it.
    var tail: BoundedTail {
        var tail = BoundedTail()
        tail.append(Data(err.utf8))
        return tail
    }

    /// The run folded as a screen folds it: each stdout line said, then its
    /// end, the notes read from the tail (the scripted children's route).
    var folded: JobProgress {
        var progress = JobProgress()
        for line in said { progress.apply(.said(line)) }
        progress.apply(.ended(JobEnd(exit: ChildExit(reason: .exited, status: exit), stderrTail: tail, interrupted: false)))
        return progress
    }
}

extension Array where Element == String {
    /// Without the empty end a final newline leaves.
    fileprivate func dropLastEmpty() -> [String] {
        last == "" ? Array(dropLast()) : self
    }
}

/// GUI M22: a finished run's notes -- the CLI's `apogee <verb>: …` lines on
/// stderr -- kept word for word, in order, and nothing else of stderr.
@Suite("Job notes", .timeLimit(.minutes(1)))
struct JobNotesTests {
    @Test("a note is `apogee`, then one or more lowercase words, then `: `; never the verb-less line, never narration", arguments: [
        ("apogee embed: could not register 'my notes' in config", true),
        ("apogee mcp serve: listening", true),
        ("apogee graph: warning: source tree '/Users/someone/src2' is not a directory", true),
        ("apogee knowledge: validated by utility (checker): the record agrees with its source", true),
        ("apogee train: note: the adapter was kept", true),
        ("apogee set-default: x", true),
        ("apogee: /Users/someone/.apogee/config/config.yaml is in the older YAML format and still loads", false),
        ("[graph] parsing src/app.py (1/1)", false),
        ("[graph] extracting note-001.txt (file 1/150, chunk 1/150)", false),
        ("apogee Embed: shouting", false),
        ("apogee embed:no space", false),
        ("  apogee embed: indented", false),
        ("apogee embed ingest notes file", false),
        ("apogee : nothing", false),
        ("apogee  embed: two spaces", false),
        ("apogeeembed: x", false),
        ("", false),
    ])
    func rule(_ line: String, _ note: Bool) {
        #expect(JobNotes.isNote(line) == note, "\(line)")
    }

    @Test("each recording's notes are its house-voice lines, word for word and in order", arguments: [
        ("embed-ingest-unregistrable", [0]),
        ("embed-ingest-no-config", [0]),
        ("graph-build-unregistrable", [1, 2]),
        ("graph-build-source-gone", [0]),
        ("knowledge-capture-validated", [0]),
        ("knowledge-capture-unregistrable", [0]),
        ("embed-ingest-graph-many", [0, 151, 152]),
        ("embed-ingest-no-config-graph", [0, 1]),
        ("embed-ingest-registered", []),
        ("embed-ingest-legacy-yaml", []),
        ("graph-update-source-gone", []),
    ])
    func recordings(_ name: String, _ noteLines: [Int]) throws {
        let recording = try Recording(name)
        let notes = JobNotes(lines: recording.errorLines)
        #expect(notes.lines == noteLines.map { recording.errorLines[$0] })
        #expect(notes.more == 0)
    }

    @Test("the verb-less YAML line rides a YAML home's every run, and is no note")
    func legacyYAML() throws {
        let recording = try Recording("embed-ingest-legacy-yaml")
        #expect(recording.errorLines.count == 1 && recording.errorLines[0].hasPrefix("apogee: "))
        #expect(JobNotes(lines: recording.errorLines).isEmpty)
    }

    @Test("the 8 KiB tail of a long run starts mid-line: that fragment is skipped, and the head note is not in it")
    func cutTail() throws {
        let recording = try Recording("embed-ingest-graph-many")
        #expect(recording.err.utf8.count == 9622)
        #expect(recording.errorLines.count == 153)
        let tail = recording.tail
        #expect(tail.cut)
        #expect(tail.text.hasPrefix("xt (file 21/150, chunk 21/150)"))
        let notes = JobNotes(in: tail)
        #expect(notes.lines == [recording.errorLines[151], recording.errorLines[152]], "two notes, never a fragment")
    }

    @Test("a tail that kept everything keeps its first line")
    func wholeTail() throws {
        let recording = try Recording("graph-build-unregistrable")
        #expect(JobNotes(in: recording.tail).lines == [recording.errorLines[1], recording.errorLines[2]])
        #expect(JobNotes(in: BoundedTail()).isEmpty)
    }

    @Test("the first 20 notes are kept, the rest counted")
    func limit() {
        let lines = (1...25).flatMap { ["apogee embed: note \($0)", "[graph] narration \($0)"] }
        let notes = JobNotes(lines: lines)
        #expect(notes.lines == (1...20).map { "apogee embed: note \($0)" })
        #expect(notes.more == 5)
        #expect(JobNotes.limit == 20)
        #expect(notes.shown == notes.lines + ["… and 5 more notes"], "the card counts the rest")
        #expect(JobNotes(lines: ["apogee embed: one"]).shown == ["apogee embed: one"])
        #expect(JobNotes(lines: (1...21).map { "apogee embed: \($0)" }).shown.last == "… and 1 more note")
    }

    @Test("an ingest that exits 0 with a note is done: its toast line stdout's, its note kept")
    func foldDone() throws {
        for name in ["embed-ingest-unregistrable", "embed-ingest-no-config"] {
            let recording = try Recording(name)
            let progress = recording.folded
            #expect(progress.status == .done)
            #expect(progress.notices == ["1 file(s), 1 chunk(s)"])
            #expect(progress.notes.lines == recording.errorLines)
        }
    }

    @Test("a capture and a build fold their notes, apart from what they said", arguments: [
        "knowledge-capture-validated", "knowledge-capture-unregistrable", "graph-build-unregistrable", "graph-build-source-gone",
    ])
    func foldOthers(_ name: String) throws {
        let recording = try Recording(name)
        let progress = recording.folded
        #expect(progress.status == .done)
        #expect(!progress.notes.isEmpty)
        #expect(progress.notes.lines.allSatisfy { !progress.notices.contains($0) })
    }

    @Test("the guards fold no notes", arguments: ["embed-ingest-registered", "embed-ingest-legacy-yaml", "graph-update-source-gone"])
    func foldGuards(_ name: String) throws {
        let progress = try Recording(name).folded
        #expect(progress.status == .done)
        #expect(progress.notes.isEmpty)
    }

    @Test("a failure keeps stderr's last line as its words, and no notes")
    func foldFailure() throws {
        let recording = try Recording("embed-ingest-no-config-graph")
        #expect(recording.exit == 1)
        let progress = recording.folded
        #expect(progress.status == .failed(recording.errorLines[1]))
        #expect(progress.notes.isEmpty)
    }

    @Test("a stopped run keeps no notes, whatever its stderr said")
    func foldStop() throws {
        let recording = try Recording("embed-ingest-unregistrable")
        var progress = JobProgress()
        progress.apply(.ended(JobEnd(exit: ChildExit(reason: .exited, status: 130), stderrTail: recording.tail, interrupted: true)))
        #expect(progress.status == .stopped)
        #expect(progress.notes.isEmpty)
    }

    @Test("a real child's every note reaches its end, the head one the tail lost included")
    func realChild() async throws {
        let recording = try Recording("embed-ingest-graph-many")
        let fake = try FakeRun()
        let err = fake.directory.appendingPathComponent("run.err")
        try recording.err.write(to: err, atomically: true, encoding: .utf8)
        let binary = try fake.binary("echo '150 file(s), 150 chunk(s)'; cat '\(err.path)' >&2; exit 0")
        let job = JobStarter(spawner: ProcessSpawner(), folder: fake.directory).startJob(
            binary: binary, arguments: ["embed", "ingest", "--graph", "--", "fresh one", "/Users/someone/docs/many"], streaming: false)
        let updates = await fake.updates(job)
        guard case .ended(let end) = updates.last else {
            Issue.record("no end")
            return
        }
        #expect(end.succeeded)
        #expect(end.notes.lines == [recording.errorLines[0], recording.errorLines[151], recording.errorLines[152]])
        #expect(end.stderrTail.cut)
        #expect(!end.stderrTail.text.contains(recording.errorLines[0]))
        var progress = JobProgress()
        for update in updates { progress.apply(update) }
        #expect(progress.notes == end.notes)
    }

    @Test("a child with no framed stderr has its notes read from its tail")
    func scriptedChild() {
        var tail = BoundedTail()
        tail.append(Data("[graph] parsing a (1/1)\napogee graph: warning: gone\n".utf8))
        let end = JobEnd(exit: ChildExit(reason: .exited, status: 0), stderrTail: tail, interrupted: false)
        #expect(end.notes.lines == ["apogee graph: warning: gone"])
        let given = JobEnd(exit: ChildExit(reason: .exited, status: 0), stderrTail: tail, interrupted: false, notes: JobNotes())
        #expect(given.notes.isEmpty, "notes handed in are the end's, never re-read")
    }
}
