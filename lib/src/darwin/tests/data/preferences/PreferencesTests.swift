import DataPreferences
import Foundation
import Testing

@Suite("Preferences")
struct PreferencesTests {
    @Test("the binary override is unset until set, kept as a path, and cleared back")
    func binaryOverride() {
        let store = MemoryStore()
        let preferences = Preferences(store: store)
        #expect(preferences.binaryOverride == nil)
        preferences.binaryOverride = URL(fileURLWithPath: "/Users/someone/My Tools/apogee")
        #expect(store.string(forKey: "binaryOverride") == "/Users/someone/My Tools/apogee")
        #expect(Preferences(store: store).binaryOverride?.path == "/Users/someone/My Tools/apogee")
        preferences.binaryOverride = nil
        #expect(store.string(forKey: "binaryOverride") == nil)
        #expect(preferences.binaryOverride == nil)
    }

    @Test("an empty stored path is no override")
    func emptyIsNone() {
        #expect(Preferences(store: MemoryStore(["binaryOverride": ""])).binaryOverride == nil)
    }
}
