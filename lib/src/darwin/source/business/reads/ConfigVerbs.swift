import DataProtocol
import Foundation

/// The config verbs the CLI's M33 (2026-10-26) finished: one scanned provider
/// registered by `providers register`, the roles a change moves said in its
/// words, and the allowed hosts read by `config get` (GUI M16).
public enum ConfigVerbs {
    /// The vocabulary's date from which the binary has `providers register`
    /// and reads `config get tools.allowed_hosts` (the CLI's M33). A
    /// development build between the CLI's M29 and M33 announces this date
    /// too without them: its refusal is said in its words, and no release is
    /// affected.
    public static let since = "2026-10-26"

    /// Whether a binary announcing `vocabulary` -- `__machine-schema`'s date,
    /// read at launch -- has them. `nil`, a binary that hasn't said, is read
    /// as one before: today's verbs.
    public static func has(vocabulary: String?) -> Bool {
        (vocabulary ?? "") >= since
    }
}

/// What `providers register <id> --output-format json` did (the CLI's M33,
/// `object: "provider.registration"`): the registration core the TUI's `r`
/// runs, over the last scan, for one provider -- its lines, the default set
/// when none was, and the rosters it fetched on this word, each a line as the
/// CLI says it. The command takes no key: keys stay the CLI's own flows.
public struct ProviderRegistration: Sendable, Equatable {
    public let provider: String
    /// What registering said: `registered codex (codex-cli)`, `models.default
    /// set to 'codex' (none was set)`, or why nothing was written.
    public let registration: [String]
    /// What fetching its roster said: `fetched codex-cli's models: 2 on the
    /// roster`, or why it couldn't; none for a type with no roster.
    public let rosters: [String]

    public init(provider: String, registration: [String], rosters: [String]) {
        self.provider = provider
        self.registration = registration
        self.rosters = rosters
    }

    /// The document, when it is a registration's; `nil` for any other.
    public init?(document: ReadDocument) {
        let value = document.value
        guard value["object"]?.stringValue == "provider.registration" else { return nil }
        func lines(_ key: String) -> [String] { (value[key]?.arrayValue ?? []).compactMap(\.stringValue) }
        self.init(provider: value["provider"]?.stringValue ?? "", registration: lines("registration"), rosters: lines("rosters"))
    }

    /// Every line, as the text form prints them: the registration's, then the
    /// rosters'.
    public var lines: [String] { registration + rosters }
}

/// A role the CLI moved on the person's behalf, as it says it (the CLI's
/// M33): `models.default set to '<name>' (none was set)` -- by `config
/// add-backend`, `config scan --register` and `providers register` when no
/// default is set -- and `models.<field> cleared: it named '<name>'` -- by
/// `config delete-backend` and `models delete --and-backends`, for each role
/// that named the entry. Only these words: `config set-default`'s own
/// `models.default = <name>` is the person's act, not one on their behalf.
public enum RoleChange: Sendable, Equatable {
    /// `models.<field>` set to the entry, none having been set.
    case set(field: String, to: String)
    /// `models.<field>` cleared, the entry it named gone.
    case cleared(field: String, named: String)

    /// The change a line says, when it says one.
    public init?(line: String) {
        let line = line.trimmingCharacters(in: .whitespaces)
        guard line.hasPrefix("models.") else { return nil }
        let rest = line.dropFirst("models.".count)
        if let set = rest.range(of: " set to '"), line.hasSuffix("' (none was set)") {
            let name = rest[set.upperBound...].dropLast("' (none was set)".count)
            self = .set(field: String(rest[..<set.lowerBound]), to: String(name))
        } else if let cleared = rest.range(of: " cleared: it named '"), line.hasSuffix("'") {
            let name = rest[cleared.upperBound...].dropLast()
            self = .cleared(field: String(rest[..<cleared.lowerBound]), named: String(name))
        } else {
            return nil
        }
    }

    /// Every change a command's output says, in its order.
    public static func said(in text: String) -> [RoleChange] {
        text.split(whereSeparator: \.isNewline).compactMap { RoleChange(line: String($0)) }
    }
}

/// The hosts `fetch_url` reaches without asking, as `config get
/// tools.allowed_hosts` reads them (the CLI's M33): one a line, none when
/// the list is empty. A binary before M33 refuses the key (`no such config
/// key`), and had no read of them at all.
public enum AllowedHosts {
    /// The key, as `config get` takes it.
    public static let key = "tools.allowed_hosts"

    /// The hosts, in the CLI's order.
    public static func list(_ text: String) -> [String] {
        text.split(whereSeparator: \.isNewline).map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty }
    }
}
