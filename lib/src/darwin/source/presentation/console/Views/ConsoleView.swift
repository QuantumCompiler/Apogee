import PresentationShell
import PresentationStyles
import SwiftUI

/// The console, over the window (⇧⌘K): any `apogee` command run as typed,
/// its output as the command wrote it, Tab answered by the binary.
public struct ConsoleView: View {
    @Bindable private var model: ConsoleViewModel
    private let close: () -> Void
    @State private var back = 0
    @FocusState private var focused: Bool

    public init(model: ConsoleViewModel, close: @escaping () -> Void) {
        self.model = model
        self.close = close
    }

    public var body: some View {
        VStack(spacing: 0) {
            ScreenHeader(title: "Console") {
                Text("apogee, as typed").font(.apogeeMeta).foregroundStyle(Palette.textTertiary)
                Button("Clear") { model.clear() }.disabled(model.lines.isEmpty)
                Button("Close", action: close).keyboardShortcut(.cancelAction)
            }
            Divider()
            ScrollViewReader { proxy in
                // Lines keep their width, as a terminal keeps a table's
                // columns: the transcript scrolls sideways instead of wrapping.
                ScrollView([.vertical, .horizontal]) {
                    LazyVStack(alignment: .leading, spacing: 2) {
                        if model.lines.isEmpty {
                            Text("Any apogee command runs here, as you'd type it at a prompt. Tab asks apogee what can come next; serve, reset and uninstall stay at the prompt.")
                                .font(.apogeeSmall)
                                .foregroundStyle(Palette.textTertiary)
                                .frame(width: 620, alignment: .leading)
                        }
                        ForEach(model.lines) { line in
                            Text(line.text)
                                .font(line.kind == .command ? .apogeeCode.weight(.semibold) : .apogeeCode)
                                .foregroundStyle(Self.color(line.kind))
                                .textSelection(.enabled)
                                .fixedSize(horizontal: true, vertical: false)
                                .padding(.top, line.kind == .command ? Spacing.s : 0)
                                .id(line.id)
                        }
                    }
                    .padding(Spacing.l)
                    .frame(maxWidth: .infinity, alignment: .leading)
                }
                .onChange(of: model.lines.count) {
                    if let last = model.lines.last { proxy.scrollTo(last.id, anchor: .bottom) }
                }
            }
            .background(Palette.window)
            Divider()
            if model.isRunning {
                HStack(spacing: Spacing.s) {
                    ProgressView().controlSize(.small)
                    Text(model.busy ?? "Running \(model.running ?? "")")
                        .font(.apogeeMeta.monospaced())
                        .foregroundStyle(Palette.textSecondary)
                        .lineLimit(1)
                        .truncationMode(.middle)
                    Spacer()
                    Button("Stop") { Task { await model.stop() } }
                }
                .padding(.horizontal, Spacing.l)
                .padding(.vertical, Spacing.s)
            }
            if !model.suggestions.isEmpty {
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: Spacing.xs) {
                        ForEach(model.suggestions, id: \.self) { candidate in
                            Button(candidate) {
                                model.accept(candidate)
                                focused = true
                            }
                            .buttonStyle(.bordered)
                            .controlSize(.small)
                            .font(.apogeeCode)
                        }
                    }
                    .padding(.horizontal, Spacing.l)
                    .padding(.top, Spacing.s)
                }
            }
            HStack(spacing: Spacing.s) {
                Text("apogee").font(.apogeeCode).foregroundStyle(Palette.textTertiary)
                TextField("models list", text: $model.input)
                    .textFieldStyle(.plain)
                    .font(.apogeeCode)
                    .focused($focused)
                    .onSubmit {
                        back = 0
                        model.run()
                    }
                    .onKeyPress(.tab) {
                        Task { await model.complete() }
                        return .handled
                    }
                    .onKeyPress(.upArrow) {
                        guard let line = model.recall(back + 1) else { return .ignored }
                        back += 1
                        model.input = line
                        return .handled
                    }
                    .onKeyPress(.downArrow) {
                        guard back > 0 else { return .ignored }
                        back -= 1
                        model.input = back == 0 ? "" : (model.recall(back) ?? "")
                        return .handled
                    }
                    .onKeyPress(.escape) {
                        guard !model.suggestions.isEmpty else { return .ignored }
                        model.dismissSuggestions()
                        return .handled
                    }
                    .disabled(model.isRunning)
                Button("Run") {
                    back = 0
                    model.run()
                }
                .disabled(model.isRunning || model.input.trimmingCharacters(in: .whitespaces).isEmpty)
            }
            .padding(Spacing.l)
        }
        .frame(minWidth: 720, minHeight: 480)
        .background(Palette.window)
        .onAppear { focused = true }
    }

    static func color(_ kind: ConsoleViewModel.Line.Kind) -> Color {
        switch kind {
        case .command: Palette.textPrimary
        case .out: Palette.textPrimary
        case .err: Palette.textSecondary
        case .note: Palette.textTertiary
        case .problem: Palette.error
        }
    }
}

#if DEBUG
#Preview("Nothing run yet") {
    ConsoleView(model: ConsolePreview.console()) {}
}

#Preview("A command refused, then one run to its end") {
    let console = ConsolePreview.console(saying: ConsolePreview.scan, finishes: true)
    ConsoleView(model: console) {}
        .frame(width: 900, height: 560)
        .task {
            console.input = "reset"
            console.run()
            console.input = "config scan"
            console.run()
        }
}

#Preview("A door refused: chat, after a root option") {
    let console = ConsolePreview.console()
    ConsoleView(model: console) {}
        .frame(width: 900, height: 420)
        .task {
            console.input = "apogee --dev chat"
            console.run()
        }
}

#Preview("An editor refused: agents edit") {
    let console = ConsolePreview.console()
    ConsoleView(model: console) {}
        .frame(width: 900, height: 420)
        .task {
            // An agent the recorded agents list names (tests/business/reads/Fixtures/agents-list.json).
            console.input = "agents edit code-review"
            console.run()
        }
}

#Preview("Running: a symphony at its first stage") {
    let console = ConsolePreview.console(saying: ConsolePreview.playing)
    ConsoleView(model: console) {}
        .frame(width: 900, height: 480)
        .task {
            console.input = #"symphonies play critique-revise --input "A draft README for the tool." --output-format json"#
            console.run()
        }
}

#Preview("A run that failed, its stderr kept") {
    let console = ConsolePreview.console(saying: ConsolePreview.refused, finishes: true)
    ConsoleView(model: console) {}
        .frame(width: 900, height: 480)
        .task {
            console.input = "review mr acme/widget#12 --dry-run --output-format json"
            console.run()
        }
}

#Preview("Tab: the binary's candidates for the word") {
    let console = ConsolePreview.console()
    ConsoleView(model: console) {}
        .task {
            console.input = "models li"
            await console.complete()
        }
}
#endif
