#if DEBUG
import BusinessReads
import Foundation

/// The system monitor's view-model for its previews (ADR 0004), over the
/// `system` document tests/presentation/monitor/MonitorViewModelTests.swift
/// holds (no `system` read is recorded): nothing starts the binary, and no
/// child's memory is read from this Mac.
@MainActor
enum MonitorPreview {
    /// An M3 Max: its CPU at 12.5%, 40 GiB of 128 used, a 1 TB volume half free.
    static let system = #"{"apogee":{"models_held":[],"process_bytes":3899920},"cpu":{"model":"Apple M3 Max","utilization":{"percent":12.5,"window_ms":500}},"disk":{"store_bytes":4000000000,"volume_available_bytes":500000000000,"volume_capacity_bytes":1000000000000},"gpu":{"model_budget_bytes":115448725504,"name":"Apple M3 Max","unified_memory":true},"memory":{"available_bytes":94586896384,"total_bytes":137438953472,"used_bytes":42852057088}}"#

    static let runner = PreviewRunner(reads: [(["system"], system)])

    /// A local session's child, and what it holds, as the tests give them.
    static let session: [(label: String, pid: Int32)] = [("Session: local", 4242)]

    /// The monitor over `system`, the app's live children `children`, each
    /// child's memory the tests' figure rather than the system's count.
    static func monitor(binary: URL? = previewBinary, children: [(label: String, pid: Int32)] = []) -> MonitorViewModel {
        let monitor = MonitorViewModel(runner: runner)
        monitor.connect(binary: binary)
        monitor.liveChildren = { children }
        monitor.residentBytes = { $0 == 4242 ? 1_500_000_000 : nil }
        return monitor
    }
}
#endif
