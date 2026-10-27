import DataProtocol
import Foundation
import InfrastructureProcess

/// The reads that answered in prose alone until the CLI's M31 (2026-10-26)
/// gave each a document: `chats info`, `chats recall`, `graph dedupe` and
/// `knowledge info --raw` -- and `models status --suite`'s footprint, typed
/// beside its text in the same document (GUI M15).
public enum ProseReads {
    /// The vocabulary's date from which the binary answers those reads with
    /// `--output-format json` (the CLI's M31). A development build between
    /// the CLI's M29 and M31 announces this date too without the documents:
    /// it refuses the flag, and the read falls back to its text
    /// (`CommandRunning.documentOrText`), so the date only says what to try
    /// first.
    public static let documentsSince = "2026-10-26"

    /// Whether a binary announcing `vocabulary` -- `__machine-schema`'s date,
    /// read at launch -- is asked for the documents first. `nil`, a binary
    /// that hasn't said, is read as text: today's reading.
    public static func asksDocuments(vocabulary: String?) -> Bool {
        (vocabulary ?? "") >= documentsSince
    }
}

/// What one of those reads gave: the document's typed facts, the text a
/// command printed, or why neither came, in the CLI's words.
public enum DocumentOrText<Typed: Sendable & Equatable>: Sendable, Equatable {
    /// The binary answered with its document, and it carried what is read.
    case document(Typed)
    /// The text the read printed: run as text on an older binary, or after
    /// the binary refused the flag or answered without the field.
    case text(String)
    /// Neither: the CLI's own reason.
    case failed(String)
}

extension ReadOutcome {
    /// Whether the binary refused `--output-format` itself -- its argument
    /// parser's "The following arguments were not expected", exit 109 -- as
    /// a build from before the read had a document does.
    public var refusedTheFormat: Bool {
        guard case .failed(let exit, let tail) = self else { return false }
        return (exit.reason == .exited && exit.status == 109) || tail.text.contains("arguments were not expected")
    }
}

extension CommandRunning {
    /// One of the reads M31 gave a document (ADR 0006's additive growth,
    /// read from both sides): asked as a document first where `asksDocument`
    /// -- `ProseReads.asksDocuments` of the binary's vocabulary -- and
    /// `decode` reads its facts; else run as the command a person types, as
    /// before.
    ///
    /// A binary that refuses the flag (exit 109), whatever its date, or a
    /// document without the field `decode` needs, falls back to the text:
    /// today's reading, never a failure. Any other failure of the document is
    /// the CLI's word on the read itself -- a chat it can't find -- said as
    /// it is, with nothing run again.
    public func documentOrText<Typed: Sendable & Equatable>(
        binary: URL, _ arguments: [String], asksDocument: Bool, _ decode: @Sendable (ReadDocument) -> Typed?
    ) async -> DocumentOrText<Typed> {
        if asksDocument {
            let outcome = await read(binary: binary, arguments)
            if let document = outcome.document {
                if let typed = decode(document) { return .document(typed) }
            } else if !outcome.refusedTheFormat {
                return .failed(outcome.failure ?? "apogee printed nothing")
            }
        }
        let printed = await command(binary: binary, arguments)
        if case .done(let stdout) = printed { return .text(stdout) }
        return .failed(printed.failure ?? "apogee printed nothing")
    }
}
