import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationTraining

/// The binary's verdict on `line` (`tests/business/reads/Fixtures/argv-end-
/// of-options-verdicts.txt`, recorded from the built CLI over a sandbox):
/// `help` or `exit <n>: <first line>`; nil when never recorded.
private func recordedVerdict(_ line: [String]) -> String? {
    let wanted = line.joined(separator: " ")
    for row in fixture("argv-end-of-options-verdicts", "txt").split(separator: "\n") {
        let parts: [String] = row.split(separator: "\t", maxSplits: 1).map(String.init)
        if parts.count == 2, parts[1] == wanted { return parts[0] }
    }
    return nil
}

/// Whether a verdict is CLI11's, before the verb ran: exit 106, 109 or help.
private func refusedByTheParse(_ verdict: String?) -> Bool {
    guard let verdict else { return true }
    return verdict == "help" || verdict.hasPrefix("exit 106:") || verdict.hasPrefix("exit 109:")
}

/// A listed dataset reaches the verb as listed (GUI M21). The CLI writes a
/// dataset named `-h` after `--` (no rule refuses it), and `datasets delete
/// -h --yes` prints the verb's help with exit 0 -- which the app toasted as
/// success, the dataset kept. Each name before `--` is scripted to answer as
/// the binary did (`datasets-delete-help.txt`), after it as it does
/// (`datasets-delete-dash-h.txt`), both recorded from the built CLI.
@Suite("Dataset names", .timeLimit(.minutes(1)))
@MainActor
struct DatasetNamesTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func training(_ jobs: [ScriptedJob] = []) async -> (TrainingViewModel, ScriptedRunner, ScriptedJobs) {
        let runner = ScriptedRunner()
        runner.reading(["train", "status"], fixture("train-status"))
        runner.reading(["train", "versions"], #"{"object":"list","data":[]}"#)
        runner.reading(["train", "regime", "list"], fixture("train-regimes"))
        runner.reading(["datasets", "kits"], fixture("datasets-kits"))
        runner.reading(["datasets", "list"], fixture("datasets-list"))
        runner.reading(["models", "list"], fixture("models-stored-list-all"))
        runner.answering(["datasets", "delete", "-h"], .done(stdout: fixture("datasets-delete-help", "txt")))
        runner.answering(["datasets", "delete", "--yes", "--", "-h"], .done(stdout: fixture("datasets-delete-dash-h", "txt")))
        runner.answering(["datasets", "create", "-d"], .done(stdout: fixture("datasets-delete-help", "txt")))
        runner.answering(["datasets", "create", "--from"], .done(stdout: "/Users/someone/.apogee/training/datasets/-d.jsonl\n"))
        let starter = ScriptedJobs(jobs)
        let model = TrainingViewModel(runner: runner, starter: starter)
        await model.connect(binary: Self.binary)
        return (model, runner, starter)
    }

    @Test("deleting a dataset -h runs datasets delete --yes -- -h, and the toast is the CLI's removed., never the help's last line")
    func deleteDashH() async throws {
        let (model, runner, _) = await training()
        var said: [String] = []
        model.say = { said.append($0) }
        model.pending = .deleteDataset("-h")
        let answer = try #require(model.pending)
        model.pending = nil
        await model.confirm(answer)
        #expect(runner.calls.contains("run datasets|delete|--yes|--|-h"))
        #expect(said == ["removed."] && model.failure == nil, "said: \(said)")
        #expect(TrainingViewModel.Pending.deleteDataset("-h").arguments == ["datasets", "delete", "--yes", "--", "-h"])
        #expect(!refusedByTheParse(recordedVerdict(["datasets", "delete", "--yes", "--", "-h"])))
        #expect(recordedVerdict(["datasets", "delete", "-h", "--yes"]) == "help", "the old line printed the verb's help")
    }

    @Test("a new dataset's name goes after --, its template before it; a typed name is trimmed, and selected once made")
    func create() async {
        let (model, runner, _) = await training()
        model.newDataset()
        model.createName = " -d "
        model.createFrom = "empty"
        #expect(model.createArguments == ["datasets", "create", "--from", "empty", "--", "-d"])
        await model.createDataset()
        #expect(runner.calls.contains("run datasets|create|--from|empty|--|-d"))
        #expect(model.selectedDataset == "-d")
        #expect(!refusedByTheParse(recordedVerdict(["datasets", "create", "--from", "empty", "--", "-d"])))
        model.createName = "  "
        #expect(model.createArguments == nil)
    }

    @Test("a synthesis names its dataset after --, the teacher, kit, count and topic before it")
    func synth() {
        var form = TrainingViewModel.SynthForm()
        form.name = "-d"
        form.teacher = "mock"
        form.kit = "k"
        form.count = "3"
        #expect(form.arguments == ["datasets", "synth", "--teacher", "mock", "--kit", "k", "--count", "3", "--", "-d"])
        form.count = ""
        form.topic = "t"
        #expect(form.arguments == ["datasets", "synth", "--teacher", "mock", "--kit", "k", "--topic", "t", "--", "-d"])
        for line in [["datasets", "synth", "--teacher", "mock", "--kit", "k", "--count", "3", "--", "-d"],
                     ["datasets", "synth", "--teacher", "mock", "--kit", "k", "--topic", "t", "--", "-d"]] {
            #expect(!refusedByTheParse(recordedVerdict(line)), "\(line): \(recordedVerdict(line) ?? "never recorded")")
        }
        #expect(Argv.shown(form.arguments ?? []) == "datasets synth --teacher mock --kit k --topic t -- -d")
    }

    @Test("the pipelines' completion keeps every candidate as written: a dash-led name, a leading space, a description dropped")
    func candidates() {
        #expect(TrainingViewModel.candidates("-x\tA pipeline\n sp\nnightly\n") == ["-x", " sp", "nightly"])
    }
}
