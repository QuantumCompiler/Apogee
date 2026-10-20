import DataProtocol
import PresentationStyles
import SwiftUI

/// The model asks: each question with its options, and room for the user's
/// own answer every time -- the options are a convenience, not a constraint.
/// Answered, it collapses to what was asked and what was said.
struct QuestionCardView: View {
    let card: QuestionCard
    let answer: ([String]) -> Void

    @State private var picked: [Int: Set<String>] = [:]
    @State private var written: [Int: String] = [:]

    var body: some View {
        if let answers = card.answers {
            QuietLine(
                symbol: "questionmark.bubble",
                text: zip(card.question.items, answers)
                    .map { "asked: \($0.question) → \($1)" }.joined(separator: "\n"))
        } else {
            Card(accent: Palette.accent) {
                CardLabel(text: "Apogee is asking")
                ForEach(Array(card.question.items.enumerated()), id: \.offset) { index, item in
                    itemView(index, item)
                }
                HStack {
                    Spacer()
                    Button("Answer") { answer(collected) }
                        .keyboardShortcut(.defaultAction)
                        .disabled(collected.count != card.question.items.count)
                }
            }
        }
    }

    @ViewBuilder
    private func itemView(_ index: Int, _ item: QuestionItem) -> some View {
        VStack(alignment: .leading, spacing: Spacing.s) {
            if !item.header.isEmpty {
                Text(item.header).font(.apogeeMeta).foregroundStyle(Palette.textTertiary)
            }
            Text(item.question).font(.apogeeHeading).foregroundStyle(Palette.textPrimary)
            ForEach(item.options, id: \.label) { option in
                Button {
                    toggle(index, option.label, multiple: item.multiSelect)
                } label: {
                    HStack(alignment: .firstTextBaseline, spacing: Spacing.s) {
                        Image(systemName: symbol(index, option.label, multiple: item.multiSelect))
                            .foregroundStyle(Palette.accent)
                        VStack(alignment: .leading, spacing: Spacing.xxs) {
                            Text(option.label).font(.apogeeBody).foregroundStyle(Palette.textPrimary)
                            if !option.description.isEmpty {
                                Text(option.description).font(.apogeeSmall).foregroundStyle(Palette.textSecondary)
                            }
                        }
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
            }
            TextField(
                "Or answer in your own words",
                text: Binding(get: { written[index] ?? "" }, set: { written[index] = $0 })
            )
            .textFieldStyle(.roundedBorder)
            .onSubmit { if collected.count == card.question.items.count { answer(collected) } }
        }
    }

    /// One answer per item: the user's own words where written, else the
    /// options picked, in the order offered.
    private var collected: [String] {
        card.question.items.enumerated().compactMap { index, item in
            let own = (written[index] ?? "").trimmingCharacters(in: .whitespacesAndNewlines)
            if !own.isEmpty { return own }
            let chosen = item.options.map(\.label).filter { picked[index]?.contains($0) == true }
            return chosen.isEmpty ? nil : chosen.joined(separator: ", ")
        }
    }

    private func toggle(_ index: Int, _ label: String, multiple: Bool) {
        var set = picked[index] ?? []
        if multiple {
            if set.contains(label) { set.remove(label) } else { set.insert(label) }
        } else {
            set = [label]
        }
        picked[index] = set
    }

    private func symbol(_ index: Int, _ label: String, multiple: Bool) -> String {
        let on = picked[index]?.contains(label) == true
        return multiple ? (on ? "checkmark.square.fill" : "square") : (on ? "largecircle.fill.circle" : "circle")
    }
}
