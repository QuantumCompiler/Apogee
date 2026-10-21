import DataPreferences
import Testing

@testable import PresentationShell

@Suite("ShellViewModel")
@MainActor
struct ShellViewModelTests {
    @Test("a first launch opens at Sessions, with the sidebar shown and nothing over the window")
    func firstLaunch() {
        let shell = ShellViewModel(preferences: Preferences(store: MemoryStore()))
        #expect(shell.route == .sessions)
        #expect(shell.overlay == nil)
        #expect(!shell.sidebarHidden)
        #expect(shell.toast == nil)
    }

    @Test("every place is shown by going to it, and a relaunch opens where the window was left", arguments: Destination.allCases)
    func routesRestore(destination: Destination) {
        let store = MemoryStore()
        let shell = ShellViewModel(preferences: Preferences(store: store))
        shell.go(destination)
        #expect(shell.route == destination)
        #expect(ShellViewModel(preferences: Preferences(store: store)).route == destination)
    }

    @Test("a remembered place this build does not have opens Sessions")
    func unknownRoute() {
        let shell = ShellViewModel(preferences: Preferences(store: MemoryStore(["route": "compose-studio"])))
        #expect(shell.route == .sessions)
    }

    @Test("each command does what its key promises")
    func commands() {
        let store = MemoryStore()
        let shell = ShellViewModel(preferences: Preferences(store: store))
        shell.perform(.go(.graph))
        #expect(shell.route == .graph)
        shell.perform(.modelsLibrary)
        #expect(shell.route == .models)
        shell.perform(.toggleMonitor)
        #expect(shell.overlay == .monitor)
        shell.perform(.go(.tasks))
        #expect(shell.overlay == .monitor, "moving between places leaves the monitor open")
        shell.perform(.toggleMonitor)
        #expect(shell.overlay == nil)
        shell.perform(.toggleSidebar)
        #expect(shell.sidebarHidden)
        #expect(ShellViewModel(preferences: Preferences(store: store)).sidebarHidden, "hiding the sidebar is remembered")
        shell.perform(.toggleSidebar)
        #expect(!shell.sidebarHidden)
        shell.perform(.openSettings)
        #expect(shell.route == .tasks && shell.overlay == nil, "Settings is its own scene; the window does not move")
    }

    @Test("an overlay opens and closes")
    func overlays() {
        let shell = ShellViewModel(preferences: Preferences(store: MemoryStore()))
        shell.open(.monitor)
        #expect(shell.overlay == .monitor)
        shell.closeOverlay()
        #expect(shell.overlay == nil)
    }

    @Test("below 900 points the sidebar becomes icons and side panes close; hidden is hidden at any width")
    func windowLaw() {
        let shell = ShellViewModel(preferences: Preferences(store: MemoryStore()))
        #expect(shell.layout == ShellLayout(width: nil, sidebarHidden: false))
        #expect(shell.layout.sidebar == .full && shell.layout.sidePanes, "before the first layout, the full window")
        shell.windowResized(width: 960)
        #expect(shell.layout.sidebar == .full && shell.layout.sidePanes)
        shell.windowResized(width: 900)
        #expect(shell.layout.sidebar == .full, "900 is not below 900")
        shell.windowResized(width: 899.5)
        #expect(shell.layout.sidebar == .icons && !shell.layout.sidePanes)
        shell.setSidebarHidden(true)
        #expect(shell.layout.sidebar == .hidden && !shell.layout.sidePanes)
        shell.windowResized(width: 1400)
        #expect(shell.layout.sidebar == .hidden && shell.layout.sidePanes)
    }

    @Test("the toast says one line, and only its own time ends it")
    func toast() {
        let shell = ShellViewModel(preferences: Preferences(store: MemoryStore()))
        shell.say("Renamed")
        let first = shell.toast
        #expect(first?.text == "Renamed")
        shell.say("Deleted")
        shell.dismissToast(first!.id)
        #expect(shell.toast?.text == "Deleted", "an older toast's timer leaves the newer one")
        shell.dismissToast(shell.toast!.id)
        #expect(shell.toast == nil)
        #expect(Toast.lifetime == .seconds(5))
    }
}
