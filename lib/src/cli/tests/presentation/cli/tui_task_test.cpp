#include "cli/tui_task.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "cli/command.h"
#include "support/cli_home.h"
#include "tasks/ledger.h"
#include "tasks/task.h"
#include "tui/list_view.h"
#include "tui/progress.h"
#include "tui/pump.h"
#include "tui/shell.h"

/// The shell's Task view (37e) held to the task commands: its rows `task list
/// --all --output-format json`'s, its card `task status`'s; a run started
/// from it leaving the ledger `apogee task run` leaves for the same scripted
/// turns -- an asking tool denied, nobody prompted -- and narrated in the
/// task events' words; halt and cancel asked first and leaving the commands'
/// own ledger states; Ctrl-C reaching a live run as `task cancel` does.
namespace {

namespace fs = std::filesystem;
using apogee::tui::Key;

[[nodiscard]] bool has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

[[nodiscard]] std::string slurp(const fs::path& path) {
    std::ifstream in{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// A home whose one backend answers from `turns`, recall off, the tools'
/// root in `work()` -- as `task_test` builds one.
struct Home {
    explicit Home(const std::string& turns) : home{"{}\n"} {
        script = home.home() / "script.json";
        std::ofstream{script} << R"({"turns": [)" << turns << "]}";
        fs::create_directories(home.home() / "work");
        std::ofstream{home.config_path(), std::ios::trunc}
            << "backends:\n  m:\n    type: mock\n    model_path: " << script.string()
            << "\nmodels:\n  default: m\nmemory:\n  recall: false\ntools:\n  fs_root: "
            << (home.home() / "work").string() << "\n";
        context.config_path = home.config_path().string();
    }

    [[nodiscard]] fs::path tasks() const {
        return home.home() / "tasks";
    }

    [[nodiscard]] apogee::tasks::Task only_task() const {
        const std::vector<apogee::tasks::Task> all = apogee::tasks::list_tasks(tasks());
        REQUIRE(all.size() == 1);
        return all.front();
    }

    apogee::testing::CliHome home;
    fs::path script;
    apogee::commands::RootContext context;
};

/// The Task view on a shell over a manual pump.
struct Stage {
    explicit Stage(const apogee::commands::RootContext& context)
        : progress{std::make_shared<apogee::tui::Progress>(pump)},
          view{pump, apogee::tui::Theme{.color = false},
               apogee::commands::task_view_options(
                   context, progress, []() { return apogee::models::MachineBudget{}; })} {
        shell.add(view.view());
        shell.activate(0);
    }

    [[nodiscard]] std::string frame() {
        for (int i = 0; i < 3; ++i) {
            view.settle();
            (void)pump.drain();
        }
        return shell.render_text(160, 50);
    }

    void press(const Key& key) {
        (void)shell.press(key);
        (void)frame();
    }

    void type(const std::string& text) {
        for (const char c : text) {
            press(Key::character(std::string(1, c)));
        }
    }

    template <typename Done>
    void until(Done done, const char* what) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
        while (!done()) {
            INFO(what << "\n" << shell.render_text(160, 50));
            REQUIRE(std::chrono::steady_clock::now() < deadline);
            (void)frame();
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
    }

    apogee::tui::ManualPump pump;
    std::shared_ptr<apogee::tui::Progress> progress;
    apogee::tui::Shell shell{{.title = "apogee test", .theme = {.color = false}}};
    apogee::tui::ListView view;
};

/// A ledger with what differs between two runs of the same turns -- the ids,
/// the clock -- written alike.
[[nodiscard]] nlohmann::json normalized(const apogee::tasks::Task& task) {
    nlohmann::json json = apogee::tasks::task_to_json(task);
    const auto walk = [&task](auto& self, nlohmann::json& node) -> void {
        if (node.is_object()) {
            for (auto& [key, value] : node.items()) {
                if (key == "at" || key.ends_with("_at")) {
                    value = "T";
                } else {
                    self(self, value);
                }
            }
        } else if (node.is_array()) {
            for (nlohmann::json& item : node) {
                self(self, item);
            }
        } else if (node.is_string()) {
            std::string value = node.get<std::string>();
            for (const std::string& id : {task.id, task.session_id}) {
                for (std::size_t at = value.find(id); !id.empty() && at != std::string::npos;
                     at = value.find(id, at + 2)) {
                    value.replace(at, id.size(), "ID");
                }
            }
            node = value;
        }
    };
    walk(walk, json);
    return json;
}

/// Plan, a write the config asks about -- denied, nobody present -- and an
/// answer reporting the task done.
constexpr const char* kDeniedWrite = R"({"text": "1. Write it."},
    {"text": "", "tool_calls": [{"name": "write_file",
      "arguments": {"path": "tui-task-out.txt", "content": "hi"}}]},
    {"text": "Refused: {{last_tool_result}}\nTASK STATUS: DONE"})";

}  // namespace

TEST_CASE("the Task view draws task list --all's document, and its card is task status's",
          "[cli][tui][task]") {
    const Home home{R"({"text": "1. Answer."}, {"text": "It is 42.\nTASK STATUS: DONE"})"};
    {
        Stage stage{home.context};
        CHECK(has(stage.frame(), "no tasks yet -- r runs one with the goal you type"));
        CHECK(has(stage.frame(), "r run"));
    }
    std::string out;
    std::string err;
    REQUIRE(home.home.run({"task", "run", "Find the answer\nsecond line", "--require", "42"}, &out,
                          &err) == 0);
    REQUIRE(home.home.run({"task", "list", "--all", "--output-format", "json"}, &out, &err) == 0);
    const nlohmann::json document = nlohmann::json::parse(out);
    const apogee::tui::ListOptions options = apogee::commands::task_view_options(
        home.context, nullptr, []() { return apogee::models::MachineBudget{}; });
    const auto [heading, rows] = options.load();
    REQUIRE(rows.size() == document["data"].size());
    REQUIRE(rows.size() == 1);
    const nlohmann::json& summary = document["data"].at(0);
    CHECK(rows.front().cells == std::vector<std::string>{summary["id"], summary["status"],
                                                         summary["rounds_used"].dump() + "/" +
                                                             summary["rounds_budget"].dump(),
                                                         "Find the answer"});
    REQUIRE(home.home.run({"task", "status", rows.front().key}, &out, &err) == 0);
    std::string card;
    for (const std::string& line : options.detail(rows.front())) {
        card += line + "\n";
    }
    CHECK(card == out);
}

TEST_CASE("a run from the Task view leaves task run's ledger, an asking tool denied, narrated",
          "[cli][tui][task]") {
    // The twin: `apogee task run --tools`, no one at the terminal to ask.
    nlohmann::json expected;
    {
        const Home twin{kDeniedWrite};
        std::string out;
        std::string err;
        REQUIRE(twin.home.run({"task", "run", "Write a file", "--tools"}, &out, &err) == 0);
        expected = normalized(twin.only_task());
    }

    const Home home{kDeniedWrite};
    Stage stage{home.context};
    stage.press(Key::character("r"));
    CHECK(stage.view.view().takes_text());
    stage.type("Write a file");
    stage.press(Key::named(Key::Name::Return));
    // Asked first, naming the goal and what it runs under; a no starts nothing.
    CHECK(has(stage.frame(),
              "Run a task with tools: \"Write a file\"? grants: none -- with nobody "
              "present, a tool that asks is denied"));
    stage.press(Key::character("n"));
    CHECK(has(stage.frame(), "not done"));
    CHECK(apogee::tasks::list_tasks(home.tasks()).empty());

    stage.press(Key::character("r"));
    stage.press(Key::named(Key::Name::Return));  // the last goal, kept
    stage.press(Key::character("y"));
    CHECK(has(stage.frame(), "running the task -- its narration is below"));
    stage.until([&stage]() { return !stage.progress->running(); }, "the run ending");
    stage.view.refresh();
    const std::string drawn = stage.frame();
    // Narrated in the task events' own words.
    CHECK(has(drawn, " task run: Write a file -- ended"));
    CHECK(has(drawn, "started · planning"));
    CHECK(has(drawn, "plan_recorded · running"));
    CHECK(has(drawn, "round_started · round 1 · running"));
    CHECK(has(drawn, "finished · done -- "));
    CHECK(has(drawn, "  answer: Refused: Error: the user denied permission to run 'write_file'"));

    // The ledger `apogee task run` leaves for the same turns.
    const apogee::tasks::Task task = home.only_task();
    CHECK(task.status == apogee::tasks::kDone);
    REQUIRE(task.rounds.size() == 2);
    REQUIRE(task.rounds.back().denied.size() == 1);
    CHECK(task.rounds.back().denied.front().by == apogee::tasks::kByNobody);
    CHECK(normalized(task) == expected);
    CHECK_FALSE(fs::exists(home.home.home() / "work" / "tui-task-out.txt"));
    CHECK(has(drawn, task.id + "  done"));
}

TEST_CASE("halt and cancel from the Task view ask first and leave the commands' ledger states",
          "[cli][tui][task]") {
    // A task no process runs -- killed mid-round -- in two homes alike.
    const auto seed = [](const Home& home) {
        apogee::tasks::Task task;
        task.id = "task-20261010-120000";
        task.goal = "a goal";
        task.status = std::string{apogee::tasks::kRunning};
        task.session_id = "20261010-120000-abcd";
        task.created_at = "2026-10-10T12:00:00Z";
        task.updated_at = "2026-10-10T12:00:00Z";
        task.rounds_budget = 8;
        task.working_directory = fs::current_path().string();
        REQUIRE(apogee::tasks::save_task(home.tasks(), task).empty());
    };
    for (const std::string verb : {"halt", "cancel"}) {
        INFO(verb);
        const Home twin{R"({"text": "x"})"};
        seed(twin);
        std::string out;
        std::string err;
        REQUIRE(twin.home.run({"task", verb, "task-20261010-120000"}, &out, &err) == 0);

        const Home home{R"({"text": "x"})"};
        seed(home);
        const std::string before = slurp(home.tasks() / "task-20261010-120000" / "task.json");
        Stage stage{home.context};
        CHECK(has(stage.frame(), "h halt"));
        CHECK(has(stage.frame(), "c cancel"));
        const std::string key = verb == "halt" ? "h" : "c";
        stage.press(Key::character(key));
        CHECK(has(stage.frame(), verb == "halt"
                                     ? "Halt task task-20261010-120000 when its round ends? [y/N]"
                                     : "Cancel task task-20261010-120000 now? [y/N]"));
        stage.press(Key::character("n"));
        CHECK(slurp(home.tasks() / "task-20261010-120000" / "task.json") == before);
        stage.press(Key::character(key));
        stage.press(Key::character("y"));
        // The command's own line, and its ledger state.
        CHECK(has(stage.frame(), out.substr(0, out.find('\n'))));
        CHECK(normalized(home.only_task()) == normalized(twin.only_task()));
    }
}

TEST_CASE("Ctrl-C stops a live run from the Task view as task cancel does", "[cli][tui][task]") {
    // A round that streams for seconds: there is time to stop it.
    const Home home{R"({"text": "1. Take a while."},
        {"text": "This answer streams slowly enough to be cancelled while it does.",
         "delay_ms": 300})"};
    Stage stage{home.context};
    stage.press(Key::character("r"));
    stage.type("Take a while");
    stage.press(Key::named(Key::Name::Return));
    stage.press(Key::character("y"));
    stage.until([&stage]() { return has(stage.frame(), "round_started · round 1"); },
                "the round running");
    stage.press(Key::named(Key::Name::CtrlC));
    CHECK(has(stage.frame(), "asked the run to stop"));
    CHECK_FALSE(stage.shell.quit_requested());
    stage.until([&stage]() { return !stage.progress->running(); }, "the run ending");
    CHECK(has(stage.frame(), "finished · cancelled"));
    CHECK(home.only_task().status == apogee::tasks::kCancelled);
}
