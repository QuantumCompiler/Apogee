import BusinessSession
import Foundation
import Testing

@Suite("Mentions")
struct MentionsTests {
    @Test("the @ being typed is the last word's, after a space or at the start; a finished one is none")
    func typing() {
        #expect(Mentions.typing(in: "look at @src/ma") == "src/ma")
        #expect(Mentions.typing(in: "@") == "")
        #expect(Mentions.typing(in: "mail me@example.com") == nil, "an @ inside a word is not a mention")
        #expect(Mentions.typing(in: "see @notes.md please") == nil)
        #expect(Mentions.typing(in: "no mention") == nil)
    }

    @Test("completions are the named folder's entries that begin with what is typed, folders marked, hidden ones only when asked")
    func candidates() throws {
        let folder = FileManager.default.temporaryDirectory.appendingPathComponent("mentions-\(UUID().uuidString)")
        defer { try? FileManager.default.removeItem(at: folder) }
        for directory in ["src", "src/main", "scripts"] {
            try FileManager.default.createDirectory(at: folder.appendingPathComponent(directory), withIntermediateDirectories: true)
        }
        for file in ["README.md", ".env", "src/main.swift", "src/Model.swift"] {
            try Data().write(to: folder.appendingPathComponent(file))
        }
        #expect(Mentions.candidates(for: "s", in: folder) == ["scripts/", "src/"])
        #expect(Mentions.candidates(for: "", in: folder) == ["README.md", "scripts/", "src/"])
        #expect(Mentions.candidates(for: ".", in: folder) == [".env"])
        #expect(Mentions.candidates(for: "src/m", in: folder) == ["src/Model.swift", "src/main.swift", "src/main/"])
        #expect(Mentions.candidates(for: "nowhere/x", in: folder).isEmpty)
        #expect(Mentions.candidates(for: "", in: folder, limit: 1).count == 1)
    }
}
