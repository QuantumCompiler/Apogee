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
                InspectorPane(inspector: library.inspector) { shell.go(.models) }
                    .frame(width: 360)
                    .task { await library.refreshRoles() }
            }
        }
        .confirmationDialog(
            pendingTitle, isPresented: Binding(get: { library.pending != nil }, set: { if !$0 { library.dismissPending() } }),
            titleVisibility: .visible
        ) {
            switch library.pending {
            case .stopTurn:
                Button("Stop the turn and switch", role: .destructive) { Task { await library.confirm() } }
            case .delete:
                Button("Delete", role: .destructive) { Task { await library.confirm() } }
            case nil:
                EmptyView()
            }
            Button("Cancel", role: .cancel) { library.dismissPending() }
        } message: {
            Text(pendingMessage)
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
        case .delete(let chat): "Delete “\(chat.name)”?"
        case nil: ""
        }
    }

    private var pendingMessage: String {
        switch library.pending {
        case .stopTurn: "Switching stops it, as Stop does, and ends this session's apogee. Its conversation is saved."
        case .delete(let chat): "apogee chats delete \(chat.id) -- the conversation and its turns are removed, and this can't be undone."
        case nil: ""
        }
    }
}

/// The inspector: the session, its model, the last turn, the roles.
struct InspectorPane: View {
    let inspector: Inspector
    let openModels: () -> Void

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 0) {
                section("Session", inspector.session)
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
