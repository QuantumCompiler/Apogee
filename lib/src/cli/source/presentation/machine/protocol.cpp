#include "machine/protocol.h"

#include <nlohmann/json.hpp>

#include <array>
#include <string>

namespace apogee::commands {

namespace {

// --- the fields --------------------------------------------------------------
//
// Each event's fields beside `type`. Every event but `session` may also carry
// `turn` (28f): added once, where the schema is rendered, not restated here.

constexpr std::array<FieldSpec, 5> kCapabilities{{
    {"events", "array", true, "Every event type this build can write.", {}, "string"},
    {"accepts", "array", true, "The line types this session reads on stdin.", {}, "string"},
    {"tools", "boolean", true, "Whether the model can call tools in this session.", {}, {}},
    {"ask",
     "boolean",
     true,
     "Whether ask_user and the permission prompt reach the driver as question events.",
     {},
     {}},
    {"schema", "string", true, "The vocabulary's version, a date.", {}, {}},
}};

constexpr std::array<FieldSpec, 4> kSession{{
    {"protocol_version",
     "integer",
     true,
     "Bumped only when an existing event's meaning changes.",
     {},
     {}},
    {"model", "string", true, "The backend answering.", {}, {}},
    {"capabilities", "object", true, "What this session can do.", kCapabilities, {}},
    {"next_turn",
     "integer",
     false,
     "A session of user lines: the number its next turn will carry.",
     {},
     {}},
}};

constexpr std::array<FieldSpec, 1> kThinking{{
    {"budget_reached",
     "boolean",
     false,
     "The reasoning reached its thinking budget and was ended there.",
     {},
     {}},
}};

constexpr std::array<FieldSpec, 1> kText{{
    {"text", "string", true, "The text, to concatenate or display as it arrives.", {}, {}},
}};

constexpr std::array<FieldSpec, 2> kMemory{{
    {"chats", "integer", true, "Past chats' summaries recalled for this turn.", {}, {}},
    {"decisions", "integer", true, "Knowledge records recalled for this turn.", {}, {}},
}};

constexpr std::array<FieldSpec, 2> kUsage{{
    {"input_tokens", "integer", true, "Tokens sent.", {}, {}},
    {"output_tokens", "integer", true, "Tokens generated.", {}, {}},
}};

constexpr std::array<FieldSpec, 4> kResult{{
    {"text", "string", true, "The whole answer, reasoning never in it.", {}, {}},
    {"model", "string", true, "The model that answered.", {}, {}},
    {"finish_reason",
     "string",
     true,
     "Why the turn ended: stop, length, tool_calls, content_filter, cancelled (a cancel "
     "ended it), other -- more may be added.",
     {},
     {}},
    {"usage",
     "object",
     false,
     "Absent when the provider reported none -- absent is not zero.",
     kUsage,
     {}},
}};

constexpr std::array<FieldSpec, 2> kOption{{
    {"label", "string", true, "The option, as an answer would give it.", {}, {}},
    {"description", "string", true, "What choosing it means.", {}, {}},
}};

constexpr std::array<FieldSpec, 4> kQuestionEntry{{
    {"header", "string", true, "A short label for the question.", {}, {}},
    {"question", "string", true, "The question.", {}, {}},
    {"multi_select", "boolean", true, "Whether more than one option may be chosen.", {}, {}},
    {"options", "array", true, "Offered answers -- free text is always accepted too.", kOption, {}},
}};

constexpr std::array<FieldSpec, 6> kQuestion{{
    {"questions",
     "array",
     true,
     "Each to be answered by one answer line, in order.",
     kQuestionEntry,
     {}},
    {"kind",
     "string",
     false,
     "permission for the permission gate's question; absent for "
     "ask_user.",
     {},
     {}},
    {"tool", "string", false, "A permission question: the tool asking.", {}, {}},
    {"target", "string", false, "A permission question: its path, command or host.", {}, {}},
    {"outbound", "boolean", false, "A permission question about data leaving the machine.", {}, {}},
    {"detail", "string", false, "An outbound question: the whole URL.", {}, {}},
}};

constexpr std::array<FieldSpec, 1> kError{{
    {"message", "string", true, "Why the turn failed.", {}, {}},
}};

constexpr std::array<FieldSpec, 5> kTaskStarted{{
    {"resumed", "boolean", true, "Whether this run resumed the task.", {}, {}},
    {"history", "array", true, "Every transition before this one.", {}, "object"},
    {"task", "object", true, "The task's whole view.", {}, {}},
    {"task_id", "string", true, "The task.", {}, {}},
    {"transition", "object", true, "The transition, as the ledger wrote it.", {}, {}},
}};

constexpr std::array<FieldSpec, 4> kTaskPlan{{
    {"round", "object|null", true, "The plan turn.", {}, {}},
    {"plan", "string", false, "Once recorded: the plan.", {}, {}},
    {"task_id", "string", true, "The task.", {}, {}},
    {"transition", "object", true, "The transition, as the ledger wrote it.", {}, {}},
}};

constexpr std::array<FieldSpec, 5> kTaskRound{{
    {"round", "object|null", true, "The round.", {}, {}},
    {"checks", "array", false, "Once the round ended: each check's state.", {}, "object"},
    {"rounds_used", "integer", false, "Once the round ended: rounds used of the budget.", {}, {}},
    {"task_id", "string", true, "The task.", {}, {}},
    {"transition", "object", true, "The transition, as the ledger wrote it.", {}, {}},
}};

constexpr std::array<FieldSpec, 5> kTaskGrant{{
    {"task_id", "string", true, "The task.", {}, {}},
    {"round", "integer", true, "The round the grant was used in.", {}, {}},
    {"tool", "string", true, "The tool the grant let through.", {}, {}},
    {"target", "string", true, "What it was let at.", {}, {}},
    {"by", "string", true, "The authority: grant.", {}, {}},
}};

constexpr std::array<FieldSpec, 5> kTaskFinished{{
    {"status", "string", true, "How the task ended.", {}, {}},
    {"reason", "string", true, "Why, in words.", {}, {}},
    {"task", "object", true, "The task's final view.", {}, {}},
    {"task_id", "string", true, "The task.", {}, {}},
    {"transition", "object", true, "The transition, as the ledger wrote it.", {}, {}},
}};

// Inbound lines.

constexpr std::array<FieldSpec, 1> kUserLine{{
    {"text", "string", true, "The user's message: one turn.", {}, {}},
}};

constexpr std::array<FieldSpec, 1> kAnswerLine{{
    {"text", "string", true, "The answer to the oldest unanswered question.", {}, {}},
}};

constexpr std::array<FieldSpec, 2> kAttachLine{{
    {"path", "string", true, "A file, folder or glob to attach.", {}, {}},
    {"graph", "string", false, "A folder of code: code or off, over the config's.", {}, {}},
}};

constexpr std::array<FieldSpec, 2> kClient{{
    {"name", "string", false, "The host application.", {}, {}},
    {"version", "string", false, "Its version.", {}, {}},
}};

constexpr std::array<FieldSpec, 2> kHelloLine{{
    {"client", "object", false, "Who is driving.", kClient, {}},
    {"wants",
     "array",
     false,
     "Words for what the host wants -- recorded, not acted on.",
     {},
     "string"},
}};

// --- the lines -----------------------------------------------------------------

constexpr std::array<LineSpec, 17> kOutbound{{
    LineSpec{"session",
             "Once, first, unprompted: the protocol version, the model, the session's "
             "capabilities.",
             kSession},
    LineSpec{"thinking", "The model began reasoning; again with budget_reached when it ran out.",
             kThinking},
    LineSpec{"thinking_delta", "A chunk of reasoning -- droppable.", kText},
    LineSpec{"memory", "What a turn was handed from earlier conversations.", kMemory},
    LineSpec{"tool_status", "A tool running, or another model call the turn makes.", kText},
    LineSpec{"notice", "A line for the user that is neither progress nor an error.", kText},
    LineSpec{"answer_start", "An answer begins.", {}},
    LineSpec{"answer_delta", "A chunk of answer text.", kText},
    LineSpec{"answer_end", "The answer ended.", {}},
    LineSpec{"result", "Ends a turn, with the whole answer.", kResult},
    LineSpec{"question", "A question that waits for answer lines.", kQuestion},
    LineSpec{"error", "A turn failed: ends it in place of a result.", kError},
    LineSpec{"task_started", "A task run started or resumed.", kTaskStarted},
    LineSpec{"task_plan", "A task's plan turn started, or its plan was recorded.", kTaskPlan},
    LineSpec{"task_round", "A task's round started or ended.", kTaskRound},
    LineSpec{"task_grant", "A call a task's grant let through.", kTaskGrant},
    LineSpec{"task_finished", "A task ended.", kTaskFinished},
}};

constexpr std::array<LineSpec, 5> kInbound{{
    LineSpec{"user", "A user turn.", kUserLine},
    LineSpec{"answer", "An answer to a pending question.", kAnswerLine},
    LineSpec{"attach", "Something to attach to the chat.", kAttachLine},
    LineSpec{"hello", "An optional first line: who is driving.", kHelloLine},
    LineSpec{"cancel", "Stop the turn in flight, as Ctrl-C would.", {}},
}};

template <std::size_t N>
[[nodiscard]] constexpr std::array<std::string_view, N> types_of(
    const std::array<LineSpec, N>& lines) {
    std::array<std::string_view, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        out[i] = lines[i].type;
    }
    return out;
}

constexpr std::array<std::string_view, kOutbound.size()> kOutboundTypes = types_of(kOutbound);
constexpr std::array<std::string_view, kInbound.size()> kInboundTypes = types_of(kInbound);

constexpr FieldSpec kTurn{"turn",
                          "integer",
                          false,
                          "In a driven chat or execute: the turn this event belongs to, the "
                          "number of its user line.",
                          {},
                          {}};

[[nodiscard]] nlohmann::json type_of(std::string_view type) {
    const std::size_t bar = type.find('|');
    if (bar == std::string_view::npos) {
        return std::string{type};
    }
    return nlohmann::json::array(
        {std::string{type.substr(0, bar)}, std::string{type.substr(bar + 1)}});
}

[[nodiscard]] nlohmann::json object_of(std::span<const FieldSpec> fields,
                                       const FieldSpec* extra = nullptr);

[[nodiscard]] nlohmann::json field_schema(const FieldSpec& field) {
    nlohmann::json schema = nlohmann::json::object();
    schema["type"] = type_of(field.type);
    schema["description"] = std::string{field.description};
    if (field.type.starts_with("object") && !field.properties.empty()) {
        nlohmann::json nested = object_of(field.properties);
        schema["properties"] = nested["properties"];
        schema["required"] = nested["required"];
        schema["additionalProperties"] = true;
    }
    if (field.type == "array") {
        if (!field.properties.empty()) {
            schema["items"] = object_of(field.properties);
        } else if (!field.items.empty()) {
            schema["items"] = nlohmann::json{{"type", std::string{field.items}}};
        }
    }
    return schema;
}

nlohmann::json object_of(std::span<const FieldSpec> fields, const FieldSpec* extra) {
    nlohmann::json schema = nlohmann::json::object();
    schema["type"] = "object";
    nlohmann::json properties = nlohmann::json::object();
    nlohmann::json required = nlohmann::json::array();
    for (const FieldSpec& field : fields) {
        properties[std::string{field.name}] = field_schema(field);
        if (field.required) {
            required.push_back(std::string{field.name});
        }
    }
    if (extra != nullptr) {
        properties[std::string{extra->name}] = field_schema(*extra);
    }
    schema["properties"] = std::move(properties);
    schema["required"] = std::move(required);
    // The stability promise: a field a later build adds validates.
    schema["additionalProperties"] = true;
    return schema;
}

/// A line's definition: its fields, and its `type` pinned to its name.
[[nodiscard]] nlohmann::json line_schema(const LineSpec& line, bool outbound) {
    nlohmann::json schema =
        object_of(line.fields, outbound && line.type != "session" ? &kTurn : nullptr);
    schema["description"] = std::string{line.description};
    schema["properties"]["type"] = nlohmann::json{{"const", std::string{line.type}}};
    schema["required"].insert(schema["required"].begin(), "type");
    return schema;
}

/// One direction: an object with a string `type`, and a known type held to
/// its definition -- an unknown one validates, as the promise says it must.
[[nodiscard]] nlohmann::json direction(std::span<const LineSpec> lines, std::string_view prefix,
                                       std::string_view description) {
    nlohmann::json schema = nlohmann::json::object();
    schema["description"] = std::string{description};
    schema["type"] = "object";
    schema["required"] = nlohmann::json::array({"type"});
    schema["properties"] = nlohmann::json{{"type", {{"type", "string"}}}};
    nlohmann::json each = nlohmann::json::array();
    for (const LineSpec& line : lines) {
        nlohmann::json when = nlohmann::json::object();
        when["if"] = nlohmann::json{{"properties", {{"type", {{"const", std::string{line.type}}}}}},
                                    {"required", nlohmann::json::array({"type"})}};
        when["then"] =
            nlohmann::json{{"$ref", "#/$defs/" + std::string{prefix} + std::string{line.type}}};
        each.push_back(std::move(when));
    }
    schema["allOf"] = std::move(each);
    return schema;
}

}  // namespace

std::span<const LineSpec> machine_events() noexcept {
    return kOutbound;
}

std::span<const LineSpec> machine_inbound() noexcept {
    return kInbound;
}

std::span<const std::string_view> machine_event_types() noexcept {
    return kOutboundTypes;
}

std::span<const std::string_view> machine_inbound_types() noexcept {
    return kInboundTypes;
}

nlohmann::json machine_schema() {
    nlohmann::json schema = nlohmann::json::object();
    schema["$schema"] = "https://json-schema.org/draft/2020-12/schema";
    schema["$id"] = "https://github.com/QuantumCompiler/Apogee/machine-schema/" +
                    std::string{kMachineSchemaVersion};
    schema["title"] = "Apogee machine mode";
    schema["description"] =
        "One JSON object per line. The root validates a line Apogee writes on stdout; "
        "#/$defs/inbound a line a driver writes on stdin. An unknown type validates, and every "
        "line admits fields it does not name: ignore what you do not know.";
    schema["x-apogee"] = nlohmann::json{{"protocol_version", kMachineProtocolVersion},
                                        {"schema", std::string{kMachineSchemaVersion}}};
    nlohmann::json defs = nlohmann::json::object();
    defs["outbound"] = direction(kOutbound, "event_", "A line Apogee writes.");
    defs["inbound"] = direction(kInbound, "line_", "A line a driver writes.");
    for (const LineSpec& line : kOutbound) {
        defs["event_" + std::string{line.type}] = line_schema(line, true);
    }
    for (const LineSpec& line : kInbound) {
        defs["line_" + std::string{line.type}] = line_schema(line, false);
    }
    schema["$defs"] = std::move(defs);
    schema["$ref"] = "#/$defs/outbound";
    return schema;
}

}  // namespace apogee::commands
