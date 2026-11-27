import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationModels

private func recorded(_ name: String, _ ext: String = "json") -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
        .appendingPathComponent("../../business/reads/Fixtures/\(name).\(ext)")
    return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
}

/// The binary's verdict on `line` (`argv-end-of-options-verdicts.txt`,
/// recorded from the built CLI over a sandbox): `help` or `exit <n>: <first
/// line>`; nil when never recorded.
private func recordedVerdict(_ line: [String]) -> String? {
    let wanted = line.joined(separator: " ")
    for row in recorded("argv-end-of-options-verdicts", "txt").split(separator: "\n") {
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

private func failed(_ status: Int32, _ stderr: String) -> CommandOutcome {
    var tail = BoundedTail()
    tail.append(Data((stderr + "\n").utf8))
    return .failed(exit: ChildExit(reason: .exited, status: status), stderrTail: tail)
}

/// A backend row reaches its verbs by its name (GUI M21). An older config
/// keeps a backend named `-x` (`models-list-dash-x.json`, recorded from the
/// built CLI over a sandbox holding a hand-written `-x` entry): the CLI's
/// edits refuse it and point to `config rename-backend`, which before `--`
/// exited 106. Each name before `--` is scripted to answer CLI11's refusal.
@Suite("Backend names", .timeLimit(.minutes(1)))
@MainActor
struct BackendNamesTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
    static let required = "Run with --help for more information."

    func models() async -> (ModelsViewModel, ScriptedRunner) {
        let runner = ScriptedRunner()
        runner.reading(["models", "list"], recorded("models-list-dash-x"))
        runner.reading(["models", "status"], #"{"fields":[{"field":"chat","value":"mock"},{"field":"vision","value":"(unset -- the chat's own backend)"}]}"#)
        runner.commanding(["__complete", "models", "rename"], .done(stdout: ""))
        runner.reading(["models", "info", "-x"], outcome: .failed(exit: ChildExit(reason: .exited, status: 106), stderrTail: {
            var tail = BoundedTail()
            tail.append(Data("name is required\n\(Self.required)\n".utf8))
            return tail
        }()))
        runner.reading(["models", "info", "--", "-x"], recorded("models-info-dash-x"))
        for verb in [["config", "rename-backend", "-x"], ["config", "delete-backend", "-x"], ["config", "set-default", "-x"], ["config", "set-backend", "-x"]] {
            runner.commanding(verb, failed(106, "name is required\n\(Self.required)"))
        }
        runner.commanding(["config", "rename-backend", "--", "-x"], .done(stdout: recorded("config-rename-backend-dash-x", "txt")))
        runner.commanding(["config", "delete-backend", "--", "-x"], failed(1, recorded("config-delete-backend-dash-x", "err").trimmingCharacters(in: .newlines)))
        let model = ModelsViewModel(runner: runner)
        await model.connect(binary: Self.binary)
        return (model, runner)
    }

    func dashX(_ model: ModelsViewModel) throws -> ModelRow {
        try #require(model.rows.first { $0.backend == "-x" }, "rows: \(model.rows.map(\.backend))")
    }

    @Test("Rename the backend… on -x runs config rename-backend -- -x <new>: the CLI's own way out reaches the verb")
    func renameBackend() async throws {
        let (model, runner) = await models()
        var said: [String] = []
        model.say = { said.append($0) }
        model.askToRename(try dashX(model), .backend)
        let renaming = try #require(model.renaming)
        #expect(renaming.arguments(to: "fixed") == ["config", "rename-backend", "--", "-x", "fixed"])
        await model.rename(renaming, to: "fixed")
        #expect(runner.calls.contains("run config rename-backend -- -x fixed"))
        #expect(model.failure == nil && said == ["Renamed to fixed"], "failure: \(model.failure ?? "none")")
        #expect(recordedVerdict(["config", "rename-backend", "--", "-x", "fixed"])?.hasPrefix("exit 0: renamed backend '-x' to 'fixed'") == true)
        #expect(recordedVerdict(["config", "rename-backend", "-x", "fixed"]) == "exit 106: new-name is required", "the old line")
    }

    @Test("selecting -x reads models info -- -x, and its details draw")
    func info() async throws {
        let (model, runner) = await models()
        await model.select(try dashX(model))
        #expect(runner.calls.contains("read models info -- -x"))
        #expect(model.info?.field("backend") == "-x" && model.infoFailure == nil, "failure: \(model.infoFailure ?? "none")")
        #expect(!refusedByTheParse(recordedVerdict(["models", "info", "--output-format", "json", "--", "-x"])))
    }

    @Test("a CLI refusal after -- is said in the CLI's words, never the parser's")
    func refusalInTheCLIsWords() async throws {
        let (model, runner) = await models()
        await model.confirmRemove("-x")
        #expect(runner.calls.contains("run config delete-backend -- -x"))
        let failure = try #require(model.failure)
        #expect(failure.hasPrefix("apogee config: backend '-x' is not one both config formats can edit"), "\(failure)")
        #expect(failure.contains("rename it with 'apogee config rename-backend'") && !failure.contains(Self.required))
    }

    @Test("delete, unpair and a role each put the backend after --; the role's backend through the builder")
    func lines() async throws {
        let (model, runner) = await models()
        let row = try dashX(model)
        model.pendingDelete = .init(row: row, plan: DeletePlan(said: "will delete:\n  /Users/someone/.apogee/models/x\n"))
        await model.confirmDelete(try #require(model.pendingDelete), andBackends: false)
        #expect(runner.calls.contains("run models delete --yes -- -x"))
        await model.unpair(row)
        #expect(runner.calls.contains("run config set-backend --draft  -- -x"), "an empty --draft clears it")
        let chat = try #require(model.roles.first { $0.role == "chat" })
        await model.assign(chat, to: "-x")
        #expect(runner.calls.contains("run config set-default -- -x"))
        let roles = RoleAssignment.list(from: ReadDocument(text: #"{"fields":[{"field":"vision","value":"(unset)"}]}"#)!)
        #expect(roles.first?.arguments(pointingAt: "-x") == ["config", "set-default-vision", "--", "-x"])
        for line in [["models", "delete", "--yes", "--", "-x"], ["models", "delete", "--yes", "--and-backends", "--", "-x"],
                     ["config", "set-backend", "--draft", "\"\"", "--", "-x"], ["config", "set-default", "--", "-x"],
                     ["config", "set-default-vision", "--", "-x"], ["config", "delete-backend", "--", "-x"]] {
            #expect(!refusedByTheParse(recordedVerdict(line)), "\(line): \(recordedVerdict(line) ?? "never recorded")")
        }
    }

    @Test("the delete and remove dialogs show the lines they run, as a person types them")
    func dialogLines() {
        #expect(ModelsViewModel.deleteArguments("-x") == ["models", "delete", "--yes", "--", "-x"])
        #expect(ModelsViewModel.deleteArguments("-x", andBackends: true) == ["models", "delete", "--yes", "--and-backends", "--", "-x"])
        #expect(ModelsViewModel.removeArguments("-x") == ["config", "delete-backend", "--", "-x"])
        #expect(Argv.shown(ModelsViewModel.deleteArguments("q4")) == "models delete --yes q4")
        #expect(Argv.shown(ModelsViewModel.removeArguments("-x")) == "config delete-backend -- -x")
        #expect(ModelsViewModel.unpairArguments("-x") == ["config", "set-backend", "--draft", "", "--", "-x"])
        #expect(Argv.shown(ModelsViewModel.unpairArguments("e4b")) == "config set-backend --draft \"\" e4b", "Unpair's tooltip")
    }

    @Test("the adapters edit is --adapters <value> -- <backend>, and the sheet's line names the backend once")
    func adapters() {
        var form = AdaptersForm(backend: "-x", backendType: "llamacpp", adapters: [])
        #expect(form.arguments == ["config", "set-backend", "--adapters", "", "--", "-x"])
        #expect(form.shown == "apogee config set-backend --adapters \"\" -- -x")
        form.add("/Users/someone/a.gguf")
        #expect(form.shown == "apogee config set-backend --adapters /Users/someone/a.gguf -- -x")
        #expect(form.shown.components(separatedBy: "-x").count == 2, "the backend once")
        #expect(AdaptersForm(backend: "q4", backendType: "llamacpp", adapters: []).shown == "apogee config set-backend --adapters \"\" q4")
        for line in [["config", "set-backend", "--adapters", "\"\"", "--", "-x"], ["config", "set-backend", "--adapters", "/Users/someone/a.gguf", "--", "-x"]] {
            #expect(!refusedByTheParse(recordedVerdict(line)), "\(line): \(recordedVerdict(line) ?? "never recorded")")
        }
    }

    @Test("a drafter paired from the base: --draft <drafter> -- <base>")
    func pairing() {
        let form = DrafterPairing(backend: "-x", offers: [.init(handle: "D/mlx/1", path: "/Users/someone/drafter", said: nil)])
        #expect(form.arguments == ["config", "set-backend", "--draft", "/Users/someone/drafter", "--", "-x"])
        #expect(!refusedByTheParse(recordedVerdict(form.arguments ?? [])))
    }
}
