#include "tasks/unattended.h"

#include <mutex>
#include <string_view>
#include <utility>

namespace apogee::tasks {

struct TurnRecorder::State {
    std::mutex mutex;
    std::vector<ToolUse> tools;
    std::vector<Denial> denials;
};

TurnRecorder::TurnRecorder() : state_{std::make_shared<State>()} {}

agent::ToolRegistry TurnRecorder::observe(const agent::ToolRegistry& registry) const {
    agent::ToolRegistry out;
    for (const std::string& name : registry.names()) {
        const agent::Tool* found = registry.find(name);
        if (found == nullptr) {
            continue;
        }
        agent::Tool tool = *found;
        const auto record = [state = state_, name](std::string_view arguments) {
            const std::scoped_lock lock{state->mutex};
            state->tools.push_back(
                ToolUse{.tool = name, .fingerprint = fingerprint(name, arguments)});
        };
        if (tool.run) {
            tool.run = [record, run = std::move(tool.run)](std::string_view arguments) {
                record(arguments);
                return run(arguments);
            };
        }
        if (tool.run_gated) {
            tool.run_gated = [record, run = std::move(tool.run_gated)](
                                 std::string_view arguments, const agent::TargetGate& target) {
                record(arguments);
                return run(arguments, target);
            };
        }
        out.add(std::move(tool));
    }
    out.set_environment(registry.environment_source());
    return out;
}

agent::PermissionChecker TurnRecorder::gate(agent::PermissionChecker checker) const {
    return [state = state_, checker = std::move(checker)](const agent::GateRequest& request) {
        const agent::Permission decision = checker ? checker(request) : agent::Permission::Ask;
        if (decision != agent::Permission::Allow) {
            const std::scoped_lock lock{state->mutex};
            state->denials.push_back(
                Denial{.tool = std::string{request.tool}, .target = std::string{request.target}});
        }
        return decision;
    };
}

std::vector<ToolUse> TurnRecorder::take_tools() const {
    const std::scoped_lock lock{state_->mutex};
    return std::exchange(state_->tools, {});
}

std::vector<Denial> TurnRecorder::take_denials() const {
    const std::scoped_lock lock{state_->mutex};
    return std::exchange(state_->denials, {});
}

UnansweredQuestion::UnansweredQuestion(std::string question)
    : std::runtime_error{"the model asked a question and no one is present to answer it: " +
                         question},
      question_{std::move(question)} {}

std::string question_text(const agentloop::QuestionRequest& request) {
    std::string out;
    for (const agentloop::Question& question : request.questions) {
        out += out.empty() ? "" : " / ";
        out += question.question.empty() ? question.header : question.question;
    }
    return out;
}

agentloop::AskFn fail_on_question() {
    return [](const agentloop::QuestionRequest& request) -> agentloop::Answers {
        throw UnansweredQuestion{question_text(request)};
    };
}

}  // namespace apogee::tasks
