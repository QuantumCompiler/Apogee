import BusinessReads
import Foundation
import Observation

/// Settings › Xcode (GUI 5a): the two routes joining Apogee and Xcode, each
/// in the CLI's words -- what `check` says of Xcode, Xcode's tools given to
/// Apogee's agents through the CLI's `xcode` preset, and the fields Xcode's
/// Add an Agent sheet takes.
///
/// SwiftUI-free (ADR 0002). **Nothing here reaches Xcode**: the pane reads
/// `mcp presets`, `config get mcp_servers.<server>` and `acp registration`,
/// and takes `check`'s rows from the read Settings already made; it never runs
/// `mcp list` or `mcp test`, which connect and would start `xcrun mcpbridge`
/// and Xcode's approval. Register only writes the config (`mcp create`), and
/// Remove only removes the entry. Every fact drawn is one the binary printed:
/// no probe of Xcode, no copy of the preset's command, no path of the
/// binary's worked out here, and what an older binary lacks said only from
/// its own answers.
@MainActor
@Observable
public final class XcodeSettings {
    /// What the binary said of one of the pane's reads.
    public enum Read<Value: Equatable & Sendable>: Equatable, Sendable {
        /// Not asked yet: nothing is asserted.
        case notRead
        /// What the binary printed.
        case read(Value)
        /// Why it printed nothing usable, in its words.
        case failed(String)
    }

    /// `check`'s Xcode section, from the rows Settings read.
    public enum Section: Equatable, Sendable {
        /// `check` not read yet: nothing is asserted.
        case notRead
        /// `check` read, and it has no Xcode section: a binary before the CLI's 56d.
        case none
        /// Every row of the section, in `check`'s order, ok rows included.
        case rows([SettingsViewModel.CheckRow])
    }

    /// The rows Settings read from `check`, `nil` until it read them --
    /// handed in by its owner, so the pane runs no `check` of its own.
    var checkRows: @MainActor () -> [SettingsViewModel.CheckRow]? = { nil }
    /// Asks `check` again, after a change, so this pane and Health both
    /// follow the config.
    var rereadCheck: @MainActor () async -> Void = {}
    public var say: (@MainActor (String) -> Void)?

    /// `mcp presets`' `xcode` row: `nil` inside `.read` where this binary
    /// lists no such row.
    public private(set) var preset: Read<MCPPreset?> = .notRead
    /// Whether the preset's server is in the config (`config get`).
    public private(set) var entry: MCPServerEntry?
    /// The fields `acp registration` printed, or why it printed none.
    public private(set) var registration: Read<AgentRegistration> = .notRead
    /// What the last change said, every line as the CLI said it, until dismissed.
    public private(set) var said: SettingsViewModel.Said?
    /// Why the last change failed, in the CLI's words.
    public private(set) var failure: String?
    /// The change running now.
    public private(set) var working: String?
    /// Remove waits on the person's word.
    public var removing = false

    /// The preset's name in `mcp presets` (the CLI's 56d).
    public static let presetName = "xcode"

    /// The binary's path the pane reads from: a new one reads it afresh.
    public private(set) var binaryPath: String?

    private let runner: any CommandRunning
    private var binary: URL?

    init(runner: any CommandRunning) {
        self.runner = runner
    }

    /// The binary this launch found. A new one is read afresh: what the last
    /// one said is no longer asserted.
    func use(binary: URL?) {
        guard binary != self.binary else { return }
        self.binary = binary
        binaryPath = binary?.path
        preset = .notRead
        entry = nil
        registration = .notRead
        said = nil
        failure = nil
    }

    // MARK: - Reading

    /// The pane's own reads, run when it is chosen and after each of its
    /// changes -- never with the other panes': `mcp presets`, then where the
    /// binary lists an `xcode` preset, `config get` of the server its
    /// `register` line names, then `acp registration`.
    public func read() async {
        guard let binary else { return }
        await readPreset(binary)
        await readEntry(binary)
        let printed = await runner.read(binary: binary, ["acp", "registration"])
        if let document = printed.document, let fields = AgentRegistration(document: document) {
            registration = .read(fields)
        } else {
            registration = .failed(printed.failure ?? "apogee printed no registration")
        }
    }

    private func readPreset(_ binary: URL) async {
        let listed = await runner.read(binary: binary, ["mcp", "presets"])
        if let document = listed.document {
            preset = .read(MCPPreset.list(from: document).first { $0.name == Self.presetName })
        } else {
            preset = .failed(listed.failure ?? "apogee printed no presets")
        }
    }

    /// Whether the preset's server is registered: a plain config read, which
    /// connects to nothing -- never `mcp list`.
    private func readEntry(_ binary: URL) async {
        guard let server = xcodePreset?.serverName else {
            entry = nil
            return
        }
        entry = MCPServerEntry(await runner.command(binary: binary, ["config", "get", MCPServerEntry.key(of: server)]))
    }

    // MARK: - What the binary said

    /// The `xcode` preset, where this binary lists one.
    public var xcodePreset: MCPPreset? {
        if case .read(let row?) = preset { row } else { nil }
    }

    /// `check`'s Xcode section, every row.
    public var section: Section {
        guard let rows = checkRows() else { return .notRead }
        let xcode = rows.filter { $0.section == XcodeCheck.section }
        return xcode.isEmpty ? .none : .rows(xcode)
    }

    /// The section's row of `name`, where `check` printed one.
    public func row(_ name: String) -> SettingsViewModel.CheckRow? {
        if case .rows(let rows) = section { rows.first { $0.name == name } } else { nil }
    }

    /// Whether `check` read the section and its row of `name` isn't `ok`
    /// -- missing, or skipped with its reason -- so the route it speaks
    /// for needs an Xcode this Mac lacks. `false` where `check` said
    /// nothing of Xcode: nothing is asserted.
    private func checkRules(out name: String) -> Bool {
        guard case .rows = section else { return false }
        return row(name)?.status != "ok"
    }

    /// Whether `check` says Xcode is absent -- its `xcode` row not `ok`:
    /// none installed, the Command Line Tools alone, a deleted one, the tools
    /// silent, or off macOS. The pane then offers nothing that needs Xcode.
    public var xcodeAbsent: Bool { checkRules(out: XcodeCheck.xcode) }

    /// What the pane says where this binary checks nothing of Xcode; `nil`
    /// where it does, or where `check` is not read yet.
    public var sectionLacking: String? {
        section == .none ? "This apogee checks nothing of Xcode." : nil
    }

    /// What the pane says where this binary ships no Xcode preset, or its
    /// presets couldn't be read; `nil` before they are read, or where it
    /// ships one.
    public var presetLacking: String? {
        switch preset {
        case .notRead, .read(_?): nil
        case .read(nil): "This apogee ships no Xcode preset."
        case .failed(let why): "The presets couldn't be read: \(why)"
        }
    }

    /// What the pane says where this binary printed no registration, in its
    /// words; `nil` before it is asked, or where it printed one.
    public var registrationLacking: String? {
        if case .failed(let why) = registration { "This apogee printed no registration: \(why)" } else { nil }
    }

    /// The fields to draw, where the binary printed them and `check` doesn't
    /// say this Mac's Xcode can't host an agent.
    public var fields: AgentRegistration? {
        guard case .read(let fields) = registration, !xcodeAbsent, !checkRules(out: XcodeCheck.agents) else { return nil }
        return fields
    }

    /// Whether the preset's server is in the config.
    public var registered: Bool {
        if case .registered = entry { true } else { false }
    }

    /// What Register runs -- the preset row's own `register` line, as a
    /// person types it -- where it can: the row lists it, says it is
    /// `runnable`, its server is read as not registered, and `check` doesn't
    /// say this Mac lacks the Xcode or the bridge it needs. `nil` otherwise.
    public var registers: [String]? {
        guard let preset = xcodePreset, preset.runnable == true, entry == .notRegistered,
              !xcodeAbsent, !checkRules(out: XcodeCheck.bridge)
        else { return nil }
        return preset.registration
    }

    /// The command Remove runs -- `config delete-mcp-server <server>` --
    /// where the server is in the config; `nil` otherwise.
    public var removes: [String]? {
        guard registered, let server = xcodePreset?.serverName else { return nil }
        return ["config", "delete-mcp-server", server]
    }

    /// Why the preset can't run here, in the CLI's words -- its `missing`,
    /// then its `remedy` where it names one; `nil` where it runs.
    public var presetMissing: String? {
        guard let preset = xcodePreset, preset.runnable != true else { return nil }
        return [preset.missing, preset.remedy].compactMap { $0 }.joined(separator: " -- ").nilIfEmpty
    }

    // MARK: - Changing

    /// Registers the preset once the person chose it -- its own line, `mcp
    /// create <server> --from xcode` -- which writes the config and connects
    /// to nothing; read back, its words said.
    public func register() async {
        guard let arguments = registers else { return }
        await change(arguments, title: "Registered for Apogee's agents")
    }

    /// Asks before removing the server (as Library's Remove does).
    public func askToRemove() {
        removing = removes != nil
    }

    /// Removes the server once the person said yes: `config delete-mcp-server
    /// <server>` -- its config entry alone -- read back, its words said.
    public func remove() async {
        removing = false
        guard let arguments = removes else { return }
        await change(arguments, title: "Removed from Apogee's agents")
    }

    /// Takes the last change's words away, once read.
    public func dismissSaid() {
        said = nil
    }

    /// Runs a change, reads the pane back -- and `check` -- and keeps what
    /// the CLI said: every line it printed, or why it refused.
    private func change(_ arguments: [String], title: String) async {
        guard let binary, working == nil else { return }
        working = arguments.prefix(3).joined(separator: " ")
        said = nil
        let outcome = await runner.command(binary: binary, arguments)
        working = nil
        await read()
        await rereadCheck()
        switch outcome {
        case .done(let stdout):
            failure = nil
            let lines = stdout.split(whereSeparator: \.isNewline).map(String.init)
            said = SettingsViewModel.Said(title: title, lines: lines)
            say?(lines.first ?? title)
        case let other:
            let why = other.failure ?? "apogee said nothing"
            failure = why
            say?("That didn't work: \(why)")
        }
    }
}

extension String {
    fileprivate var nilIfEmpty: String? { isEmpty ? nil : self }
}
