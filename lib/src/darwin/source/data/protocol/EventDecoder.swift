/// Reads one stdout line into a `ProtocolEvent`, and never fails.
///
/// A known type is read into its case; one missing a field its case needs is
/// kept as `unknown` under its own type rather than guessed at. Fields the
/// case does not read stay in `fields`. A `task_*` type is kept whole.
public enum EventDecoder {
    public static func decode(line: String) -> ProtocolEvent {
        guard let value = JSONValue.parse(line), let fields = value.objectValue,
            let type = fields["type"]?.stringValue
        else {
            return ProtocolEvent(kind: .unreadable(line: line), fields: [:])
        }
        let kind = known(type: type, fields) ?? (type.hasPrefix("task_") ? .task(type: type) : .unknown(type: type))
        return ProtocolEvent(kind: kind, fields: fields)
    }

    private static func known(type: String, _ fields: [String: JSONValue]) -> ProtocolEvent.Kind? {
        func string(_ key: String) -> String? { fields[key]?.stringValue }
        switch type {
        case "session":
            guard let version = fields["protocol_version"]?.intValue, let model = string("model") else { return nil }
            return .session(
                SessionInfo(
                    protocolVersion: version, model: model, nextTurn: fields["next_turn"]?.intValue ?? 1,
                    capabilities: fields["capabilities"].flatMap(capabilities)))
        case "thinking":
            return .thinking(budgetReached: fields["budget_reached"]?.boolValue ?? false)
        case "thinking_delta":
            return string("text").map { .thinkingDelta($0) }
        case "memory":
            return .memory(chats: fields["chats"]?.intValue ?? 0, decisions: fields["decisions"]?.intValue ?? 0)
        case "tool_status":
            return string("text").map { .toolStatus($0) }
        case "notice":
            return string("text").map { .notice($0) }
        case "todo":
            guard let items = fields["items"]?.arrayValue else { return nil }
            return .todo(
                items.compactMap { item in
                    item["text"]?.stringValue.map { TodoItem(text: $0, state: item["state"]?.stringValue ?? "pending") }
                })
        case "answer_start":
            return .answerStart
        case "answer_delta":
            return string("text").map { .answerDelta($0) }
        case "answer_end":
            return .answerEnd
        case "result":
            guard let text = string("text") else { return nil }
            let usage = fields["usage"].map {
                Usage(inputTokens: $0["input_tokens"]?.intValue, outputTokens: $0["output_tokens"]?.intValue)
            }
            return .result(
                TurnResult(text: text, model: string("model"), finishReason: string("finish_reason"), usage: usage))
        case "question":
            guard let questions = fields["questions"]?.arrayValue else { return nil }
            return .question(
                Question(
                    kind: string("kind"), tool: string("tool"), target: string("target"),
                    outbound: fields["outbound"]?.boolValue ?? false, detail: string("detail"),
                    items: questions.map(questionItem)))
        case "error":
            return string("message").map { .error(message: $0) }
        default:
            return nil
        }
    }

    private static func capabilities(_ value: JSONValue) -> Capabilities? {
        guard value.objectValue != nil else { return nil }
        func strings(_ key: String) -> [String] { value[key]?.arrayValue?.compactMap(\.stringValue) ?? [] }
        return Capabilities(
            events: strings("events"), accepts: strings("accepts"), tools: value["tools"]?.boolValue ?? false,
            ask: value["ask"]?.boolValue ?? false, schema: value["schema"]?.stringValue ?? "")
    }

    private static func questionItem(_ value: JSONValue) -> QuestionItem {
        QuestionItem(
            header: value["header"]?.stringValue ?? "", question: value["question"]?.stringValue ?? "",
            multiSelect: value["multi_select"]?.boolValue ?? false,
            options: (value["options"]?.arrayValue ?? []).compactMap { option in
                option["label"]?.stringValue.map {
                    QuestionOption(label: $0, description: option["description"]?.stringValue ?? "")
                }
            })
    }
}
