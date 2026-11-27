import BusinessJobs
import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationModels

private func refusedBeforeTheVerb() -> CommandOutcome {
    var tail = BoundedTail()
    tail.append(Data("name is required\nRun with --help for more information.\n".utf8))
    return .failed(exit: ChildExit(reason: .exited, status: 106), stderrTail: tail)
}

private func refused(_ words: String) -> CommandOutcome {
    var tail = BoundedTail()
    tail.append(Data((words + "\n").utf8))
    return .failed(exit: ChildExit(reason: .exited, status: 1), stderrTail: tail)
}

/// Stored weights' and the Jobs tab's lines with every positional after
/// `--` (GUI M23): a set's handle, a model's name, a backend, a path, a
/// pull's reference -- and `--register-with`, which takes a `--` directly
/// after it as its own end, never last before it. Before `--` the parser
/// read a dash-led one as an option; a typed `-h` in Register… printed the
/// verb's help with exit 0.
@Suite("Store names", .timeLimit(.minutes(1)))
@MainActor
struct StoreNamesTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")

    @Test("Register… with a typed -h runs config add-backend … -- -h: the CLI refuses it by its name rule, and nothing is toasted as registered")
    func registerTypedDashH() async throws {
        let runner = ScriptedRunner()
        runner.commanding(["config", "add-backend", "-h"], .done(stdout: "Add a backend\nUsage: apogee config add-backend [OPTIONS] name\n"))
        runner.commanding(["config", "add-backend", "--type"], refused("apogee config: backend name '-h' is not one both config formats can edit -- a name is letters, digits, '_', '-', '.' and '/', first neither '-' nor '.', and not null; a backend already named otherwise is reached by 'apogee config rename-backend'"))
        let store = StoreViewModel(runner: runner)
        var said: [String] = []
        store.say = { said.append($0) }
        await store.connect(binary: Self.binary)
        var form = StoreViewModel.RegisterForm(handle: "x/gguf/1", type: "llamacpp", path: "/Users/someone/.apogee/models/x/gguf/1/x.gguf", name: "-h", projector: nil, kind: nil, vectorWidth: nil)
        #expect(form.arguments == ["config", "add-backend", "--type", "llamacpp", "--model-path", "/Users/someone/.apogee/models/x/gguf/1/x.gguf", "--", "-h"])
        form.window = "4096"
        store.register = form
        await store.registerIt()
        #expect(runner.calls.contains { $0.hasPrefix("run config add-backend --type llamacpp") && $0.hasSuffix("-- -h") })
        #expect(store.failure?.contains("backend name '-h' is not one both config formats can edit") == true, "failure: \(store.failure ?? "none")")
        #expect(!said.contains { $0.hasPrefix("Registered") }, "said: \(said)")
    }

    @Test("a -x model's rename, plan, unpairing, embedding role and video default each name it after --")
    func storeActs() async {
        let runner = ScriptedRunner()
        runner.commanding(["models", "delete", "-x"], refusedBeforeTheVerb())
        runner.commanding(["models", "delete", "--", "-x"], refused("apogee models: no model '-x' in /Users/someone/.apogee/models"))
        let store = StoreViewModel(runner: runner)
        await store.connect(binary: Self.binary)
        await store.rename("-x", to: "y")
        await store.askToDeleteModel("-x")
        #expect(store.failure == "apogee models: no model '-x' in /Users/someone/.apogee/models (exited with status 1)" || store.failure?.hasPrefix("apogee models: no model '-x'") == true,
                "the plan's refusal in the CLI's words: \(store.failure ?? "none")")
        await store.unpair("-x")
        await store.useForEmbeddings("-x")
        #expect(runner.calls.contains("run models rename -- -x y"))
        #expect(runner.calls.contains("run models delete -- -x"))
        #expect(runner.calls.contains("run config set-backend --draft  -- -x"))
        #expect(runner.calls.contains("run config set-default-embedding -- -x"))
        #expect(StoreViewModel.defaultVideoArguments("-x") == ["config", "set-default-video", "--", "-x"])
    }

    @Test("a pull names its reference after --, --register-with first; Check the files names the set after --, and the reference is read from after --")
    func downloads() throws {
        let starter = ScriptedJobs([ScriptedJob(), ScriptedJob()])
        let model = DownloadsViewModel(starter: starter)
        model.connect(binary: Self.binary)
        model.reference = "-x"
        model.convert = true
        model.chosenLevels = ["Q4_K_M"]
        #expect(model.formArguments == ["models", "pull", "--register-with", "Q4_K_M", "--safetensors", "--", "-x"])
        model.start()
        let download = try #require(model.downloads.first)
        #expect(download.reference == "-x", "the reference the line passed")
        #expect(DownloadsViewModel.repairArguments("-x") == ["models", "repair", "--", "-x"])
    }

    @Test("every line Stored weights and the Jobs tab build with -x, -h or --graph reaches the verb: each recorded from the built CLI, none the parser's refusal")
    func sweep() async {
        var lines: [[String]] = []
        for name in NameVerdicts.names {
            let runner = ScriptedRunner()
            let store = StoreViewModel(runner: runner)
            await store.connect(binary: Self.binary)
            await store.rename(name, to: "renamed")
            await store.askToDeleteModel(name)
            await store.unpair(name)
            await store.useForEmbeddings(name)
            lines += NameVerdicts.lines(ofCalls: runner.calls, separator: " ")
            var convert = ConvertForm(handle: name, mlx: false, bytes: nil)
            convert.levels = ["Q4_K_M"]
            var quantize = QuantizeForm(handle: name, base: "base", bytes: nil)
            quantize.choose("Q8_0")
            lines += [
                StoreViewModel.RegisterForm(handle: "a/gguf/1", type: "llamacpp", path: "/Users/someone/a.gguf", name: name, projector: nil, kind: nil, vectorWidth: nil).arguments ?? [],
                StoreViewModel.PairForm(drafter: "d", draft: "/Users/someone/drafter", said: nil, bases: [.init(backend: name, handle: nil)]).arguments ?? [],
                StoreViewModel.defaultVideoArguments(name), StoreViewModel.renameArguments(name, to: "renamed"),
                ModelsViewModel.deleteArguments(name), ModelsViewModel.deleteArguments(name, andBackends: true),
                convert.arguments, quantize.arguments, DownloadsViewModel.repairArguments(name),
                Argv.adding(["--output-format", "json"], to: Argv.line(["models", "info"], names: [name])),
            ]
            let starter = ScriptedJobs([ScriptedJob(), ScriptedJob()])
            let downloads = DownloadsViewModel(starter: starter)
            downloads.connect(binary: Self.binary)
            downloads.reference = name
            lines.append(downloads.formArguments ?? [])
            downloads.convert = true
            downloads.chosenLevels = ["Q4_K_M"]
            lines.append(downloads.formArguments ?? [])
            downloads.chosenLevels = []
            lines.append(downloads.formArguments ?? [])
        }
        let (missing, refused) = NameVerdicts.judged(lines)
        #expect(missing.isEmpty, "never recorded -- add these rows to tests/presentation/shell/Fixtures/argv-name-verdicts.txt and run scripts/refresh-argv-name-verdicts.sh <the built apogee>:\n\(missing.joined(separator: "\n"))")
        #expect(refused.isEmpty, "the parser refuses:\n\(refused.joined(separator: "\n"))")
    }
}
