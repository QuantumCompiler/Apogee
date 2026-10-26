import PresentationStyles
import SwiftUI

/// The window's content: one chat session, or why there is none yet.
/// `accessory` sits at the header's end: the controls of whatever hosts it.
public struct SessionWindow<Accessory: View>: View {
    @Bindable private var model: SessionViewModel
    private let accessory: Accessory

    public init(model: SessionViewModel, @ViewBuilder accessory: () -> Accessory) {
        self.model = model
        self.accessory = accessory()
    }

    public var body: some View {
        Group {
            switch model.connection {
            case .idle, .locating:
                EmptyState(symbol: "hourglass", title: "Starting Apogee", message: "Looking for the apogee command.")
            case .missing(let hint, let places):
                MissingBinaryView(hint: hint, places: places, retry: model.start)
            case .connected:
                ConversationView(model: model, accessory: accessory)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Palette.window)
        // Esc stops the turn in flight, through the protocol's cancel.
        .onExitCommand { model.cancel() }
        .sheet(item: Binding(get: { model.presenting == .settings ? Presenting.settings : nil }, set: { if $0 == nil { model.presenting = nil } })) { _ in
            SettingsSheet(model: model)
        }
        .sheet(item: Binding(get: { model.presenting == .suites ? Presenting.suites : nil }, set: { if $0 == nil { model.presenting = nil } })) { _ in
            SuiteSheet(model: model)
        }
        .fileImporter(
            isPresented: Binding(get: { model.presenting == .files }, set: { if !$0 { model.presenting = nil } }),
            allowedContentTypes: [.item, .folder], allowsMultipleSelection: true
        ) { result in
            if case .success(let urls) = result {
                urls.forEach { model.attach($0.path) }
            }
        }
        // A file dropped on the session is attached.
        .dropDestination(for: URL.self) { urls, _ in
            guard case .connected = model.connection else { return false }
            urls.filter(\.isFileURL).forEach { model.attach($0.path) }
            return !urls.isEmpty
        }
    }
}

extension SessionWindow where Accessory == EmptyView {
    public init(model: SessionViewModel) {
        self.init(model: model) { EmptyView() }
    }
}

/// The header, the transcript and the input.
struct ConversationView<Accessory: View>: View {
    @Bindable var model: SessionViewModel
    let accessory: Accessory

    var body: some View {
        VStack(spacing: 0) {
            header
            Divider()
            ScrollViewReader { proxy in
                if model.transcript.items.isEmpty && model.transcript.status == .ready {
                    EmptySessionView(model: model)
                } else {
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: Spacing.m) {
                        ForEach(model.transcript.items) { item in
                            TranscriptRow(item: item, model: model).id(item.id)
                        }
                    }
                    .padding(Spacing.xl)
                    .frame(maxWidth: Layout.conversationMax)
                    .frame(maxWidth: .infinity)
                }
                .onChange(of: model.transcript.revision) {
                    if let last = model.transcript.items.last {
                        proxy.scrollTo(last.id, anchor: .bottom)
                    }
                }
                }
            }
            Divider()
            InputBar(model: model)
        }
    }

    private var header: some View {
        HStack(spacing: Spacing.s) {
            Text(model.transcript.model ?? "apogee")
                .font(.apogeeTitle)
                .foregroundStyle(Palette.textPrimary)
            if let banner = model.executeBanner {
                Text(banner)
                    .font(.apogeeMeta)
                    .foregroundStyle(Palette.textSecondary)
                    .lineLimit(1)
                    .help("Opened as apogee execute: its suite's symphonies play with /play")
            } else if let suite = model.suiteWords {
                Button(suite) { model.onAction?(.chooseSuite) }
                    .buttonStyle(.plain)
                    .font(.apogeeMeta)
                    .foregroundStyle(Palette.textSecondary)
                    .lineLimit(1)
                    .disabled(!model.canSwitchSuite)
                    .help("The suite this session runs under -- /suite, or click, to run it under another, or none")
            }
            Spacer()
            if model.transcript.turnInFlight {
                ProgressView().controlSize(.small)
            }
            MeterView(meter: model.meter)
            Divider().frame(height: 18)
            Text(model.statusText)
                .font(.apogeeMeta)
                .foregroundStyle(model.transcript.status == .ended ? Palette.error : Palette.textSecondary)
            accessory
        }
        .padding(.horizontal, Spacing.l)
        .frame(height: 44)
    }
}

/// No binary where the app looks: how to install it, and where it looked.
struct MissingBinaryView: View {
    let hint: String
    let places: [String]
    let retry: () -> Void

    var body: some View {
        VStack(spacing: Spacing.l) {
            EmptyState(symbol: "terminal", title: "Apogee isn't installed", message: "The app drives the apogee command, and found none where it looks.")
            Text(hint)
                .font(.apogeeCode)
                .textSelection(.enabled)
                .padding(Spacing.m)
                .background(Palette.code, in: RoundedRectangle(cornerRadius: Radius.control))
            VStack(alignment: .leading, spacing: Spacing.xs) {
                ForEach(places, id: \.self) { place in
                    Text(place).font(.apogeeMeta).foregroundStyle(Palette.textTertiary)
                }
            }
            Button("Try again", action: retry)
                .keyboardShortcut(.defaultAction)
        }
        .padding(Spacing.xxl)
        .frame(maxWidth: Layout.conversationMax)
    }
}

#if DEBUG
#Preview("Starting") {
    SessionWindow(model: SessionPreview.session(SessionPreview.mockActor))
        .frame(width: 900, height: 640)
}

#Preview("Apogee isn't installed") {
    let model = SessionPreview.missing()
    SessionWindow(model: model)
        .frame(width: 900, height: 640)
        .task { model.start() }
}

#Preview("A chat under a suite") {
    let session = SessionPreview.underSuite()
    SessionWindow(model: session)
        .frame(width: 900, height: 520)
        .task { await SessionPreview.answered(session) }
}

#Preview("Switched to another suite: restarted under it, said") {
    let session = SessionPreview.savedUnderSuite()
    SessionWindow(model: session)
        .frame(width: 900, height: 620)
        .task {
            await SessionPreview.answered(session)
            await session.switchSuite(to: "fast")
            await SessionPreview.until { session.transcript.acceptsInput }
            session.draft = SessionPreview.suiteTurn
            session.submit()
        }
}

#Preview("A suite apogee refused at launch, put back") {
    let session = SessionPreview.savedUnderSuite()
    SessionWindow(model: session)
        .frame(width: 900, height: 620)
        .task {
            await SessionPreview.answered(session)
            await session.switchSuite(to: "vast")
        }
}

#Preview("A new session") {
    let model = SessionPreview.session(SessionPreview.mockActor)
    SessionWindow(model: model)
        .frame(width: 900, height: 640)
        .task { await SessionPreview.ready(model) }
}

#Preview("Thinking") {
    let model = SessionPreview.session(SessionPreview.mockActor, silentFrom: "question")
    SessionWindow(model: model)
        .frame(width: 900, height: 640)
        .task { await SessionPreview.converse(model, typed: SessionPreview.noteTurn) }
}

#Preview("A question waiting") {
    let model = SessionPreview.session(SessionPreview.mockActor)
    SessionWindow(model: model)
        .frame(width: 900, height: 720)
        .task { await SessionPreview.converse(model, typed: SessionPreview.noteTurn) }
}

#Preview("A permission waiting") {
    let model = SessionPreview.session(SessionPreview.mockActor)
    SessionWindow(model: model)
        .frame(width: 900, height: 720)
        .task { await SessionPreview.converse(model, typed: SessionPreview.noteTurn, answers: [SessionPreview.noteAnswers[0]]) }
}

#Preview("Answering") {
    let model = SessionPreview.session(SessionPreview.mockActor, silentFrom: "answer_end")
    SessionWindow(model: model)
        .frame(width: 900, height: 720)
        .task { await SessionPreview.converse(model, typed: SessionPreview.noteTurn, answers: SessionPreview.noteAnswers) }
}

#Preview("Answered") {
    let model = SessionPreview.session(SessionPreview.mockActor)
    SessionWindow(model: model)
        .frame(width: 900, height: 720)
        .task { await SessionPreview.converse(model, typed: SessionPreview.noteTurn, answers: SessionPreview.noteAnswers) }
}

#Preview("A play running") {
    let model = SessionPreview.session(SessionPreview.executePlay, silentFrom: "answer_start", launch: SessionPreview.execute())
    SessionWindow(model: model)
        .frame(width: 900, height: 640)
        .task { await SessionPreview.converse(model, typed: SessionPreview.playTurn) }
}

#Preview("A play narrated") {
    let model = SessionPreview.session(SessionPreview.executePlay, launch: SessionPreview.execute())
    SessionWindow(model: model)
        .frame(width: 900, height: 640)
        .task { await SessionPreview.converse(model, typed: SessionPreview.playTurn) }
}

#Preview("A play refused: the turn failed") {
    let model = SessionPreview.session(SessionPreview.executePlayRefused, launch: SessionPreview.execute())
    SessionWindow(model: model)
        .frame(width: 900, height: 640)
        .task { await SessionPreview.converse(model, typed: SessionPreview.refusedTurn) }
}

#Preview("Ended: the child exited after its answer") {
    let model = SessionPreview.session(SessionPreview.mockActor, ending: PreviewChat.wentWhenAsked)
    SessionWindow(model: model)
        .frame(width: 900, height: 720)
        .task { await SessionPreview.converse(model, typed: SessionPreview.noteTurn, answers: SessionPreview.noteAnswers) }
}

#Preview("Ended: failed, the stderr behind it") {
    let model = SessionPreview.session([SessionPreview.bareSession], ending: SessionPreview.wentAway)
    SessionWindow(model: model)
        .frame(width: 900, height: 520)
        .task { await SessionPreview.converse(model, typed: SessionPreview.noteTurn) }
}

#Preview("Resumed: its earlier turns read back") {
    let model = SessionPreview.session(SessionPreview.resumedChat, launch: SessionPreview.resumeSaved, reader: .saved)
    SessionWindow(model: model)
        .frame(width: 900, height: 720)
        .task { await SessionPreview.ready(model) }
}

#Preview("Resumed, carried on under the divider") {
    let model = SessionPreview.session(SessionPreview.resumedChat, launch: SessionPreview.resumeSaved, reader: .saved)
    SessionWindow(model: model)
        .frame(width: 900, height: 720)
        .task { await SessionPreview.converse(model, typed: SessionPreview.resumedTurn) }
}

#Preview("Resumed on a binary without chats show: the notice") {
    let model = SessionPreview.session(SessionPreview.resumedChat, launch: SessionPreview.resumeSaved, reader: .older)
    SessionWindow(model: model)
        .frame(width: 900, height: 520)
        .task { await SessionPreview.ready(model) }
}

#Preview("Paused: freed for another session") {
    let model = SessionPreview.session(SessionPreview.mockActor)
    SessionWindow(model: model)
        .frame(width: 900, height: 720)
        .task {
            await SessionPreview.converse(model, typed: SessionPreview.noteTurn, answers: SessionPreview.noteAnswers)
            await model.pause()
        }
}

#Preview("The conversation, a host's control at the header's end") {
    let model = SessionPreview.session(SessionPreview.mockActor)
    ConversationView(model: model, accessory: Image(systemName: "sidebar.right").foregroundStyle(Palette.textTertiary))
        .frame(width: 900, height: 720)
        .task { await SessionPreview.converse(model, typed: SessionPreview.noteTurn, answers: SessionPreview.noteAnswers) }
}

#Preview("How to install, and where it looked") {
    @Previewable @State var model = SessionPreview.missing()
    Group {
        if case .missing(let hint, let places) = model.connection {
            MissingBinaryView(hint: hint, places: places, retry: model.start)
        }
    }
    .frame(width: 760, height: 520)
    .task { model.start() }
}
#endif
