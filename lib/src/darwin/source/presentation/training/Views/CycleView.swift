import BusinessJobs
import BusinessReads
import PresentationStyles
import SwiftUI

/// The unattended cycle: where it stands -- the backend it promotes into,
/// the anchor, the breaker, a halt and its reason, whether a pass holds it --
/// its passes, newest first, and one pass run here, live. Its schedule stays
/// the person's own, said at the top; with no history yet it says that,
/// rather than the read that exits 1.
struct CycleView: View {
    @Bindable var model: TrainingViewModel

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: Spacing.l) {
                Text(TrainingViewModel.scheduleWords)
                    .font(.apogeeSmall)
                    .foregroundStyle(Palette.textSecondary)
                    .fixedSize(horizontal: false, vertical: true)
                if let hold = model.cycleHoldText {
                    Text(hold)
                        .font(.apogeeSmall)
                        .foregroundStyle(Palette.warning)
                        .fixedSize(horizontal: false, vertical: true)
                        .textSelection(.enabled)
                }
                if model.live == .cycle, let run = model.run {
                    LivePass(run: run, stages: model.liveStages)
                }
                if let said = model.cycleText {
                    Text(said).font(.apogeeSmall).foregroundStyle(Palette.textTertiary).textSelection(.enabled)
                } else if let said = model.noHistoryText {
                    Text(said)
                        .font(.apogeeSmall)
                        .foregroundStyle(Palette.textTertiary)
                        .fixedSize(horizontal: false, vertical: true)
                } else if let cycle = model.cycle {
                    facts(cycle)
                    passes(cycle)
                }
            }
            .padding(Spacing.xl)
            .frame(maxWidth: .infinity, alignment: .leading)
        }
    }

    @ViewBuilder private func facts(_ cycle: CycleHistory) -> some View {
        HStack(alignment: .top, spacing: Spacing.xl) {
            fact("Promotes into", cycle.backend.isEmpty ? "—" : cycle.backend)
            fact("Anchor", TrainingViewModel.anchorWords(cycle))
            fact("State", model.cycleHeld ? "a pass is running" : cycle.halted ? "halted" : "idle")
            fact("Failed in a row", "\(cycle.consecutiveFails)")
            fact("Passes", "\(cycle.totalRuns)")
        }
        if !cycle.sessionsUntil.isEmpty {
            fact("Sessions read until", cycle.sessionsUntil)
        }
    }

    @ViewBuilder private func passes(_ cycle: CycleHistory) -> some View {
        Text("PASSES, NEWEST FIRST").font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary)
        if cycle.runs.isEmpty {
            Text("None yet.").font(.apogeeSmall).foregroundStyle(Palette.textTertiary)
        }
        ForEach(cycle.runs.reversed()) { pass in
            VStack(alignment: .leading, spacing: 2) {
                HStack {
                    Text(pass.runAt).font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary)
                    if !pass.pipelineID.isEmpty {
                        Text("pipeline \(pass.pipelineID)").font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary)
                    }
                }
                Text(TrainingViewModel.words(pass))
                    .font(.apogeeBody)
                    .foregroundStyle(pass.gate == "fail" ? Palette.error : pass.gate == "pass" ? Palette.textPrimary : Palette.textSecondary)
                if !pass.gateReason.isEmpty {
                    Text(pass.gateReason)
                        .font(.apogeeSmall)
                        .foregroundStyle(Palette.textSecondary)
                        .fixedSize(horizontal: false, vertical: true)
                        .textSelection(.enabled)
                }
                if let note = pass.note {
                    Text(note).font(.apogeeSmall).foregroundStyle(Palette.warning).textSelection(.enabled)
                }
            }
            .padding(.vertical, Spacing.xs)
        }
    }

    private func fact(_ label: String, _ value: String) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(label.uppercased()).font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary)
            Text(value).font(.apogeeBody).textSelection(.enabled)
        }
    }
}

/// A pass running here: its narration as it comes, the line it is on
/// strongest, and its pipeline's stages live.
private struct LivePass: View {
    let run: JobProgress
    let stages: [TrainingViewModel.StageRow]

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.s) {
            Text("THIS PASS, RUNNING HERE").font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary)
            ForEach(Array(run.passes.enumerated()), id: \.offset) { index, line in
                HStack(alignment: .firstTextBaseline, spacing: Spacing.m) {
                    Text(line.state).font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary).frame(width: 80, alignment: .leading)
                    Text(line.text)
                        .font(.apogeeSmall)
                        .foregroundStyle(index == run.passes.count - 1 ? Palette.textPrimary : Palette.textSecondary)
                        .fixedSize(horizontal: false, vertical: true)
                        .textSelection(.enabled)
                }
            }
            if run.passes.isEmpty {
                Text("Starting…").font(.apogeeSmall).foregroundStyle(Palette.textTertiary)
            }
            if !stages.isEmpty {
                StageList(stages: stages)
            }
        }
        .padding(Spacing.l)
        .background(Palette.card, in: RoundedRectangle(cornerRadius: Radius.row))
    }
}

#if DEBUG
#Preview("The cycle: no history yet") {
    let training = TrainingPreview.training(runner: TrainingPreview.noRuns)
    CycleView(model: training)
        .frame(width: 900, height: 360)
        .task { await training.connect(binary: previewBinary) }
}

#Preview("The cycle: a pass holds it") {
    let training = TrainingPreview.training(runner: TrainingPreview.cycleActive)
    TrainingScreen(model: training)
        .frame(width: 1100, height: 640)
        .task {
            await training.connect(binary: previewBinary)
            training.tab = .cycle
        }
}

#Preview("The cycle: halted by its breaker, Resume offered") {
    let training = TrainingPreview.training(runner: TrainingPreview.cycleHalted)
    TrainingScreen(model: training)
        .frame(width: 1100, height: 640)
        .task {
            await training.connect(binary: previewBinary)
            training.tab = .cycle
        }
}

#Preview("The cycle: Run one pass, asking first") {
    let training = TrainingPreview.training(runner: TrainingPreview.cycleIdle)
    TrainingScreen(model: training)
        .frame(width: 1100, height: 640)
        .task {
            await training.connect(binary: previewBinary)
            training.tab = .cycle
            training.pending = .runCycle
        }
}

#Preview("The cycle: a pass running here, live") {
    let training = TrainingPreview.training(said: TrainingPreview.cycleRunning, runner: TrainingPreview.cycleIdle)
    TrainingScreen(model: training)
        .frame(width: 1100, height: 720)
        .task {
            await training.connect(binary: previewBinary)
            training.tab = .cycle
            training.pending = .runCycle
            await training.confirm()
        }
}

#Preview("The cycle: idle, its passes") {
    let training = TrainingPreview.training(runner: TrainingPreview.cycleIdle)
    CycleView(model: training)
        .frame(width: 900, height: 560)
        .task { await training.connect(binary: previewBinary) }
}

#Preview("The cycle: Apogee not installed") {
    let training = TrainingPreview.training()
    CycleView(model: training)
        .frame(width: 900, height: 300)
        .task { await training.connect(binary: nil) }
}
#endif
