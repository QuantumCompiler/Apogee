import BusinessReads
import Foundation
import Testing

@testable import PresentationSessions

/// Every positional of a Sessions line after `--` (GUI M23): a chat's id is
/// the CLI's and never begins with a dash, but the rule holds for every
/// positional, so the line needs no idea of which words are ids; a title is
/// the person's, and shares the id's `--`.
@Suite("Session names")
struct SessionNamesTests {
    @Test("delete, title, info, compact and show put the id after --; a title shares its --")
    func lines() {
        #expect(SessionLibraryViewModel.deleteArguments("c1") == ["chats", "delete", "--", "c1"])
        #expect(SessionLibraryViewModel.titleArguments("c1", "-h") == ["chats", "title", "--", "c1", "-h"])
        #expect(SessionLibraryViewModel.infoArguments("c1") == ["chats", "info", "--", "c1"])
        #expect(SessionLibraryViewModel.compactArguments("c1") == ["chats", "compact", "--yes", "--", "c1"])
        #expect(SavedChat.arguments(id: "c1") == ["chats", "show", "--", "c1"])
    }

    @Test("the dialogs' lines are the lines that run, as a person types them: -- only before a dash-led word")
    func shown() {
        #expect(Argv.shown(SessionLibraryViewModel.deleteArguments("20261022-182803-f9f6")) == "chats delete 20261022-182803-f9f6")
        #expect(Argv.shown(SessionLibraryViewModel.compactArguments("-x")) == "chats compact --yes -- -x")
        #expect(Argv.shown(SessionLibraryViewModel.titleArguments("c1", "…")) == "chats title c1 …")
    }

    @Test("every line Sessions builds with -x, -h or --graph reaches the verb: each recorded from the built CLI, none the parser's refusal")
    func sweep() {
        var lines: [[String]] = []
        for name in NameVerdicts.names {
            lines += [
                SessionLibraryViewModel.deleteArguments(name), SessionLibraryViewModel.titleArguments(name, "Ports"),
                SessionLibraryViewModel.titleArguments("20261022-182803-f9f6", name), SessionLibraryViewModel.compactArguments(name),
                SessionLibraryViewModel.infoArguments(name),
                Argv.adding(["--output-format", "json"], to: SessionLibraryViewModel.infoArguments(name)),
                Argv.adding(["--output-format", "json"], to: SavedChat.arguments(id: name)),
                Argv.adding(["--output-format", "json"], to: Argv.line(["models", "info"], names: [name])),
            ]
        }
        let (missing, refused) = NameVerdicts.judged(lines)
        #expect(missing.isEmpty, "never recorded -- add these rows to tests/presentation/shell/Fixtures/argv-name-verdicts.txt and run scripts/refresh-argv-name-verdicts.sh <the built apogee>:\n\(missing.joined(separator: "\n"))")
        #expect(refused.isEmpty, "the parser refuses:\n\(refused.joined(separator: "\n"))")
    }
}
