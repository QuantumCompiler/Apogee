#include "tasks/task.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <ranges>
#include <stdexcept>
#include <system_error>
#include <utility>

#include "contracts/sha256.h"

namespace apogee::tasks {
namespace {

constexpr std::array<std::pair<CheckKind, std::string_view>, 2> kCheckKinds{{
    {CheckKind::Require, "require"},
    {CheckKind::RequireFile, "require_file"},
}};

constexpr std::array<std::pair<SelfReport, std::string_view>, 3> kReports{{
    {SelfReport::Unreported, "unreported"},
    {SelfReport::Done, "done"},
    {SelfReport::NotDone, "not_done"},
}};

[[nodiscard]] bool is_word_char(char c) noexcept {
    return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

/// `line` without what decorates it -- Markdown's `**`, backticks, a quote's
/// `>`, a bullet, a trailing full stop -- at either end.
[[nodiscard]] std::string_view undecorated(std::string_view line) noexcept {
    while (!line.empty() && !is_word_char(line.front())) {
        line.remove_prefix(1);
    }
    while (!line.empty() && !is_word_char(line.back())) {
        line.remove_suffix(1);
    }
    return line;
}

[[nodiscard]] std::string upper(std::string_view text) {
    std::string out{text};
    std::ranges::transform(out, out.begin(), [](char c) {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    });
    return out;
}

/// `text` with every run of spaces and tabs made one space.
[[nodiscard]] std::string squeezed(std::string_view text) {
    std::string out;
    bool space = false;
    for (const char c : text) {
        if (c == ' ' || c == '\t') {
            space = !out.empty();
            continue;
        }
        if (space) {
            out += ' ';
            space = false;
        }
        out += c;
    }
    return out;
}

[[nodiscard]] bool passed_before(const Task& task, const Round& round, std::size_t check) {
    return std::ranges::any_of(task.rounds, [&](const Round& earlier) {
        return &earlier != &round && earlier.kind != kPlanRound && earlier.index < round.index &&
               earlier.outcome == kCompleted && check < earlier.checks.size() &&
               earlier.checks[check].passed;
    });
}

[[nodiscard]] bool ran_before(const Task& task, const Round& round, const ToolUse& use) {
    return std::ranges::any_of(task.rounds, [&](const Round& earlier) {
        return &earlier != &round && earlier.index < round.index &&
               std::ranges::any_of(
                   earlier.tools,
                   [&](const ToolUse& seen) { return seen.fingerprint == use.fingerprint; });
    });
}

[[nodiscard]] std::string quoted_plan(const Task& task) {
    if (task.plan.size() <= kPlanQuoteLimit) {
        return task.plan;
    }
    std::size_t end = kPlanQuoteLimit;
    while (end > 0 && (static_cast<unsigned char>(task.plan[end]) & 0xC0U) == 0x80U) {
        --end;  // never through a codepoint
    }
    return task.plan.substr(0, end) + "\n[... the plan continues; it is cut here]";
}

[[nodiscard]] std::string status_instruction() {
    return "End your answer with one line, exactly `" + std::string{kStatusMarker} +
           " DONE` if the goal is reached, or `" + std::string{kStatusMarker} +
           " NOT DONE` if work remains.";
}

[[nodiscard]] std::string checks_list(const Task& task) {
    std::string out;
    for (const Check& check : task.checks) {
        out += "- " + describe(check) + "\n";
    }
    out += "- you report the task done\n";
    return out;
}

[[nodiscard]] std::string round_header(const Task& task, int index) {
    return "Round " + std::to_string(index) + " of " + std::to_string(task.rounds_budget);
}

/// The completed round numbered `index`, other than the plan, or null.
[[nodiscard]] const Round* completed_round(const Task& task, int index) {
    for (const Round& round : task.rounds) {
        if (round.kind != kPlanRound && round.index == index && round.outcome == kCompleted) {
            return &round;
        }
    }
    return nullptr;
}

[[nodiscard]] nlohmann::json round_to_json(const Round& round) {
    nlohmann::json checks = nlohmann::json::array();
    for (const CheckResult& result : round.checks) {
        checks.push_back({{"passed", result.passed}, {"detail", result.detail}});
    }
    nlohmann::json tools = nlohmann::json::array();
    for (const ToolUse& use : round.tools) {
        tools.push_back({{"tool", use.tool}, {"fingerprint", use.fingerprint}});
    }
    nlohmann::json denied = nlohmann::json::array();
    for (const Denial& denial : round.denied) {
        denied.push_back({{"tool", denial.tool}, {"target", denial.target}});
    }
    nlohmann::json out{{"index", round.index},
                       {"kind", round.kind},
                       {"started_at", round.started_at},
                       {"ended_at", round.ended_at},
                       {"session_turns_before", round.session_turns_before},
                       {"outcome", round.outcome},
                       {"self_report", round.self_report},
                       {"checks", std::move(checks)},
                       {"tools", std::move(tools)},
                       {"denied", std::move(denied)},
                       {"tokens", round.tokens},
                       {"tokens_estimated", round.tokens_estimated},
                       {"progress", round.progress}};
    if (round.adopted) {
        out["adopted"] = true;
    }
    return out;
}

[[nodiscard]] Round round_from_json(const nlohmann::json& json) {
    if (!json.is_object()) {
        throw std::runtime_error("task ledger: a round must be an object");
    }
    Round round;
    round.index = json.value("index", 0);
    round.kind = json.value("kind", std::string{});
    round.started_at = json.value("started_at", std::string{});
    round.ended_at = json.value("ended_at", std::string{});
    round.session_turns_before = json.value("session_turns_before", 0);
    round.outcome = json.value("outcome", std::string{});
    round.adopted = json.value("adopted", false);
    round.self_report = json.value("self_report", std::string{});
    round.tokens = json.value("tokens", std::int64_t{0});
    round.tokens_estimated = json.value("tokens_estimated", false);
    round.progress = json.value("progress", false);
    for (const nlohmann::json& result : json.value("checks", nlohmann::json::array())) {
        round.checks.push_back(CheckResult{.passed = result.value("passed", false),
                                           .detail = result.value("detail", std::string{})});
    }
    for (const nlohmann::json& use : json.value("tools", nlohmann::json::array())) {
        round.tools.push_back(ToolUse{.tool = use.value("tool", std::string{}),
                                      .fingerprint = use.value("fingerprint", std::string{})});
    }
    for (const nlohmann::json& denial : json.value("denied", nlohmann::json::array())) {
        round.denied.push_back(Denial{.tool = denial.value("tool", std::string{}),
                                      .target = denial.value("target", std::string{})});
    }
    return round;
}

}  // namespace

std::string_view to_string(CheckKind kind) noexcept {
    for (const auto& [candidate, name] : kCheckKinds) {
        if (candidate == kind) {
            return name;
        }
    }
    return "require";
}

std::optional<CheckKind> check_kind_from_string(std::string_view text) noexcept {
    for (const auto& [kind, name] : kCheckKinds) {
        if (name == text) {
            return kind;
        }
    }
    return std::nullopt;
}

std::string describe(const Check& check) {
    switch (check.kind) {
        case CheckKind::Require:
            return "the answer contains \"" + check.value + "\"";
        case CheckKind::RequireFile:
            return "the file " + check.value + " exists and is not empty";
    }
    return check.value;
}

CheckResult evaluate(const Check& check, std::string_view answer) {
    switch (check.kind) {
        case CheckKind::Require:
            if (!check.value.empty() && answer.find(check.value) != std::string_view::npos) {
                return CheckResult{.passed = true, .detail = "found in the answer"};
            }
            return CheckResult{.passed = false, .detail = "not in the answer"};
        case CheckKind::RequireFile: {
            std::error_code code;
            const std::filesystem::path path{check.value};
            const std::filesystem::file_status status = std::filesystem::status(path, code);
            if (code || !std::filesystem::exists(status)) {
                return CheckResult{.passed = false, .detail = "no such file"};
            }
            if (!std::filesystem::is_regular_file(status)) {
                return CheckResult{.passed = false, .detail = "not a regular file"};
            }
            const std::uintmax_t size = std::filesystem::file_size(path, code);
            if (code) {
                return CheckResult{.passed = false, .detail = "could not be read"};
            }
            if (size == 0) {
                return CheckResult{.passed = false, .detail = "empty"};
            }
            return CheckResult{.passed = true, .detail = std::to_string(size) + " bytes"};
        }
    }
    return CheckResult{};
}

std::string_view to_string(SelfReport report) noexcept {
    for (const auto& [candidate, name] : kReports) {
        if (candidate == report) {
            return name;
        }
    }
    return "unreported";
}

SelfReport self_report_from_string(std::string_view text) noexcept {
    for (const auto& [report, name] : kReports) {
        if (name == text) {
            return report;
        }
    }
    return SelfReport::Unreported;
}

std::string_view report_words(SelfReport report) noexcept {
    switch (report) {
        case SelfReport::Done:
            return "reported done";
        case SelfReport::NotDone:
            return "reported not done";
        case SelfReport::Unreported:
            break;
    }
    return "no status reported";
}

SelfReport parse_self_report(std::string_view answer) {
    const std::string marker = upper(kStatusMarker);
    SelfReport report = SelfReport::Unreported;
    std::size_t start = 0;
    while (start <= answer.size()) {
        const std::size_t newline = answer.find('\n', start);
        const std::size_t end = newline == std::string_view::npos ? answer.size() : newline;
        const std::string line = upper(undecorated(answer.substr(start, end - start)));
        if (line.starts_with(marker)) {
            const std::string said =
                squeezed(undecorated(std::string_view{line}.substr(marker.size())));
            if (said == "DONE") {
                report = SelfReport::Done;
            } else if (said == "NOT DONE") {
                report = SelfReport::NotDone;
            }
        }
        if (newline == std::string_view::npos) {
            break;
        }
        start = newline + 1;
    }
    return report;
}

std::string fingerprint(std::string_view tool, std::string_view arguments) {
    std::string bytes{tool};
    bytes += '\0';
    bytes += arguments;
    return models::sha256_hex(bytes).substr(0, 16);
}

bool finished(const Task& task) noexcept {
    return task.status == kDone || task.status == kExhausted;
}

bool live(const Task& task) noexcept {
    return task.status == kPlanning || task.status == kRunning || task.status == kHalted;
}

int rounds_used(const Task& task) noexcept {
    return static_cast<int>(std::ranges::count_if(task.rounds, [](const Round& round) {
        return round.kind != kPlanRound && round.outcome == kCompleted;
    }));
}

const Round* last_completed_round(const Task& task) noexcept {
    for (const Round& round : std::views::reverse(task.rounds)) {
        if (round.kind != kPlanRound && round.outcome == kCompleted) {
            return &round;
        }
    }
    return nullptr;
}

std::vector<std::string> unpassed_checks(const Task& task) {
    std::vector<std::string> out;
    const Round* last = last_completed_round(task);
    for (std::size_t index = 0; index < task.checks.size(); ++index) {
        if (last == nullptr || index >= last->checks.size() || !last->checks[index].passed) {
            out.push_back(describe(task.checks[index]));
        }
    }
    return out;
}

void record_transition(Task& task, std::string_view event, std::string at, int round,
                       std::string detail) {
    task.updated_at = at;
    task.transitions.push_back(Transition{.at = std::move(at),
                                          .event = std::string{event},
                                          .status = task.status,
                                          .round = round,
                                          .detail = std::move(detail)});
}

std::string plan_message(const Task& task) {
    std::string out = "Task: " + task.goal + "\n\n";
    out += "This is the first turn of a task you will carry out on your own, over up to " +
           std::to_string(task.rounds_budget) +
           " rounds, with no one present. Each round is one turn, and when it ends the task is "
           "checked:\n";
    out += checks_list(task);
    out +=
        "\nPlan first: reply with a short, numbered plan for reaching the goal, and nothing "
        "else. Do not carry it out yet.\n\n";
    out += "No one will answer questions while the task runs: asking one ends the task.";
    if (task.tools) {
        out +=
            " A tool that changes something runs only where the configuration allows it; "
            "otherwise it is refused, and the refusal is its result.";
    }
    return out;
}

std::string round_message(const Task& task, int index) {
    std::string out;
    const Round* before = index > 1 ? completed_round(task, index - 1) : nullptr;
    if (before == nullptr) {
        out = round_header(task, index) + ": carry out your plan now.\n\n";
    } else {
        out = round_header(task, index) + ". The task is not done -- after round " +
              std::to_string(index - 1) + ":\n";
        for (std::size_t check = 0; check < task.checks.size(); ++check) {
            const bool passed = check < before->checks.size() && before->checks[check].passed;
            const std::string detail =
                check < before->checks.size() ? before->checks[check].detail : std::string{};
            out += std::string{"- "} + (passed ? "passed: " : "FAILED: ") +
                   describe(task.checks[check]) + (detail.empty() ? "" : " -- " + detail) + "\n";
        }
        switch (self_report_from_string(before->self_report)) {
            case SelfReport::Done:
                out += "- passed: you report the task done\n";
                break;
            case SelfReport::NotDone:
                out += "- FAILED: you report the task done -- you reported it not done\n";
                break;
            case SelfReport::Unreported:
                out += "- FAILED: you report the task done -- your answer did not end with `" +
                       std::string{kStatusMarker} + " DONE`\n";
                break;
        }
        out += "\nFix what failed and continue the task.\n\n";
    }
    out += "Your plan:\n" + quoted_plan(task) + "\n\n";
    if (before == nullptr) {
        out += "When this round ends the task is checked:\n" + checks_list(task) + "\n";
    }
    out += status_instruction();
    return out;
}

std::string shortfall(const Task& task) {
    std::string out;
    const auto add = [&out](const std::string& item) {
        out += out.empty() ? "" : "; ";
        out += item;
    };
    const Round* last = last_completed_round(task);
    for (std::size_t index = 0; index < task.checks.size(); ++index) {
        if (last != nullptr && index < last->checks.size() && last->checks[index].passed) {
            continue;
        }
        const std::string detail = last != nullptr && index < last->checks.size()
                                       ? " (" + last->checks[index].detail + ")"
                                       : std::string{};
        add("not passed: " + describe(task.checks[index]) + detail);
    }
    if (last == nullptr || self_report_from_string(last->self_report) != SelfReport::Done) {
        add("not reported done by the model");
    }
    return out;
}

void conclude_round(Task& task, std::string_view answer) {
    if (task.rounds.empty()) {
        return;
    }
    Round& round = task.rounds.back();
    round.outcome = std::string{kCompleted};
    const SelfReport report = parse_self_report(answer);
    round.self_report = std::string{to_string(report)};
    round.checks.clear();
    for (const Check& check : task.checks) {
        round.checks.push_back(evaluate(check, answer));
    }

    bool newly_passing = false;
    for (std::size_t index = 0; index < round.checks.size(); ++index) {
        newly_passing =
            newly_passing || (round.checks[index].passed && !passed_before(task, round, index));
    }
    const bool new_tool = std::ranges::any_of(
        round.tools, [&](const ToolUse& use) { return !ran_before(task, round, use); });
    round.progress = newly_passing || new_tool;

    const bool all_passed =
        std::ranges::all_of(round.checks, [](const CheckResult& result) { return result.passed; });
    if (all_passed && report == SelfReport::Done) {
        task.status = std::string{kDone};
        task.reason = "every check passed and the model reported the task done, in " +
                      std::to_string(rounds_used(task)) + " of " +
                      std::to_string(task.rounds_budget) + " rounds";
        return;
    }
    task.no_progress = round.progress ? 0 : task.no_progress + 1;
    if (rounds_used(task) >= task.rounds_budget) {
        task.status = std::string{kExhausted};
        task.reason = "the round budget (" + std::to_string(task.rounds_budget) +
                      ") is spent and the task is not done -- " + shortfall(task);
        return;
    }
    if (task.no_progress >= kBreakerRounds) {
        task.status = std::string{kStalled};
        task.reason = "no progress in " + std::to_string(task.no_progress) +
                      " rounds -- no check newly passed and no new tool call ran -- " +
                      shortfall(task);
        return;
    }
    task.status = std::string{kRunning};
    task.reason.clear();
}

nlohmann::json task_to_json(const Task& task) {
    nlohmann::json checks = nlohmann::json::array();
    for (const Check& check : task.checks) {
        checks.push_back({{"kind", to_string(check.kind)}, {"value", check.value}});
    }
    nlohmann::json rounds = nlohmann::json::array();
    for (const Round& round : task.rounds) {
        rounds.push_back(round_to_json(round));
    }
    nlohmann::json transitions = nlohmann::json::array();
    for (const Transition& transition : task.transitions) {
        nlohmann::json row{
            {"at", transition.at}, {"event", transition.event}, {"status", transition.status}};
        if (transition.round > 0) {
            row["round"] = transition.round;
        }
        if (!transition.detail.empty()) {
            row["detail"] = transition.detail;
        }
        transitions.push_back(std::move(row));
    }
    return nlohmann::json{{"schema_version", task.schema_version},
                          {"id", task.id},
                          {"goal", task.goal},
                          {"checks", std::move(checks)},
                          {"rounds_budget", task.rounds_budget},
                          {"working_directory", task.working_directory},
                          {"session_id", task.session_id},
                          {"tools", task.tools},
                          {"status", task.status},
                          {"reason", task.reason},
                          {"plan", task.plan},
                          {"rounds", std::move(rounds)},
                          {"no_progress", task.no_progress},
                          {"created_at", task.created_at},
                          {"updated_at", task.updated_at},
                          {"transitions", std::move(transitions)}};
}

Task task_from_json(const nlohmann::json& json) {
    if (!json.is_object()) {
        throw std::runtime_error("task ledger: expected an object");
    }
    Task task;
    task.schema_version = json.value("schema_version", kLedgerSchemaVersion);
    task.id = json.value("id", std::string{});
    if (task.id.empty()) {
        throw std::runtime_error("task ledger: no id");
    }
    task.goal = json.value("goal", std::string{});
    task.rounds_budget = std::clamp(json.value("rounds_budget", kDefaultRounds), 1, kMaxRounds);
    task.working_directory = json.value("working_directory", std::string{});
    task.session_id = json.value("session_id", std::string{});
    task.tools = json.value("tools", false);
    task.status = json.value("status", std::string{kPlanning});
    task.reason = json.value("reason", std::string{});
    task.plan = json.value("plan", std::string{});
    task.no_progress = json.value("no_progress", 0);
    task.created_at = json.value("created_at", std::string{});
    task.updated_at = json.value("updated_at", std::string{});
    for (const nlohmann::json& check : json.value("checks", nlohmann::json::array())) {
        const std::optional<CheckKind> kind =
            check_kind_from_string(check.value("kind", std::string{}));
        if (!kind.has_value()) {
            throw std::runtime_error("task ledger: a check of an unknown kind");
        }
        task.checks.push_back(Check{.kind = *kind, .value = check.value("value", std::string{})});
    }
    for (const nlohmann::json& round : json.value("rounds", nlohmann::json::array())) {
        task.rounds.push_back(round_from_json(round));
    }
    for (const nlohmann::json& row : json.value("transitions", nlohmann::json::array())) {
        if (!row.is_object()) {
            throw std::runtime_error("task ledger: a transition must be an object");
        }
        task.transitions.push_back(Transition{.at = row.value("at", std::string{}),
                                              .event = row.value("event", std::string{}),
                                              .status = row.value("status", std::string{}),
                                              .round = row.value("round", 0),
                                              .detail = row.value("detail", std::string{})});
    }
    return task;
}

}  // namespace apogee::tasks
