#include "tasks/view.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <utility>

namespace apogee::tasks {
namespace {

/// The outcome as the view words it: the ledger's, or `in_flight` while a
/// round has none.
[[nodiscard]] std::string outcome_word(const Round& round) {
    return round.outcome.empty() ? std::string{"in_flight"} : round.outcome;
}

[[nodiscard]] nlohmann::json uses_json(const std::vector<UseView>& uses) {
    nlohmann::json out = nlohmann::json::array();
    for (const UseView& use : uses) {
        out.push_back({{"tool", use.tool}, {"target", use.target}, {"by", use.by}});
    }
    return out;
}

}  // namespace

TurnView make_turn_view(const Round& round) {
    TurnView view;
    view.round = round.index;
    view.kind = round.kind;
    view.outcome = outcome_word(round);
    view.adopted = round.adopted;
    view.checks_passed = static_cast<int>(std::ranges::count_if(
        round.checks, [](const CheckResult& result) { return result.passed; }));
    view.self_report = round.self_report;
    view.tools = static_cast<int>(round.tools.size());
    for (const Permit& permit : round.allowed) {
        view.allowed.push_back(
            UseView{.tool = permit.tool, .target = permit.target, .by = permit.by});
    }
    for (const Denial& denial : round.denied) {
        view.denied.push_back(
            UseView{.tool = denial.tool, .target = denial.target, .by = denial.by});
    }
    for (const Answered& question : round.answered) {
        // The question and who answered it -- the answer stays in the ledger.
        view.answered.push_back(QuestionView{.question = question.question, .by = question.by});
    }
    view.tokens = round.tokens;
    view.tokens_estimated = round.tokens_estimated;
    view.started_at = round.started_at;
    view.ended_at = round.ended_at;
    return view;
}

std::vector<CheckView> make_check_views(const Task& task) {
    const Round* last = last_completed_round(task);
    std::vector<CheckView> out;
    out.reserve(task.checks.size());
    for (std::size_t index = 0; index < task.checks.size(); ++index) {
        const Check& check = task.checks[index];
        CheckView view{.kind = std::string{to_string(check.kind)},
                       .value = check.value,
                       .description = describe(check)};
        if (last != nullptr && index < last->checks.size()) {
            view.ran = true;
            view.passed = last->checks[index].passed;
            view.detail = last->checks[index].detail;
        }
        out.push_back(std::move(view));
    }
    return out;
}

TaskView make_task_view(const Task& task, const std::optional<LockHolder>& holder) {
    TaskView view;
    view.id = task.id;
    view.status = task.status;
    if (task.status == kPlanning || task.status == kRunning) {
        if (holder.has_value() && holder->running && holder->task_id == task.id) {
            view.process = holder->pid;
        } else {
            view.interrupted = true;
        }
    }
    view.goal = task.goal;
    view.conversation = task.session_id;
    view.folder = task.working_directory;
    view.tools = task.tools;
    if (task.tools) {
        view.policy = PolicyView{.agent = task.policy.agent,
                                 .grants = task.policy.grants,
                                 .on_question = std::string{to_string(task.policy.on_question)}};
    }
    view.rounds_used = rounds_used(task);
    view.rounds_budget = task.rounds_budget;
    view.created_at = task.created_at;
    view.updated_at = task.updated_at;
    view.reason = task.reason;
    view.checks = make_check_views(task);
    const Round* last = last_completed_round(task);
    view.self_report = std::string{
        to_string(self_report_from_string(last != nullptr ? last->self_report : std::string{}))};
    view.plan = task.plan;
    for (const Round& round : task.rounds) {
        view.turns.push_back(make_turn_view(round));
    }
    return view;
}

TaskSummary make_task_summary(const Task& task) {
    return TaskSummary{.id = task.id,
                       .status = task.status,
                       .rounds_used = rounds_used(task),
                       .rounds_budget = task.rounds_budget,
                       .goal = task.goal};
}

nlohmann::json to_json(const CheckView& view) {
    return nlohmann::json{
        {"kind", view.kind}, {"value", view.value},   {"description", view.description},
        {"ran", view.ran},   {"passed", view.passed}, {"detail", view.detail}};
}

nlohmann::json to_json(const TurnView& view) {
    nlohmann::json answered = nlohmann::json::array();
    for (const QuestionView& question : view.answered) {
        answered.push_back({{"question", question.question}, {"by", question.by}});
    }
    return nlohmann::json{{"round", view.round},
                          {"kind", view.kind},
                          {"outcome", view.outcome},
                          {"adopted", view.adopted},
                          {"checks_passed", view.checks_passed},
                          {"self_report", view.self_report},
                          {"tools", view.tools},
                          {"allowed", uses_json(view.allowed)},
                          {"denied", uses_json(view.denied)},
                          {"answered", std::move(answered)},
                          {"tokens", view.tokens},
                          {"tokens_estimated", view.tokens_estimated},
                          {"started_at", view.started_at},
                          {"ended_at", view.ended_at}};
}

nlohmann::json to_json(const TaskView& view) {
    nlohmann::json checks = nlohmann::json::array();
    for (const CheckView& check : view.checks) {
        checks.push_back(to_json(check));
    }
    nlohmann::json turns = nlohmann::json::array();
    for (const TurnView& turn : view.turns) {
        turns.push_back(to_json(turn));
    }
    nlohmann::json policy = nullptr;
    if (view.policy.has_value()) {
        policy = nlohmann::json{{"agent", view.policy->agent},
                                {"grants", view.policy->grants},
                                {"on_question", view.policy->on_question}};
    }
    return nlohmann::json{
        {"id", view.id},
        {"status", view.status},
        {"process", view.process.has_value() ? nlohmann::json(*view.process) : nlohmann::json()},
        {"interrupted", view.interrupted},
        {"goal", view.goal},
        {"conversation", view.conversation},
        {"folder", view.folder},
        {"tools", view.tools},
        {"policy", std::move(policy)},
        {"rounds_used", view.rounds_used},
        {"rounds_budget", view.rounds_budget},
        {"created_at", view.created_at},
        {"updated_at", view.updated_at},
        {"reason", view.reason},
        {"checks", std::move(checks)},
        {"self_report", view.self_report},
        {"plan", view.plan},
        {"turns", std::move(turns)}};
}

nlohmann::json to_json(const TaskSummary& summary) {
    return nlohmann::json{{"id", summary.id},
                          {"status", summary.status},
                          {"rounds_used", summary.rounds_used},
                          {"rounds_budget", summary.rounds_budget},
                          {"goal", summary.goal}};
}

TaskListView make_task_list(const std::vector<Task>& tasks, bool all) {
    TaskListView list;
    list.total = tasks.size();
    const std::size_t shown = all ? tasks.size() : std::min(tasks.size(), kListLimit);
    list.shown.reserve(shown);
    for (std::size_t index = 0; index < shown; ++index) {
        list.shown.push_back(make_task_summary(tasks[index]));
    }
    return list;
}

nlohmann::json to_json(const TaskListView& list) {
    nlohmann::json data = nlohmann::json::array();
    for (const TaskSummary& summary : list.shown) {
        data.push_back(to_json(summary));
    }
    return nlohmann::json{{"object", "list"}, {"data", std::move(data)}, {"total", list.total}};
}

}  // namespace apogee::tasks
