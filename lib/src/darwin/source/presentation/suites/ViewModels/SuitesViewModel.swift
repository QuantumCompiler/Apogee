import BusinessReads
import Foundation
import Observation

/// The Suites place: each suite and its members, read through `config get`
/// and `models status`, with whether it fits -- worked out here from what
/// `models info` and `system` state, unknown wherever they state nothing
/// (the design's G10 fallback: the CLI offers no read of a suite's fit).
///
/// SwiftUI-free (ADR 0002). Making a suite the default is its verb, read back.
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
    public private(set) var budget: Int?
    public private(set) var failure: String?
    public private(set) var reading = false
    public var say: (@MainActor (String) -> Void)?

    private let runner: any CommandRunning
    private var binary: URL?
    private var kinds: [String: ModelKind] = [:]

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
        var sizes: [String: Memory] = [:]
        for backend in Set(read.flatMap { $0.members.map(\.backend) }) {
            sizes[backend] = await memory(of: backend, binary: binary)
        }
        fits = Dictionary(uniqueKeysWithValues: read.map { suite in
            (suite.name, suite.members.map { MemberFit(member: $0, memory: sizes[$0.backend] ?? .unknown) })
        })
        if selected == nil || !read.contains(where: { $0.name == selected }) {
            selected = read.first?.name
        }
    }

    private func memory(of backend: String, binary: URL) async -> Memory {
        if let kind = kinds[backend], kind != .local { return .none }
        guard let info = await runner.read(binary: binary, ["models", "info", backend]).document else { return .unknown }
        return Self.memory(in: ModelInfo(document: info))
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

    public func members(_ suite: Suite) -> [MemberFit] { fits[suite.name] ?? [] }

    /// Whether the suite fits this machine's model budget, as far as the
    /// documents say.
    public func fit(_ suite: Suite) -> Fit {
        let members = self.members(suite)
        let unknown = members.filter { $0.memory == .unknown }.map(\.member.backend)
        if !unknown.isEmpty { return .unknown(members: Array(Set(unknown)).sorted()) }
        // A backend serving two roles is held once.
        var held: [String: Int] = [:]
        for member in members {
            if case .known(let weights, let cache) = member.memory { held[member.member.backend] = weights + cache }
        }
        let total = held.values.reduce(0, +)
        guard let budget else { return .noBudget(total: total) }
        return total <= budget ? .fits(total: total, budget: budget) : .doesNotFit(total: total, budget: budget)
    }

    public static func words(_ fit: Fit) -> String {
        func size(_ bytes: Int) -> String { ByteCountFormatter.string(fromByteCount: Int64(bytes), countStyle: .memory) }
        switch fit {
        case .fits(let total, let budget): return "fits: \(size(total)) of \(size(budget))"
        case .doesNotFit(let total, let budget): return "does not fit: \(size(total)) of \(size(budget))"
        case .unknown(let members): return "unknown: no size stated for \(members.joined(separator: ", "))"
        case .noBudget(let total): return "\(size(total)); this machine's budget is not stated"
        }
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
