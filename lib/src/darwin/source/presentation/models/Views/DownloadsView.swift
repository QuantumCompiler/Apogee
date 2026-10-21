import BusinessJobs
import PresentationStyles
import SwiftUI

/// The Downloads tab: the pull form, and each run's steps, bytes and log.
public struct DownloadsView: View {
    @Bindable private var model: DownloadsViewModel

    public init(model: DownloadsViewModel) {
        self.model = model
    }

    public var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: Spacing.l) {
                form
                Text("Each download runs as steps. A step that stops runs again from its start; the steps before it are kept and never repeated.")
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
            if download.isRunning {
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
