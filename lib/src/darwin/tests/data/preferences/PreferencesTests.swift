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

    @Test("the route and the sidebar's state are remembered, and a store without them is the defaults")
    func windowState() {
        let store = MemoryStore()
        let preferences = Preferences(store: store)
        #expect(preferences.route == nil)
        #expect(!preferences.sidebarHidden)
        preferences.route = "models"
        preferences.sidebarHidden = true
        let again = Preferences(store: store)
        #expect(again.route == "models")
        #expect(again.sidebarHidden)
        again.sidebarHidden = false
        #expect(store.string(forKey: "sidebarHidden") == nil)
        #expect(Preferences(store: MemoryStore(["route": ""])).route == nil)
    }

    @Test("the media folders the person added are kept, one a line, and none is absent")
    func mediaFolders() {
        let store = MemoryStore()
        let preferences = Preferences(store: store)
        #expect(preferences.videoFolders.isEmpty)
        preferences.videoFolders = ["/Users/someone/Clips", "/Volumes/Big/Video"]
        #expect(store.string(forKey: "videoFolders") == "/Users/someone/Clips\n/Volumes/Big/Video")
        #expect(Preferences(store: store).videoFolders == ["/Users/someone/Clips", "/Volumes/Big/Video"])
        preferences.voiceFolders = []
        #expect(store.string(forKey: "voiceFolders") == nil)
    }
}
