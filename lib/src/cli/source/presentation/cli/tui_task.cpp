#include "cli/tui_task.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <mutex>
#include <ostream>
#include <streambuf>
#include <string>
#include <utility>
#include <vector>

#include "cli/task_cmd.h"
#include "cli/tui_common.h"
#include "contracts/layout.h"
#include "tasks/ledger.h"
#include "tasks/task.h"
#include "tasks/view.h"

namespace apogee::commands {

namespace {

/// What the last read drew, by id: what Enter, `h`, `c` and `r` ask of a row.
struct LastTasks {
    std::mutex mutex;
    std::map<std::string, tasks::Task> by_id;
};

/// The run in the widget: its id, once its first event names it.
struct LiveRun {
    std::mutex mutex;
    std::string id;
};

/// A stream that hands each line written to it to `line`: a run's events,
/// one JSON object per line, as `JsonReporter` writes them.
class LineStream final : public std::streambuf {
public:
    explicit LineStream(std::function<void(const std::string&)> line) : line_{std::move(line)} {}

    ~LineStream() override {
        if (!pending_.empty()) {
            line_(pending_);
        }
    }

    LineStream(const LineStream&) = delete;
    LineStream& operator=(const LineStream&) = delete;
    LineStream(LineStream&&) = delete;
    LineStream& operator=(LineStream&&) = delete;

protected:
    int_type overflow(int_type ch) override {
        if (traits_type::eq_int_type(ch, traits_type::eof())) {
            return traits_type::not_eof(ch);
        }
        put(traits_type::to_char_type(ch));
        return ch;
    }

    std::streamsize xsputn(const char* text, std::streamsize count) override {
        for (std::streamsize i = 0; i < count; ++i) {
            put(text[i]);
        }
        return count;
    }

private:
    void put(char c) {
        if (c != '\n') {
            pending_.push_back(c);
            return;
        }
        line_(std::exchange(pending_, std::string{}));
    }

    std::function<void(const std::string&)> line_;
    std::string pending_;
};

[[nodiscard]] std::string first_line(const std::string& text, std::size_t limit = 120) {
    std::string line = text.substr(0, text.find('\n'));
    if (line.size() > limit) {
        line = line.substr(0, limit) + "…";
    }
    return line;
}

/// `<event> · <status>[ · <detail>]`: a transition in its own words.
[[nodiscard]] std::string transition_line(const nlohmann::json& event) {
    const nlohmann::json transition = event.value("transition", nlohmann::json::object());
    std::string line = field(transition, "event");
    if (transition.contains("round") && transition["round"].is_number() &&
        transition["round"].get<int>() > 0) {
        line += " · round " + field(transition, "round");
    }
    line += " · " + field(transition, "status");
    if (const std::string detail = field(transition, "detail"); !detail.empty()) {
        line += " · " + first_line(detail);
    }
    return line;
}

/// Who may resume, stop or run what: the read's facts, kept by id.
[[nodiscard]] const tasks::Task* task_of(const LastTasks& last, const std::string& id) {
    const auto found = last.by_id.find(id);
    return found == last.by_id.end() ? nullptr : &found->second;
}

[[nodiscard]] tui::ListRow::Look look_of(const std::string& status) {
    if (status == tasks::kDone) {
        return tui::ListRow::Look::Plain;
    }
    if (status == tasks::kPlanning || status == tasks::kRunning) {
        return tui::ListRow::Look::Active;
    }
    return tui::ListRow::Look::Dim;
}

/// Starts `request` in the widget: its events worded, line by line, and
/// cancelled through `task cancel`'s core on the task it runs.
[[nodiscard]] bool start_run(tui::Progress& progress, const RootContext& context,
                             const MachineBudgetSource& machine, const TaskRunRequest& request,
                             std::string heading) {
    const auto live = std::make_shared<LiveRun>();
    {
        const std::lock_guard lock{live->mutex};
        live->id = request.resume;
    }
    return progress.start(
        std::move(heading),
        [&context, machine, request, live](const tui::Progress::Say& say) {
            bool told = false;
            int code = 0;
            {
                LineStream lines{[&say, &told, live](const std::string& line) {
                    nlohmann::json event;
                    try {
                        event = nlohmann::json::parse(line);
                    } catch (const nlohmann::json::exception&) {
                        return;
                    }
                    if (const std::string id = field(event, "task_id"); !id.empty()) {
                        const std::lock_guard lock{live->mutex};
                        live->id = id;
                    }
                    for (std::string& worded : task_event_lines(event)) {
                        told = true;
                        say(std::move(worded));
                    }
                }};
                std::ostream events{&lines};
                code = run_task_events(context, machine, request, events);
            }
            if (!told) {
                say("not run (exit " + std::to_string(code) +
                    ") -- the reason is on the notice row");
            }
        },
        [live]() {
            std::string id;
            {
                const std::lock_guard lock{live->mutex};
                id = live->id;
            }
            try {
                (void)request_task_stop(id, tasks::Request::Cancel);
            } catch (const std::exception&) {
                // Not running yet, or already over: nothing to stop.
            }
        });
}

}  // namespace

std::vector<std::string> task_event_lines(const nlohmann::json& event) {
    const std::string type = field(event, "type");
    if (type == "task_started" || type == "task_plan") {
        std::vector<std::string> lines{transition_line(event) + "  " + field(event, "task_id")};
        if (const std::string plan = field(event, "plan"); !plan.empty()) {
            for (const std::string& line : lines_of(plan)) {
                lines.push_back("  " + line);
            }
        }
        return lines;
    }
    if (type == "task_round") {
        std::string line = transition_line(event);
        if (event.contains("checks") && event["checks"].is_array()) {
            int passed = 0;
            for (const nlohmann::json& check : event["checks"]) {
                passed += check.value("passed", false) ? 1 : 0;
            }
            line += " · checks " + std::to_string(passed) + "/" +
                    std::to_string(event["checks"].size()) + " passed";
        }
        return {line};
    }
    if (type == "task_grant") {
        const std::string target = field(event, "target");
        return {"grant · round " + field(event, "round") + " · " + field(event, "tool") +
                (target.empty() ? std::string{} : " on " + target) + " · by " + field(event, "by")};
    }
    if (type == "task_finished") {
        return {"finished · " + field(event, "status") + " -- " + field(event, "reason")};
    }
    if (type == "tool_status") {
        return {"  " + first_line(field(event, "text"))};
    }
    if (type == "result") {
        return {"  answer: " + first_line(field(event, "text"))};
    }
    if (type == "error") {
        return {"  error: " + first_line(field(event, "message"))};
    }
    return {};
}

tui::ListOptions task_view_options(const RootContext& context,
                                   std::shared_ptr<tui::Progress> progress,
                                   MachineBudgetSource machine) {
    const auto last = std::make_shared<LastTasks>();
    tui::ListOptions options;
    options.title = "Tasks";
    options.columns = {"ID", "STATUS", "ROUNDS", "GOAL"};
    options.progress = progress;
    options.load = [last]() {
        // `task list --all`'s own read, its rows as its document states them.
        const std::filesystem::path root = harness::tasks_dir();
        std::vector<std::string> problems;
        std::vector<tasks::Task> all = tasks::list_tasks(root, &problems);
        const nlohmann::json document = tasks::to_json(tasks::make_task_list(all, true));
        std::vector<tui::ListRow> rows;
        for (const nlohmann::json& summary : document["data"]) {
            const std::string status = field(summary, "status");
            rows.push_back(tui::ListRow{
                .key = field(summary, "id"),
                .cells = {field(summary, "id"), status,
                          field(summary, "rounds_used") + "/" + field(summary, "rounds_budget"),
                          first_line(field(summary, "goal"), 80)},
                .look = look_of(status)});
        }
        std::vector<std::string> heading;
        for (const std::string& problem : problems) {
            heading.push_back("[task] skipped: " + problem);
        }
        if (rows.empty()) {
            heading.emplace_back("no tasks yet -- r runs one with the goal you type");
        }
        {
            const std::lock_guard lock{last->mutex};
            last->by_id.clear();
            for (tasks::Task& task : all) {
                std::string id = task.id;
                last->by_id.emplace(std::move(id), std::move(task));
            }
        }
        return std::pair{std::move(heading), std::move(rows)};
    };
    options.enter_label = "status";
    options.detail = [](const tui::ListRow& row) { return lines_of(task_status_text(row.key)); };

    // `r`: a new task, its goal typed, asked first with what it runs under.
    options.ask_key = "r";
    options.ask_label = "run";
    options.ask_here = true;
    options.ask_confirm = [](const tui::ListRow& /*row*/, const std::string& goal) {
        return "Run a task with tools: \"" + goal +
               "\"? grants: none -- with nobody present, a tool that asks is denied; on "
               "question: fail -- a question nobody answers ends the task";
    };
    options.ask = [&context, progress, machine](const tui::ListRow& /*row*/,
                                                const std::string& goal) {
        if (!start_run(*progress, context, machine, TaskRunRequest{.goal = goal},
                       "task run: " + first_line(goal, 60))) {
            return std::string{"not now: a run is going -- Ctrl-C stops it"};
        }
        return std::string{"running the task -- its narration is below"};
    };

    const auto stoppable = [last](tasks::Request request) {
        return [last, request](const tui::ListRow& row) {
            const std::lock_guard lock{last->mutex};
            const tasks::Task* task = task_of(*last, row.key);
            return task != nullptr && task_stoppable(*task, request);
        };
    };
    options.actions = {
        // `r` on a row `task resume` takes: that task, resumed.
        tui::ListAction{
            .key = "r",
            .label = "resume",
            .applies =
                [last](const tui::ListRow& row) {
                    const std::lock_guard lock{last->mutex};
                    const tasks::Task* task = task_of(*last, row.key);
                    std::error_code missing;
                    return task != nullptr &&
                           task_resume_refusal(*task, std::filesystem::current_path(missing))
                               .empty();
                },
            .confirm =
                [](const tui::ListRow& row) {
                    return "Resume task " + row.key +
                           "? It runs as 'apogee task resume' does, with nobody asked anything";
                },
            .run =
                [&context, progress, machine](const tui::ListRow& row) {
                    if (!start_run(*progress, context, machine, TaskRunRequest{.resume = row.key},
                                   "task resume: " + row.key)) {
                        return std::string{"not now: a run is going -- Ctrl-C stops it"};
                    }
                    return "resuming task " + row.key + " -- its narration is below";
                }},
        tui::ListAction{.key = "h",
                        .label = "halt",
                        .applies = stoppable(tasks::Request::Halt),
                        .confirm =
                            [](const tui::ListRow& row) {
                                return "Halt task " + row.key + " when its round ends?";
                            },
                        .run =
                            [](const tui::ListRow& row) {
                                return request_task_stop(row.key, tasks::Request::Halt);
                            }},
        tui::ListAction{
            .key = "c",
            .label = "cancel",
            .applies = stoppable(tasks::Request::Cancel),
            .confirm = [](const tui::ListRow& row) { return "Cancel task " + row.key + " now?"; },
            .run =
                [](const tui::ListRow& row) {
                    return request_task_stop(row.key, tasks::Request::Cancel);
                }}};
    return options;
}

}  // namespace apogee::commands
