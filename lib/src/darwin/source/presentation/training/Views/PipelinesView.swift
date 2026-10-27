import BusinessJobs
import BusinessReads
import PresentationStyles
import SwiftUI

/// The named pipelines, each run on the person's word -- with its stage
/// count, its student and its page's stage plan where the binary lists them
/// (the CLI's M30); every pipeline run, newest first; and the open page -- a
/// spec's plan, or a run's stages as the manifest records them, live while it
/// runs here (the CLI's 55h), with Resume where it stopped short.
struct PipelinesView: View {
    @Bindable var model: TrainingViewModel

    var body: some View {
        HStack(spacing: 0) {
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 1) {
                    heading("NAMED PIPELINES")
                    ForEach(model.pipelineSpecs, id: \.self) { name in
                        HStack {
                            if let spec = model.specs.first(where: { $0.name == name }) {
                                // Listed by the binary: its page is its plan.
                                Button {
                                    Task { await model.openSpec(name) }
                                } label: {
                                    VStack(alignment: .leading, spacing: 2) {
                                        Text(name).font(.apogeeBody)
                                        Text(TrainingViewModel.words(spec)).font(.apogeeMeta).foregroundStyle(Palette.textTertiary)
                                    }
                                    .frame(maxWidth: .infinity, alignment: .leading)
                                    .contentShape(Rectangle())
                                }
                                .buttonStyle(.plain)
                                .help("apogee train pipeline show \(name): its stages, each a dataset and a gate")
                            } else {
                                Text(name).font(.apogeeBody)
                                Spacer()
                            }
                            Button("Run…") { model.pending = .runPipeline(name) }
                                .buttonStyle(.borderless)
                                .disabled(model.isRunning)
                                .help("apogee train pipeline run --pipeline \(name): each stage a training run, held to the cumulative gate")
                        }
                        .padding(.horizontal, Spacing.m)
                        .padding(.vertical, Spacing.s)
                        .background(
                            model.openedSpec == name ? Palette.selection : .clear, in: RoundedRectangle(cornerRadius: Radius.row))
                    }
                    if let said = model.specsText {
                        note(said)
                    }
                    heading("RUNS").padding(.top, Spacing.l)
                    if model.live == .pipeline {
                        row(selected: model.selectedPipelineRun == nil && model.openedSpec == nil, open: nil) {
                            Text(model.livePipelineID ?? model.running ?? "Starting").font(.apogeeBody.monospacedDigit())
                            Text("running here").font(.apogeeMeta).foregroundStyle(Palette.accent)
                        }
                    }
                    ForEach(model.pipelineRuns.filter { $0.id != model.livePipelineID }) { run in
                        row(selected: model.selectedPipelineRun == run.id, open: run.id) {
                            Text(run.id).font(.apogeeBody.monospacedDigit())
                            Text(TrainingViewModel.words(run))
                                .font(.apogeeMeta)
                                .foregroundStyle(run.isResumable ? Palette.error : Palette.textTertiary)
                                .lineLimit(1)
                        }
                    }
                    if let said = model.pipelinesText {
                        note(said)
                    } else if model.pipelineRuns.isEmpty && model.live != .pipeline {
                        note("No pipeline runs yet.")
                    }
                }
                .padding(Spacing.s)
            }
            .frame(width: 320)
            .background(Palette.sidebar)
            Divider()
            PipelinePage(model: model)
        }
    }

    private func heading(_ text: String) -> some View {
        Text(text).font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary).padding(.horizontal, Spacing.m)
    }

    private func note(_ text: String) -> some View {
        Text(text)
            .font(.apogeeSmall)
            .foregroundStyle(Palette.textTertiary)
            .padding(.horizontal, Spacing.m)
            .padding(.vertical, Spacing.s)
            .fixedSize(horizontal: false, vertical: true)
            .textSelection(.enabled)
    }

    private func row<Content: View>(selected: Bool, open: String?, @ViewBuilder content: () -> Content) -> some View {
        Button {
            Task { await model.openPipelineRun(open) }
        } label: {
            VStack(alignment: .leading, spacing: 2, content: content)
                .padding(Spacing.m)
                .frame(maxWidth: .infinity, alignment: .leading)
                .background(selected ? Palette.selection : .clear, in: RoundedRectangle(cornerRadius: Radius.row))
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }
}

/// The open page: a named pipeline's plan, the run going here, live, or a
/// run's manifest as last read -- its stages, and where a resume would start.
private struct PipelinePage: View {
    let model: TrainingViewModel

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: Spacing.m) {
                if let name = model.openedSpec {
                    if let spec = model.openSpecPlan {
                        plan(spec)
                    } else if let failure = model.specFailure {
                        Text(name).font(.apogeeHeading)
                        Text(failure).font(.apogeeSmall).foregroundStyle(Palette.error).textSelection(.enabled)
                    } else {
                        Text("Reading \(name)…").font(.apogeeSmall).foregroundStyle(Palette.textTertiary)
                    }
                } else if model.showsLivePipeline {
                    Text(model.livePipelineID ?? model.running ?? "Starting").font(.apogeeHeading.monospacedDigit())
                    Text("Running here, each stage as its manifest moves -- Stop is above; what passed stays.")
                        .font(.apogeeSmall)
                        .foregroundStyle(Palette.textSecondary)
                    StageList(stages: model.pipelineStages)
                } else if let run = model.openPipeline {
                    manifest(run)
                } else if let failure = model.pipelineRunFailure {
                    Text(failure).font(.apogeeSmall).foregroundStyle(Palette.error).textSelection(.enabled)
                } else if let id = model.selectedPipelineRun {
                    Text("Reading \(id)…").font(.apogeeSmall).foregroundStyle(Palette.textTertiary)
                } else {
                    Text(model.specs.isEmpty ? "Choose a pipeline run to see its stages." : "Choose a named pipeline to see its plan, or a run to see its stages.")
                        .font(.apogeeSmall)
                        .foregroundStyle(Palette.textTertiary)
                }
            }
            .padding(Spacing.xl)
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Palette.inspector)
    }

    /// A named pipeline's plan, as `train pipeline show` states it: its
    /// student, and each stage's dataset, gate and tuning -- and Run….
    @ViewBuilder private func plan(_ spec: PipelineSpec) -> some View {
        HStack(alignment: .firstTextBaseline) {
            Text(spec.name).font(.apogeeHeading).textSelection(.enabled)
            Spacer()
            Button("Run…") { model.pending = .runPipeline(spec.name) }
                .disabled(model.isRunning)
                .help("apogee train pipeline run --pipeline \(spec.name): each stage a training run, held to the cumulative gate")
        }
        Text(((spec.student.isEmpty ? [] : ["student \(spec.student)"]) + [TrainingViewModel.source(spec)]).joined(separator: " · "))
            .font(.apogeeSmall)
            .foregroundStyle(Palette.textSecondary)
            .textSelection(.enabled)
        VStack(alignment: .leading, spacing: Spacing.s) {
            Text("STAGE PLAN").font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary)
            ForEach(spec.stages) { stage in
                HStack(alignment: .firstTextBaseline, spacing: Spacing.m) {
                    Text("\(stage.index)")
                        .font(.apogeeMeta.monospacedDigit())
                        .foregroundStyle(Palette.textTertiary)
                        .frame(width: 18, alignment: .trailing)
                    VStack(alignment: .leading, spacing: 2) {
                        Text(stage.name).font(.apogeeBody)
                        Text(TrainingViewModel.planWords(stage)).font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textPrimary)
                        Text(TrainingViewModel.tuningWords(stage)).font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary)
                    }
                    .textSelection(.enabled)
                    Spacer()
                }
            }
            if spec.stages.isEmpty {
                Text("The spec names no stage.").font(.apogeeSmall).foregroundStyle(Palette.textTertiary)
            }
        }
        Text(TrainingViewModel.gateRule)
            .font(.apogeeMeta)
            .foregroundStyle(Palette.textTertiary)
            .fixedSize(horizontal: false, vertical: true)
    }

    @ViewBuilder private func manifest(_ run: PipelineRun) -> some View {
        Text(run.id).font(.apogeeHeading.monospacedDigit()).textSelection(.enabled)
        Text([run.specName, run.status, run.student.isEmpty ? "" : "on \(run.student)"].filter { !$0.isEmpty }.joined(separator: " · "))
            .font(.apogeeSmall)
            .foregroundStyle(run.isResumable ? Palette.error : Palette.textSecondary)
        Text([run.startedAt, run.completedAt ?? ""].filter { !$0.isEmpty }.joined(separator: " → "))
            .font(.apogeeMeta.monospaced())
            .foregroundStyle(Palette.textTertiary)
        StageList(stages: model.pipelineStages)
        if let resume = TrainingViewModel.resumeWords(run) {
            HStack {
                Text(resume).font(.apogeeSmall)
                Spacer()
                Button("Resume…") { model.pending = .resumePipeline(run.id) }
                    .disabled(!model.canResume(run))
                    .help("apogee train pipeline resume \(run.id): from the first stage that has not passed")
            }
        }
        Text("Each stage's run is an ordinary run: gate it and promote it from Runs.")
            .font(.apogeeMeta)
            .foregroundStyle(Palette.textTertiary)
    }
}

/// A pipeline's stages: each its manifest's word, its own run, its gate's
/// score once gated, and the trainer's facts while it trains.
struct StageList: View {
    let stages: [TrainingViewModel.StageRow]

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.s) {
            Text("STAGES").font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary)
            ForEach(stages) { stage in
                HStack(alignment: .firstTextBaseline, spacing: Spacing.m) {
                    Text("\(stage.index)")
                        .font(.apogeeMeta.monospacedDigit())
                        .foregroundStyle(Palette.textTertiary)
                        .frame(width: 18, alignment: .trailing)
                    VStack(alignment: .leading, spacing: 2) {
                        Text(stage.name).font(.apogeeBody)
                        Text(TrainingViewModel.words(stage)).font(.apogeeMeta.monospaced()).foregroundStyle(Self.color(stage.state))
                        if let runID = stage.runID {
                            Text("run \(runID)").font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary).textSelection(.enabled)
                        }
                    }
                    Spacer()
                }
            }
            if stages.isEmpty {
                Text("No stage reported yet.").font(.apogeeSmall).foregroundStyle(Palette.textTertiary)
            }
        }
    }

    /// A stage's word in its colour: passed, failed, at work, or waiting.
    static func color(_ state: String) -> Color {
        switch state {
        case "passed": Palette.accent
        case "failed": Palette.error
        case "pending": Palette.textTertiary
        default: Palette.textPrimary
        }
    }
}

#if DEBUG
#Preview("Pipelines: none named, none run") {
    let training = TrainingPreview.training(runner: TrainingPreview.noPipelines)
    PipelinesView(model: training)
        .frame(width: 960, height: 420)
        .task {
            await training.connect(binary: previewBinary)
            training.tab = .pipelines
        }
}

#Preview("Pipelines: two named, their runs, one open") {
    let training = TrainingPreview.training(runner: TrainingPreview.pipelines)
    PipelinesView(model: training)
        .frame(width: 960, height: 560)
        .task {
            await training.connect(binary: previewBinary)
            await training.openPipelineRun(TrainingPreview.completeID)
        }
}

#Preview("Pipelines: the specs listed, skills' plan open") {
    let training = TrainingPreview.training(runner: TrainingPreview.specsListed)
    PipelinesView(model: training)
        .frame(width: 960, height: 560)
        .task {
            await training.connect(binary: previewBinary)
            await training.openSpec("skills")
        }
}

#Preview("Pipelines: the specs listed, none open") {
    let training = TrainingPreview.training(runner: TrainingPreview.specsListed)
    PipelinesView(model: training)
        .frame(width: 960, height: 560)
        .task { await training.connect(binary: previewBinary) }
}

#Preview("Pipelines: one running, stage by stage") {
    let training = TrainingPreview.training(said: TrainingPreview.pipelineRunning, runner: TrainingPreview.noRuns)
    PipelinesView(model: training)
        .frame(width: 960, height: 560)
        .task {
            await training.connect(binary: previewBinary)
            training.pending = .runPipeline("skills")
            await training.confirm()
        }
}

#Preview("Pipelines: failed at a stage, Resume offered") {
    let training = TrainingPreview.training(runner: TrainingPreview.aborted)
    PipelinesView(model: training)
        .frame(width: 960, height: 560)
        .task {
            await training.connect(binary: previewBinary)
            await training.openPipelineRun(TrainingPreview.abortedID)
        }
}

#Preview("Pipelines: Apogee not installed") {
    let training = TrainingPreview.training()
    PipelinesView(model: training)
        .frame(width: 960, height: 360)
        .task { await training.connect(binary: nil) }
}

#Preview("A pipeline run: Resume, asking first") {
    let training = TrainingPreview.training(runner: TrainingPreview.aborted)
    TrainingScreen(model: training)
        .frame(width: 1100, height: 600)
        .task {
            await training.connect(binary: previewBinary)
            training.tab = .pipelines
            await training.openPipelineRun(TrainingPreview.abortedID)
            training.pending = .resumePipeline(TrainingPreview.abortedID)
        }
}

#Preview("Stages: one passed, one training") {
    StageList(stages: TrainingViewModel.stages(of: TrainingPreview.folded(TrainingPreview.pipelineRunning)))
        .padding()
        .frame(width: 420)
}

#Preview("Stages: the manifest of a run that failed its gate") {
    StageList(stages: TrainingViewModel.stages(of: TrainingPreview.abortedRun))
        .padding()
        .frame(width: 420)
}

#Preview("Stages: none reported yet") {
    StageList(stages: [])
        .padding()
        .frame(width: 420)
}
#endif
