import BusinessReads
import PresentationShell
import PresentationStyles
import SwiftUI

/// The sidebar's session list: search, the date groups, a row per saved
/// conversation -- opened by a click, renamed and deleted from its menu.
public struct SessionList: View {
    @Bindable private var library: SessionLibraryViewModel
    private let shell: ShellViewModel
    @FocusState private var searching: Bool
    @State private var closed: Set<String> = []

    public init(library: SessionLibraryViewModel, shell: ShellViewModel) {
        self.library = library
        self.shell = shell
    }

    public var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: Spacing.xs) {
                Text("Sessions")
                    .font(.apogeeSmall.weight(.medium))
                    .foregroundStyle(Palette.textSecondary)
                Spacer()
                Button {
                    shell.go(.sessions)
                    Task { await library.newPrivateSession() }
                } label: {
                    Image(systemName: "eye.slash")
                }
                .buttonStyle(.plain)
                .foregroundStyle(Palette.textSecondary)
                .help("New private session ⇧⌘N")
                .accessibilityLabel("New private session")
                Button {
                    shell.go(.sessions)
                    Task { await library.newSession() }
                } label: {
                    Image(systemName: "square.and.pencil")
                }
                .buttonStyle(.plain)
                .foregroundStyle(Palette.textSecondary)
                .help("New session ⌘N")
                .accessibilityLabel("New session")
            }
            .padding(.horizontal, Spacing.m)
            .frame(height: 36)
            TextField("Search sessions", text: $library.query)
                .textFieldStyle(.roundedBorder)
                .font(.apogeeSmall)
                .focused($searching)
                .padding(.horizontal, Spacing.s)
                .padding(.bottom, Spacing.xs)
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 1) {
                    if library.current.privateMode != nil {
                        openPrivateRow
                    }
                    ForEach(library.groups) { group in
                        groupView(group)
                    }
                    if let empty = library.emptyText {
                        Text(empty)
                            .font(.apogeeMeta)
                            .foregroundStyle(Palette.textTertiary)
                            .fixedSize(horizontal: false, vertical: true)
                            .padding(Spacing.s)
                    }
                }
                .padding(.horizontal, Spacing.s)
                .padding(.bottom, Spacing.s)
            }
        }
        .onChange(of: library.searchRequests) { searching = true }
    }

    /// The private session open now: it is in no saved list.
    private var openPrivateRow: some View {
        VStack(alignment: .leading, spacing: 1) {
            Text("Open now")
                .font(.apogeeSmall.weight(.medium))
                .foregroundStyle(Palette.textSecondary)
                .padding(.horizontal, Spacing.s)
                .frame(height: 26)
            Button {
                shell.go(.sessions)
            } label: {
                HStack(spacing: Spacing.s) {
                    Image(systemName: "eye.slash").font(.system(size: 11)).frame(width: 14)
                    Text("Private session").frame(maxWidth: .infinity, alignment: .leading)
                }
                .font(.apogeeSmall)
                .foregroundStyle(Palette.textPrimary)
                .padding(.leading, Spacing.l)
                .frame(height: 28)
                .background(Palette.selection, in: RoundedRectangle(cornerRadius: Radius.row))
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
        }
    }

    private func groupView(_ group: ChatGroup) -> some View {
        let open = !closed.contains(group.id) || !library.query.isEmpty
        return Group {
            Button {
                if closed.contains(group.id) { closed.remove(group.id) } else { closed.insert(group.id) }
            } label: {
                HStack(spacing: Spacing.xs) {
                    Image(systemName: "chevron.right")
                        .font(.system(size: 9, weight: .semibold))
                        .rotationEffect(.degrees(open ? 90 : 0))
                    Text(group.title)
                    Spacer()
                    Text(String(group.chats.count)).foregroundStyle(Palette.textTertiary)
                }
                .font(.apogeeSmall.weight(.medium))
                .foregroundStyle(Palette.textSecondary)
                .padding(.horizontal, Spacing.s)
                .frame(height: 26)
                .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .accessibilityAddTraits(open ? .isSelected : [])
            if open {
                ForEach(group.chats) { chat in row(chat) }
            }
        }
    }

    private func row(_ chat: ChatSummary) -> some View {
        let current = library.isOpen(chat)
        return Button {
            shell.go(.sessions)
            Task { await library.open(.chat(chat.id)) }
        } label: {
            HStack(spacing: Spacing.s) {
                KindGlyph(kind: library.kind(of: chat))
                Text(chat.name)
                    .lineLimit(1)
                    .truncationMode(.tail)
                    .frame(maxWidth: .infinity, alignment: .leading)
                Text(library.when(chat))
                    .font(.apogeeMeta.monospacedDigit())
                    .foregroundStyle(Palette.textTertiary)
            }
            .font(.apogeeSmall)
            .foregroundStyle(current ? Palette.textPrimary : Palette.textSecondary)
            .padding(.leading, Spacing.l)
            .padding(.trailing, Spacing.s)
            .frame(height: 28)
            .background(current ? Palette.selection : .clear, in: RoundedRectangle(cornerRadius: Radius.row))
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .help(chat.backend.map { "\(chat.name) · \($0)" } ?? chat.name)
        .contextMenu {
            // Both ask in the Sessions place, so it comes into view.
            Button("Rename…") {
                shell.go(.sessions)
                library.renaming = chat
            }
            Button("Delete…", role: .destructive) {
                shell.go(.sessions)
                library.askToDelete(chat)
            }
        }
        .accessibilityAddTraits(current ? .isSelected : [])
    }
}

/// Where a session's model runs: on this machine, a billed cloud API, or a
/// subscription -- or one neutral glyph when the list does not say.
struct KindGlyph: View {
    let kind: ModelKind?

    var body: some View {
        Image(systemName: symbol)
            .font(.system(size: 11))
            .foregroundStyle(color)
            .frame(width: 14)
            .accessibilityLabel(label)
    }

    private var symbol: String {
        switch kind {
        case .local: "cpu"
        case .api: "cloud"
        case .subscription: "arrow.triangle.2.circlepath"
        case nil: "bubble.left"
        }
    }

    private var color: Color {
        switch kind {
        case .local: Palette.local
        case .api: Palette.api
        case .subscription: Palette.subscription
        case nil: Palette.textTertiary
        }
    }

    private var label: String {
        switch kind {
        case .local: "on this machine"
        case .api: "cloud API, billed"
        case .subscription: "subscription"
        case nil: "session"
        }
    }
}

/// A new name for a saved conversation.
struct RenameSheet: View {
    let chat: ChatSummary
    let rename: (String) -> Void
    @State private var name: String
    @Environment(\.dismiss) private var dismiss

    init(chat: ChatSummary, rename: @escaping (String) -> Void) {
        self.chat = chat
        self.rename = rename
        _name = State(initialValue: chat.name)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: Spacing.m) {
            Text("Rename session").font(.apogeeHeading)
            TextField("Name", text: $name)
                .textFieldStyle(.roundedBorder)
                .onSubmit(save)
            Text("apogee chats title \(chat.id) …")
                .font(.apogeeCode)
                .foregroundStyle(Palette.textTertiary)
            HStack {
                Spacer()
                Button("Cancel", role: .cancel) { dismiss() }
                    .keyboardShortcut(.cancelAction)
                Button("Rename", action: save)
                    .keyboardShortcut(.defaultAction)
                    .disabled(name.trimmingCharacters(in: .whitespaces).isEmpty)
            }
        }
        .padding(Spacing.xl)
        .frame(width: 460)
    }

    private func save() {
        rename(name)
        dismiss()
    }
}
