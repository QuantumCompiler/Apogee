import BusinessReads
import DataPreferences
import Foundation
import Testing

@testable import PresentationSessions

/// A title is never an option (GUI 4g): on the branch binary `chats title
/// <id> "-x title"` answered "title is required"; after `--` it is the
/// title.
@Suite("Chat title after --", .timeLimit(.minutes(1)))
@MainActor
struct ChatTitleTextTests {
    @Test("a dash-leading name is the title: chats title <id> -- <name>, read back")
    func dashTitle() async {
        let runner = ScriptedRunner()
        runner.reading(["chats", "list"], SessionLibraryTests.twoChats)
        let library = SessionLibraryViewModel(
            starter: ScriptedStarter(sessions: [ScriptedSession()]), runner: runner, preferences: Preferences(store: MemoryStore()))
        var said: [String] = []
        library.say = { said.append($0) }
        await library.start(inView: false)
        await library.rename(library.chats[1], to: " -- draft, v2 ")
        #expect(runner.calls.suffix(2) == ["run chats title c1 -- -- draft, v2", "read chats list"])
        #expect(said == ["Renamed to -- draft, v2"])
    }
}
