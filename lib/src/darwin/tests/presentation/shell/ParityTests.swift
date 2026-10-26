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

    @Test("today's truth: twenty-four screens, the gaps by their items, the console's two, three carve-outs, the plumbing")
    func census() {
        func count(_ match: (CommandPlace.Kind) -> Bool) -> Int { ParityTable.places.filter { match($0.kind) }.count }
        #expect(count { if case .screen = $0 { true } else { false } } == 24)
        #expect(count { if case .queued = $0 { true } else { false } } == 0)
        #expect(count { $0 == .console } == 2)
        #expect(count { if case .carvedOut = $0 { true } else { false } } == 3)
        #expect(count { $0 == .plumbing } == 3)
    }

    @Test("4i: the rows name what Training, Models, Suites and the session now cover -- adapters, pull, the CLI's fit, a chat under a suite")
    func trainingAndSuitesRows() throws {
        let train = try #require(ParityTable.place(of: "train"))
        #expect(train.note.contains("--adapter") && train.note.contains("--suite") && train.note.contains("--judge"))
        let datasets = try #require(ParityTable.place(of: "datasets"))
        #expect(datasets.note.contains("pull"))
        let chat = try #require(ParityTable.place(of: "chat"))
        #expect(chat.note.contains("--suite") && chat.note.contains("/suite"))
        let config = try #require(ParityTable.place(of: "config"))
        #expect(config.note.contains("set-backend --adapters") && config.note.contains("PresentationModels"))
        let models = try #require(ParityTable.place(of: "models"))
        #expect(models.note.contains("models status --suite"))
    }

    @Test("a verb an item placed is named on its family's row: chats show, a resumed chat's turns in the session window (M7)")
    func chatsShowPlaced() throws {
        let chats = try #require(ParityTable.place(of: "chats"))
        #expect(chats.kind == .screen(module: "PresentationSessions"))
        #expect(chats.note.contains("`chats show`"))
        #expect(chats.note.contains("session window (PresentationSession)"))
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

    @Test("the console refuses a carve-out by its row's reason, a door and an $EDITOR verb by theirs, and nothing else")
    func refusals() {
        #expect(ParityTable.refusal(of: ["reset", "-y"])?.reason.contains("data directory the running app stands on") == true)
        #expect(ParityTable.refusal(of: ["serve"])?.reason.contains("orphan") == true)
        #expect(ParityTable.refusal(of: ["uninstall"])?.reason.contains("never uninstalls the binary it runs") == true)
        #expect(ParityTable.refusal(of: ["chat", "-m", "local"])?.reason.contains("New session (⌘N)") == true)
        #expect(ParityTable.refusal(of: ["execute"])?.reason.contains("New execute session (⌥⌘N)") == true)
        #expect(ParityTable.refusal(of: ["tui"])?.reason.contains("full-screen shell's counterpart") == true)
        #expect(ParityTable.refusal(of: ["agents", "edit", "code-review"])?.words == ["agents", "edit"])
        #expect(ParityTable.refusal(of: ["symphonies", "edit", "digest"])?.reason.contains("$EDITOR") == true)
        for path in [["models"], ["chats", "list"], ["agents"], ["agents", "list"], ["symphonies", "play", "edit"], ["executes"], ["nothing"], []] {
            #expect(ParityTable.refusal(of: path) == nil, "\(path)")
        }
    }

    @Test("the refusals are the CLI exec line's set, word for word: the three doors, the two $EDITOR verbs, the three carve-outs -- each carve-out by its row's reason")
    func refusalSet() {
        // tui_parity.cpp's kRefusals, in its order.
        #expect(ParityTable.refusals.map { $0.words.joined(separator: " ") }
            == ["chat", "execute", "tui", "agents edit", "symphonies edit", "serve", "reset", "uninstall"])
        for place in ParityTable.places {
            guard case .carvedOut(let reason) = place.kind else { continue }
            #expect(ParityTable.refusal(of: [place.command])?.reason == reason, "\(place.command)")
        }
        for refusal in ParityTable.refusals {
            #expect(refusal.words.count <= 2 && !refusal.reason.isEmpty)
            #expect(ParityTable.place(of: refusal.words[0]) != nil, "a refusal names a classified command")
        }
    }

    @Test("a refusal of a command the binary no longer has fails by name, as does one with no reason")
    func refusalLaw() throws {
        let words = try fixtureWords()
        let planted = ParityTable.refusals + [Refusal("frobnicate edit", "it would"), Refusal("agents purge", "")]
        #expect(ParityTable.violations(words: words, refusals: planted) == [
            "the console refuses 'frobnicate edit', which the binary no longer has",
            "the console refuses 'agents purge' with no reason",
        ])
        #expect(ParityTable.violations(words: words.filter { $0 != "symphonies" }).contains("the console refuses 'symphonies edit', which the binary no longer has"))
    }
}
