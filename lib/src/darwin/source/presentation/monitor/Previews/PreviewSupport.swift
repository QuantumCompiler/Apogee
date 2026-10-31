#if DEBUG
import BusinessReads
import DataProtocol
import Foundation

/// Preview support (ADR 0004). Each module keeps its own copy, as each test
/// bundle keeps its own scripted support.

/// Answers a preview's reads from the binary's own documents: nothing starts
/// the binary or touches the person's files.
struct PreviewRunner: CommandRunning {
    /// Argument prefixes and what they answer; the longest prefix wins.
    let reads: [([String], String)]

    func read(binary: URL, _ arguments: [String]) async -> ReadOutcome {
        guard let text = Self.answer(arguments, reads) else {
            return .couldNotStart("no preview document for apogee \(arguments.joined(separator: " "))")
        }
        // The document is DataProtocol's, which this module reaches only
        // through `ReadOutcome`: made by its initializer alone, never named.
        // Every preview document is one JSON object.
        return .document(ReadDocument(text: text)!)
    }

    func command(binary: URL, _ arguments: [String]) async -> CommandOutcome {
        .done(stdout: "")
    }

    static func answer(_ arguments: [String], _ rules: [([String], String)]) -> String? {
        rules.filter { arguments.starts(with: $0.0) }.max { $0.0.count < $1.0.count }?.1
    }
}

/// Where a preview says the binary is; never run.
let previewBinary = URL(fileURLWithPath: "/Users/someone/.local/bin/apogee")
#endif
