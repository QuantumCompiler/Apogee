import BusinessJobs
import PresentationStyles
import SwiftUI

/// The Jobs tab: the pull form, and each run -- a pull, a conversion, a
/// quantization, a check -- with its steps, bytes or time, and log.
public struct DownloadsView: View {
    @Bindable private var model: DownloadsViewModel

    public init(model: DownloadsViewModel) {
        self.model = model
    }

    public var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: Spacing.l) {
                form
                Text("Downloads, conversions and checks run as steps. A step that stops runs again from its start; the steps before it are kept and never repeated.")
                    .font(.apogeeSmall)
                    .foregroundStyle(Palette.textSecondary)
                // Read so a change to any download redraws the list.
                let _ = model.revision
                ForEach(model.downloads) { download in
                    DownloadCard(download: download, model: model)
                }
            }
            .padding(Spacing.xl)
            .frame(maxWidth: Layout.mediaContentMax)
            .frame(maxWidth: .infinity)
        }
    }

    private var form: some View {
        VStack(alignment: .leading, spacing: Spacing.s) {
            Text("Download from Hugging Face or Ollama").font(.apogeeTitle)
            HStack {
                TextField("owner/repo, owner/repo:file.gguf, or name:tag", text: $model.reference)
                    .textFieldStyle(.roundedBorder)
                    .onSubmit { model.start() }
                Button("Download") { model.start() }
                    .keyboardShortcut(.return, modifiers: .command)
                    .disabled(!model.canStart)
            }
            Toggle("Full weights, then convert and register them (--safetensors)", isOn: $model.convert)
            if model.convert {
                HStack(spacing: Spacing.s) {
                    Text("Levels").foregroundStyle(Palette.textSecondary)
                    ForEach(DownloadsViewModel.levels, id: \.self) { level in
                        Toggle(level, isOn: Binding(
                            get: { model.chosenLevels.contains(level) },
                            set: { if $0 { model.chosenLevels.insert(level) } else { model.chosenLevels.remove(level) } }))
                            .toggleStyle(.button)
                    }
                }
                .font(.apogeeMeta.monospaced())
            }
            if let arguments = model.formArguments {
                Text((["apogee"] + arguments).joined(separator: " "))
                    .font(.apogeeCode)
                    .foregroundStyle(Palette.textTertiary)
                    .textSelection(.enabled)
            }
        }
        .padding(Spacing.l)
        .background(Palette.card, in: RoundedRectangle(cornerRadius: Radius.card))
        .overlay(RoundedRectangle(cornerRadius: Radius.card).strokeBorder(Palette.border))
    }
}

/// One download: what it is, where it stands, and what can be done.
struct DownloadCard: View {
    let download: DownloadsViewModel.Download
    let model: DownloadsViewModel
    @State private var logOpen = false

    var body: some View {
        let progress = download.progress
        VStack(alignment: .leading, spacing: Spacing.s) {
            HStack(spacing: Spacing.s) {
                Text(download.reference).font(.apogeeCode).foregroundStyle(Palette.textPrimary)
                Spacer()
                status(progress)
                if download.isRunning {
                    Button("Stop") { Task { await model.stop(download) } }
                } else {
                    if let resume = progress.resumeCommand {
                        Button("Run \(resume.prefix(2).joined(separator: " "))…") { model.resume(download) }
                            .help((["apogee"] + resume).joined(separator: " "))
                    } else if progress.status != .done {
                        Button("Run again") { model.again(download) }
                    }
                    Button("Dismiss") { model.dismiss(download) }
                }
                Button(logOpen ? "Hide log" : "Log") { logOpen.toggle() }.buttonStyle(.link)
            }
            if !download.steps.isEmpty {
                VStack(alignment: .leading, spacing: Spacing.xs) {
                    ForEach(Array(download.steps.enumerated()), id: \.offset) { index, step in
                        let state = download.state(of: index)
                        HStack(spacing: Spacing.s) {
                            Image(systemName: state == .done ? "checkmark.circle" : state == .running ? "circle.dotted" : "circle")
                                .foregroundStyle(state == .waiting ? Palette.textTertiary : Palette.textSecondary)
                            Text("\(index + 1)").font(.apogeeMeta.monospacedDigit()).foregroundStyle(Palette.textTertiary)
                            Text(step.label).font(.apogeeSmall)
                            if let estimate = step.estimate { Text(estimate).font(.apogeeMeta).foregroundStyle(Palette.textTertiary) }
                        }
                    }
                }
            }
            if download.kind != .pull {
                TimelineView(.periodic(from: .now, by: 1)) { _ in
                    Text((download.isRunning ? "running for " : "ran for ") + model.elapsed(download) + (download.isRunning ? " -- no percent: the verb says none (G18)" : ""))
                        .font(.apogeeMeta)
                        .foregroundStyle(Palette.textTertiary)
                }
            }
            if download.isRunning, download.kind != .pull, let busy = progress.busy {
                Text(busy).font(.apogeeMeta.monospaced()).foregroundStyle(Palette.textTertiary).lineLimit(1)
            }
            if let outcome = download.outcome {
                Text(outcome).font(.apogeeSmall).foregroundStyle(Palette.textSecondary).textSelection(.enabled)
            }
            if !download.isRunning, download.kind != .pull, download.kind != .check, let first = download.firstLanded {
                Button("Show the weights") { model.showWeights?(first) }
            }
            if !progress.stages.isEmpty {
                VStack(alignment: .leading, spacing: Spacing.xs) {
                    ForEach(progress.orderedStages, id: \.index) { entry in
                        HStack(spacing: Spacing.s) {
                            Image(systemName: Self.symbol(entry.stage.state))
                                .foregroundStyle(entry.stage.state == "failed" ? Palette.error : Palette.textSecondary)
                            Text("\(entry.index)").font(.apogeeMeta.monospacedDigit()).foregroundStyle(Palette.textTertiary)
                            Text(entry.stage.label).font(.apogeeSmall)
                        }
                    }
                }
            }
            if download.isRunning && download.kind == .pull {
                if let fraction = progress.bytes?.fraction {
                    ProgressView(value: fraction)
                } else {
                    ProgressView().progressViewStyle(.linear)
                }
                Text(Self.detail(progress))
                    .font(.apogeeMeta.monospaced())
                    .foregroundStyle(Palette.textTertiary)
                    .lineLimit(1)
            }
            if case .failed(let message) = progress.status {
                Text(message).font(.apogeeSmall).foregroundStyle(Palette.error).textSelection(.enabled)
            }
            if logOpen {
                Text((progress.notices + (progress.stderr.isEmpty ? [] : ["", progress.stderr])).joined(separator: "\n"))
                    .font(.apogeeCode)
                    .foregroundStyle(Palette.textSecondary)
                    .textSelection(.enabled)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(Spacing.s)
                    .background(Palette.code, in: RoundedRectangle(cornerRadius: Radius.control))
            }
        }
        .padding(Spacing.l)
        .background(Palette.card, in: RoundedRectangle(cornerRadius: Radius.card))
        .overlay(RoundedRectangle(cornerRadius: Radius.card).strokeBorder(Palette.border))
    }

    @ViewBuilder
    private func status(_ progress: JobProgress) -> some View {
        switch progress.status {
        case .running:
            HStack(spacing: Spacing.xs) {
                Circle().fill(Palette.accent).frame(width: 6, height: 6)
                Text(DownloadsViewModel.now(progress))
            }
            .font(.apogeeSmall)
            .foregroundStyle(Palette.textSecondary)
        case .stopped: Text("stopped").font(.apogeeSmall).foregroundStyle(Palette.warning)
        case .failed: Text("failed").font(.apogeeSmall).foregroundStyle(Palette.error)
        case .done: Text("done").font(.apogeeSmall).foregroundStyle(Palette.accent)
        }
    }

    static func symbol(_ state: String) -> String {
        switch state {
        case "done": "checkmark.circle"
        case "started": "circle.dotted"
        case "failed": "xmark.circle"
        case "stopped": "pause.circle"
        default: "circle"
        }
    }

    /// The bytes as the stream counts them, or the busy line as said.
    static func detail(_ progress: JobProgress) -> String {
        if let bytes = progress.bytes {
            let done = ByteCountFormatter.string(fromByteCount: Int64(bytes.done), countStyle: .file)
            let total = bytes.total.map { " of " + ByteCountFormatter.string(fromByteCount: Int64($0), countStyle: .file) } ?? ""
            let file = bytes.file.map { " · \($0)" + (bytes.index.map { i in " (\(i) of \(bytes.count ?? i))" } ?? "") } ?? ""
            return done + total + file
        }
        return progress.busy ?? ""
    }
}

#if DEBUG
#Preview("The pull form: full weights, converted at two levels") {
    let jobs = ModelsPreview.jobs()
    DownloadsView(model: jobs)
        .frame(width: 900, height: 420)
        .task {
            jobs.reference = "Qwen/Qwen3-8B"
            jobs.convert = true
            jobs.chosenLevels = ["Q4_K_M", "Q8_0"]
        }
}

#Preview("A conversion running its first step") {
    DownloadsView(model: ModelsPreview.converting())
        .frame(width: 900, height: 560)
}

// A card reads its download, which is not observed: each card here is drawn
// anew as its job speaks (`.id`), as the Jobs tab's list redraws on `revision`.

#Preview("A pull streaming: its first stage and its bytes") {
    @Previewable @State var jobs = ModelsPreview.pulling()
    if let download = jobs.downloads.first {
        DownloadCard(download: download, model: jobs)
            .id(jobs.revision)
            .padding(Spacing.xl)
            .frame(width: 760)
    }
}

#Preview("A conversion running: its steps and its time") {
    @Previewable @State var jobs = ModelsPreview.converting()
    if let download = jobs.downloads.first {
        DownloadCard(download: download, model: jobs)
            .id(jobs.revision)
            .padding(Spacing.xl)
            .frame(width: 760)
    }
}

#Preview("A check passed, in the CLI's words") {
    @Previewable @State var jobs = ModelsPreview.checked()
    if let download = jobs.downloads.first {
        DownloadCard(download: download, model: jobs)
            .id(jobs.revision)
            .padding(Spacing.xl)
            .frame(width: 760)
    }
}

#Preview("A pull failed at its last step, the CLI's way on offered") {
    @Previewable @State var jobs = ModelsPreview.pullFailed()
    if let download = jobs.downloads.first {
        DownloadCard(download: download, model: jobs)
            .id(jobs.revision)
            .padding(Spacing.xl)
            .frame(width: 760)
    }
}
#endif
