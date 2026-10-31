import BusinessReads
import Foundation
import InfrastructureProcess
import Observation

/// The system monitor: `system --output-format json` every two seconds while
/// it is open -- and only then -- and the memory each of the app's own
/// children holds, read from the system by their process ids.
///
/// SwiftUI-free (ADR 0002).
@MainActor
@Observable
public final class MonitorViewModel {
    /// One of the app's children: what it is, and what it holds.
    public struct Child: Equatable, Identifiable, Sendable {
        public let label: String
        public let pid: Int32
        public let bytes: Int?

        public var id: Int32 { pid }
    }

    public private(set) var reading: SystemReading?
    public private(set) var failure: String?
    public private(set) var children: [Child] = []
    /// Whether it is reading now: the monitor is open.
    public private(set) var isOpen = false

    /// The app's live children, by what they are: the composition's.
    public var liveChildren: @MainActor () -> [(label: String, pid: Int32)] = { [] }
    /// The resident bytes of a pid: the system's count, in the app.
    public var residentBytes: @Sendable (Int32) -> Int? = { ProcessMemory.resident(pid: $0) }

    private let runner: any CommandRunning
    private let interval: Duration
    private var binary: URL?
    private var polling: Task<Void, Never>?

    public init(runner: any CommandRunning, interval: Duration = .seconds(2)) {
        self.runner = runner
        self.interval = interval
    }

    public func connect(binary: URL?) {
        self.binary = binary
    }

    /// The monitor opened: read now, and every interval until it closes.
    public func opened() {
        guard !isOpen else { return }
        isOpen = true
        polling = Task { [weak self] in
            while let self, self.isOpen, !Task.isCancelled {
                await self.read()
                try? await Task.sleep(for: self.interval)
            }
        }
    }

    /// The monitor closed: no more reads.
    public func closed() {
        isOpen = false
        polling?.cancel()
        polling = nil
    }

    /// One read: the system, and the children.
    public func read() async {
        guard let binary else {
            failure = "Apogee isn't installed, so the system can't be read."
            return
        }
        let outcome = await runner.read(binary: binary, ["system"])
        guard isOpen else { return }
        if let document = outcome.document {
            reading = SystemReading(document: document)
            failure = nil
        } else {
            failure = outcome.failure
        }
        children = liveChildren().map { Child(label: $0.label, pid: $0.pid, bytes: residentBytes($0.pid)) }
    }

    /// Percent of `part` in `whole`, 0-100, when both are known.
    public static func percent(_ part: Int?, of whole: Int?) -> Int? {
        guard let part, let whole, whole > 0 else { return nil }
        return Int((Double(part) / Double(whole) * 100).rounded())
    }

    public static func size(_ bytes: Int?) -> String {
        guard let bytes else { return "unknown" }
        let formatter = ByteCountFormatter()
        formatter.countStyle = .memory
        formatter.allowsNonnumericFormatting = false
        return formatter.string(fromByteCount: Int64(bytes))
    }
}
