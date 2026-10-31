import BusinessReads
import Foundation
import Observation

/// The Suites place: each suite and its members, read through `config get`
/// and `models status`, with whether it fits -- the CLI's own verdict where
/// `models status --suite <name>` states its footprint (the one `chat
/// --suite` admits a session by: a base two members share counted once,
/// adapters priced by their files, a member's drafter counted -- the CLI's
/// 27e, 53d, M25), and only where no read states one, an estimate worked out
/// here from what `models info` and `system` state, said as the app's,
/// unknown wherever they state nothing.
///
/// SwiftUI-free (ADR 0002). Making a suite the default is its verb, read back;
/// a chat opened under a suite, or the open session switched to it, is the
/// session library's -- reached through the composition (GUI 4i).
@MainActor
@Observable
public final class SuitesViewModel {
    /// What a member holds on this machine.
    public enum Memory: Equatable, Sendable {
        /// A cloud API or a subscription: nothing here.
        case none
        /// Its weights and cache at its window, as `models info` states them.
        case known(weights: Int, cache: Int)
        /// The documents give no size for it.
        case unknown
    }

    public struct MemberFit: Equatable, Identifiable, Sendable {
        public let member: Suite.Member
        public let memory: Memory
        public var id: String { member.role }
    }

    /// The verdict, said honestly.
    public enum Fit: Equatable, Sendable {
        /// The CLI's own: `models status --suite <name>`'s footprint.
        case apogee(SuiteFootprint)
        // The app's estimate, where no read states the CLI's:
        case fits(total: Int, budget: Int)
        case doesNotFit(total: Int, budget: Int)
        /// Some member's size is not stated; which.
        case unknown(members: [String])
        /// The machine's budget is not stated.
        case noBudget(total: Int)
    }

    public private(set) var suites: [Suite] = []
    public private(set) var defaultSuite: String?
    public private(set) var selected: String?
    public private(set) var fits: [String: [MemberFit]] = [:]
    /// Each suite's footprint, as the CLI priced it; a suite missing here is
    /// estimated (`fits`).
    public private(set) var footprints: [String: SuiteFootprint] = [:]
    public private(set) var budget: Int?
    public private(set) var failure: String?
    public private(set) var reading = false
    public var say: (@MainActor (String) -> Void)?
    /// Opens a new chat under the suite -- `chat --suite <name>`, forced
    /// (`--force`) when said -- in the session library: the composition's way
    /// there.
    public var openChat: (@MainActor (String, Bool) -> Void)?
    /// Switches the open session to the suite, as the session's own `/suite`
    /// does: the composition's way to the library.
    public var switchSession: (@MainActor (String, Bool) -> Void)?
    /// Whether a session is open that can be switched: the library's word.
    public var sessionSwitchable: (@MainActor () -> Bool)?

    private let runner: any CommandRunning
    private var binary: URL?
    private var kinds: [String: ModelKind] = [:]
    /// Each local backend's weights file, as its `models info` names it: an
    /// estimate counts a file two members share once, as the CLI's `BaseKey`
    /// does.
    private var weightsFiles: [String: String] = [:]

    public init(runner: any CommandRunning) {
        self.runner = runner
    }

    public func connect(binary: URL?) async {
        self.binary = binary
        await refresh()
    }

    public func appeared() async {
        await refresh()
    }

    /// The suites (`config get suites`, each `config get suites.<name>`),
    /// the default (`models status`'s `suite`), the kinds (`models list`), the
    /// budget (`system`), and each member's sizes (`models info`).
    public func refresh() async {
        guard let binary else {
            failure = "Apogee isn't installed, so there are no suites to read."
            return
        }
        reading = true
        defer { reading = false }
        let names = await runner.command(binary: binary, ["config", "get", "suites"])
        guard case .done(let listing) = names else {
            failure = names.failure
            return
        }
        failure = nil
        var read: [Suite] = []
        for name in Suite.names(listing) {
            if case .done(let text) = await runner.command(binary: binary, ["config", "get", "suites.\(name)"]) {
                read.append(Suite(name: name, text: text))
            }
        }
        suites = read
        if let status = await runner.read(binary: binary, ["models", "status"]).document {
            defaultSuite = Field.list(from: status).first { $0.label == "suite" }?.value
        }
        if let models = await runner.read(binary: binary, ["models", "list"]).document {
            kinds = ModelKind.byBackend(from: models)
        }
        if let system = await runner.read(binary: binary, ["system"]).document {
            budget = SystemReading(document: system).modelBudget
        }
        // The CLI's verdict for each suite, where its read states one.
        var priced: [String: SuiteFootprint] = [:]
        for suite in read {
            if let document = await runner.read(binary: binary, ["models", "status", "--suite", suite.name]).document,
                let footprint = SuiteFootprint(document: document)
            {
                priced[suite.name] = footprint
            }
        }
        footprints = priced
        // The estimate, only for the suites the CLI's read did not price.
        let estimated = read.filter { priced[$0.name] == nil }
        var sizes: [String: Memory] = [:]
        for backend in Set(estimated.flatMap { $0.members.map(\.backend) }) {
            sizes[backend] = await memory(of: backend, binary: binary)
        }
        fits = Dictionary(uniqueKeysWithValues: estimated.map { suite in
            (suite.name, suite.members.map { MemberFit(member: $0, memory: sizes[$0.backend] ?? .unknown) })
        })
        if selected == nil || !read.contains(where: { $0.name == selected }) {
            selected = read.first?.name
        }
    }

    /// A member's memory for the estimate: none for a cloud one; else its
    /// `models info` sizes, its adapters priced by their files (as the CLI's
    /// footprint prices them, 53d) -- unknown when one cannot be read.
    private func memory(of backend: String, binary: URL) async -> Memory {
        if let kind = kinds[backend], kind != .local { return .none }
        guard let info = await runner.read(binary: binary, ["models", "info", backend]).document else { return .unknown }
        let model = ModelInfo(document: info)
        if let path = model.field("model_path"), !path.isEmpty { weightsFiles[backend] = path }
        let memory = Self.memory(in: model)
        guard case .known(let weights, let cache) = memory else { return memory }
        guard case .done(let listed) = await runner.command(binary: binary, ["config", "get", "backends.\(backend).adapters"])
        else { return memory }
        let adapters = BackendAdapter.list(listed)
        guard !adapters.isEmpty else { return memory }
        guard let bytes = Self.bytes(of: adapters.map(\.path)) else { return .unknown }
        return .known(weights: weights, cache: cache + bytes)
    }

    /// What `paths` take on disk -- a file's size, a directory's files' --
    /// or `nil` when any cannot be read.
    nonisolated static func bytes(of paths: [String]) -> Int? {
        var total = 0
        let files = FileManager.default
        for path in paths {
            let expanded = (path as NSString).expandingTildeInPath
            var directory: ObjCBool = false
            guard files.fileExists(atPath: expanded, isDirectory: &directory) else { return nil }
            if directory.boolValue {
                guard let walk = files.enumerator(atPath: expanded) else { return nil }
                for case let name as String in walk {
                    let size = (try? files.attributesOfItem(atPath: expanded + "/" + name))?[.size] as? Int
                    total += size ?? 0
                }
            } else {
                guard let size = (try? files.attributesOfItem(atPath: expanded))?[.size] as? Int else { return nil }
                total += size
            }
        }
        return total
    }

    /// A member's weights and cache: 52e's typed facts where the document has
    /// them, else the record's `size` and `cache` lines in MiB; else unknown.
    static func memory(in info: ModelInfo) -> Memory {
        if let bytes = info.data?.bytes, let cache = info.data?.cache?.bytes {
            return .known(weights: bytes, cache: cache)
        }
        guard let size = mib(info.field("size")), let cache = info.field("cache") else { return .unknown }
        if cache.hasPrefix("none") { return .known(weights: size, cache: 0) }
        guard let cacheBytes = mib(cache) else { return .unknown }
        return .known(weights: size, cache: cacheBytes)
    }

    /// `4608 MiB …` in bytes.
    static func mib(_ text: String?) -> Int? {
        guard let text else { return nil }
        let digits = text.prefix { $0.isNumber }
        guard let n = Int(digits), text.dropFirst(digits.count).trimmingCharacters(in: .whitespaces).hasPrefix("MiB") else {
            return nil
        }
        return n * 1024 * 1024
    }

    // MARK: - What the place shows

    public var current: Suite? { selected.flatMap { name in suites.first { $0.name == name } } }

    public func select(_ name: String) {
        selected = name
    }

    /// The estimate's members, for a suite the CLI's read did not price.
    public func members(_ suite: Suite) -> [MemberFit] { fits[suite.name] ?? [] }

    /// The CLI's footprint of the suite, where its read stated one.
    public func footprint(_ suite: Suite) -> SuiteFootprint? { footprints[suite.name] }

    /// Whether the suite fits this machine's model budget: the CLI's verdict
    /// where it states one, else the app's estimate, as far as the documents
    /// say.
    public func fit(_ suite: Suite) -> Fit {
        if let footprint = footprints[suite.name] { return .apogee(footprint) }
        let members = self.members(suite)
        let unknown = members.filter { $0.memory == .unknown }.map(\.member.backend)
        if !unknown.isEmpty { return .unknown(members: Array(Set(unknown)).sorted()) }
        // A backend serving two roles is held once -- and so is a weights
        // file two backends share: the first member in role order pays its
        // weights, each its own cache and adapters (the CLI's 53d).
        var held: [String: Int] = [:]
        var weightsPaid: Set<String> = []
        for member in members {
            guard case .known(let weights, let cache) = member.memory, held[member.member.backend] == nil else { continue }
            let file = weightsFiles[member.member.backend] ?? "backend:" + member.member.backend
            held[member.member.backend] = (weightsPaid.insert(file).inserted ? weights : 0) + cache
        }
        let total = held.values.reduce(0, +)
        guard let budget else { return .noBudget(total: total) }
        return total <= budget ? .fits(total: total, budget: budget) : .doesNotFit(total: total, budget: budget)
    }

    /// Whether `fit` is the app's own estimate rather than the CLI's verdict.
    public static func isEstimate(_ fit: Fit) -> Bool {
        if case .apogee = fit { false } else { true }
    }

    /// Whether `fit` says the suite does not fit: `chat --suite` refuses such
    /// a suite unless it is forced (`--force`).
    public static func overBudget(_ fit: Fit) -> Bool {
        switch fit {
        case .apogee(let footprint): footprint.verdict == .doesNotFit
        case .doesNotFit: true
        default: false
        }
    }

    /// The verdict in a few words, for the list: the CLI's own where it gave
    /// one, else the estimate's, said as the app's.
    public static func summary(_ fit: Fit) -> String {
        if case .apogee(let footprint) = fit { return footprint.verdictWords }
        return "estimated here: " + words(fit)
    }

    public static func words(_ fit: Fit) -> String {
        func size(_ bytes: Int) -> String { ByteCountFormatter.string(fromByteCount: Int64(bytes), countStyle: .memory) }
        switch fit {
        case .apogee(let footprint): return footprint.total
        case .fits(let total, let budget): return "fits: \(size(total)) of \(size(budget))"
        case .doesNotFit(let total, let budget): return "does not fit: \(size(total)) of \(size(budget))"
        case .unknown(let members): return "unknown: no size stated for \(members.joined(separator: ", "))"
        case .noBudget(let total): return "\(size(total)); this machine's budget is not stated"
        }
    }

    // MARK: - Chats under a suite

    /// Whether a session is open that Switch the session to it can move.
    public var canSwitchSession: Bool { sessionSwitchable?() ?? false }

    /// A new chat under `suite` -- `chat --suite <name>` -- forced past the
    /// CLI's memory refusal only where its verdict says it does not fit, the
    /// person having pressed the button that says so.
    public func openChat(under suite: Suite) {
        openChat?(suite.name, Self.overBudget(fit(suite)))
    }

    /// The open session moved under `suite`, as `/suite <name>` moves it.
    public func switchSession(to suite: Suite) {
        guard canSwitchSession else { return }
        switchSession?(suite.name, Self.overBudget(fit(suite)))
    }

    /// Makes `suite` the one every command resolves under:
    /// `config set-default-suite <name>`, or `off`; read back.
    public func makeDefault(_ suite: Suite?) async {
        guard let binary else { return }
        let outcome = await runner.command(binary: binary, ["config", "set-default-suite", suite?.name ?? "off"])
        await refresh()
        if let failure = outcome.failure {
            say?("That didn't work: \(failure)")
        } else {
            say?(suite.map { "\($0.name) is the default suite" } ?? "No default suite")
        }
    }
}
