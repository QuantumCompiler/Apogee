import BusinessReads
import DataProtocol
import Foundation
import InfrastructureProcess
import Testing

private func fixture(_ name: String) -> String {
    let url = URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("Fixtures/\(name)")
    return (try? String(contentsOf: url, encoding: .utf8)) ?? ""
}

private func document(_ name: String) throws -> ReadDocument {
    try #require(ReadDocument(text: fixture(name)), "\(name)")
}

private func refusal(_ name: String, status: Int32 = 1) -> CommandOutcome {
    var tail = BoundedTail()
    tail.append(Data(fixture(name).utf8))
    return .failed(exit: ChildExit(reason: .exited, status: status), stderrTail: tail)
}

/// GUI 5a's reads over the CLI's 56d documents, recorded from the branch's
/// `apogee-dev` in a sandbox home on a Mac with Xcode 27.1, the home written
/// `/Users/someone` -- the Executable and every path in them included.
@Suite("Xcode setup reads (GUI 5a)")
struct XcodeSetupTests {
    @Test("acp registration's document: every field as printed, the environment in the document's order, nothing composed")
    func registration() throws {
        let fields = try #require(AgentRegistration(document: try document("xcode-acp-registration.json")))
        #expect(fields.location == "Settings › Intelligence › Agents › Add an Agent")
        #expect(fields.name == "Apogee (dev)")
        #expect(fields.executable == "/Users/someone/.local/bin/apogee-dev")
        #expect(fields.interpreter == nil, "null: the executable is a binary")
        #expect(fields.arguments == ["acp"])
        #expect(fields.environment.map(\.name) == ["APOGEE_HOME", "PATH"])
        #expect(fields.environment.first?.value == "/Users/someone/.apogee-dev")
        #expect(fields.channel == "dev" && fields.xcodeMinimum == "26.6")
        #expect(fields.approval == "Xcode approves an agent by its code signature and its binary's hash. Apogee's macOS builds are ad-hoc signed, so Xcode asks again after every install or update.")
        #expect(fields.heading == "Add Apogee (dev) to Xcode 26.6 or later: Settings › Intelligence › Agents › Add an Agent")
    }

    @Test("a document that is not a registration, or one with no Executable, is no registration; unknown fields are tolerated")
    func registrationTolerant() throws {
        #expect(AgentRegistration(document: try document("xcode-mcp-presets.json")) == nil)
        #expect(AgentRegistration(document: ReadDocument(text: #"{"object":"acp.registration","name":"Apogee"}"#)!) == nil)
        let grown = try #require(AgentRegistration(document: ReadDocument(text: #"{"object":"acp.registration","executable":"/Users/someone/.local/bin/apogee","arguments":["--dev","acp"],"environment":{},"interpreter":"/usr/bin/env","later":{"a":1}}"#)!))
        #expect(grown.arguments == ["--dev", "acp"] && grown.environment.isEmpty && grown.interpreter == "/usr/bin/env")
        #expect(grown.heading == nil, "no place printed, no heading made")
    }

    @Test("mcp presets' xcode row: runnable, its note and its register line -- whose server name is the line's, and its argv built from it")
    func presetRow() throws {
        let presets = MCPPreset.list(from: try document("xcode-mcp-presets.json"))
        #expect(presets.map(\.name) == ["code-exec", "self", "xcode"])
        let xcode = try #require(presets.first { $0.name == "xcode" })
        #expect(xcode.runnable == true && xcode.missing == nil && xcode.remedy == nil)
        #expect(xcode.note?.hasPrefix("Xcode answers only while it runs with your project open") == true)
        #expect(xcode.register == "apogee mcp create xcode --from xcode")
        #expect(xcode.serverName == "xcode")
        #expect(xcode.registration == ["mcp", "create", "xcode", "--from", "xcode"])
        #expect(presets.first { $0.name == "code-exec" }?.note == nil, "present only on a row that has one")
        #expect(presets.first { $0.name == "self" }?.registration == nil, "a refusal registers nothing")
    }

    @Test("a row not runnable says what is missing; a code-exec row needing the training environment names its remedy")
    func presetMissing() throws {
        let xcode = try #require(MCPPreset.list(from: try document("xcode-mcp-presets-no-xcrun.json")).first { $0.name == "xcode" })
        #expect(xcode.runnable == false)
        #expect(xcode.missing == "xcrun, which comes with macOS's developer tools, is not on PATH")
        #expect(xcode.register == "apogee mcp create xcode --from xcode", "still listed, still its line")
        #expect(
            fixture("xcode-mcp-create-no-xcrun.err").contains("the 'xcode' preset cannot run here: \(xcode.missing ?? "-")"),
            "the words the listing gives are the ones mcp create refuses with")
    }

    @Test("a binary before 56d lists no xcode row")
    func olderPresets() throws {
        #expect(!MCPPreset.list(from: try document("mcp-presets.json")).contains { $0.name == "xcode" })
    }

    @Test("a register line of any other shape names no server: the app composes none")
    func serverNameStrict() {
        func preset(_ line: String?) -> MCPPreset {
            MCPPreset(name: "xcode", description: "", provides: "", register: line, refusal: nil)
        }
        #expect(preset("apogee mcp create xcode --from xcode").serverName == "xcode")
        #expect(preset("apogee-dev mcp create xc --from xcode").serverName == "xc")
        #expect(preset("apogee mcp create xcode --from code-exec").serverName == nil)
        #expect(preset("apogee mcp create xcode --command xcrun --args mcpbridge").serverName == nil)
        #expect(preset(nil).serverName == nil)
    }

    @Test("config get mcp_servers.xcode: registered prints its command; not registered is the CLI's no such config key; anything else unread in its words")
    func entry() {
        #expect(MCPServerEntry.key(of: "xcode") == "mcp_servers.xcode")
        #expect(MCPServerEntry(.done(stdout: fixture("xcode-config-get.txt"))) == .registered(command: "/usr/bin/xcrun"))
        #expect(MCPServerEntry(refusal("xcode-config-get-none.err")) == .notRegistered)
        #expect(MCPServerEntry(.failed(exit: ChildExit(reason: .exited, status: 1), stderrTail: BoundedTail()))
            == .unreadable("apogee said nothing (exited with status 1)"))
        #expect(MCPServerEntry(.couldNotStart("no such file")) == .unreadable("no such file"))
    }

    @Test("check's Xcode section by 56d's names: found, registered, absent -- every row ok or skipped, never a warning or a failure")
    func checkSection() throws {
        func rows(_ name: String) throws -> [JSONValue] {
            (try document(name).value["rows"]?.arrayValue ?? []).filter { $0["section"]?.stringValue == XcodeCheck.section }
        }
        let names = [XcodeCheck.xcode, XcodeCheck.bridge, XcodeCheck.agents, XcodeCheck.approval]
        #expect(try rows("xcode-check.json").compactMap { $0["name"]?.stringValue } == names)
        #expect(try rows("xcode-check-registered.json").compactMap { $0["name"]?.stringValue } == names)
        let absent = try rows("xcode-check-absent.json")
        #expect(absent.compactMap { $0["name"]?.stringValue } == [XcodeCheck.xcode])
        #expect(absent.first?["status"]?.stringValue == "skipped")
        for name in ["xcode-check.json", "xcode-check-registered.json", "xcode-check-absent.json"] {
            #expect(try rows(name).allSatisfy { ["ok", "skipped"].contains($0["status"]?.stringValue) }, "\(name)")
        }
        #expect(try rows("check-captions-skipped.json").isEmpty, "a binary before 56d")
    }
}
