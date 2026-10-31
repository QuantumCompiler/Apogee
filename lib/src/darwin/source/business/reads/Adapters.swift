import DataProtocol
import Foundation

/// An adapter a backend applies over its `model_path`, never merged into it
/// (the CLI's 53a): one line of `config get backends.<name>.adapters` -- its
/// path, and its scale after the last colon when it is not 1, exactly as
/// `config set-backend --adapters` takes it; the pin is left out (the CLI's
/// 55g), and is `check`'s to state (`AdapterCheck`).
public struct BackendAdapter: Sendable, Equatable, Identifiable {
    /// The GGUF LoRA file (llama.cpp), or the adapter directory (mlx), as the
    /// config holds it.
    public let path: String
    /// In (0, 2]; 1 unless the line says otherwise.
    public let scale: Double
    public var id: String { path }

    public init(path: String, scale: Double = 1) {
        self.path = path
        self.scale = scale
    }

    /// `<path>[:<scale>]`: only a number after the last colon is a scale, as
    /// the CLI reads it (53a, 49e's `--lora`).
    public init(line: String) {
        if let colon = line.lastIndex(of: ":"), let scale = Double(line[line.index(after: colon)...]) {
            self.init(path: String(line[..<colon]), scale: scale)
        } else {
            self.init(path: line)
        }
    }

    /// Every adapter `config get backends.<name>.adapters` printed, one per
    /// line; none for an empty answer.
    public static func list(_ text: String) -> [BackendAdapter] {
        text.split(whereSeparator: \.isNewline)
            .map { $0.trimmingCharacters(in: .whitespaces) }
            .filter { !$0.isEmpty }
            .map(BackendAdapter.init(line:))
    }

    /// Its last path component: the name `check`'s row gives it.
    public var file: String {
        let trimmed = path.hasSuffix("/") ? String(path.dropLast()) : path
        return (trimmed as NSString).lastPathComponent
    }

    /// As `--adapters` takes it: the path, then `:<scale>` unless it is 1.
    public var argument: String {
        scale == 1 ? path : "\(path):\(Self.number(scale))"
    }

    /// A scale written as the CLI writes a number: `0.5`, `1.5`.
    public static func number(_ value: Double) -> String {
        JSONValue.number(value).serialized
    }
}

/// What `check`'s Adapters section says of one adapter (the CLI's 53c): its
/// row's status -- `ok`, `warn`, `fail` or `skipped` -- the detail, which
/// names the scale and the pin, and the way to mend it where the row has one.
public struct AdapterCheck: Sendable, Equatable {
    public let status: String
    public let detail: String
    public let remedy: String?

    public init(status: String, detail: String, remedy: String?) {
        self.status = status
        self.detail = detail
        self.remedy = remedy
    }

    /// Each of `adapters`' rows in `check --output-format json`'s document,
    /// by its adapter's path: the rows name an adapter `<backend>: <file>`,
    /// in the entry's order, so two of one file name pair off in order. An
    /// adapter with no row -- a binary before 53c -- has none.
    public static func rows(for adapters: [BackendAdapter], of backend: String, in check: JSONValue) -> [String: AdapterCheck] {
        var rows = (check["rows"]?.arrayValue ?? []).filter { $0["section"]?.stringValue == "Adapters" }
        var found: [String: AdapterCheck] = [:]
        for adapter in adapters {
            guard let index = rows.firstIndex(where: { $0["name"]?.stringValue == "\(backend): \(adapter.file)" }) else { continue }
            let row = rows.remove(at: index)
            found[adapter.path] = AdapterCheck(
                status: row["status"]?.stringValue ?? "", detail: row["detail"]?.stringValue ?? "",
                remedy: row["remedy"]?.stringValue)
        }
        return found
    }
}
