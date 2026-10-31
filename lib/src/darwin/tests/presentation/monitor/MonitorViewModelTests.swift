import BusinessReads
import Foundation
import Testing

@testable import PresentationMonitor

@Suite("MonitorViewModel", .timeLimit(.minutes(1)))
@MainActor
struct MonitorViewModelTests {
    static let system = #"{"apogee":{"models_held":[],"process_bytes":3899920},"cpu":{"model":"Apple M3 Max","utilization":{"percent":12.5,"window_ms":500}},"disk":{"store_bytes":4000000000,"volume_available_bytes":500000000000,"volume_capacity_bytes":1000000000000},"gpu":{"model_budget_bytes":115448725504,"name":"Apple M3 Max","unified_memory":true},"memory":{"available_bytes":94586896384,"total_bytes":137438953472,"used_bytes":42852057088}}"#

    @Test("open, it reads the system every interval and the children's memory; closed, it reads no more")
    func pollsOnlyWhileOpen() async throws {
        let runner = ScriptedRunner()
        runner.reading(["system"], Self.system)
        let monitor = MonitorViewModel(runner: runner, interval: .milliseconds(40))
        monitor.connect(binary: URL(fileURLWithPath: "/Users/someone/.local/bin/apogee"))
        monitor.liveChildren = { [("Session: local", 4242)] }
        monitor.residentBytes = { $0 == 4242 ? 1_500_000_000 : nil }
        try await Task.sleep(for: .milliseconds(150))
        #expect(runner.calls.isEmpty, "closed: nothing is read")
        monitor.opened()
        monitor.opened()
        #expect(await eventually { runner.calls.count >= 3 })
        #expect(monitor.reading?.cpuPercent == 12.5)
        #expect(monitor.reading?.modelBudget == 115_448_725_504)
        #expect(MonitorViewModel.percent(monitor.reading?.memoryUsed, of: monitor.reading?.memoryTotal) == 31)
        #expect(monitor.children == [MonitorViewModel.Child(label: "Session: local", pid: 4242, bytes: 1_500_000_000)])
        monitor.closed()
        let after = runner.calls.count
        try await Task.sleep(for: .milliseconds(200))
        #expect(runner.calls.count <= after + 1, "closed: the poll stops")
        #expect(!monitor.isOpen)
    }

    @Test("with no binary it says so, and an unknown size is said unknown")
    func states() async {
        let monitor = MonitorViewModel(runner: ScriptedRunner())
        monitor.opened()
        await monitor.read()
        #expect(monitor.failure == "Apogee isn't installed, so the system can't be read.")
        monitor.closed()
        #expect(MonitorViewModel.size(nil) == "unknown")
        #expect(MonitorViewModel.size(0) == "0 bytes", "a number, never \"Zero KB\"")
        #expect(MonitorViewModel.percent(5, of: nil) == nil)
    }
}

@MainActor
func eventually(_ condition: @MainActor () -> Bool) async -> Bool {
    let deadline = ContinuousClock.now + .seconds(5)
    while !condition() {
        if ContinuousClock.now > deadline { return false }
        try? await Task.sleep(for: .milliseconds(10))
    }
    return true
}
