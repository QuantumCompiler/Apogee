import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import Testing

@testable import PresentationTraining

/// GUI 4i: the adapter promote (the CLI's 53b) with its quantized-base gate
/// (53c), and datasets pull -- over the documents and refusals recorded from
/// the branch's binary in a sandbox on the mock trainer: a run that passed
/// its gate, `based` holding an adapter version, `q4` one gated on its
/// quantized base, `tuned` a merged one.
@Suite("Adapter promote and datasets pull", .timeLimit(.minutes(1)))
@MainActor
struct AdapterPromoteTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    func training(_ jobs: [ScriptedJob] = []) async -> (TrainingViewModel, ScriptedRunner, ScriptedJobs) {
        let runner = ScriptedRunner()
        runner.reading(["train", "status"], fixture("train-status-passed"))
        runner.reading(["train", "versions"], fixture("train-versions-adapters"))
        runner.reading(["train", "regime", "list"], fixture("train-regimes"))
        runner.reading(["datasets", "kits"], fixture("datasets-kits"))
        runner.reading(["datasets", "list"], fixture("datasets-list"))
        runner.reading(["models", "list"], fixture("models-list-adapters"))
        let starter = ScriptedJobs(jobs)
        let model = TrainingViewModel(runner: runner, starter: starter)
        await model.connect(binary: Self.binary)
        return (model, runner, starter)
    }

    @Test("adapter mode's argv: --adapter, and the gate's --suite and --judge only there; a merge's --target, --quantize and --keep-fused only in a merge")
    func adapterArgv() {
        var form = TrainingViewModel.PromoteForm()
        form.run = "20261026-065637"
        form.backend = "based"
        form.target = "gguf"
        form.quantize = "Q4_K_M"
        form.keepFused = true
        #expect(form.arguments == ["train", "promote", "20261026-065637", "--as", "based", "--target", "gguf", "--quantize", "Q4_K_M", "--keep-fused"])
        form.adapter = true
        #expect(form.arguments == ["train", "promote", "20261026-065637", "--as", "based", "--adapter"],
            "the CLI refuses --quantize and --keep-fused with --adapter: none sent")
        form.backend = "q4"
        form.suite = " /Users/someone/suite.jsonl "
        form.judge = "claude"
        #expect(form.arguments == ["train", "promote", "20261026-065637", "--as", "q4", "--adapter", "--suite", "/Users/someone/suite.jsonl", "--judge", "claude"])
        form.trainer = "mock"
        form.force = true
        #expect(form.arguments == [
            "train", "promote", "20261026-065637", "--as", "q4", "--adapter", "--suite", "/Users/someone/suite.jsonl", "--judge", "claude",
            "--trainer", "mock", "--force",
        ])
        form.adapter = false
        #expect(!(form.arguments ?? []).contains("--suite") && !(form.arguments ?? []).contains("--judge"),
            "the CLI refuses --suite and --judge on a merge: none sent")
        #expect(TrainingViewModel.PromoteForm.trainers == ["auto", "mlx", "peft", "mock"])
    }

    @Test("the sheet launches adapter mode supervised, and says what the backend's ledger holds before the CLI is asked")
    func adapterPromoteRuns() async {
        let job = ScriptedJob()
        let (model, _, starter) = await training([job])
        #expect(model.adapterBackends == ["based", "q4", "tuned"], "the llamacpp entries: they take adapters")
        #expect(model.ledgerWords(for: "based") == "based holds adapter versions (active v1 of 1)")
        #expect(model.ledgerWords(for: "tuned") == "tuned holds merged versions (active v1 of 1)")
        #expect(model.ledgerWords(for: "nowhere") == nil)
        model.newPromotion(of: model.runs[0])
        model.promoteForm.adapter = true
        model.promoteForm.backend = "based"
        #expect(starter.starts.isEmpty)
        model.promote()
        #expect(starter.starts == [["train", "promote", model.runs[0].id, "--as", "based", "--adapter"]])
        for line in fixture("train-promote-adapter", "err").split(separator: "\n") { job.say(.busy(String(line))) }
        #expect(await eventually { model.run?.busy == "[promote] pinning the adapter to the base it applies over: /Users/someone/.apogee/models/tiny/gguf/bbbbbbbbbbbb/tiny-F16.gguf" })
        job.end()
        #expect(await eventually { !model.isRunning })
        #expect(model.failure == nil && model.refusedPromotion == nil)
    }

    @Test("a plain promote into an adapter ledger is refused in the CLI's words, which name --adapter: the sheet comes back in adapter mode, the words above it")
    func refusalNamesAdapter() async throws {
        let job = ScriptedJob()
        let (model, _, starter) = await training([job])
        model.newPromotion(of: model.runs[0])
        model.promoteForm.backend = "based"
        model.promote()
        let refusal = fixture("train-promote-names-adapter", "err").trimmingCharacters(in: .newlines)
        job.end(status: 1, stderr: refusal)
        #expect(await eventually { model.failure != nil })
        #expect(model.failure == refusal, "the CLI's own words")
        #expect(model.refusedPromotion?.form.adapter == false)
        model.changePromotion()
        #expect(model.sheet == .promote && model.failure == nil)
        #expect(model.promoteForm.adapter, "the refusal names --adapter as the way")
        #expect(model.promoteForm.backend == "based" && model.promoteForm.run == model.runs[0].id)
        #expect(model.promoteRefusal == refusal)
        #expect(model.promoteForm.arguments == ["train", "promote", model.runs[0].id, "--as", "based", "--adapter"])
        #expect(starter.starts.count == 1, "nothing runs again until the person promotes")
    }

    @Test("--adapter into a merged ledger: the refusal names the merge as the way, and the sheet comes back without --adapter")
    func refusalNamesMerge() {
        var form = TrainingViewModel.PromoteForm()
        form.run = "r"
        form.backend = "tuned"
        form.adapter = true
        #expect(!form.door(after: fixture("train-promote-holds-merged", "err")).adapter)
        #expect(form.door(after: fixture("train-promote-adapter-unconfigured", "err")).adapter,
            "'promote --adapter into it' names no other mode: the form stays as it was")
        #expect(form.door(after: fixture("train-promote-adapter-quantize", "err")).adapter == false,
            "'drop it, or promote without --adapter'")
    }

    @Test("an adapter onto a quantized base with no suite anywhere: the CLI's refusal names --suite, and the sheet comes back in adapter mode to fill it")
    func refusalNamesSuite() async {
        let job = ScriptedJob()
        let (model, _, _) = await training([job])
        model.newPromotion(of: model.runs[0])
        model.promoteForm.adapter = true
        model.promoteForm.backend = "q4"
        model.promote()
        let refusal = fixture("train-promote-adapter-no-suite", "err").trimmingCharacters(in: .newlines)
        job.end(status: 1, stderr: refusal)
        #expect(await eventually { model.refusedPromotion != nil })
        model.changePromotion()
        #expect(model.promoteForm.adapter && model.promoteRefusal?.contains("pass --suite <path|name>") == true)
        model.dismissFailure()
        #expect(model.refusedPromotion == nil)
    }

    @Test("a promoted version in a line: an adapter one said so, with where its gate scored it")
    func versionWords() throws {
        let ledgers = VersionLedger.list(from: ReadDocument(text: fixture("train-versions-adapters"))!)
        let q4 = try #require(ledgers.first { $0.backend == "q4" })
        #expect(RunsView.words(q4.versions[0], active: q4.active) == "v1 · run 20261026-065638 · adapter · scored on tiny-Q4_K_M.gguf (Q4_K_M) · active")
        let tuned = try #require(ledgers.last)
        #expect(RunsView.words(tuned.versions[0], active: tuned.active) == "v1 · run 20261026-065637 · active")
    }

    // MARK: - datasets pull

    @Test("datasets pull is its verb as typed, launched only from the sheet, supervised on the jobs core")
    func pullArgv() async {
        let job = ScriptedJob()
        let (model, _, starter) = await training([job])
        model.newPull()
        #expect(model.sheet == .pull && model.pullArguments == nil)
        model.pullRef = "  someone/heat-notes@main:data/train.jsonl "
        #expect(model.pullArguments == ["datasets", "pull", "someone/heat-notes@main:data/train.jsonl"])
        #expect(starter.starts.isEmpty, "nothing starts until the person pulls")
        model.pull()
        #expect(model.sheet == nil && model.running == "Pulling")
        #expect(starter.starts == [["datasets", "pull", "someone/heat-notes@main:data/train.jsonl"]])
        job.end()
        #expect(await eventually { !model.isRunning })
    }

    @Test("a finished pull names its directory, and Prepare runs datasets prepare on it, as the CLI says next")
    func pulledOffersPrepare() async throws {
        let pull = ScriptedJob()
        let prepare = ScriptedJob()
        let (model, _, starter) = await training([pull, prepare])
        var said: [String] = []
        model.say = { said.append($0) }
        model.newPull()
        model.pullRef = "someone/heat-notes"
        model.pull()
        // Hand-made from datasets.cpp's own lines: a real pull needs the network.
        for line in fixture("datasets-pull-handmade", "txt").split(separator: "\n", omittingEmptySubsequences: false) {
            pull.say(.said(String(line)))
        }
        pull.end()
        #expect(await eventually { model.pulled != nil })
        let pulled = try #require(model.pulled)
        #expect(pulled.path == "/Users/someone/.apogee/training/datasets/raw/someone--heat-notes")
        #expect(pulled.summary == "2 file(s), 3 MiB")
        #expect(said == ["Pulled: 2 file(s), 3 MiB"])
        model.prepare(pulled.path)
        #expect(starter.starts.last == ["datasets", "prepare", "/Users/someone/.apogee/training/datasets/raw/someone--heat-notes"])
        prepare.end()
        #expect(await eventually { !model.isRunning })
        model.dismissPulled()
        #expect(model.pulled == nil)
    }

    @Test("a refused pull is said in the CLI's words, and offers nothing to prepare")
    func refusedPull() async {
        let job = ScriptedJob()
        let (model, _, _) = await training([job])
        model.newPull()
        model.pullRef = "nonsense"
        model.pull()
        let refusal = fixture("datasets-pull-bad-ref", "err").trimmingCharacters(in: .newlines)
        job.end(status: 1, stderr: refusal)
        #expect(await eventually { model.failure != nil })
        #expect(model.failure == "apogee datasets: 'nonsense' is not a Hugging Face dataset ref (expected owner/name)")
        #expect(model.pulled == nil && model.refusedPromotion == nil)
    }
}
