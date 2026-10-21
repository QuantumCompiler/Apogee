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

    @Test("the window opens where the app's preferences left it")
    func composesTheShell() {
        let store = MemoryStore(["route": "voice"])
        let model = AppViewModel(info: nil, preferences: Preferences(store: store))
        #expect(model.shell.route == .voice)
        model.shell.go(.sessions)
        #expect(store.string(forKey: "route") == "sessions")
    }
}
