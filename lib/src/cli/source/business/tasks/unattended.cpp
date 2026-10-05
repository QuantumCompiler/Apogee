#include "tasks/unattended.h"

#include <algorithm>
#include <mutex>
#include <string_view>
#include <utility>

namespace apogee::tasks {

namespace {

/// One question as a record names it: its text, else its header.
[[nodiscard]] std::string question_of(const agentloop::Question& question) {
    return question.question.empty() ? question.header : question.question;
}

}  // namespace

struct TurnRecorder::State {
    std::mutex mutex;
    std::vector<ToolUse> tools;
    std::vector<Denial> denials;
    std::vector<Permit> allowed;
    std::vector<Answered> answered;

    void deny(const agent::GateRequest& request, std::string_view by) {
        const std::scoped_lock lock{mutex};
        denials.push_back(Denial{.tool = std::string{request.tool},
                                 .target = std::string{request.target},
                                 .by = std::string{by}});
    }

    void allow(const agent::GateRequest& request, std::string_view by) {
        const std::scoped_lock lock{mutex};
        allowed.push_back(Permit{.tool = std::string{request.tool},
                                 .target = std::string{request.target},
                                 .by = std::string{by}});
    }

    void answer(const agentloop::QuestionRequest& request, const agentloop::Answers& answers,
                std::string_view by) {
        const std::scoped_lock lock{mutex};
        for (std::size_t index = 0; index < request.questions.size(); ++index) {
            answered.push_back(Answered{
                .question = question_of(request.questions[index]),
                .answer = index < answers.values.size() ? answers.values[index] : std::string{},
                .by = std::string{by}});
        }
    }
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

WatchedGate TurnRecorder::watch(agent::PermissionChecker permission,
                                agent::PermissionChecker standing, std::vector<std::string> grants,
                                agent::ConfirmFn confirm) const {
    WatchedGate out;
    const bool attended = static_cast<bool>(confirm);
    out.permission = [state = state_, permission = std::move(permission),
                      standing = std::move(standing), grants = std::move(grants),
                      attended](const agent::GateRequest& request) {
        const agent::Permission decision =
            permission ? permission(request) : agent::Permission::Ask;
        switch (decision) {
            case agent::Permission::Allow: {
                // The config's own allow first: a grant of a tool the config
                // already allows is not what let it run.
                std::string_view by = kByPerson;
                if (standing && standing(request) == agent::Permission::Allow) {
                    by = kByConfig;
                } else if (!request.outbound &&
                           std::ranges::find(grants, request.tool) != grants.end()) {
                    by = kByGrant;
                }
                state->allow(request, by);
                break;
            }
            case agent::Permission::Deny:
                state->deny(request, kByConfig);
                break;
            case agent::Permission::Ask:
                // With someone present the prompt decides, and is recorded
                // there; with nobody, the gate resolves `ask` to deny.
                if (!attended) {
                    state->deny(request, kByNobody);
                }
                break;
        }
        return decision;
    };
    if (attended) {
        out.confirm = [state = state_,
                       confirm = std::move(confirm)](const agent::GateRequest& request) {
            const bool allowed = confirm(request);
            if (allowed) {
                state->allow(request, kByPerson);
            } else {
                state->deny(request, kByPerson);
            }
            return allowed;
        };
    }
    return out;
}

agentloop::AskFn TurnRecorder::declared_answer(std::string answer) const {
    return [state = state_, answer = std::move(answer)](const agentloop::QuestionRequest& request) {
        agentloop::Answers answers;
        answers.values.assign(request.questions.size(), answer);
        state->answer(request, answers, kByDeclared);
        return answers;
    };
}

agentloop::AskFn TurnRecorder::person(agentloop::AskFn ask) const {
    if (!ask) {
        return {};
    }
    return [state = state_, ask = std::move(ask)](const agentloop::QuestionRequest& request) {
        agentloop::Answers answers = ask(request);
        state->answer(request, answers, kByPerson);
        return answers;
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

std::vector<Permit> TurnRecorder::take_allowed() const {
    const std::scoped_lock lock{state_->mutex};
    return std::exchange(state_->allowed, {});
}

std::vector<Answered> TurnRecorder::take_answered() const {
    const std::scoped_lock lock{state_->mutex};
    return std::exchange(state_->answered, {});
}

UnansweredQuestion::UnansweredQuestion(std::string question)
    : std::runtime_error{"the model asked a question and no one is present to answer it: " +
                         question},
      question_{std::move(question)} {}

std::string question_text(const agentloop::QuestionRequest& request) {
    std::string out;
    for (const agentloop::Question& question : request.questions) {
        out += out.empty() ? "" : " / ";
        out += question_of(question);
    }
    return out;
}

agentloop::AskFn fail_on_question() {
    return [](const agentloop::QuestionRequest& request) -> agentloop::Answers {
        throw UnansweredQuestion{question_text(request)};
    };
}

}  // namespace apogee::tasks
