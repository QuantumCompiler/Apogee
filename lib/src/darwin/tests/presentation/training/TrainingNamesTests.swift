import BusinessReads
import Foundation
import Testing

@testable import PresentationTraining

/// Training's remaining runs with every positional after `--` (GUI M23): a
/// run's student, a promotion's run, a rollback's backend, a pipeline's spec
/// and run, a pull's reference, a prepare's path, an eval's run and a
/// regime -- and a run's stream added before the `--`, never after it.
@Suite("Training names")
@MainActor
struct TrainingNamesTests {
    @Test("each line names its positional after --; the stream goes before it")
    func lines() {
        var run = TrainingViewModel.RunForm()
        run.student = "-x"
        run.dataset = "heat-notes"
        #expect(run.arguments == ["train", "run", "--dataset", "heat-notes", "--", "-x"])
        var promote = TrainingViewModel.PromoteForm()
        promote.run = "-x"
        promote.backend = "helper"
        #expect(promote.arguments == ["train", "promote", "--as", "helper", "--", "-x"])
        #expect(TrainingViewModel.Pending.rollback(backend: "-x").arguments == ["train", "rollback", "--", "-x"])
        #expect(TrainingViewModel.Pending.resumePipeline("-x", pipeline: "nightly").arguments == ["train", "pipeline", "resume", "--pipeline", "nightly", "--", "-x"])
        #expect(Argv.adding(TrainingViewModel.stream, to: TrainingViewModel.Pending.resumePipeline("-x").arguments)
            == ["train", "pipeline", "resume", "--output-format", "stream-json", "--", "-x"])
        #expect(TrainingViewModel.gateArguments("-x", suite: "s.jsonl") == ["train", "eval", "--suite", "s.jsonl", "--", "-x"])
        #expect(TrainingViewModel.regimeArguments("-x") == ["train", "regime", "run", "--", "-x"])
        #expect(TrainingViewModel.prepareArguments("-x") == ["datasets", "prepare", "--", "-x"])
        #expect(TrainingViewModel.specArguments("-x") == ["train", "pipeline", "show", "--", "-x"])
    }

    @Test("every line Training builds with -x, -h or --graph reaches the verb: each recorded from the built CLI, none the parser's refusal")
    func sweep() {
        var lines: [[String]] = []
        for name in NameVerdicts.names {
            var run = TrainingViewModel.RunForm()
            run.student = name
            run.dataset = "heat-notes"
            var promote = TrainingViewModel.PromoteForm()
            promote.run = name
            promote.backend = "mock"
            lines += [
                run.arguments ?? [], promote.arguments ?? [],
                TrainingViewModel.Pending.rollback(backend: name).arguments,
                Argv.adding(TrainingViewModel.stream, to: TrainingViewModel.Pending.resumePipeline(name).arguments),
                Argv.adding(TrainingViewModel.stream, to: TrainingViewModel.Pending.resumePipeline(name, pipeline: "nightly").arguments),
                Argv.adding(TrainingViewModel.stream, to: TrainingViewModel.Pending.runPipeline(name).arguments),
                TrainingViewModel.gateArguments(name), TrainingViewModel.regimeArguments(name), TrainingViewModel.prepareArguments(name),
                Argv.adding(["--output-format", "json"], to: TrainingViewModel.specArguments(name)),
                Argv.adding(["--output-format", "json"], to: Argv.line(["train", "pipeline", "status"], names: [name])),
                Argv.line(["datasets", "pull"], names: [name]),
                Argv.adding(TrainingViewModel.stream, to: Argv.line(["datasets", "pull"], names: [name])),
            ]
        }
        let (missing, refused) = NameVerdicts.judged(lines)
        #expect(missing.isEmpty, "never recorded -- add these rows to tests/presentation/shell/Fixtures/argv-name-verdicts.txt and run scripts/refresh-argv-name-verdicts.sh <the built apogee>:\n\(missing.joined(separator: "\n"))")
        #expect(refused.isEmpty, "the parser refuses:\n\(refused.joined(separator: "\n"))")
    }
}
