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
        case .recalled(let recall):
            VStack(alignment: .leading, spacing: Spacing.xxs) {
                QuietLine(symbol: "clock.arrow.circlepath", text: recall.summary)
                ForEach(recall.chats, id: \.id) { chat in
                    Button(chat.name) { model.onAction?(.openChat(chat.id)) }
                        .buttonStyle(.link)
                        .font(.apogeeMeta)
                        .padding(.leading, Spacing.l)
                        .help("Open this session")
                }
            }
        case .sources(let sources):
            SourcesRow(sources: sources)
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
                if model.lastSent != nil && item.id == model.transcript.items.last?.id {
                    Button("Try again") { model.retry() }
                        .disabled(!model.transcript.acceptsInput)
                }
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

/// The excerpts a turn drew on, numbered as the answer cites them.
struct SourcesRow: View {
    let sources: [Source]

    var body: some View {
        HStack(alignment: .firstTextBaseline, spacing: Spacing.xs) {
            Image(systemName: "text.quote")
                .foregroundStyle(Palette.textTertiary)
            FlowChips(items: sources.enumerated().map { index, source in
                "[\(index + 1)] \(source.file)" + (source.place.map { " · \($0)" } ?? "")
            })
        }
        .font(.apogeeMeta)
        .accessibilityElement(children: .combine)
        .accessibilityLabel("Sources: " + sources.map(\.file).joined(separator: ", "))
    }
}

/// Chips that wrap to the width they have.
struct FlowChips: View {
    let items: [String]

    var body: some View {
        ViewThatFits(in: .horizontal) {
            HStack(spacing: Spacing.xs) { chips }
            VStack(alignment: .leading, spacing: Spacing.xs) { chips }
        }
    }

    private var chips: some View {
        ForEach(items, id: \.self) { item in
            Text(item)
                .foregroundStyle(Palette.textSecondary)
                .padding(.horizontal, Spacing.s)
                .padding(.vertical, Spacing.xxs)
                .background(Palette.control, in: RoundedRectangle(cornerRadius: Radius.chip))
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
                    HStack(alignment: .firstTextBaseline, spacing: Spacing.s) {
                        // A play's stage, marked where it stands: the line is the CLI's.
                        if let stage = PlayNarration.stage(line) {
                            Text("\(stage.index)/\(stage.count)")
                                .font(.apogeeMeta.monospacedDigit())
                                .foregroundStyle(Palette.accent)
                        }
                        Text(line)
                            .font(.apogeeCode)
                            .foregroundStyle(Palette.textSecondary)
                            .textSelection(.enabled)
                    }
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
        if let play = activity.play {
            return play.summary(live: activity.live)
        }
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
            // A saved conversation is resumed; one never saved starts afresh.
            Button("Start again", action: restart)
        }
    }
}

#if DEBUG
#Preview("A turn that asked, wrote and answered") {
    let model = SessionPreview.session(SessionPreview.mockActor)
    let transcript = SessionPreview.folded(SessionPreview.mockActor, typed: SessionPreview.noteTurn, answers: SessionPreview.noteAnswers)
    VStack(alignment: .leading, spacing: Spacing.m) {
        ForEach(transcript.items) { TranscriptRow(item: $0, model: model) }
    }
    .frame(width: 720)
    .padding(Spacing.xl)
}

#Preview("Recalled chats, and the sources drawn on") {
    let model = SessionPreview.session(SessionPreview.mockActor)
    VStack(alignment: .leading, spacing: Spacing.m) {
        ForEach(SessionPreview.recalledAndSources.items) { TranscriptRow(item: $0, model: model) }
    }
    .frame(width: 720)
    .padding(Spacing.xl)
}

#Preview("A plan drawn in place, among notices") {
    let model = SessionPreview.session(SessionPreview.mockActor)
    VStack(alignment: .leading, spacing: Spacing.m) {
        ForEach(SessionPreview.plan.items) { TranscriptRow(item: $0, model: model) }
    }
    .frame(width: 720)
    .padding(Spacing.xl)
}

#Preview("An answer and its tokens") {
    let model = SessionPreview.session(SessionPreview.mockActor)
    VStack(alignment: .leading, spacing: Spacing.m) {
        ForEach(SessionPreview.answered.items) { TranscriptRow(item: $0, model: model) }
    }
    .frame(width: 720)
    .padding(Spacing.xl)
}

#Preview("The turn failed") {
    let model = SessionPreview.session(SessionPreview.mockActor)
    VStack(alignment: .leading, spacing: Spacing.m) {
        ForEach(SessionPreview.failed.items) { TranscriptRow(item: $0, model: model) }
    }
    .frame(width: 720)
    .padding(Spacing.xl)
}

#Preview("Stopped mid-answer") {
    let model = SessionPreview.session(SessionPreview.mockActor)
    VStack(alignment: .leading, spacing: Spacing.m) {
        ForEach(SessionPreview.stopped.items) { TranscriptRow(item: $0, model: model) }
    }
    .frame(width: 720)
    .padding(Spacing.xl)
}

#Preview("Opened in a project: the session's notices") {
    let model = SessionPreview.session(SessionPreview.mockActor)
    let transcript = SessionPreview.folded(SessionPreview.executeProject, typed: "hi")
    VStack(alignment: .leading, spacing: Spacing.m) {
        ForEach(transcript.items) { TranscriptRow(item: $0, model: model) }
    }
    .frame(width: 720)
    .padding(Spacing.xl)
}

#Preview("What this app does not know yet") {
    let model = SessionPreview.session(SessionPreview.mockActor)
    VStack(alignment: .leading, spacing: Spacing.m) {
        ForEach(SessionPreview.unknown.items) { TranscriptRow(item: $0, model: model) }
    }
    .frame(width: 720)
    .padding(Spacing.xl)
}

#Preview("The excerpts, numbered") {
    SourcesRow(sources: SessionPreview.sources)
        .padding(Spacing.xl)
}

#Preview("Chips on one line") {
    FlowChips(items: SessionPreview.plays.prefix(3).map(\.name))
        .font(.apogeeMeta)
        .padding(Spacing.xl)
}

#Preview("Chips wrapped to a narrow width") {
    FlowChips(items: SessionPreview.plays.map(\.name))
        .font(.apogeeMeta)
        .frame(width: 280)
        .padding(Spacing.xl)
}

#Preview("A quiet line") {
    QuietLine(symbol: "info.circle", text: "persona: coder, the shipped coding prompt, in every request -- -s replaces it")
        .padding(Spacing.xl)
}

#Preview("Thinking, open while live") {
    let transcript = SessionPreview.folded(SessionPreview.mockActor, typed: SessionPreview.noteTurn, silentFrom: "question")
    ActivityView(activity: SessionPreview.activity(transcript))
        .frame(width: 640)
        .padding(Spacing.xl)
}

#Preview("Thought, closed to its line") {
    let transcript = SessionPreview.folded(SessionPreview.mockActor, typed: SessionPreview.noteTurn, answers: SessionPreview.noteAnswers)
    ActivityView(activity: SessionPreview.activity(transcript))
        .frame(width: 640)
        .padding(Spacing.xl)
}

#Preview("A play, its stage live") {
    let transcript = SessionPreview.folded(SessionPreview.executePlay, typed: SessionPreview.playTurn, silentFrom: "answer_start")
    ActivityView(activity: SessionPreview.activity(transcript))
        .frame(width: 640)
        .padding(Spacing.xl)
}

#Preview("A play, played") {
    let transcript = SessionPreview.folded(SessionPreview.executePlay, typed: SessionPreview.playTurn)
    ActivityView(activity: SessionPreview.activity(transcript))
        .frame(width: 640)
        .padding(Spacing.xl)
}

#Preview("The model's own play") {
    let transcript = SessionPreview.folded(SessionPreview.executeOrchestrate, typed: "go")
    ActivityView(activity: SessionPreview.activity(transcript))
        .frame(width: 640)
        .padding(Spacing.xl)
}

#Preview("Ended") {
    EndedCard(ending: SessionPreview.finished) {}
        .frame(width: 640)
        .padding(Spacing.xl)
}

#Preview("Ended: failed, the details behind it") {
    EndedCard(ending: SessionPreview.failedEnding) {}
        .frame(width: 640)
        .padding(Spacing.xl)
}
#endif
