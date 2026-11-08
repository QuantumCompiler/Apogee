import AppKit
import BusinessJobs
import BusinessReads
import DataPreferences
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

@testable import PresentationSettings

/// GUI 5a, Settings › Xcode, over the CLI's 56d documents recorded from the
/// branch's `apogee-dev` in a sandbox home on a Mac with Xcode 27.1 (the home
/// written `/Users/someone`; `tests/business/reads/Fixtures/xcode-*`): Xcode
/// found, its preset registered and not, Xcode absent (a stub `xcode-select`
/// that fails, first on `PATH`), `xcrun` off `PATH`; and a binary before 56d
/// -- `check` with no Xcode section, two presets, `acp registration` refused.
/// Nothing here reaches Xcode: the pane never runs `mcp list` or `mcp test`.
@Suite("Settings › Xcode (GUI 5a)", .timeLimit(.minutes(1)))
@MainActor
struct XcodeSettingsTests {
    static let binary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee-dev")

    /// The verbs the pane may run, and nothing else: its three reads, its two
    /// changes, and `check` read again after a change.
    static let paneVerbs = [
        "read mcp presets", "run config get mcp_servers.xcode", "read acp registration",
        "run mcp create xcode --from xcode", "run config delete-mcp-server xcode", "read check",
    ]

    /// The Xcode-found home, its preset not registered: what `apogee-dev`
    /// printed there.
    func home(
        check: String = "xcode-check.json", presets: String = "xcode-mcp-presets.json", registered: Bool = false
    ) -> RuleRunner {
        let runner = RuleRunner()
        runner.reading(["check"], recordedText(check))
        runner.reading(["mcp", "presets"], recordedText(presets))
        runner.reading(["acp", "registration"], recordedText("xcode-acp-registration.json"))
        runner.when(
            ["config", "get", "mcp_servers.xcode"],
            registered ? .done(stdout: recordedText("xcode-config-get.txt")) : refused("xcode-config-get-none.err"))
        return runner
    }

    /// Settings over `runner`, read as the window reads it, then the pane as
    /// choosing it reads it.
    func settings(_ runner: RuleRunner) async -> (SettingsViewModel, Said) {
        let model = SettingsViewModel(runner: runner, starter: NoJobs(), preferences: Preferences(store: MemoryStore()))
        let said = Said()
        model.say = { said.lines.append($0) }
        await model.connect(binary: Self.binary, version: "0.2.0")
        await model.xcode.read()
        return (model, said)
    }

    final class Said {
        var lines: [String] = []
    }

    // MARK: - check's Xcode rows

    @Test("check's Xcode section is drawn whole -- every row, ok rows included, in check's order and words -- from the read Settings made, no check of its own")
    func checkRowsWhole() async throws {
        let runner = home()
        let (model, _) = await settings(runner)
        guard case .rows(let rows) = model.xcode.section else {
            Issue.record("no rows: \(model.xcode.section)")
            return
        }
        #expect(rows.map(\.name) == ["xcode", "mcpbridge", "acp agents", "approval"])
        #expect(rows.allSatisfy { $0.status == "ok" && $0.section == "Xcode" })
        #expect(rows[0].detail == "Xcode 27.1 (27A9275), /Applications/Xcode.app/Contents/Developer")
        #expect(rows[1].detail.hasPrefix("Xcode's tools for Apogee's agents: apogee mcp create xcode --from xcode"))
        #expect(rows[3].detail.contains("ad-hoc signed, so Xcode asks again after every install or update"), "W3, in the CLI's words")
        #expect(!model.xcode.xcodeAbsent && model.xcode.sectionLacking == nil)
        #expect(!model.healthRows.contains { $0.section == "Xcode" }, "Health keeps its filter: ok rows are the pane's alone")
        #expect(runner.calls.filter { $0 == "read check" }.count == 1, "one check run, Settings' own")
    }

    @Test("a binary whose check has no Xcode section is said to check nothing of Xcode -- and only once check was read")
    func checkWithoutXcode() async {
        let runner = home(check: "check-captions-skipped.json")
        let (model, _) = await settings(runner)
        #expect(model.xcode.section == .none)
        #expect(model.xcode.sectionLacking == "This apogee checks nothing of Xcode.")
        #expect(!model.xcode.xcodeAbsent, "a section it lacks asserts no absence")
    }

    @Test("Xcode absent: the one xcode row in check's words, and nothing that needs Xcode offered -- no Register, no fields")
    func xcodeAbsent() async throws {
        let (model, _) = await settings(home(check: "xcode-check-absent.json"))
        guard case .rows(let rows) = model.xcode.section else {
            Issue.record("no rows")
            return
        }
        #expect(rows.map(\.name) == ["xcode"])
        #expect(rows[0].status == "skipped")
        #expect(rows[0].detail == "not installed -- needed only to use Apogee from Xcode, or Xcode's tools from Apogee")
        #expect(model.xcode.xcodeAbsent)
        #expect(model.xcode.registers == nil, "Register needs Xcode")
        #expect(model.xcode.fields == nil, "the fields go into Xcode")
        #expect(model.xcode.registration != .notRead, "acp registration still read: the binary printed it")
    }

    // MARK: - The preset

    @Test("a runnable xcode row, not in the config: Register offered as its own register line, mcp create xcode --from xcode")
    func runnableNotRegistered() async throws {
        let runner = home()
        let (model, _) = await settings(runner)
        let preset = try #require(model.xcode.xcodePreset)
        #expect(preset.runnable == true && preset.missing == nil)
        #expect(preset.register == "apogee mcp create xcode --from xcode")
        #expect(preset.note?.contains("\"Allow External Agents to Use Xcode Tools\"") == true)
        #expect(model.xcode.entry == .notRegistered, "the CLI's no such config key")
        #expect(model.xcode.registers == ["mcp", "create", "xcode", "--from", "xcode"])
        #expect(model.xcode.removes == nil)
        #expect(runner.calls.contains("run config get mcp_servers.xcode"))
    }

    @Test("a row not runnable -- xcrun off the PATH -- says what is missing in the CLI's words, and Register isn't offered")
    func notRunnable() async throws {
        let (model, _) = await settings(home(presets: "xcode-mcp-presets-no-xcrun.json"))
        let preset = try #require(model.xcode.xcodePreset)
        #expect(preset.runnable == false)
        #expect(model.xcode.presetMissing == "xcrun, which comes with macOS's developer tools, is not on PATH")
        #expect(model.xcode.registers == nil)
    }

    @Test("a binary listing no xcode preset is said to ship none, and its config is never asked of a server the app would name")
    func noPreset() async {
        let runner = home(check: "check-captions-skipped.json", presets: "mcp-presets.json")
        let (model, _) = await settings(runner)
        #expect(model.xcode.preset == .read(nil))
        #expect(model.xcode.presetLacking == "This apogee ships no Xcode preset.")
        #expect(model.xcode.entry == nil && model.xcode.registers == nil && model.xcode.removes == nil)
        #expect(!runner.calls.contains { $0.hasPrefix("run config get mcp_servers") })
    }

    @Test("registered: config get's command, Remove offered and Register not")
    func registered() async {
        let (model, _) = await settings(home(check: "xcode-check-registered.json", registered: true))
        #expect(model.xcode.entry == .registered(command: "/usr/bin/xcrun"))
        #expect(model.xcode.registered)
        #expect(model.xcode.registers == nil)
        #expect(model.xcode.removes == ["config", "delete-mcp-server", "xcode"])
        #expect(model.xcode.row("mcpbridge")?.detail.hasPrefix("Xcode's tools for Apogee's agents: registered as 'xcode'") == true)
    }

    // MARK: - The registration

    @Test("acp registration as a document: its fields as printed, the Executable an absolute path, the menu path and the approval the CLI's")
    func registrationDocument() async throws {
        let (model, _) = await settings(home())
        let fields = try #require(model.xcode.fields)
        #expect(fields.name == "Apogee (dev)" && fields.channel == "dev")
        #expect(fields.executable == "/Users/someone/.local/bin/apogee-dev")
        #expect(fields.executable.hasPrefix("/"))
        #expect(fields.interpreter == nil && fields.arguments == ["acp"])
        #expect(fields.environment.map(\.entry) == [
            "APOGEE_HOME=/Users/someone/.apogee-dev",
            "PATH=/Users/someone/.local/bin:/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin",
        ])
        #expect(fields.heading == "Add Apogee (dev) to Xcode 26.6 or later: Settings › Intelligence › Agents › Add an Agent")
        #expect(fields.approval?.hasPrefix("Xcode approves an agent by its code signature") == true)
        #expect(model.xcode.registrationLacking == nil)
    }

    @Test("acp registration refused by an older binary: said in its words, and no field drawn")
    func registrationRefused() async {
        let runner = home(check: "check-captions-skipped.json", presets: "mcp-presets.json")
        var tail = BoundedTail()
        tail.append(Data(recordedText("xcode-acp-registration-older.err").utf8))
        runner.failingRead(["acp", "registration"], .failed(exit: ChildExit(reason: .exited, status: 109), stderrTail: tail))
        let (model, _) = await settings(runner)
        #expect(model.xcode.fields == nil)
        let lacking = model.xcode.registrationLacking ?? ""
        #expect(lacking.hasPrefix("This apogee printed no registration: apogee: --output-format json is taken by:"))
        #expect(lacking.hasSuffix("(exited with status 109)"), "\(lacking)")
    }

    @Test("Copy puts each field on the pasteboard exactly as printed -- the Executable absolute -- and the empty Interpreter copies nothing")
    func copyEachField() async throws {
        let (model, _) = await settings(home())
        let fields = RegistrationField.fields(of: try #require(model.xcode.fields))
        #expect(fields.map(\.label) == ["Name", "Executable", "Interpreter", "Arguments", "Environment Variables", ""])
        #expect(fields.map(\.value) == [
            "Apogee (dev)", "/Users/someone/.local/bin/apogee-dev", nil, "acp",
            "APOGEE_HOME=/Users/someone/.apogee-dev",
            "PATH=/Users/someone/.local/bin:/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin",
        ])
        let pasteboard = NSPasteboard(name: NSPasteboard.Name("apogee-xcode-tests-\(UUID().uuidString)"))
        defer { pasteboard.releaseGlobally() }
        for field in fields where field.copies {
            field.copy(to: pasteboard)
            #expect(pasteboard.string(forType: .string) == field.value, "\(field.label)")
        }
        let interpreter = try #require(fields.first { $0.label == "Interpreter" })
        #expect(!interpreter.copies && interpreter.shown == "(leave empty)")
        pasteboard.clearContents()
        pasteboard.setString("before", forType: .string)
        interpreter.copy(to: pasteboard)
        #expect(pasteboard.string(forType: .string) == "before", "nothing to copy, nothing written")
    }

    // MARK: - Changing

    @Test("Register runs mcp create xcode --from xcode byte for byte, reads it back by config get and check, and shows the CLI's words")
    func register() async throws {
        let runner = home()
        let (model, said) = await settings(runner)
        runner.when(["mcp", "create", "xcode", "--from", "xcode"], .done(stdout: recordedText("xcode-mcp-create.txt")))
        runner.when(["config", "get", "mcp_servers.xcode"], .done(stdout: recordedText("xcode-config-get.txt")))
        runner.reading(["check"], recordedText("xcode-check-registered.json"))
        let before = runner.calls.count
        await model.xcode.register()
        let after = Array(runner.calls.dropFirst(before))
        #expect(after.first == "run mcp create xcode --from xcode")
        #expect(after.contains("run config get mcp_servers.xcode"), "read back")
        #expect(after.contains("read check"), "check read again: the bridge row follows")
        #expect(model.xcode.entry == .registered(command: "/usr/bin/xcrun"))
        #expect(model.xcode.row("mcpbridge")?.detail.contains("registered as 'xcode'") == true)
        let lines = try #require(model.xcode.said?.lines)
        #expect(lines == recordedText("xcode-mcp-create.txt").split(whereSeparator: \.isNewline).map(String.init))
        #expect(lines.first == "registered MCP server 'xcode'")
        #expect(lines.last == "It connects at the start of the next tool-using run.")
        #expect(said.lines.last == "registered MCP server 'xcode'")
        #expect(model.xcode.failure == nil && model.xcode.registers == nil && model.xcode.removes != nil)
        model.xcode.dismissSaid()
        #expect(model.xcode.said == nil)
    }

    @Test("a refused Register is said in the CLI's words, nothing claimed")
    func registerRefused() async {
        let runner = home()
        let (model, said) = await settings(runner)
        runner.when(["mcp", "create", "xcode", "--from", "xcode"], refused("xcode-mcp-create-taken.err"))
        await model.xcode.register()
        #expect(model.xcode.failure?.hasPrefix("apogee mcp: updating config: MCP server 'xcode' already exists; pass --force to replace it") == true)
        #expect(model.xcode.said == nil)
        #expect(said.lines.last?.hasPrefix("That didn't work: apogee mcp:") == true)
    }

    @Test("Remove is asked first, then runs config delete-mcp-server xcode, read back, its words shown")
    func remove() async throws {
        let runner = home(check: "xcode-check-registered.json", registered: true)
        let (model, _) = await settings(runner)
        model.xcode.askToRemove()
        #expect(model.xcode.removing, "asked, as Library's Remove is")
        #expect(!runner.calls.contains { $0.hasPrefix("run config delete-mcp-server") }, "nothing removed on the ask")
        runner.when(["config", "delete-mcp-server", "xcode"], .done(stdout: recordedText("xcode-config-delete.txt")))
        runner.when(["config", "get", "mcp_servers.xcode"], refused("xcode-config-get-none.err"))
        runner.reading(["check"], recordedText("xcode-check.json"))
        await model.xcode.remove()
        #expect(!model.xcode.removing)
        #expect(runner.calls.contains("run config delete-mcp-server xcode"))
        #expect(model.xcode.entry == .notRegistered, "read back")
        #expect(model.xcode.said?.lines == ["removed MCP server 'xcode' from /Users/someone/.apogee-dev/config/config.json"])
        #expect(model.xcode.registers == ["mcp", "create", "xcode", "--from", "xcode"], "offered again")
    }

    @Test("Remove isn't asked where nothing is registered")
    func removeNothing() async {
        let (model, _) = await settings(home())
        model.xcode.askToRemove()
        #expect(!model.xcode.removing)
    }

    // MARK: - Never reaching Xcode

    @Test("over every story the runner never holds mcp list or mcp test, and the pane runs nothing outside its named verbs")
    func neverConnects() async {
        let runner = home()
        let model = SettingsViewModel(runner: runner, starter: NoJobs(), preferences: Preferences(store: MemoryStore()))
        await model.connect(binary: Self.binary, version: "0.2.0")
        let settingsReads = runner.calls.count
        await model.xcode.read()
        runner.when(["mcp", "create", "xcode", "--from", "xcode"], .done(stdout: recordedText("xcode-mcp-create.txt")))
        runner.when(["config", "get", "mcp_servers.xcode"], .done(stdout: recordedText("xcode-config-get.txt")))
        await model.xcode.register()
        model.xcode.askToRemove()
        runner.when(["config", "delete-mcp-server", "xcode"], .done(stdout: recordedText("xcode-config-delete.txt")))
        await model.xcode.remove()
        let pane = runner.calls.dropFirst(settingsReads)
        #expect(!runner.calls.contains { $0.hasPrefix("read mcp list") || $0.hasPrefix("run mcp list") })
        #expect(!runner.calls.contains { $0.contains("mcp test") })
        #expect(pane.allSatisfy { Self.paneVerbs.contains($0) }, "\(Array(pane))")
        #expect(pane.contains("run mcp create xcode --from xcode") && pane.contains("run config delete-mcp-server xcode"))
    }

    @Test("the pane's reads run when it is chosen, never in Settings' refresh")
    func notInRefresh() async {
        let runner = home()
        let model = SettingsViewModel(runner: runner, starter: NoJobs(), preferences: Preferences(store: MemoryStore()))
        await model.connect(binary: Self.binary, version: "0.2.0")
        await model.refresh()
        #expect(!runner.calls.contains { $0 == "read mcp presets" || $0 == "read acp registration" || $0.contains("mcp_servers") })
        #expect(model.xcode.preset == .notRead && model.xcode.registration == .notRead)
    }

    @Test("a binary not yet read asserts nothing: no section, no lack, no offer, no field -- and reading with none runs nothing")
    func notYetRead() async {
        let runner = home()
        let model = SettingsViewModel(runner: runner, starter: NoJobs(), preferences: Preferences(store: MemoryStore()))
        let xcode = model.xcode
        #expect(xcode.section == .notRead)
        #expect(xcode.sectionLacking == nil && xcode.presetLacking == nil && xcode.registrationLacking == nil)
        #expect(!xcode.xcodeAbsent && xcode.registers == nil && xcode.removes == nil && xcode.fields == nil)
        await xcode.read()
        await xcode.register()
        #expect(runner.calls.isEmpty)
    }

    @Test("a new binary is read afresh: what the last one said is no longer asserted")
    func newBinary() async {
        let (model, _) = await settings(home())
        #expect(model.xcode.fields != nil)
        await model.connect(binary: URL(fileURLWithPath: "/Users/someone/tools/apogee"), version: "0.2.0")
        #expect(model.xcode.preset == .notRead && model.xcode.registration == .notRead && model.xcode.entry == nil)
        #expect(model.xcode.binaryPath == "/Users/someone/tools/apogee")
    }
}
