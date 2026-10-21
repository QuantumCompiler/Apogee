import Foundation
import Testing

@testable import PresentationShell

/// The parity law (ADR 0003): the table held to the binary's own command
/// words, recorded in `Fixtures/command-words.txt` by
/// `scripts/refresh-parity-fixture.sh`.
private let here = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
private let appRoot = here.deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
private let repoRoot = appRoot.deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()

private func fixtureWords() throws -> [String] {
    try String(contentsOf: here.appendingPathComponent("Fixtures/command-words.txt"), encoding: .utf8)
        .split(whereSeparator: \.isNewline).map(String.init)
}

@Suite("Parity")
struct ParityTests {
    @Test("every command word the binary answers has its place, and nothing classified has vanished")
    func everyWordPlaced() throws {
        let words = try fixtureWords()
        #expect(words.count >= 25, "the real word list, not a stub")
        #expect(ParityTable.violations(words: words) == [])
    }

    @Test("an unclassified word fails by name, a vanished command too; an empty list never passes")
    func lawHasTeeth() throws {
        let words = try fixtureWords()
        #expect(ParityTable.violations(words: words + ["frobnicate"]) == ["'frobnicate' has no place in the app -- classify it (ADR 0003)"])
        #expect(ParityTable.violations(words: words.filter { $0 != "voice" }) == ["'voice' is classified, but the binary no longer has it"])
        #expect(ParityTable.violations(words: []) == ["no command words -- the law would pass vacuously"])
    }

    @Test("today's truth: twenty-three screens, the gaps by their items, the console's two, three carve-outs, the plumbing")
    func census() {
        func count(_ match: (CommandPlace.Kind) -> Bool) -> Int { ParityTable.places.filter { match($0.kind) }.count }
        #expect(count { if case .screen = $0 { true } else { false } } == 23)
        #expect(count { if case .queued = $0 { true } else { false } } == 1)
        #expect(count { $0 == .console } == 2)
        #expect(count { if case .carvedOut = $0 { true } else { false } } == 3)
        #expect(count { $0 == .plumbing } == 3)
    }

    @Test("a screen names a module the app has: its folder exists")
    func screensAreReal() {
        for place in ParityTable.places {
            guard case .screen(let module) = place.kind else { continue }
            #expect(module.hasPrefix("Presentation"), "\(place.command): \(module) is no Presentation module")
            let package = module.dropFirst("Presentation".count).lowercased()
            let folder = appRoot.appendingPathComponent("source/presentation/\(package)").path
            #expect(FileManager.default.fileExists(atPath: folder), "\(place.command): no module at \(folder)")
        }
    }

    @Test("a gap names an item the backlog still holds: shipping it flips the row to its screen")
    func gapsAreQueued() throws {
        let index = try String(
            contentsOf: repoRoot.appendingPathComponent("lib/documentation/gui/backlog/README.md"), encoding: .utf8)
        for place in ParityTable.places {
            guard case .queued(let item) = place.kind else { continue }
            #expect(index.contains("| \(item) |"), "\(place.command): \(item) is no row of the backlog -- flip it to its screen")
        }
    }

    @Test("the console refuses a carve-out by its reason, and nothing else")
    func refusals() {
        #expect(ParityTable.refusal(of: "reset")?.contains("data directory the running app stands on") == true)
        #expect(ParityTable.refusal(of: "serve")?.contains("orphan") == true)
        #expect(ParityTable.refusal(of: "uninstall")?.contains("never uninstalls the binary it runs") == true)
        #expect(ParityTable.refusal(of: "models") == nil)
        #expect(ParityTable.refusal(of: "nothing") == nil)
    }
}
