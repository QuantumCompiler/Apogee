import DataProtocol
import PresentationStyles
import SwiftUI

/// A session with nothing said yet: what it is, the model it runs on, and the
/// first acts -- or, private, what is and is not kept.
struct EmptySessionView: View {
    @Bindable var model: SessionViewModel

    var body: some View {
        VStack(spacing: Spacing.l) {
            switch model.privateMode {
            case .some(let mode):
                Image(systemName: "eye.slash")
                    .font(.system(size: 22))
                    .foregroundStyle(Palette.textSecondary)
                    .frame(width: 44, height: 44)
                    .background(Palette.control, in: RoundedRectangle(cornerRadius: 10))
                Text("Private session").font(.apogeeDisplay).foregroundStyle(Palette.textPrimary)
                Text(SessionViewModel.privateWords(mode))
                    .font(.apogeeProse)
                    .foregroundStyle(Palette.textSecondary)
                    .multilineTextAlignment(.center)
                    .frame(maxWidth: 460)
            case nil:
                Text("What are we working on?").font(.apogeeDisplay).foregroundStyle(Palette.textPrimary)
            }
            if let name = model.transcript.model {
                Text(name)
                    .font(.apogeeMeta.monospaced())
                    .foregroundStyle(Palette.textSecondary)
                    .padding(.horizontal, Spacing.s)
                    .padding(.vertical, Spacing.xxs)
                    .background(Palette.control, in: RoundedRectangle(cornerRadius: Radius.chip))
            }
            HStack(spacing: Spacing.s) {
                Button("Attach a folder") { model.presenting = .files }
                Button("Choose a model") { model.presenting = .settings }
                if !model.isExecute {
                    Button("Choose a suite") { model.onAction?(.chooseSuite) }
                        .disabled(!model.canSwitchSuite)
                }
                if model.privateMode == nil {
                    Button("Private session instead") { model.onAction?(.newPrivateSession) }
                }
            }
            Text("/ commands · @ mention a file · ⏎ send")
                .font(.apogeeMeta.monospaced())
                .foregroundStyle(Palette.textTertiary)
        }
        .padding(Spacing.xxl)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

/// The context meter in the header: a bar, and the words.
struct MeterView: View {
    let meter: ContextMeter

    var body: some View {
        HStack(spacing: Spacing.s) {
            Text("Context").foregroundStyle(Palette.textTertiary)
            if let percent = meter.percent {
                ZStack(alignment: .leading) {
                    Capsule().fill(Palette.border)
                    Capsule().fill(meter.warns ? Palette.warning : Palette.accent)
                        .frame(width: 80 * CGFloat(percent) / 100)
                }
                .frame(width: 80, height: 5)
                Text("\(percent)%").foregroundStyle(meter.warns ? Palette.warning : Palette.textSecondary)
            } else {
                Text(meter.text).foregroundStyle(Palette.textTertiary)
            }
        }
        .font(.apogeeMeta.monospacedDigit())
        .help(meter.window == nil
            ? "The model's window isn't known to the app, so how full it is can't be shown."
            : "\(meter.text). Warns at 80%; at 90% apogee summarizes older turns and says so.")
        .accessibilityElement(children: .combine)
        .accessibilityLabel("Context window: \(meter.text)")
    }
}

/// What the session holds attached, as chips above the input -- a resumed
/// chat's own among them -- each detachable when the build takes `detach`
/// (the CLI's 52d), hidden otherwise.
struct AttachmentChips: View {
    @Bindable var model: SessionViewModel

    var body: some View {
        if !model.attachments.isEmpty {
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: Spacing.xs) {
                    ForEach(model.attachments, id: \.self) { path in
                        HStack(spacing: Spacing.xs) {
                            Image(systemName: "paperclip")
                            Text(path).lineLimit(1)
                            if model.canDetach {
                                Button {
                                    model.detach(path)
                                } label: {
                                    Image(systemName: "xmark")
                                }
                                .buttonStyle(.plain)
                                .accessibilityLabel("Detach \(path)")
                            }
                        }
                        .font(.apogeeMeta.monospaced())
                        .foregroundStyle(Palette.textSecondary)
                        .padding(.horizontal, Spacing.s)
                        .padding(.vertical, Spacing.xxs)
                        .background(Palette.control, in: RoundedRectangle(cornerRadius: Radius.chip))
                    }
                }
            }
        }
    }
}

/// What waits for the turn to end -- a setting, an attach, a detach -- said
/// above the input until it is sent: none of it is applied yet, since the CLI
/// drops a line sent while a turn asks (52d).
struct HeldLines: View {
    @Bindable var model: SessionViewModel

    var body: some View {
        if !model.held.isEmpty {
            Label(
                "Sent when this turn ends: " + model.held.map(\.words).joined(separator: " · "),
                systemImage: "clock.arrow.circlepath"
            )
            .font(.apogeeMeta)
            .foregroundStyle(Palette.textTertiary)
            .help("A session takes these between turns; sent now, the turn's question would drop them.")
        }
    }
}

/// The list the input offers: slash commands after `/`, the symphonies after
/// `/play `, paths after `@`.
struct SuggestionList: View {
    @Bindable var model: SessionViewModel

    var body: some View {
        let commands = model.slashSuggestions
        let plays = model.playSuggestions
        let mentions = model.mentionSuggestions
        if !commands.isEmpty || !plays.isEmpty || !mentions.isEmpty {
            VStack(alignment: .leading, spacing: 0) {
                ForEach(commands) { command in
                    Button {
                        model.run(command)
                    } label: {
                        HStack(spacing: Spacing.m) {
                            Text(command.name).font(.apogeeCode).foregroundStyle(Palette.textPrimary)
                            Text(command.summary).font(.apogeeSmall).foregroundStyle(Palette.textTertiary)
                            Spacer()
                        }
                        .padding(.horizontal, Spacing.m)
                        .frame(height: 28)
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                }
                ForEach(plays) { play in
                    Button {
                        model.complete(play: play.name)
                    } label: {
                        HStack(spacing: Spacing.m) {
                            Image(systemName: "music.note.list").foregroundStyle(Palette.textTertiary)
                            Text(play.name).font(.apogeeCode).foregroundStyle(Palette.textPrimary)
                            Text(play.description).font(.apogeeSmall).foregroundStyle(Palette.textTertiary).lineLimit(1)
                            Spacer()
                        }
                        .padding(.horizontal, Spacing.m)
                        .frame(height: 28)
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                }
                ForEach(mentions, id: \.self) { path in
                    Button {
                        model.complete(mention: path)
                    } label: {
                        HStack(spacing: Spacing.s) {
                            Image(systemName: path.hasSuffix("/") ? "folder" : "doc")
                            Text(path).font(.apogeeCode)
                            Spacer()
                        }
                        .foregroundStyle(Palette.textPrimary)
                        .padding(.horizontal, Spacing.m)
                        .frame(height: 26)
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                }
            }
            .padding(.vertical, Spacing.xs)
            .background(Palette.card, in: RoundedRectangle(cornerRadius: Radius.card))
            .overlay(RoundedRectangle(cornerRadius: Radius.card).strokeBorder(Palette.border))
        }
    }
}

/// The run settings: the model, thinking, effort, temperature, the system
/// prompt. Applied as the `set` line -- held while a turn runs, sent when it
/// ends -- or on an older build by restarting the session with the flags and
/// `--resume`, which it says.
struct SettingsSheet: View {
    @Bindable var model: SessionViewModel
    @State private var draft: RunSettings
    @Environment(\.dismiss) private var dismiss

    init(model: SessionViewModel) {
        self.model = model
        _draft = State(initialValue: model.intendedSettings)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.l) {
            Text("Session settings").font(.apogeeHeading)
            Form {
                TextField("Model", text: binding(\.model), prompt: Text(model.transcript.model ?? "the configured default"))
                if !model.modelChoices.isEmpty {
                    Picker("Configured", selection: binding(\.model)) {
                        Text("—").tag("")
                        ForEach(model.modelChoices, id: \.self) { Text($0).tag($0) }
                    }
                }
                Picker("Thinking", selection: binding(\.think)) {
                    Text("As configured").tag("")
                    Text("On").tag("on")
                    Text("Off").tag("off")
                    Text("Auto").tag("auto")
                }
                Picker("Effort", selection: binding(\.effort)) {
                    Text("As configured").tag("")
                    ForEach(["low", "medium", "high", "xhigh", "max"], id: \.self) { Text($0.capitalized).tag($0) }
                }
                TextField("Temperature", value: $draft.temperature, format: .number)
                TextField("System prompt", text: binding(\.system), axis: .vertical)
                    .lineLimit(3...6)
            }
            Text(footer)
                .font(.apogeeSmall)
                .foregroundStyle(Palette.textTertiary)
                .fixedSize(horizontal: false, vertical: true)
            HStack {
                Spacer()
                Button("Cancel", role: .cancel) { dismiss() }.keyboardShortcut(.cancelAction)
                Button("Apply") {
                    let change = model.intendedSettings.changes(to: draft)
                    Task { await model.apply(change) }
                    dismiss()
                }
                .keyboardShortcut(.defaultAction)
                .disabled(model.intendedSettings.changes(to: draft).isEmpty)
            }
        }
        .padding(Spacing.xl)
        .frame(width: 520)
    }

    private var footer: String {
        guard model.canSet else {
            return "This apogee changes these by restarting the session: the conversation carries on (--resume), and a local model loads again."
        }
        return model.takesLinesNow
            ? "Applied from the next turn; apogee says each change."
            : "A turn is running: the change is sent when it ends, and applied from the turn after; apogee says each change."
    }

    private func binding(_ key: WritableKeyPath<RunSettings, String?>) -> Binding<String> {
        Binding(get: { draft[keyPath: key] ?? "" }, set: { draft[keyPath: key] = $0.isEmpty ? nil : $0 })
    }
}

/// A file change on a permission card: the counts, and the unified diff on
/// demand (the CLI's 52a carries it as the question's `detail`).
struct DiffView: View {
    let diff: String
    @State private var shown = false

    var body: some View {
        let lines = diff.split(separator: "\n", omittingEmptySubsequences: false).map(String.init)
        VStack(alignment: .leading, spacing: Spacing.xs) {
            HStack(spacing: Spacing.s) {
                Text("+\(DiffView.count(lines, "+"))").foregroundStyle(Palette.local)
                Text("−\(DiffView.count(lines, "-"))").foregroundStyle(Palette.error)
                Button(shown ? "Hide changes" : "Show changes") { shown.toggle() }
                    .buttonStyle(.link)
            }
            .font(.apogeeMeta.monospacedDigit())
            if shown {
                VStack(alignment: .leading, spacing: 0) {
                    ForEach(Array(lines.enumerated()), id: \.offset) { _, line in
                        Text(line.isEmpty ? " " : line)
                            .font(.apogeeCode)
                            .foregroundStyle(DiffView.color(line))
                            .frame(maxWidth: .infinity, alignment: .leading)
                    }
                }
                .textSelection(.enabled)
                .padding(Spacing.s)
                .background(Palette.code, in: RoundedRectangle(cornerRadius: Radius.control))
            }
        }
    }

    /// Lines added or removed, headers aside.
    static func count(_ lines: [String], _ sign: String) -> Int {
        lines.filter { $0.hasPrefix(sign) && !$0.hasPrefix(sign + sign + sign) }.count
    }

    static func color(_ line: String) -> Color {
        if line.hasPrefix("+++") || line.hasPrefix("---") || line.hasPrefix("@@") { return Palette.textTertiary }
        if line.hasPrefix("+") { return Palette.local }
        if line.hasPrefix("-") { return Palette.error }
        return Palette.textSecondary
    }
}

#if DEBUG
#Preview("A new session") {
    let model = SessionPreview.session(SessionPreview.mockActor)
    EmptySessionView(model: model)
        .frame(width: 760, height: 420)
        .task { await SessionPreview.ready(model) }
}

#Preview("Private: nothing written") {
    let model = SessionPreview.session([SessionPreview.newerBuild], launch: SessionPreview.privately)
    EmptySessionView(model: model)
        .frame(width: 760, height: 460)
        .task { await SessionPreview.ready(model) }
}

#Preview("Private on an older build: deleted on close") {
    let model = SessionPreview.session([SessionPreview.olderBuild], launch: SessionPreview.noRecall)
    EmptySessionView(model: model)
        .frame(width: 760, height: 460)
        .task { await SessionPreview.ready(model) }
}

// The meter's states, as the session's tests reach them (SessionStatesTests' `meter`).
#Preview("Context: the window unknown") {
    MeterView(meter: ContextMeter(used: nil, window: nil))
        .padding(Spacing.xl)
}

#Preview("Context: no turn reported yet") {
    MeterView(meter: ContextMeter(used: nil, window: 8192))
        .padding(Spacing.xl)
}

#Preview("Context: a tenth full") {
    MeterView(meter: ContextMeter(used: 100, window: 1000))
        .padding(Spacing.xl)
}

#Preview("Context: past 80%, warning") {
    MeterView(meter: ContextMeter(used: 6800, window: 8192))
        .padding(Spacing.xl)
}

#Preview("Attached, on a build before detach") {
    let model = SessionPreview.session([SessionPreview.olderBuild])
    AttachmentChips(model: model)
        .frame(width: 640)
        .padding(Spacing.xl)
        .task {
            await SessionPreview.ready(model)
            model.attach("report.pdf")
        }
}

#Preview("Attached, each detachable") {
    let model = SessionPreview.session([SessionPreview.newerBuild])
    AttachmentChips(model: model)
        .frame(width: 640)
        .padding(Spacing.xl)
        .task {
            await SessionPreview.ready(model)
            model.attach("report.pdf")
            model.attach("src")
        }
}

#Preview("A resumed chat's own attachments, each detachable") {
    let model = SessionPreview.session(SessionPreview.resumedDetach, launch: SessionPreview.resumeAttached, reader: .attached)
    AttachmentChips(model: model)
        .frame(width: 640)
        .padding(Spacing.xl)
        .task { await SessionPreview.ready(model) }
}

#Preview("A setting and a detach held for the turn's end") {
    let model = SessionPreview.session(SessionPreview.setBetweenTurns, silentFrom: "result")
    HeldLines(model: model)
        .frame(width: 640)
        .padding(Spacing.xl)
        .task { await SessionPreview.holdingASetting(model) }
}

#Preview("Slash commands after /") {
    let model = SessionPreview.session([SessionPreview.newerBuild])
    SuggestionList(model: model)
        .frame(width: 640)
        .padding(Spacing.xl)
        .task {
            await SessionPreview.ready(model)
            model.draft = "/"
        }
}

#Preview("Symphonies after /play") {
    let model = SessionPreview.session(SessionPreview.executePlay, launch: SessionPreview.execute())
    SuggestionList(model: model)
        .frame(width: 640)
        .padding(Spacing.xl)
        .task {
            model.plays = SessionPreview.plays
            await SessionPreview.ready(model)
            model.draft = "/play "
        }
}

#Preview("Settings, moved by the set line") {
    let model = SessionPreview.session([SessionPreview.newerBuild])
    SettingsSheet(model: model)
        .task { await SessionPreview.ready(model) }
}

#Preview("Settings, on a build that restarts for them") {
    let model = SessionPreview.session([SessionPreview.olderBuild])
    SettingsSheet(model: model)
        .task { await SessionPreview.ready(model) }
}

#Preview("Settings while a question waits: held for the turn's end") {
    let model = SessionPreview.session(SessionPreview.setBetweenTurns, silentFrom: "result")
    SettingsSheet(model: model)
        .task { await SessionPreview.holdingASetting(model) }
}

#Preview("A new file's change, folded") {
    DiffView(diff: SessionPreview.newFileDiff)
        .frame(width: 560)
        .padding(Spacing.xl)
}
#endif
