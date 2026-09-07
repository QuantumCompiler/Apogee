#include "commands/json_reporter.h"

#include <nlohmann/json.hpp>

#include <istream>
#include <stdexcept>

namespace apogee::commands {
namespace {

/// Builds an event object with its type already set.
[[nodiscard]] nlohmann::json event(std::string_view type) {
    nlohmann::json object;
    object["type"] = std::string{type};
    return object;
}

}  // namespace

JsonReporter::JsonReporter(std::ostream& out) : out_{&out} {}

void JsonReporter::write(const std::string& line) {
    // One object per line, flushed immediately. Flushing per event is the point
    // of a streaming protocol: a driver rendering live must not wait for a
    // buffer to fill, and the whole reason this mode exists is that a GUI wants
    // tokens as they arrive.
    (*out_) << line << "\n";
    out_->flush();
}

void JsonReporter::begin_session(std::string_view model) {
    nlohmann::json object = event("session");
    object["protocol_version"] = kMachineProtocolVersion;
    object["model"] = std::string{model};
    write(object.dump());
}

void JsonReporter::on_thinking() {
    write(event("thinking").dump());
}

void JsonReporter::on_thinking_token(std::string_view chunk) {
    if (chunk.empty()) {
        return;
    }
    nlohmann::json object = event("thinking_delta");
    object["text"] = std::string{chunk};
    write(object.dump());
}

void JsonReporter::on_tool_status(std::string_view detail) {
    if (detail.empty()) {
        return;
    }
    nlohmann::json object = event("tool_status");
    object["text"] = std::string{detail};
    write(object.dump());
}

void JsonReporter::on_clear_status() {
    // Erasing a transient indicator is a *terminal* concern -- there is nothing
    // to erase in a stream of records. Emitting an event for it would put a
    // rendering detail into the protocol, which is exactly the kind of leak
    // that makes a second vocabulary grow out of the first.
}

void JsonReporter::on_answer_start() {
    write(event("answer_start").dump());
}

void JsonReporter::on_answer_token(std::string_view chunk) {
    if (chunk.empty()) {
        return;
    }
    wrote_answer_ = true;
    nlohmann::json object = event("answer_delta");
    object["text"] = std::string{chunk};
    write(object.dump());
}

void JsonReporter::on_answer_end() {
    write(event("answer_end").dump());
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
    write(object.dump());
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
    write(object.dump());
}

void JsonReporter::emit_error(std::string_view message) {
    nlohmann::json object = event("error");
    object["message"] = std::string{message};
    write(object.dump());
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
    } else {
        return message;
    }

    message.text = root.value("text", std::string{});
    return message;
}

agentloop::AskFn make_driver_ask_fn(JsonReporter& reporter, std::istream& input) {
    return [&reporter, &input](const agentloop::QuestionRequest& request) {
        reporter.emit_question(request);

        agentloop::Answers answers;
        std::string line;
        while (answers.values.size() < request.questions.size() && std::getline(input, line)) {
            const DriverMessage message = parse_driver_line(line);
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
