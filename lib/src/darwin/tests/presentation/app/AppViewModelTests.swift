import Testing

@testable import PresentationApp

@Suite("AppViewModel")
@MainActor
struct AppViewModelTests {
    @Test("a bundled build names its version")
    func bundledVersion() {
        let model = AppViewModel(info: ["CFBundleShortVersionString": "0.2.0"])
        #expect(model.title == "Apogee")
        #expect(model.versionLine == "Version 0.2.0")
    }

    @Test("a build with no version stamped says it is a development build")
    func unversioned() {
        #expect(AppViewModel(info: nil).versionLine == "Development build")
        #expect(AppViewModel(info: ["CFBundleShortVersionString": ""]).versionLine == "Development build")
    }
}
