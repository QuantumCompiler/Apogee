#include "tasks/policy.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace apogee::tasks {
namespace {

constexpr std::array<std::string_view, 2> kOnQuestionForms{"fail", "answer:"};
constexpr std::string_view kFail = kOnQuestionForms[0];
constexpr std::string_view kAnswerPrefix = kOnQuestionForms[1];

/// The tools in `registry` the gate asks about per tool -- those that write
/// -- joined for a refusal.
[[nodiscard]] std::string askable(const agent::ToolRegistry& registry) {
    std::string out;
    for (const std::string& name : registry.names()) {
        if (const agent::Tool* tool = registry.find(name); tool != nullptr && tool->writes) {
            out += out.empty() ? "" : ", ";
            out += name;
        }
    }
    return out;
}

/// Why the one grant `name` exceeds the scope, or empty.
[[nodiscard]] std::string refuse_one(const std::string& name, const GrantScope& scope) {
    const std::string flag = "--allow " + name + ": ";
    const agent::Tool* tool = scope.available->find(name);
    if (tool == nullptr) {
        const std::string listed = askable(*scope.offered);
        return flag + "no tool named '" + name + "' in this task -- " +
               (listed.empty() ? std::string{"none of its tools asks before it runs"}
                               : "the ones that ask before they run: " + listed);
    }
    if (!tool->writes) {
        if (tool->outbound) {
            return flag + name +
                   " is asked about per website, never per tool -- with nobody present a task "
                   "reaches only the hosts tools.allowed_hosts lists";
        }
        return flag + name +
               " never asks before it runs, so there is nothing to grant -- a grant is for a "
               "tool that writes";
    }
    if (scope.offered->find(name) == nullptr) {
        return flag +
               (scope.agent.empty() ? name + " is not among this task's tools"
                                    : "the agent '" + scope.agent + "' runs with tools: " +
                                          scope.agent_tools + ", which leaves out " + name) +
               " -- a task's grant is never wider than its agent's policy";
    }
    if (scope.permissions != nullptr &&
        scope.permissions->level(name) == harness::PermissionLevel::Deny) {
        return flag + "the config says permissions." + name +
               ": deny -- a task's grant is never wider than the config";
    }
    return {};
}

}  // namespace

std::string_view to_string(OnQuestion policy) noexcept {
    return policy == OnQuestion::Answer ? "answer" : "fail";
}

bool AutonomyPolicy::grants_tool(std::string_view tool) const noexcept {
    return std::ranges::find(grants, tool) != grants.end();
}

std::span<const std::string_view> on_question_forms() noexcept {
    return kOnQuestionForms;
}

std::string parse_on_question(std::string_view text, AutonomyPolicy& policy) {
    if (text == kFail) {
        policy.on_question = OnQuestion::Fail;
        policy.answer.clear();
        return {};
    }
    if (!text.starts_with(kAnswerPrefix)) {
        return "--on-question: '" + std::string{text} +
               "' is neither fail nor answer:<text> -- answer:\"blue\" serves that one answer "
               "to every question the task asks";
    }
    std::string_view answer = text.substr(kAnswerPrefix.size());
    if (answer.size() >= 2 && (answer.front() == '"' || answer.front() == '\'') &&
        answer.back() == answer.front()) {
        answer = answer.substr(1, answer.size() - 2);
    }
    if (answer.find_first_not_of(" \t\r\n") == std::string_view::npos) {
        return "--on-question answer: needs the answer to give, as answer:\"blue\"";
    }
    policy.on_question = OnQuestion::Answer;
    policy.answer = std::string{answer};
    return {};
}

std::vector<std::string> normalized_grants(std::vector<std::string> grants) {
    std::ranges::sort(grants);
    const auto [first, last] = std::ranges::unique(grants);
    grants.erase(first, last);
    return grants;
}

std::string grant_refusal(const std::vector<std::string>& grants, const GrantScope& scope) {
    if (grants.empty()) {
        return {};
    }
    if (scope.available == nullptr || scope.offered == nullptr) {
        // Nothing to hold a grant to is nothing it may name.
        return "--allow " + grants.front() + ": this task has no tools (--tools)";
    }
    for (const std::string& name : grants) {
        if (std::string refused = refuse_one(name, scope); !refused.empty()) {
            return refused;
        }
    }
    return {};
}

nlohmann::json policy_to_json(const AutonomyPolicy& policy) {
    nlohmann::json out{{"on_question", to_string(policy.on_question)}, {"grants", policy.grants}};
    if (policy.on_question == OnQuestion::Answer) {
        out["answer"] = policy.answer;
    }
    if (!policy.agent.empty()) {
        out["agent"] = policy.agent;
    }
    return out;
}

AutonomyPolicy policy_from_json(const nlohmann::json& json) {
    AutonomyPolicy policy;
    if (json.is_null()) {
        return policy;
    }
    if (!json.is_object()) {
        throw std::runtime_error("task ledger: the policy must be an object");
    }
    const std::string on_question = json.value("on_question", std::string{"fail"});
    if (on_question == "answer") {
        policy.on_question = OnQuestion::Answer;
        policy.answer = json.value("answer", std::string{});
    } else if (on_question != "fail") {
        throw std::runtime_error("task ledger: an unknown question policy '" + on_question + "'");
    }
    const nlohmann::json grants = json.value("grants", nlohmann::json::array());
    if (!grants.is_array()) {
        throw std::runtime_error("task ledger: the grants must be a list");
    }
    for (const nlohmann::json& grant : grants) {
        if (!grant.is_string()) {
            throw std::runtime_error("task ledger: a grant must name a tool");
        }
        policy.grants.push_back(grant.get<std::string>());
    }
    policy.grants = normalized_grants(std::move(policy.grants));
    policy.agent = json.value("agent", std::string{});
    return policy;
}

}  // namespace apogee::tasks
