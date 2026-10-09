#include "machine/json_reporter.h"

#include <nlohmann/json.hpp>

#include <array>
#include <istream>
#include <stdexcept>

#include "contracts/errors.h"
#include "machine/driver_input.h"
#include "tasks/view.h"

namespace apogee::commands {
namespace {

/// Builds an event object with its type already set.
[[nodiscard]] nlohmann::json event(std::string_view type) {
    nlohmann::json object;
    object["type"] = std::string{type};
    return object;
}

/// A bounded copy of a driver's text for the log: a hello is the client's own
/// words, and a log line is not the place for an essay.
[[nodiscard]] std::string bounded(std::string text) {
    constexpr std::size_t kLimit = 200;
    if (text.size() > kLimit) {
        text.resize(kLimit);
        text += "...";
    }
    return text;
}

}  // namespace

JsonReporter::JsonReporter(std::ostream& out) : out_{&out} {}

void JsonReporter::write(nlohmann::json object) {
    // Every event of an open turn carries its number (28f) -- the session
    // event never does: it is before any turn.
    if (turn_.has_value() && object.value("type", std::string{}) != "session") {
        object["turn"] = *turn_;
    }
    // One object per line, flushed immediately. Flushing per event is the point
    // of a streaming protocol: a driver rendering live must not wait for a
    // buffer to fill, and the whole reason this mode exists is that a GUI wants
    // tokens as they arrive.
    //
    // Never a throw for the bytes in it. The answer and its reasoning arrive
    // as whole characters already (the Harness's streams), but an event's
    // other text comes from anywhere -- a vendor CLI's stderr tail cut at a
    // byte bound, a name on disk -- and a writer that threw would end the
    // session over a notice. What is not UTF-8 is said as U+FFFD; valid text is
    // written byte for byte as a strict dump writes it.
    (*out_) << object.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << "\n";
    out_->flush();
}

void JsonReporter::begin_turn(std::int64_t turn) {
    turn_ = turn;
    turn_text_.clear();
}

void JsonReporter::end_turn() {
    turn_.reset();
    turn_text_.clear();
}

const std::string& JsonReporter::turn_text() const noexcept {
    return turn_text_;
}

void JsonReporter::begin_session(std::string_view model, const MachineCapabilities& capabilities,
                                 std::optional<std::int64_t> next_turn) {
    nlohmann::json object = event("session");
    object["protocol_version"] = kMachineProtocolVersion;
    object["model"] = std::string{model};
    nlohmann::json announced = nlohmann::json::object();
    announced["events"] = nlohmann::json::array();
    for (const std::string_view type : machine_event_types()) {
        announced["events"].push_back(std::string{type});
    }
    announced["accepts"] = nlohmann::json::array();
    for (const std::string_view type : capabilities.accepts) {
        announced["accepts"].push_back(std::string{type});
    }
    announced["tools"] = capabilities.tools;
    announced["ask"] = capabilities.ask;
    announced["schema"] = std::string{kMachineSchemaVersion};
    object["capabilities"] = std::move(announced);
    if (next_turn.has_value()) {
        object["next_turn"] = *next_turn;
    }
    write(object);
}

void JsonReporter::on_thinking() {
    write(event("thinking"));
}

void JsonReporter::on_thinking_budget_reached() {
    // The same event the turn's thinking opened with, now saying the
    // reasoning was ended at its budget (26i) -- an added field, as the
    // protocol grows.
    nlohmann::json object = event("thinking");
    object["budget_reached"] = true;
    write(object);
}

void JsonReporter::on_thinking_token(std::string_view chunk) {
    if (chunk.empty()) {
        return;
    }
    nlohmann::json object = event("thinking_delta");
    object["text"] = std::string{chunk};
    write(object);
}

void JsonReporter::on_side_call(const agentloop::SideCall& call) {
    if (call.done) {
        return;
    }
    nlohmann::json object = event("tool_status");
    object["text"] = call.role + " — " + call.detail;
    write(object);
}

void JsonReporter::on_recall(int chats, int decisions) {
    if (chats <= 0 && decisions <= 0) {
        return;
    }
    nlohmann::json object = event("memory");
    object["chats"] = chats;
    object["decisions"] = decisions;
    write(object);
}

void JsonReporter::on_notice(std::string_view text) {
    if (text.empty()) {
        return;
    }
    nlohmann::json object = event("notice");
    object["text"] = std::string{text};
    write(object);
}

void JsonReporter::on_tool_status(std::string_view detail) {
    if (detail.empty()) {
        return;
    }
    nlohmann::json object = event("tool_status");
    object["text"] = std::string{detail};
    write(object);
}

void JsonReporter::on_clear_status() {
    // Erasing a transient indicator is a *terminal* concern -- there is nothing
    // to erase in a stream of records. Emitting an event for it would put a
    // rendering detail into the protocol, which is exactly the kind of leak
    // that makes a second vocabulary grow out of the first.
}

void JsonReporter::on_answer_start() {
    write(event("answer_start"));
}

void JsonReporter::on_answer_token(std::string_view chunk) {
    if (chunk.empty()) {
        return;
    }
    wrote_answer_ = true;
    if (turn_.has_value()) {
        turn_text_ += chunk;
    }
    nlohmann::json object = event("answer_delta");
    object["text"] = std::string{chunk};
    write(object);
}

void JsonReporter::on_answer_end() {
    write(event("answer_end"));
}

void JsonReporter::emit_result(const harness::ChatResponse& response) {
    nlohmann::json object = event("result");
    // The whole answer, so a driver that dropped every delta still has it --
    // the same reason stream_chat returns the complete response as well as
    // streaming it. Reasoning is NOT here: `plain_text()` is the answer, and
    // thinking never reaches the message in the first place.
    object["text"] = response.message.content.plain_text();
    object["model"] = response.model;
    object["finish_reason"] = std::string{harness::to_string(response.finish_reason)};

    if (response.usage.reported()) {
        nlohmann::json usage;
        usage["input_tokens"] = response.usage.prompt_tokens;
        usage["output_tokens"] = response.usage.completion_tokens;
        object["usage"] = std::move(usage);
    }
    write(object);
}

void JsonReporter::emit_question(const agentloop::QuestionRequest& request) {
    nlohmann::json object = event("question");
    nlohmann::json questions = nlohmann::json::array();
    for (const agentloop::Question& question : request.questions) {
        nlohmann::json entry;
        entry["header"] = question.header;
        entry["question"] = question.question;
        entry["multi_select"] = question.multi_select;

        nlohmann::json options = nlohmann::json::array();
        for (const agentloop::QuestionOption& option : question.options) {
            nlohmann::json rendered;
            rendered["label"] = option.label;
            rendered["description"] = option.description;
            options.push_back(std::move(rendered));
        }
        entry["options"] = std::move(options);
        questions.push_back(std::move(entry));
    }
    object["questions"] = std::move(questions);
    write(object);
}

void JsonReporter::emit_permission_question(const agent::GateRequest& request) {
    const std::string tool{request.tool};
    const std::string target{request.target};
    nlohmann::json object = event("question");
    object["kind"] = "permission";
    object["tool"] = tool;
    object["target"] = target;
    if (request.outbound) {
        object["outbound"] = true;
    }
    if (!request.detail.empty()) {
        object["detail"] = std::string{request.detail};
    }
    nlohmann::json entry;
    entry["header"] = "Permission";
    entry["question"] = request.outbound
                            ? "Allow " + tool + " to reach " + target + "?"
                            : "Allow " + tool + (target.empty() ? "" : " on " + target) + "?";
    entry["multi_select"] = false;
    entry["options"] = nlohmann::json::array(
        {{{"label", "yes"}, {"description", "Allow this once"}},
         {{"label", "no"}, {"description", "Deny"}},
         {{"label", "always"},
          {"description", request.outbound ? "Allow, and add the website to tools.allowed_hosts"
                                           : "Allow, and remember it in the config"}},
         {{"label", "session"},
          {"description", request.outbound ? "Allow this website for the rest of this session"
                                           : "Allow for the rest of this session"}}});
    object["questions"] = nlohmann::json::array({std::move(entry)});
    write(object);
}

void JsonReporter::emit_error(std::string_view message) {
    nlohmann::json object = event("error");
    object["message"] = std::string{message};
    write(object);
}

void JsonReporter::emit_task_transition(const tasks::Task& task, std::size_t index,
                                        const std::optional<tasks::LockHolder>& holder) {
    if (index >= task.transitions.size()) {
        return;
    }
    const tasks::Transition& transition = task.transitions[index];
    const std::string& name = transition.event;
    // The turn a plan or round transition is about: the plan, or the newest
    // round of that number -- a round run again after a restart is the same
    // round, as the ledger keeps it.
    const auto turn = [&task, &transition](bool plan) -> nlohmann::json {
        for (auto round = task.rounds.rbegin(); round != task.rounds.rend(); ++round) {
            if ((round->kind == tasks::kPlanRound) == plan &&
                (plan || round->index == transition.round)) {
                return tasks::to_json(tasks::make_turn_view(*round));
            }
        }
        return nullptr;
    };
    nlohmann::json object;
    if (name == tasks::kStartedEvent || name == tasks::kResumedEvent) {
        object = event("task_started");
        object["resumed"] = name == tasks::kResumedEvent;
        nlohmann::json history = nlohmann::json::array();
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            history.push_back(tasks::transition_to_json(task.transitions[earlier]));
        }
        object["history"] = std::move(history);
        object["task"] = tasks::to_json(tasks::make_task_view(task, holder));
    } else if (name == tasks::kPlanStartedEvent || name == tasks::kPlanRecordedEvent) {
        object = event("task_plan");
        object["round"] = turn(true);
        if (name == tasks::kPlanRecordedEvent) {
            object["plan"] = task.plan;
        }
    } else if (name == tasks::kRoundStartedEvent || name == tasks::kRoundEndedEvent) {
        object = event("task_round");
        object["round"] = turn(false);
        if (name == tasks::kRoundEndedEvent) {
            nlohmann::json checks = nlohmann::json::array();
            for (const tasks::CheckView& check : tasks::make_check_views(task)) {
                checks.push_back(tasks::to_json(check));
            }
            object["checks"] = std::move(checks);
            object["rounds_used"] = tasks::rounds_used(task);
        }
    } else if (name == tasks::kFinishedEvent) {
        object = event("task_finished");
        object["status"] = transition.status;
        object["reason"] = task.reason;
        object["task"] = tasks::to_json(tasks::make_task_view(task, holder));
    } else {
        // `created` is written before any run, and reaches a driver as the
        // history `task_started` carries.
        return;
    }
    object["task_id"] = task.id;
    object["transition"] = tasks::transition_to_json(transition);
    write(object);
}

void JsonReporter::emit_task_grant(const tasks::Task& task, int round,
                                   const tasks::Permit& permit) {
    nlohmann::json object = event("task_grant");
    object["task_id"] = task.id;
    object["round"] = round;
    object["tool"] = permit.tool;
    object["target"] = permit.target;
    object["by"] = permit.by;
    write(object);
}

bool JsonReporter::wrote_answer() const noexcept {
    return wrote_answer_;
}

std::string_view to_string(OutputFormat format) noexcept {
    return format == OutputFormat::StreamJson ? "stream-json" : "text";
}

std::string_view to_string(InputFormat format) noexcept {
    return format == InputFormat::StreamJson ? "stream-json" : "text";
}

std::optional<InputFormat> input_format_from_string(std::string_view name) noexcept {
    if (name == "text" || name.empty()) {
        return InputFormat::Text;
    }
    if (name == "stream-json") {
        return InputFormat::StreamJson;
    }
    return std::nullopt;
}

std::vector<std::string_view> format_names() {
    return {to_string(OutputFormat::Text), to_string(OutputFormat::StreamJson)};
}

std::string_view to_string(ReadFormat format) noexcept {
    return format == ReadFormat::Json ? "json" : "text";
}

std::optional<ReadFormat> read_format_from_string(std::string_view name) noexcept {
    if (name == "text" || name.empty()) {
        return ReadFormat::Text;
    }
    if (name == "json") {
        return ReadFormat::Json;
    }
    return std::nullopt;
}

std::vector<std::string_view> read_format_names() {
    return {to_string(ReadFormat::Text), to_string(ReadFormat::Json)};
}

void write_document(std::ostream& out, const nlohmann::json& document) {
    out << document.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << "\n";
    out.flush();
}

std::optional<OutputFormat> output_format_from_string(std::string_view name) noexcept {
    if (name == "text" || name.empty()) {
        return OutputFormat::Text;
    }
    if (name == "stream-json") {
        return OutputFormat::StreamJson;
    }
    return std::nullopt;
}

DriverMessage parse_driver_line(std::string_view line) {
    DriverMessage message;

    // Non-throwing, and an unrecognised line is Unknown rather than an error:
    // a driver sending something this build does not know must not kill the
    // session. It is the same tolerance this protocol demands of drivers,
    // applied in the other direction.
    const nlohmann::json root = nlohmann::json::parse(line, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return message;
    }

    const std::string type = root.value("type", std::string{});
    if (type == "user") {
        message.kind = DriverMessage::Kind::User;
    } else if (type == "answer") {
        message.kind = DriverMessage::Kind::Answer;
    } else if (type == "cancel") {
        message.kind = DriverMessage::Kind::Cancel;
        return message;
    } else if (type == "hello") {
        // A driver introducing itself (28d): recorded, never acted on in this
        // cut -- and a malformed field is just absent.
        message.kind = DriverMessage::Kind::Hello;
        if (const auto client = root.find("client"); client != root.end() && client->is_object()) {
            const auto text_of = [&client](const char* key) {
                const auto found = client->find(key);
                return found != client->end() && found->is_string() ? found->get<std::string>()
                                                                    : std::string{};
            };
            message.client_name = text_of("name");
            message.client_version = text_of("version");
        }
        if (const auto wants = root.find("wants"); wants != root.end()) {
            message.wants = wants->dump();
        }
        return message;
    } else if (type == "attach") {
        // A file, folder or glob to attach, as `/attach` takes one (26d),
        // and its method, as `/attach`'s `--graph` (27p).
        message.kind = DriverMessage::Kind::Attach;
        message.text = root.value("path", std::string{});
        if (const auto graph = root.find("graph"); graph != root.end() && !graph->is_null()) {
            message.graph = graph->is_string() ? graph->get<std::string>() : graph->dump();
        }
        return message;
    } else {
        return message;
    }

    message.text = root.value("text", std::string{});
    return message;
}

std::string describe_hello(const DriverMessage& hello) {
    std::string out = "hello from " + (hello.client_name.empty() ? std::string{"an unnamed client"}
                                                                 : bounded(hello.client_name));
    if (!hello.client_version.empty()) {
        out += " " + bounded(hello.client_version);
    }
    if (!hello.wants.empty()) {
        out += ", wants " + bounded(hello.wants);
    }
    return out;
}

agentloop::AskFn make_driver_ask_fn(JsonReporter& reporter, DriverInput& input) {
    return [&reporter, &input](const agentloop::QuestionRequest& request) {
        reporter.emit_question(request);

        agentloop::Answers answers;
        while (answers.values.size() < request.questions.size()) {
            bool cancelled = false;
            const std::optional<DriverLine> line = input.next_line_in_turn(cancelled);
            if (cancelled) {
                // The driver cancelled the turn while it was asked (28f): it
                // fails as a closed stdin fails it, the half-turn rolled
                // back, and the session goes on.
                throw harness::CancelledError();
            }
            if (!line.has_value()) {
                break;
            }
            const DriverMessage message = parse_driver_line(line->text);
            if (message.kind == DriverMessage::Kind::Answer) {
                answers.values.push_back(message.text);
            }
            // Anything else is dropped while a question is outstanding: a
            // driver may not interleave a new user turn into a pending
            // question, and an unknown type is never fatal.
        }

        if (answers.values.size() < request.questions.size()) {
            // The driver hung up mid-question. Throwing is what the seam asks
            // for -- the loop rolls the half-turn out of history, so nothing
            // dangling is persisted. Fabricating an answer would be worse than
            // failing, which is the whole reason the AskFn may throw at all.
            throw std::runtime_error("the driver closed stdin with a question unanswered");
        }
        return answers;
    };
}

}  // namespace apogee::commands
