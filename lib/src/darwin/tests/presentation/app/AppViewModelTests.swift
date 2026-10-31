import DataPreferences
import PresentationShell
import Testing

@testable import PresentationApp

@Suite("AppViewModel")
@MainActor
struct AppViewModelTests {
    @Test("a bundled build names its version")
    func bundledVersion() {
        let model = AppViewModel(info: ["CFBundleShortVersionString": "0.2.0"], preferences: Preferences(store: MemoryStore()))
        #expect(model.title == "Apogee")
        #expect(model.versionLine == "Version 0.2.0")
    }

    @Test("a build with no version stamped says it is a development build")
    func unversioned() {
        let preferences = Preferences(store: MemoryStore())
        #expect(AppViewModel(info: nil, preferences: preferences).versionLine == "Development build")
        #expect(AppViewModel(info: ["CFBundleShortVersionString": ""], preferences: preferences).versionLine == "Development build")
    }

    @Test("Settings is handed the app's PATH resolver, and composing the app never asks the login shell")
    func childPathWired() async {
        let model = AppViewModel(info: nil, preferences: Preferences(store: MemoryStore()))
        await model.settings.readChildPath()
        #expect(!model.settings.childPathFromShell)
        #expect(model.settings.childPathOrigin?.contains("hasn't answered yet") == true, "the fallback, the shell never asked")
        #expect(model.settings.childPathDirectories.contains("/opt/homebrew/bin"))
    }

    @Test("the window opens where the app's preferences left it")
    func composesTheShell() {
        let store = MemoryStore(["route": "voice"])
        let model = AppViewModel(info: nil, preferences: Preferences(store: store))
        #expect(model.shell.route == .voice)
        model.shell.go(.sessions)
        #expect(store.string(forKey: "route") == "sessions")
    }
}

@Suite("Suites reach Sessions (GUI 4i)")
@MainActor
struct SuitesReachSessionsTests {
    @Test("the composition gives Suites its doors into Sessions, and asks the library whether a session can be switched")
    func doorsWired() {
        let model = AppViewModel(info: nil, preferences: Preferences(store: MemoryStore()))
        #expect(model.suites.openChat != nil && model.suites.switchSession != nil)
        #expect(model.suites.sessionSwitchable != nil)
        #expect(!model.suites.canSwitchSession, "nothing open before the library has started")
    }
}
