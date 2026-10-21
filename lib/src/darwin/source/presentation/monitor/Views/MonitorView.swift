import BusinessReads
import PresentationShell
import PresentationStyles
import SwiftUI

/// The system monitor, over the window: the machine every two seconds while
/// it is open, and what the app's own children hold.
public struct MonitorView: View {
    @Bindable private var model: MonitorViewModel
    private let close: () -> Void

    public init(model: MonitorViewModel, close: @escaping () -> Void) {
        self.model = model
        self.close = close
    }

    public var body: some View {
        VStack(spacing: 0) {
            ScreenHeader(title: "System monitor") {
                Text("every 2 s while open").font(.apogeeMeta).foregroundStyle(Palette.textTertiary)
                Button("Close", action: close).keyboardShortcut(.cancelAction)
            }
            Divider()
            VStack(alignment: .leading, spacing: Spacing.l) {
                if let reading = model.reading {
                    meter("CPU", reading.cpuModel, percent: reading.cpuPercent.map { Int($0.rounded()) },
                        value: reading.cpuPercent.map { String(format: "%.0f%%", $0) } ?? "unknown")
                    meter("Memory", "", percent: MonitorViewModel.percent(reading.memoryUsed, of: reading.memoryTotal),
                        value: "\(MonitorViewModel.size(reading.memoryUsed)) of \(MonitorViewModel.size(reading.memoryTotal))")
                    row("GPU", (reading.gpuName ?? "unknown") + " · models may use " + MonitorViewModel.size(reading.modelBudget))
                    meter("Disk", "the model store's volume",
                        percent: MonitorViewModel.percent(reading.diskCapacity.flatMap { c in reading.diskAvailable.map { c - $0 } }, of: reading.diskCapacity),
                        value: "\(MonitorViewModel.size(reading.diskAvailable)) free · the store holds \(MonitorViewModel.size(reading.storeBytes))")
                } else if let failure = model.failure {
                    Text(failure).font(.apogeeSmall).foregroundStyle(Palette.error)
                } else {
                    ProgressView().controlSize(.small)
                }
                VStack(alignment: .leading, spacing: Spacing.xs) {
                    Text("THE APP'S APOGEE CHILDREN")
                        .font(.system(size: TypeScale.label, weight: .medium, design: .monospaced))
                        .foregroundStyle(Palette.textTertiary)
                    if model.children.isEmpty {
                        Text("None running.").font(.apogeeSmall).foregroundStyle(Palette.textSecondary)
                    }
                    ForEach(model.children) { child in
                        HStack {
                            Text(child.label).font(.apogeeSmall)
                            Spacer()
                            Text(MonitorViewModel.size(child.bytes)).font(.apogeeMeta.monospacedDigit())
                            Text("pid " + String(child.pid)).font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary)
                        }
                    }
                }
            }
            .padding(Spacing.xl)
        }
        .frame(width: 520)
        .background(Palette.window)
        .onAppear { model.opened() }
        .onDisappear { model.closed() }
    }

    private func meter(_ label: String, _ detail: String, percent: Int?, value: String) -> some View {
        VStack(alignment: .leading, spacing: Spacing.xs) {
            HStack {
                Text(label).font(.apogeeTitle)
                Text(detail).font(.apogeeMeta).foregroundStyle(Palette.textTertiary)
                Spacer()
                Text(value).font(.apogeeMeta.monospacedDigit()).foregroundStyle(Palette.textSecondary)
            }
            if let percent {
                ProgressView(value: Double(percent), total: 100).tint(percent >= 90 ? Palette.warning : Palette.accent)
            }
        }
    }

    private func row(_ label: String, _ value: String) -> some View {
        HStack {
            Text(label).font(.apogeeTitle)
            Spacer()
            Text(value).font(.apogeeMeta).foregroundStyle(Palette.textSecondary)
        }
    }
}
