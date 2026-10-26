import BusinessReads
import PresentationSession
import PresentationShell
import PresentationStyles
import SwiftUI

/// The Sessions place: the open session, the inspector beside it, and the
/// questions the library asks before it changes anything.
public struct SessionsScreen: View {
    @Bindable private var library: SessionLibraryViewModel
    private let shell: ShellViewModel

    public init(library: SessionLibraryViewModel, shell: ShellViewModel) {
        self.library = library
        self.shell = shell
    }

    public var body: some View {
        HStack(spacing: 0) {
            SessionWindow(model: library.current) {
                Button {
                    library.inspectorOpen.toggle()
                } label: {
                    Image(systemName: "sidebar.right")
                }
                .buttonStyle(.plain)
                .foregroundStyle(showsInspector ? Palette.textPrimary : Palette.textTertiary)
                .disabled(!shell.layout.sidePanes)
                .help("Inspector ⌥⌘I")
                .accessibilityLabel(showsInspector ? "Hide the inspector" : "Show the inspector")
            }
            // A new session is a new view: its own scroll and its own input.
            .id(ObjectIdentifier(library.current))
            if showsInspector {
                Divider()
                InspectorPane(
                    inspector: library.inspector,
                    showInfo: { if let chat = library.currentChat { Task { await library.showInfo(chat) } } },
                    compact: { if let chat = library.currentChat { library.askToCompact(chat) } },
                    forgetRecall: { Task { await library.askToForgetRecall() } }
                ) { shell.go(.models) }
                    .frame(width: 360)
                    .task {
                        await library.refreshRoles()
                        await library.refreshRecall()
                    }
            }
        }
        .confirmationDialog(
            pendingTitle, isPresented: Binding(get: { library.pending != nil }, set: { if !$0 { library.dismissPending() } }),
            titleVisibility: .visible
        ) {
            switch library.pending {
            case .stopTurn:
                Button("Stop the turn and switch", role: .destructive) { Task { await library.confirm() } }
            case .endPrivate:
                Button("End it and switch", role: .destructive) { Task { await library.confirm() } }
            case .delete:
                Button("Delete", role: .destructive) { Task { await library.confirm() } }
            case .compact:
                Button("Compact it", role: .destructive) { Task { await library.confirm() } }
            case .forgetRecall:
                Button("Forget every summary", role: .destructive) { Task { await library.confirm() } }
            case nil:
                EmptyView()
            }
            Button(keepTitle, role: .cancel) { library.dismissPending() }
        } message: {
            Text(pendingMessage)
        }
        .sheet(isPresented: $library.executeDoorOpen) {
            ExecuteDoorSheet(library: library)
        }
        .sheet(item: $library.info) { info in
            ChatInfoSheet(info: info, canCompact: library.canCompact(info.chat)) {
                library.info = nil
                library.askToCompact(info.chat)
            }
        }
        .sheet(item: $library.renaming) { chat in
            RenameSheet(chat: chat) { name in
                Task { await library.rename(chat, to: name) }
            }
        }
        .alert(
            "That didn't work", isPresented: Binding(get: { library.failure != nil }, set: { if !$0 { library.dismissFailure() } })
        ) {
            Button("OK") { library.dismissFailure() }
        } message: {
            Text(library.failure ?? "")
        }
    }

    private var showsInspector: Bool { library.inspectorOpen && shell.layout.sidePanes }

    private var pendingTitle: String {
        switch library.pending {
        case .stopTurn: "A turn is running in this session"
        case .endPrivate: "This private conversation can't be reopened"
        case .delete(let chat): "Delete “\(chat.name)”?"
        case .compact(let chat): "Compact “\(chat.name)”?"
        case .forgetRecall: "Forget what recall holds?"
        case nil: ""
        }
    }

    private var pendingMessage: String {
        switch library.pending {
        case .stopTurn: "Switching stops it, as Stop does, and ends this session's apogee. Its conversation is saved."
        case .endPrivate:
            library.current.transcript.turnInFlight
                ? "Nothing of it is written: switching stops the turn running in it and ends this session's apogee, and the conversation goes with it. Keep it running, or end it?"
                : "Nothing of it is written: switching ends this session's apogee, and the conversation goes with it. Keep it running, or end it?"
        case .delete(let chat): "apogee chats delete \(chat.id) -- the conversation and its turns are removed, and this can't be undone."
        case .compact(let chat):
            library.isOpen(chat) && library.current.isLive
                ? "Its session compacts it, as /compact does: the history so far is folded into a summary the model carries on from. The turns before it aren't kept word for word."
                : "apogee chats compact \(chat.id) --yes -- its history is folded into a summary the model carries on from. The turns before it aren't kept word for word, and this can't be undone."
        case .forgetRecall:
            (library.recall.map { "Now: \($0). " } ?? "")
                + "apogee chats recall --forget --yes -- every chat's summary goes, so no later session recalls them; every transcript stays."
        case nil: ""
        }
    }

    /// The answer that changes nothing: a private conversation is kept running.
    private var keepTitle: String {
        if case .endPrivate = library.pending { "Keep it running" } else { "Cancel" }
    }
}

/// The inspector: the session, its model, the last turn, the roles, and
/// what recall holds of earlier chats.
struct InspectorPane: View {
    let inspector: Inspector
    /// The saved conversation's details (`chats info`).
    var showInfo: (() -> Void)?
    /// Its compaction, asked first.
    var compact: (() -> Void)?
    /// Recall's summaries forgotten, asked first.
    var forgetRecall: (() -> Void)?
    let openModels: () -> Void

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 0) {
                section("Session", inspector.session) {
                    if inspector.chat != nil {
                        HStack(spacing: Spacing.m) {
                            if let showInfo { Button("Details…", action: showInfo) }
                            if let compact, inspector.canCompact { Button("Compact…", action: compact) }
                        }
                        .font(.apogeeSmall)
                    }
                }
                section("Model", inspector.modelFacts) {
                    Button("Open the models library", action: openModels)
                        .font(.apogeeSmall)
                }
                section("This turn", inspector.turnFacts)
                section("Roles", inspector.roles) {
                    if let failure = inspector.rolesFailure {
                        Text(failure)
                            .font(.apogeeMeta)
                            .foregroundStyle(Palette.error)
                    }
                }
                if inspector.recall != nil || inspector.recallFailure != nil {
                    section("Recall", []) {
                        if let recall = inspector.recall {
                            Text(recall)
                                .font(.apogeeSmall)
                                .foregroundStyle(Palette.textPrimary)
                                .textSelection(.enabled)
                                .fixedSize(horizontal: false, vertical: true)
                        }
                        if let failure = inspector.recallFailure {
                            Text(failure).font(.apogeeMeta).foregroundStyle(Palette.error)
                        }
                        Text("Each finished chat is summarised so later sessions can recall it. Forgetting removes the summaries; every transcript stays.")
                            .font(.apogeeMeta)
                            .foregroundStyle(Palette.textTertiary)
                            .fixedSize(horizontal: false, vertical: true)
                        if let forgetRecall {
                            Button("Forget every summary…", action: forgetRecall).font(.apogeeSmall)
                        }
                    }
                }
            }
        }
        .background(Palette.inspector)
    }

    private func section<Extra: View>(_ title: String, _ fields: [Field], @ViewBuilder extra: () -> Extra = { EmptyView() }) -> some View {
        VStack(alignment: .leading, spacing: Spacing.s) {
            Text(title.uppercased())
                .font(.system(size: TypeScale.label, weight: .medium, design: .monospaced))
                .tracking(0.8)
                .foregroundStyle(Palette.textTertiary)
            Grid(alignment: .leadingFirstTextBaseline, horizontalSpacing: Spacing.m, verticalSpacing: Spacing.xs) {
                ForEach(Array(fields.enumerated()), id: \.offset) { _, field in
                    GridRow {
                        Text(field.label)
                            .foregroundStyle(Palette.textTertiary)
                            .gridColumnAlignment(.leading)
                        Text(field.value)
                            .foregroundStyle(Palette.textPrimary)
                            .textSelection(.enabled)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                }
            }
            .font(.apogeeSmall)
            extra()
        }
        .padding(Spacing.l)
        .frame(maxWidth: .infinity, alignment: .leading)
        .overlay(alignment: .bottom) { Divider() }
    }
}

/// A saved conversation's details, as `chats info` prints them, with its
/// compaction beside them.
struct ChatInfoSheet: View {
    let info: SessionLibraryViewModel.ChatInfo
    let canCompact: Bool
    let compact: () -> Void
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.m) {
            Text(info.chat.name).font(.apogeeHeading)
            if let failure = info.failure {
                Text(failure).font(.apogeeSmall).foregroundStyle(Palette.error).textSelection(.enabled)
            }
            Grid(alignment: .leadingFirstTextBaseline, horizontalSpacing: Spacing.m, verticalSpacing: Spacing.xs) {
                ForEach(Array(info.fields.enumerated()), id: \.offset) { _, field in
                    GridRow {
                        Text(field.label).foregroundStyle(Palette.textTertiary).gridColumnAlignment(.leading)
                        Text(field.value).foregroundStyle(Palette.textPrimary).textSelection(.enabled)
                    }
                }
            }
            .font(.apogeeSmall)
            Text("apogee chats info \(info.chat.id)")
                .font(.apogeeCode)
                .foregroundStyle(Palette.textTertiary)
            HStack {
                if canCompact { Button("Compact…", action: compact) }
                Spacer()
                Button("Done") { dismiss() }.keyboardShortcut(.defaultAction)
            }
        }
        .padding(Spacing.xl)
        .frame(width: 460)
    }
}

/// The Sessions place's keys: New session, Search sessions, the inspector.
public struct SessionCommands: Commands {
    private let library: SessionLibraryViewModel
    private let shell: ShellViewModel

    public init(library: SessionLibraryViewModel, shell: ShellViewModel) {
        self.library = library
        self.shell = shell
    }

    public var body: some Commands {
        CommandGroup(replacing: .newItem) {
            Button("New Session") {
                shell.go(.sessions)
                Task { await library.newSession() }
            }
            .keyboardShortcut("n", modifiers: .command)
            Button("New Private Session") {
                shell.go(.sessions)
                Task { await library.newPrivateSession() }
            }
            .keyboardShortcut("n", modifiers: [.shift, .command])
            Button("New Execute Session…") {
                shell.go(.sessions)
                Task { await library.openExecuteDoor() }
            }
            .keyboardShortcut("n", modifiers: [.option, .command])
        }
        CommandGroup(after: .toolbar) {
            Button("Session Settings…") {
                shell.go(.sessions)
                library.current.presenting = .settings
            }
            .keyboardShortcut("m", modifiers: [.shift, .command])
        }
        CommandGroup(after: .sidebar) {
            Button("Search Sessions") {
                shell.go(.sessions)
                shell.setSidebarHidden(false)
                library.requestSearch()
            }
            .keyboardShortcut("f", modifiers: [.shift, .command])
            Button(library.inspectorOpen ? "Hide Inspector" : "Show Inspector") {
                library.inspectorOpen.toggle()
            }
            .keyboardShortcut("i", modifiers: [.option, .command])
            .disabled(shell.route != .sessions || !shell.layout.sidePanes)
        }
    }
}

#if DEBUG
#Preview("Starting") {
    SessionsScreen(library: SessionsPreview.library(), shell: SessionsPreview.shell())
        .frame(width: 1100, height: 720)
}

#Preview("A new session answered, the inspector beside it") {
    let library = SessionsPreview.library()
    SessionsScreen(library: library, shell: SessionsPreview.shell())
        .frame(width: 1200, height: 760)
        .task {
            library.inspectorOpen = true
            await library.start(inView: true)
            await SessionsPreview.converse(library.current, typed: SessionsPreview.noteTurn, answers: SessionsPreview.noteAnswers)
        }
}

#Preview("A question waiting") {
    let library = SessionsPreview.library()
    SessionsScreen(library: library, shell: SessionsPreview.shell())
        .frame(width: 1100, height: 760)
        .task {
            await library.start(inView: true)
            await SessionsPreview.converse(library.current, typed: SessionsPreview.noteTurn)
        }
}

#Preview("A saved session reopened: its earlier turns, then the live one") {
    let library = SessionsPreview.library(
        chats: SessionsDocuments.chatsList, lines: SessionsPreview.resumedChat, openChat: SessionsPreview.savedChatID)
    SessionsScreen(library: library, shell: SessionsPreview.shell())
        .frame(width: 1100, height: 760)
        .task {
            await library.start(inView: true)
            await SessionsPreview.converse(library.current, typed: SessionsPreview.resumedTurn)
        }
}

#Preview("Apogee isn't installed") {
    let library = SessionsPreview.library(missing: true)
    SessionsScreen(library: library, shell: SessionsPreview.shell())
        .frame(width: 1100, height: 720)
        .task { await library.start(inView: true) }
}

#Preview("A delete waiting on the person's word") {
    let library = SessionsPreview.library()
    SessionsScreen(library: library, shell: SessionsPreview.shell())
        .frame(width: 1100, height: 720)
        .task {
            await library.start(inView: true)
            if let chat = library.chats.first { library.askToDelete(chat) }
        }
}

#Preview("A private conversation's end waiting on the person's word") {
    let library = SessionsPreview.library(lines: SessionsPreview.privateConversation)
    SessionsScreen(library: library, shell: SessionsPreview.shell())
        .frame(width: 1100, height: 760)
        .task {
            await library.start(inView: true)
            await SessionsPreview.until { library.privateIsNothingWritten }
            await library.newPrivateSession()
            await SessionsPreview.converse(library.current, typed: SessionsPreview.noteTurn, answers: SessionsPreview.noteAnswers)
            await SessionsPreview.until { library.current.transcript.status == .ready }
            await library.newSession()
        }
}

#Preview("A saved session: its model, the last turn, the roles") {
    @Previewable @State var library = SessionsPreview.library(openChat: "c1")
    InspectorPane(inspector: library.inspector) {}
        .frame(width: 360, height: 640)
        .task {
            await library.start(inView: true)
            await SessionsPreview.converse(library.current, typed: SessionsPreview.noteTurn, answers: SessionsPreview.noteAnswers)
            await library.refreshRoles()
        }
}

#Preview("A new session on its own chat, switched to a local model: the model that answered, the backend its chat was saved on") {
    @Previewable @State var library = SessionsPreview.switching()
    InspectorPane(inspector: library.inspector) {}
        .frame(width: 360, height: 520)
        .task {
            await library.start(inView: true)
            await SessionsPreview.switchToLocal(library.current)
        }
}

#Preview("A saved session: what recall holds, Forget beside it") {
    @Previewable @State var library = SessionsPreview.library(
        chats: SessionsDocuments.chatsListTwo, openChat: SessionsPreview.savedTwo[0].id)
    InspectorPane(inspector: library.inspector, showInfo: {}, compact: {}, forgetRecall: {}) {}
        .frame(width: 360, height: 760)
        .task {
            await library.start(inView: true)
            await library.refreshRoles()
            await library.refreshRecall()
        }
}

#Preview("A saved chat's details, as chats info prints them") {
    @Previewable @State var library = SessionsPreview.library(chats: SessionsDocuments.chatsListTwo)
    Group {
        if let info = library.info {
            ChatInfoSheet(info: info, canCompact: library.canCompact(info.chat)) {}
        } else {
            ProgressView().padding(Spacing.xl)
        }
    }
    .task {
        await library.start(inView: false)
        if let chat = library.chats.first { await library.showInfo(chat) }
    }
}

#Preview("Compacting a saved chat, asked first") {
    let library = SessionsPreview.library(chats: SessionsDocuments.chatsListTwo)
    SessionsScreen(library: library, shell: SessionsPreview.shell())
        .frame(width: 1100, height: 720)
        .task {
            await library.start(inView: true)
            if let chat = library.chats.first { library.askToCompact(chat) }
        }
}

#Preview("Forgetting what recall holds, asked first") {
    let library = SessionsPreview.library(chats: SessionsDocuments.chatsListTwo)
    SessionsScreen(library: library, shell: SessionsPreview.shell())
        .frame(width: 1100, height: 720)
        .task {
            await library.start(inView: true)
            await library.askToForgetRecall()
        }
}

#Preview("A new session, not saved yet") {
    @Previewable @State var library = SessionsPreview.library()
    InspectorPane(inspector: library.inspector) {}
        .frame(width: 360, height: 640)
        .task {
            await library.start(inView: true)
            await SessionsPreview.converse(library.current, typed: SessionsPreview.noteTurn)
        }
}
#endif
