#if DEBUG
import Foundation
import SwiftUI

/// The answers `MarkdownText`'s previews draw, read from the binary's own
/// recorded streams (ADR 0004): each the `answer_delta` texts joined, as the
/// session's transcript joins them, or a prefix of them as one stands
/// mid-stream.
enum StylesPreview {
    /// The text of every `answer_delta` in a recorded stream, in order.
    static func deltas(_ document: String) -> [String] {
        document.split(separator: "\n").compactMap { line in
            guard let event = try? JSONSerialization.jsonObject(with: Data(line.utf8)) as? [String: Any],
                event["type"] as? String == "answer_delta"
            else { return nil }
            return event["text"] as? String
        }
    }

    /// The deltas joined up to and including the first after which the
    /// answer holds `mark`: the answer as it stood when that arrived.
    static func prefix(_ document: String, through mark: String) -> String {
        var text = ""
        for delta in deltas(document) {
            text += delta
            if text.contains(mark) { break }
        }
        return text
    }

    /// The scripted mock's markdown answer, every shape the app draws
    /// (tests/data/protocol/Fixtures/markdown-answer.jsonl).
    static let markdownAnswer = deltas(StylesDocuments.markdownAnswer).joined()

    /// That answer's fenced block alone, fences and all.
    static var codeBlock: String {
        guard let start = markdownAnswer.range(of: "```python"),
            let end = markdownAnswer.range(of: "\n```\n", range: start.upperBound..<markdownAnswer.endIndex)
        else { return markdownAnswer }
        return String(markdownAnswer[start.lowerBound..<end.upperBound])
    }

    /// The answer as it stood mid-stream, its fence open: the code so far.
    static let openFence = prefix(StylesDocuments.markdownAnswer, through: "self.client.get")

    /// The answer as it stood with its table's header row arrived and its
    /// delimiter row not yet.
    static let tableHeader = prefix(StylesDocuments.markdownAnswer, through: "| Option | Keeps the batch | Cancels")

    /// A plain answer, no markdown in it: the resumed chat's live turn
    /// (tests/data/protocol/Fixtures/resumed-chat.jsonl).
    static let plainAnswer = deltas(StylesDocuments.resumedChat).joined()

    /// The scripted mock's answer with fences in Python, Swift, TypeScript,
    /// Rust, shell and JSON, a diff, an unnamed fence and an Elixir one
    /// (tests/data/protocol/Fixtures/code-answer.jsonl).
    static let codeAnswer = deltas(StylesDocuments.codeAnswer).joined()

    /// That answer's fence opening with `opening`, fences and all.
    static func fence(_ opening: String) -> String {
        guard let start = codeAnswer.range(of: opening),
            let end = codeAnswer.range(of: "\n```", range: start.upperBound..<codeAnswer.endIndex)
        else { return codeAnswer }
        return String(codeAnswer[start.lowerBound..<end.upperBound])
    }

    /// The code answer as it stood mid-stream: its TypeScript fence open, the
    /// stream's edge inside a template literal.
    static let openString = prefix(StylesDocuments.codeAnswer, through: "`attempt ${attempt} failed: $")

    /// The scripted mock's edge-case answer: a short arithmetic line, a table
    /// straight under prose, a here-document and shifts in shell and Ruby, a
    /// reference use and its link definitions, pipe-led prose, and a last
    /// line of one number (tests/data/protocol/Fixtures/edges-answer.jsonl).
    static let edgesAnswer = deltas(StylesDocuments.edgesAnswer).joined()

    /// That answer as it stood with its short arithmetic line half arrived.
    static let arithmeticArriving = prefix(StylesDocuments.edgesAnswer, through: "128 * 8")

    /// That answer as it stood with its table's first row arriving under
    /// the prose it follows.
    static let tableRowArriving = prefix(StylesDocuments.edgesAnswer, through: "| 2 | 512 | 10")

    /// That answer as it stood with its second link definition arriving.
    static let definitionsArriving = prefix(StylesDocuments.edgesAnswer, through: "[2]: https://b.exa")

    /// That answer as it stood with its pipe-led prose into its second line.
    static let pipeProseArriving = prefix(StylesDocuments.edgesAnswer, through: "|rows - 1024| is how far")

    /// That answer as it stood just after its Ruby fence opened: what a turn
    /// stopped there leaves, finished.
    static let endedAtAFence = prefix(StylesDocuments.edgesAnswer, through: "```r")

    /// The install `video generate`'s refusal names, as it names it
    /// (tests/business/media/Fixtures/generate-refused-runtime.err).
    static var installCommand: String {
        StylesDocuments.generateRefusedErr.components(separatedBy: " -- ").last?
            .trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
    }

    /// The first line of what `train setup` printed that starts with `prefix`
    /// (tests/business/media/Fixtures/install-video-*).
    static func installLine(_ document: String, startingWith prefix: String) -> String? {
        document.split(separator: "\n").first { $0.hasPrefix(prefix) }.map(String.init)
    }
}

extension View {
    /// Drawn in `theme` on the window's ground, as the app draws in the
    /// person's Appearance setting.
    func previewTheme(_ theme: Appearance.Theme) -> some View {
        Appearance.apply(theme: theme, scale: 1)
        return background(Palette.window).preferredColorScheme(Appearance.colorScheme)
    }
}
#endif
