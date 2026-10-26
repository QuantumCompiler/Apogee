import DataProtocol
import PresentationStyles
import SwiftUI

/// The permission gate's question: exactly what would happen -- the tool and
/// its whole target, and for a website the whole URL -- and the four answers
/// the protocol takes. Denying is normal: the model is told and carries on.
/// Answered, it collapses to its line; closed by its turn's end before an
/// answer, to the line that says it went unanswered.
struct PermissionCardView: View {
    let card: PermissionCard
    let answer: (String) -> Void

    var body: some View {
        if let given = card.answer {
            QuietLine(symbol: given == "no" ? "hand.raised" : "checkmark.shield", text: "\(subject) — \(Self.said(given))")
        } else if card.closed {
            QuietLine(symbol: "shield.slash", text: "\(subject) — \(QuestionCardView.unanswered)")
        } else {
            Card(accent: Palette.warning) {
                CardLabel(text: Self.label(card.question))
                Text(card.question.items.first?.question ?? "Allow \(card.question.tool ?? "this tool")?")
                    .font(.apogeeHeading)
                    .foregroundStyle(Palette.textPrimary)
                Text(subject)
                    .font(.apogeeCode)
                    .foregroundStyle(Palette.textPrimary)
                    .textSelection(.enabled)
                    .padding(Spacing.s)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .background(Palette.code, in: RoundedRectangle(cornerRadius: Radius.control))
                if card.question.outbound, let detail = card.question.detail {
                    Text("Leaves this machine: \(detail)")
                        .font(.apogeeSmall)
                        .foregroundStyle(Palette.textSecondary)
                        .textSelection(.enabled)
                } else if let diff = card.question.detail, Self.isFileChange(card.question) {
                    // The change itself, since the CLI's 52a; a build before
                    // it names the path alone (the design's G1 fallback).
                    DiffView(diff: diff)
                }
                HStack(spacing: Spacing.s) {
                    Button("Allow once") { answer("yes") }.keyboardShortcut(.defaultAction)
                    Button("Allow for this session") { answer("session") }
                    Button("Always allow") { answer("always") }
                    Spacer()
                    Button("Deny") { answer("no") }
                }
            }
        }
    }

    /// The tool and its exact target, never shortened.
    private var subject: String {
        [card.question.tool, card.question.target].compactMap { $0 }.joined(separator: " ")
    }

    static func label(_ question: Question) -> String {
        switch question.tool {
        case "write_file", "edit_file", "delete_file": "Change a file"
        case "run_command": "Run a command"
        case "write_note", "delete_note": "Change a note"
        default: question.outbound ? "Visit a new website" : "Permission"
        }
    }

    static func isFileChange(_ question: Question) -> Bool {
        question.tool == "write_file" || question.tool == "edit_file"
    }

    static func said(_ answer: String) -> String {
        switch answer {
        case "yes": "allowed once"
        case "session": "allowed for this session"
        case "always": "always allowed"
        default: "denied by you"
        }
    }
}

#if DEBUG
#Preview("Asking to write a file") {
    PermissionCardView(card: SessionPreview.permission()) { _ in }
        .frame(width: 640)
        .padding(Spacing.xl)
}

#Preview("Allowed once") {
    PermissionCardView(card: SessionPreview.permission(SessionPreview.noteAnswers)) { _ in }
        .frame(width: 640)
        .padding(Spacing.xl)
}

#Preview("Closed unanswered: the turn was stopped") {
    PermissionCardView(card: SessionPreview.closedPermission) { _ in }
        .frame(width: 640)
        .padding(Spacing.xl)
}

#Preview("Denied") {
    PermissionCardView(card: SessionPreview.permission([SessionPreview.noteAnswers[0], "no"])) { _ in }
        .frame(width: 640)
        .padding(Spacing.xl)
}
#endif
