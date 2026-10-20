import DataProtocol
import PresentationStyles
import SwiftUI

/// One row of the conversation, drawn for what it is.
struct TranscriptRow: View {
    let item: TranscriptItem
    let model: SessionViewModel

    var body: some View {
        switch item.content {
        case .user(let text):
            VStack(alignment: .leading, spacing: Spacing.xs) {
                CardLabel(text: "You")
                Text(text)
                    .font(.apogeeProse)
                    .foregroundStyle(Palette.textPrimary)
                    .textSelection(.enabled)
            }
            .padding(Spacing.m)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(Palette.control, in: RoundedRectangle(cornerRadius: Radius.card))
        case .activity(let activity):
            ActivityView(activity: activity)
        case .answer(let answer):
            VStack(alignment: .leading, spacing: Spacing.xs) {
                Text(answer.text)
                    .font(.apogeeProse)
                    .foregroundStyle(Palette.textPrimary)
                    .textSelection(.enabled)
                    .frame(maxWidth: .infinity, alignment: .leading)
                if let usage = answer.usage {
                    Text(Self.tokens(usage))
                        .font(.apogeeMeta)
                        .foregroundStyle(Palette.textTertiary)
                }
            }
        case .notice(let text):
            QuietLine(symbol: "info.circle", text: text)
        case .question(let card):
            QuestionCardView(card: card) { model.answer(item.id, with: $0) }
        case .permission(let card):
            PermissionCardView(card: card) { model.answer(item.id, with: [$0]) }
        case .error(let message):
            Card(accent: Palette.error) {
                CardLabel(text: "The turn failed")
                Text(message)
                    .font(.apogeeBody)
                    .foregroundStyle(Palette.textPrimary)
                    .textSelection(.enabled)
            }
        case .stopped:
            QuietLine(symbol: "stop.circle", text: "stopped")
        case .todo(let items):
            Card {
                CardLabel(text: "Plan")
                ForEach(Array(items.enumerated()), id: \.offset) { _, entry in
                    Label(entry.text, systemImage: Self.symbol(entry.state))
                        .font(.apogeeBody)
                        .foregroundStyle(entry.state == "done" ? Palette.textTertiary : Palette.textPrimary)
                }
            }
        case .unknown(let type):
            QuietLine(symbol: "questionmark.circle", text: "apogee said something this app does not know yet: \(type)")
        case .ended(let ending):
            EndedCard(ending: ending, restart: model.start)
        }
    }

    static func tokens(_ usage: Usage) -> String {
        [usage.inputTokens.map { "\($0) in" }, usage.outputTokens.map { "\($0) out" }]
            .compactMap { $0 }.joined(separator: " · ")
    }

    static func symbol(_ state: String) -> String {
        switch state {
        case "done": "checkmark.circle"
        case "active": "circle.dotted"
        default: "circle"
        }
    }
}

/// A line the session said for the user: quiet, kept.
struct QuietLine: View {
    let symbol: String
    let text: String

    var body: some View {
        Label(text, systemImage: symbol)
            .font(.apogeeMeta)
            .foregroundStyle(Palette.textTertiary)
            .textSelection(.enabled)
    }
}

/// A turn's thinking section: open while live, closed to one line after.
struct ActivityView: View {
    let activity: Activity
    @State private var expanded = false

    var body: some View {
        DisclosureGroup(isExpanded: Binding(get: { activity.live || expanded }, set: { expanded = $0 })) {
            VStack(alignment: .leading, spacing: Spacing.xs) {
                if !activity.reasoning.isEmpty {
                    Text(activity.reasoning)
                        .font(.apogeeSmall)
                        .foregroundStyle(Palette.textSecondary)
                        .textSelection(.enabled)
                }
                ForEach(Array(activity.lines.enumerated()), id: \.offset) { _, line in
                    Text(line)
                        .font(.apogeeCode)
                        .foregroundStyle(Palette.textSecondary)
                        .textSelection(.enabled)
                }
            }
            .padding(.top, Spacing.xs)
        } label: {
            HStack(spacing: Spacing.s) {
                if activity.live {
                    Circle().fill(Palette.accent).frame(width: 6, height: 6)
                }
                Text(Self.summary(activity))
                    .font(.apogeeMeta)
                    .foregroundStyle(Palette.textTertiary)
            }
        }
    }

    static func summary(_ activity: Activity) -> String {
        var parts = [activity.live ? "thinking" : "thought"]
        if activity.budgetReached { parts.append("budget reached") }
        if !activity.lines.isEmpty {
            parts.append("\(activity.lines.count) \(activity.lines.count == 1 ? "step" : "steps")")
        }
        return parts.joined(separator: " · ")
    }
}

/// The session is over: why, the details behind it, and a way on.
struct EndedCard: View {
    let ending: Ending
    let restart: () -> Void

    var body: some View {
        Card(accent: ending.reason == .finished ? Palette.border : Palette.error) {
            CardLabel(text: "Session ended")
            Text(ending.message)
                .font(.apogeeBody)
                .foregroundStyle(Palette.textPrimary)
                .textSelection(.enabled)
            if !ending.details.isEmpty {
                DisclosureGroup("Details") {
                    Text(ending.details)
                        .font(.apogeeCode)
                        .foregroundStyle(Palette.textSecondary)
                        .textSelection(.enabled)
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
                .font(.apogeeMeta)
            }
            Button("Start a new session", action: restart)
        }
    }
}
