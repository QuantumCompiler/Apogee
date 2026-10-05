#include "cli/task_cmd.h"

#include <CLI/CLI.hpp>

#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "agentloop/member_call.h"
#include "agentloop/tool_selection.h"
#include "ansi/ansi.h"
#include "backends/factory.h"
#include "cli/chat_history.h"
#include "cli/chat_recall.h"
#include "cli/chat_turn.h"
#include "cli/helpers.h"
#include "cli/interrupt.h"
#include "cli/permissions.h"
#include "cli/suite_residency.h"
#include "contracts/config.h"
#include "contracts/errors.h"
#include "contracts/layout.h"
#include "contracts/paths.h"
#include "harness/harness.h"
#include "harness/roles.h"
#include "logger/operational.h"
#include "logger/session.h"
#include "modelstore/footprint.h"
#include "operations/suites.h"
#include "platform/platform.h"
#include "tasks/ledger.h"
#include "tasks/runner.h"
#include "tasks/task.h"
#include "tasks/unattended.h"
#include "tools/consult.h"
#include "views/cli_reporter.h"
#include "views/terminal.h"

namespace apogee::commands {
namespace {

namespace fs = std::filesystem;

[[noreturn]] void fail_user(const std::string& message) {
    std::cerr << "apogee task: " << message << "\n";
    throw CLI::RuntimeError(kUserError);
}

/// `task list` shows this many unless `--all` (27j's confirmed default,
/// taken here where the list is first built).
constexpr std::size_t kListLimit = 50;

struct TaskFlags {
    std::string goal;
    std::vector<std::string> require;
    std::vector<std::string> require_files;
    int rounds = tasks::kDefaultRounds;
    std::string model;
    std::string suite;
    bool tools = false;
    bool no_recall = false;
    bool force = false;
    bool verbose = false;
    bool no_color = false;
    bool raw = false;
    /// The task `status`, `resume`, `halt` and `cancel` name; empty for the
    /// running or the newest one.
    std::string id;
    bool all = false;
};

/// The folder as a comparison sees it: its real path when it has one.
[[nodiscard]] fs::path real_folder(const fs::path& folder) {
    std::error_code code;
    const fs::path real = fs::weakly_canonical(folder, code);
    return code ? folder.lexically_normal() : real;
}

[[nodiscard]] fs::path working_folder() {
    std::error_code code;
    const fs::path here = fs::current_path(code);
    if (code) {
        fail_user("the working folder cannot be read: " + code.message());
    }
    return real_folder(here);
}

/// The exit code a task's end means: done is success; a cancel is a
/// cancel; a provider that failed is the backend's error; anything else
/// short of done is a failure a scheduler must see -- never a silent zero.
[[nodiscard]] int exit_code_for(const tasks::Task& task, bool provider_failed) {
    if (task.status == tasks::kDone) {
        return kSuccess;
    }
    if (task.status == tasks::kCancelled) {
        return kCancelled;
    }
    if (task.status == tasks::kFailed && provider_failed) {
        return kBackendError;
    }
    return kUserError;
}

[[nodiscard]] std::string clip(std::string_view text, std::size_t limit) {
    if (text.size() <= limit) {
        return std::string{text};
    }
    std::size_t end = limit;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0U) == 0x80U) {
        --end;
    }
    return std::string{text.substr(0, end)} + "…";
}

/// The goal on one line, for a listing.
[[nodiscard]] std::string one_line(std::string_view text, std::size_t limit) {
    std::string flat;
    for (const char c : text) {
        flat += c == '\n' || c == '\r' || c == '\t' ? ' ' : c;
    }
    return clip(flat, limit);
}

/// The status as a person reads it: a task whose process died mid-turn says
/// so, and one running now says where.
[[nodiscard]] std::string status_words(const tasks::Task& task, const fs::path& root) {
    if (task.status != tasks::kPlanning && task.status != tasks::kRunning) {
        return task.status;
    }
    if (const std::optional<tasks::LockHolder> holder = tasks::lock_holder(root);
        holder.has_value() && holder->running && holder->task_id == task.id) {
        return task.status + " (now, process " + std::to_string(holder->pid) + ")";
    }
    return task.status + " (interrupted -- its process is gone; 'apogee task resume " + task.id +
           "' continues it)";
}

[[nodiscard]] std::string round_row(const tasks::Task& task, const tasks::Round& round) {
    std::ostringstream out;
    out << "  "
        << (round.kind == tasks::kPlanRound ? std::string{"plan"} : std::to_string(round.index))
        << "  " << round.kind << "  "
        << (round.outcome.empty() ? std::string{"in flight"} : round.outcome);
    if (round.outcome == tasks::kCompleted && round.kind != tasks::kPlanRound) {
        int passed = 0;
        for (const tasks::CheckResult& result : round.checks) {
            passed += result.passed ? 1 : 0;
        }
        out << "  " << passed << "/" << task.checks.size() << " checks, "
            << tasks::report_words(tasks::self_report_from_string(round.self_report));
    }
    if (round.adopted) {
        out << "  (taken from the conversation after a restart)";
    }
    out << "  tools: " << round.tools.size() << " ran";
    if (!round.denied.empty()) {
        out << ", " << round.denied.size() << " denied";
    }
    out << "  tokens: " << round.tokens << (round.tokens_estimated ? " (estimated)" : "");
    return out.str();
}

void print_status(const tasks::Task& task, const fs::path& root) {
    std::cout << "task " << task.id << "  " << status_words(task, root) << "\n"
              << "goal:          " << task.goal << "\n"
              << "conversation:  " << task.session_id << "\n"
              << "folder:        " << task.working_directory << "\n"
              << "tools:         " << (task.tools ? "on" : "off") << "\n"
              << "rounds:        " << tasks::rounds_used(task) << " of " << task.rounds_budget
              << " used\n"
              << "started:       " << task.created_at << "\n"
              << "updated:       " << task.updated_at << "\n";
    if (!task.reason.empty()) {
        std::cout << "reason:        " << task.reason << "\n";
    }
    // Each check's state as the newest completed round left it.
    const tasks::Round* last = tasks::last_completed_round(task);
    std::cout << "checks:\n";
    for (std::size_t index = 0; index < task.checks.size(); ++index) {
        const bool ran = last != nullptr && index < last->checks.size();
        const bool passed = ran && last->checks[index].passed;
        std::cout << "  [" << (passed ? "x" : " ") << "] " << tasks::describe(task.checks[index])
                  << (ran ? " -- " + last->checks[index].detail : std::string{" -- not run yet"})
                  << "\n";
    }
    const bool reported = last != nullptr && last->self_report == "done";
    std::cout << "  [" << (reported ? "x" : " ") << "] the model reports the task done\n";
    std::cout << "plan:" << (task.plan.empty() ? " (not recorded yet)\n" : "\n");
    if (!task.plan.empty()) {
        std::istringstream lines{task.plan};
        std::string line;
        while (std::getline(lines, line)) {
            std::cout << "  " << line << "\n";
        }
    }
    if (!task.rounds.empty()) {
        std::cout << "turns:\n";
        for (const tasks::Round& round : task.rounds) {
            std::cout << round_row(task, round) << "\n";
        }
    }
    bool any_denied = false;
    for (const tasks::Round& round : task.rounds) {
        for (const tasks::Denial& denial : round.denied) {
            if (!any_denied) {
                std::cout << "denied:\n";
                any_denied = true;
            }
            std::cout << "  "
                      << (round.kind == tasks::kPlanRound ? std::string{"plan"}
                                                          : "round " + std::to_string(round.index))
                      << ": " << denial.tool
                      << (denial.target.empty() ? std::string{} : " on " + denial.target) << "\n";
        }
    }
}

/// The task a command names, or the running one, or the newest one.
[[nodiscard]] tasks::Task resolve_task(const fs::path& root, const std::string& id,
                                       bool prefer_running) {
    std::string chosen = id;
    if (chosen.empty() && prefer_running) {
        chosen = tasks::running_task(root).value_or(std::string{});
    }
    if (chosen.empty()) {
        const std::vector<tasks::Task> all = tasks::list_tasks(root);
        if (all.empty()) {
            fail_user("no tasks yet -- 'apogee task run \"<goal>\"' starts one");
        }
        return all.front();
    }
    std::string error;
    std::optional<tasks::Task> task = tasks::load_task(root, chosen, error);
    if (!task.has_value()) {
        fail_user(error);
    }
    return std::move(*task);
}

/// What a run or a resume needs to know, beyond the flags.
struct Drive {
    std::optional<tasks::Task> existing;
};

/// `task run` and `task resume`: the session assembled as `chat` assembles
/// one, with nobody to ask, and the runner driven over its turns.
void drive(const RootContext& context, const MachineBudgetSource& machine, const TaskFlags& flags,
           const Drive& what) {
    const bool decorate = platform::is_terminal(platform::StandardStream::Out);
    const fs::path root = harness::tasks_dir();

    harness::Config config;
    fs::path config_path;
    try {
        config_path = harness::resolve_config_path(context.config_path);
        config = harness::load_config(config_path);
    } catch (const harness::ConfigError& e) {
        fail_user(e.what());
    }

    // --- the task: a new one, or the one named -----------------------------
    const fs::path here = working_folder();
    std::string task_id;
    if (what.existing.has_value()) {
        task_id = what.existing->id;
    } else {
        if (flags.goal.empty()) {
            fail_user("a task needs a goal: apogee task run \"<goal>\"");
        }
        for (const std::string& text : flags.require) {
            if (text.empty()) {
                fail_user("--require needs the text the answer must contain");
            }
        }
        for (const std::string& path : flags.require_files) {
            if (path.empty()) {
                fail_user("--require-file needs a path");
            }
        }
        if (!flags.suite.empty()) {
            harness::Config probe = config;
            if (const std::string refused = select_suite(probe, flags.suite); !refused.empty()) {
                fail_user("--suite: " + refused);
            }
        }
        task_id = tasks::new_task_id(root);
    }

    // One task at a time: the lock first, so nothing else writes the ledger
    // this run is about to read.
    std::string lock_error;
    std::optional<tasks::TaskLock> lock = tasks::TaskLock::acquire(root, task_id, lock_error);
    if (!lock.has_value()) {
        fail_user(lock_error);
    }

    tasks::Task task;
    logger::Session session;
    logger::KnownDependencies known;
    known.backends = config.backend_names();
    known.suites = config.suite_names();
    known.check_suites = true;
    if (what.existing.has_value()) {
        std::string error;
        std::optional<tasks::Task> loaded = tasks::load_task(root, task_id, error);
        if (!loaded.has_value()) {
            fail_user(error);
        }
        task = std::move(*loaded);
        if (const std::string refused = tasks::resume_refusal(task); !refused.empty()) {
            fail_user(refused);
        }
        // A task's tools work where it was started; resumed anywhere else,
        // its files would be written in one place and checked in another.
        if (real_folder(task.working_directory) != here) {
            fail_user("task " + task.id + " was started in " + task.working_directory +
                      " and resumes only there -- cd there first");
        }
        try {
            logger::LoadedSession loaded_session = logger::load(task.session_id, known);
            session = std::move(loaded_session.session);
            for (const logger::ResumeWarning& warning : loaded_session.warnings) {
                std::cerr << "[resume] " << warning.message << "\n";
                if (warning.kind == logger::WarningKind::BackendMissing) {
                    session.backend.clear();
                }
            }
        } catch (const std::exception& e) {
            fail_user("task " + task.id + "'s conversation " + task.session_id +
                      " cannot be read (" + e.what() + ") -- the task cannot resume");
        }
    } else {
        task.id = task_id;
        task.goal = flags.goal;
        for (const std::string& text : flags.require) {
            task.checks.push_back(tasks::Check{.kind = tasks::CheckKind::Require, .value = text});
        }
        for (const std::string& path : flags.require_files) {
            // Absolute where the task starts, so a check reads exactly the
            // file the status names.
            task.checks.push_back(
                tasks::Check{.kind = tasks::CheckKind::RequireFile,
                             .value = (here / fs::path{path}).lexically_normal().string()});
        }
        task.rounds_budget = flags.rounds;
        task.working_directory = here.string();
        task.tools = flags.tools;
        session.chat_id = logger::new_chat_id();
        task.session_id = session.chat_id;
        session.task = task.id;
        session.title = sanitize_title("task: " + flags.goal);
    }

    // --- the session's providers, as `chat` builds them ------------------------
    harness::Harness harness{config};
    backends::BuildOptions build_options;
    build_options.config_path = config_path;
    const backends::BuildResult built = backends::build_providers(harness, build_options);
    if (built.constructed_count() == 0) {
        std::string message = "no usable backend is configured";
        if (!built.skipped_summary().empty()) {
            message += " -- " + built.skipped_summary();
        }
        fail_user(message);
    }

    TerminalWriter status_writer{std::cerr};
    CliReporter::Options reporter_options;
    reporter_options.answer_stream = &std::cout;
    reporter_options.decorate = decorate;
    reporter_options.verbosity = flags.verbose ? ansi::Verbosity::Verbose : ansi::Verbosity::Line;
    reporter_options.style =
        ansi::Style::detect(flags.no_color ? ansi::ColorMode::Never : ansi::ColorMode::Auto);
    reporter_options.width = static_cast<std::size_t>(platform::terminal_width().value_or(80));
    reporter_options.markdown = !flags.raw && config.ui.markdown;
    reporter_options.hyperlinks = ansi::hyperlinks_supported();
    CliReporter reporter{status_writer, reporter_options};
    const ansi::Style& style = reporter_options.style;
    harness.listen_for_loads(
        [&reporter](std::string_view backend, const harness::StatusEvent& event) {
            reporter.on_model_load(backend, event);
        });

    // The suite: the flag on a new task, the session's on a resumed one, else
    // the config's default -- as `chat` chooses it.
    std::optional<std::string> chosen_suite;
    if (!what.existing.has_value() && !flags.suite.empty()) {
        harness::Config probe = config;
        (void)select_suite(probe, flags.suite);  // validated above
        chosen_suite = probe.models.default_suite;
    } else if (session.suite.has_value() && session.suite->empty()) {
        chosen_suite = std::string{};
    } else if (session.suite.has_value()) {
        if (harness::Config probe = config; select_suite(probe, *session.suite).empty()) {
            chosen_suite = probe.models.default_suite;
        }
    }
    if (chosen_suite.has_value() && *chosen_suite != config.models.default_suite) {
        for (const std::string& line :
             activate_suite(harness, config, *chosen_suite, build_options)) {
            reporter.status().print_line(style.tag(ansi::Role::Warning) + " " + line);
        }
    }
    if (const std::string refused = validate_active_suite(config); !refused.empty()) {
        fail_user(refused);
    }
    std::optional<models::SuiteFootprint> footprint;
    if (!config.models.default_suite.empty()) {
        footprint = price_suite(config, config.models.default_suite, machine);
        if (const std::string refused = admission_refusal(*footprint, "--force");
            !refused.empty() && !flags.force) {
            fail_user(refused);
        }
    }
    const bool forced =
        footprint.has_value() && footprint->admission() == models::Admission::OverBudget;
    const harness::SessionHold hold{harness};
    if (!config.models.default_suite.empty()) {
        session.suite = config.models.default_suite;
    } else if (chosen_suite.has_value()) {
        session.suite = std::string{};
    }
    if (!what.existing.has_value() && !flags.suite.empty() && flags.model.empty()) {
        if (const harness::Resolution chat = harness::resolve_backend(
                config, harness::RoleRequest{.role = harness::ModelRole::Chat});
            chat.from == harness::ResolvedFrom::Suite) {
            session.backend = chat.key;
        }
    }
    session.backend = harness::resolve_backend_key(
        config, harness::RoleRequest{
                    .role = harness::ModelRole::Chat,
                    .override = what.existing.has_value() ? std::string_view{} : flags.model,
                    .entry_backend = session.backend});
    harness.resume_conversation(session.backend, session.chat_id);
    int saved_compactions = session.compactions;

    if (!what.existing.has_value()) {
        // The session exists from the start, naming its task: a resume that
        // later finds it gone knows it was deleted, not never made.
        logger::save(session);
        task.created_at = tasks::now_timestamp();
        tasks::record_transition(task, tasks::kCreatedEvent, task.created_at, 0, task.goal);
        if (const std::string failure = tasks::save_task(root, task); !failure.empty()) {
            fail_user("the task's ledger could not be written: " + failure);
        }
    }
    logger::log(
        logger::Level::Info, "task",
        "task " + task.id + " on backend " + session.backend + ", session " + session.chat_id);

    ChatRecall recall{harness, config, session, config.memory.recall && !flags.no_recall};
    for (const std::string& line : recall.catch_up({})) {
        reporter.status().print_line(style.dim("[memory] " + line));
    }

    reporter.status().print_line(
        "[task] " + task.id + (what.existing.has_value() ? " resumed" : "") + "  ·  " +
        session.backend + banner_suite(config.models.default_suite, forced) + "  ·  chat " +
        session.chat_id);
    if (footprint.has_value()) {
        reporter.status().print_line(style.tag(ansi::Role::Apogee) + " " +
                                     admission_line(*footprint, forced));
    }

    // --- the tools, watched, behind a gate nobody can answer -------------------
    const tasks::TurnRecorder recorder;
    agent::ToolRegistry registry;
    agent::ToolRegistry observed;
    std::unique_ptr<agentloop::ToolSelection> selection;
    const auto mcp_registry = std::make_shared<mcp::Registry>();
    const auto member_calls = std::make_shared<agentloop::MemberCalls>(harness);
    if (task.tools) {
        registry = pin_toolset(
            make_built_in_tools(BuiltInToolOptions{
                .config = &config,
                .harness = &harness,
                .mcp = mcp_registry,
                .mcp_status = mcp_status_line(reporter.status()),
                .mcp_server_log = flags.verbose ? mcp::StderrTail::Sink{[](std::string_view bytes) {
                    std::cerr << bytes << std::flush;
                }}
                                                : mcp::StderrTail::Sink{}}),
            config, session.backend);
        // The consult tool when the suite designates members (27f).
        for (const std::string& note :
             tools::register_consult_tool(registry, harness, member_calls).notes) {
            reporter.status().print_line(style.tag(ansi::Role::Warning) + " " + note);
        }
        observed = recorder.observe(registry);
        std::string ranked_by;
        selection = make_tool_selection(harness, config, observed, config_path, ranked_by);
        if (is_base_model(harness, session.backend)) {
            reporter.status().print_line(style.tag(ansi::Role::Warning) + " " +
                                         base_model_tools_note(session.backend));
        }
    }
    // Deny-by-default survives autonomy: the config's levels, nothing
    // remembered, and no confirm function -- so `ask` denies, as on any pipe,
    // each refusal recorded. Widening it is 27i's, never this runner's.
    const ToolGate gate{.permission = recorder.gate(make_permission_checker(config, nullptr)),
                        .confirm = agent::ConfirmFn{}};
    // `ask_user` is offered where a chat offers it -- with tools -- and a
    // question ends the task, naming it.
    const agentloop::AskFn ask = task.tools ? tasks::fail_on_question() : agentloop::AskFn{};
    const RagSettings rag{
        .flag_given = false, .flag_value = {}, .limit = 4, .config_path = config_path};
    const auto notice = [&reporter, &style](const std::string& message) {
        reporter.keep_line(style.tag(ansi::Role::Warning) + " " + message);
    };

    bool provider_failed = false;
    const tasks::TurnFn turn = [&](const tasks::TurnRequest& request) -> tasks::TurnResult {
        reporter.set_resting_label(request.round == 0
                                       ? std::string{"Planning…"}
                                       : "Round " + std::to_string(request.round) + " of " +
                                             std::to_string(request.budget) + "…");
        if (decorate) {
            reporter.status().print_line("");
        }
        // What the session was before the turn: a turn cut short leaves it
        // just so -- as a kill would -- so the round, run again on resume, is
        // said once in the conversation.
        const logger::Session before = session;
        tasks::TurnResult result;
        try {
            const ChatTurnResult chat =
                run_chat_turn(harness, session, request.message, task.tools ? &observed : nullptr,
                              selection.get(), member_calls.get(), ask, gate, reporter, notice, rag,
                              {}, nullptr, &recall, request.cancellation);
            result.completed = chat.completed;
            result.cancelled = chat.cancelled;
            result.error = chat.error;
            provider_failed = provider_failed || !chat.error.empty();
            result.answer = chat.run.answer;
            result.tokens = chat.run.tokens.tokens;
            result.tokens_estimated = chat.run.tokens.estimated;
        } catch (const tasks::UnansweredQuestion& question) {
            result.question = question.question();
        }
        result.tools = recorder.take_tools();
        result.denied = recorder.take_denials();
        if (!result.completed) {
            session = before;
            logger::save(session);
        }
        if (session.compactions != saved_compactions) {
            saved_compactions = session.compactions;
            harness.save_conversation(session.backend, session.chat_id, progress_sink(reporter));
        }
        if (decorate) {
            reporter.status().print_line("");
        }
        return result;
    };

    const InterruptScope interrupt;
    const tasks::RunOutcome outcome = tasks::run_task(
        tasks::RunRequest{.root = root,
                          .task = task,
                          .resume = what.existing.has_value(),
                          .turn = turn,
                          .say =
                              [&reporter, &style](std::string_view line) {
                                  reporter.status().print_line(style.dim(std::string{line}));
                              },
                          .interrupt = InterruptScope::token(),
                          .poll = std::chrono::milliseconds{200}});

    // A clean end of the session, as `chat`'s clean exit: its summary for
    // recall and the model's saved state -- not after a cancel, which asked
    // for no more model calls.
    if (outcome.task.status != tasks::kCancelled) {
        if (const std::string said = recall.finish({}); !said.empty()) {
            reporter.status().print_line(style.dim("[memory] " + said));
        }
        harness.save_conversation(session.backend, session.chat_id, progress_sink(reporter));
    }
    lock->release();

    if (!outcome.error.empty()) {
        fail_user(outcome.error);
    }
    const tasks::Task& ended = outcome.task;
    std::cout << "task " << ended.id << " " << ended.status << " -- " << ended.reason << "\n"
              << "  conversation: " << ended.session_id << "\n";
    if (ended.status != tasks::kDone) {
        std::cout << "  'apogee task status " << ended.id << "' shows each round";
        if (tasks::resume_refusal(ended).empty()) {
            std::cout << "; 'apogee task resume " << ended.id << "' continues it";
        }
        std::cout << "\n";
    }
    if (const int code = exit_code_for(ended, provider_failed); code != kSuccess) {
        throw CLI::RuntimeError(code);
    }
}

/// `task halt` and `task cancel`: asked of the running process, or -- for a
/// task no process runs -- written to its ledger under the lock.
void stop_task(const TaskFlags& flags, tasks::Request request) {
    const fs::path root = harness::tasks_dir();
    const std::string verb{tasks::to_string(request)};
    tasks::Task task = resolve_task(root, flags.id, /*prefer_running=*/true);
    if (tasks::finished(task)) {
        fail_user("task " + task.id + " is " + task.status + " -- there is nothing to " + verb);
    }
    if (tasks::running_task(root) == task.id) {
        if (const std::string failure = tasks::write_request(root, task.id, request);
            !failure.empty()) {
            fail_user("could not ask task " + task.id + " to " + verb + ": " + failure);
        }
        std::cout << (request == tasks::Request::Halt
                          ? "asked task " + task.id + " to halt: it stops when its round ends"
                          : "asked task " + task.id +
                                " to cancel: its turn ends now, through the loop's own "
                                "cancellation")
                  << "\n";
        return;
    }
    // Nothing runs it. Under the lock when it is free, so a resume cannot
    // start between the read and the write; when another task holds it, this
    // one cannot be resumed meanwhile anyway.
    std::string lock_error;
    const std::optional<tasks::TaskLock> lock = tasks::TaskLock::acquire(root, task.id, lock_error);
    if (lock.has_value()) {
        std::string error;
        std::optional<tasks::Task> fresh = tasks::load_task(root, task.id, error);
        if (!fresh.has_value()) {
            fail_user(error);
        }
        task = std::move(*fresh);
    }
    if (request == tasks::Request::Halt) {
        if (task.status == tasks::kHalted) {
            std::cout << "task " << task.id << " is already halted\n";
            return;
        }
        if (task.status != tasks::kPlanning && task.status != tasks::kRunning) {
            fail_user("task " + task.id + " is " + task.status +
                      " and not running -- there is nothing to halt");
        }
        task.status = std::string{tasks::kHalted};
        task.reason = "halted by 'apogee task halt' while no process ran it";
    } else {
        if (task.status == tasks::kCancelled) {
            std::cout << "task " << task.id << " is already cancelled\n";
            return;
        }
        task.status = std::string{tasks::kCancelled};
        task.reason = "cancelled by 'apogee task cancel' while no process ran it";
    }
    tasks::record_transition(task, tasks::kFinishedEvent, tasks::now_timestamp(), 0, task.reason);
    if (const std::string failure = tasks::save_task(root, task); !failure.empty()) {
        fail_user("the task's ledger could not be written: " + failure);
    }
    std::cout << "task " << task.id << " " << task.status << "\n";
}

}  // namespace

TaskCommand::TaskCommand(MachineBudgetSource machine) : machine_{std::move(machine)} {}

std::string_view TaskCommand::name() const noexcept {
    return "task";
}

std::string_view TaskCommand::summary() const noexcept {
    return "Hand the application a goal: it plans, drives turns, checks and corrects";
}

void TaskCommand::bind(CLI::App& root, const RootContext& context) {
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->require_subcommand(1);

    // ---- run -------------------------------------------------------------------
    auto run_flags = std::make_shared<TaskFlags>();
    CLI::App* run = cmd->add_subcommand(
        "run",
        "Start a task: the application plans, then drives rounds -- each checked against the "
        "acceptance you state, each after the first correcting what failed -- until done or "
        "the budget is spent. Nobody is asked anything: a tool that would ask is denied, and "
        "a question ends the task");
    run->add_option("goal", run_flags->goal, "What the task is to achieve")->required();
    run->add_option("--require", run_flags->require,
                    "Text the answer must contain for the task to be done (repeatable)")
        ->expected(1)
        ->allow_extra_args(false);
    run->add_option("--require-file", run_flags->require_files,
                    "A file that must exist, not empty, for the task to be done (repeatable)")
        ->type_name(kPathValue)
        ->expected(1)
        ->allow_extra_args(false);
    run->add_option("--rounds", run_flags->rounds,
                    "The round budget: at most this many rounds after the plan (default " +
                        std::to_string(tasks::kDefaultRounds) + ", at most " +
                        std::to_string(tasks::kMaxRounds) + ")")
        ->check(CLI::Range(1, tasks::kMaxRounds));
    run->add_option("-m,--model", run_flags->model, "Backend or model to use")
        ->type_name(kBackendValue);
    run->add_option("--suite", run_flags->suite,
                    "Run under this suite -- its members answer for the roles it names -- or off "
                    "for none")
        ->type_name(kModelSuiteValue);
    run->add_flag("--tools", run_flags->tools, "Let the model call tools");
    run->add_flag("--no-recall", run_flags->no_recall, "Recall no earlier chats in this task");
    run->add_flag("--force", run_flags->force,
                  "Run the suite even when what it takes is over this machine's memory");
    run->add_flag("-v,--verbose", run_flags->verbose, "Print progress notes");
    run->add_flag("--no-color", run_flags->no_color, "Disable ANSI colour output");
    run->add_flag("--raw", run_flags->raw,
                  "Show answers' Markdown as written instead of rendering it on the terminal");
    run->callback([&context, run_flags, machine = machine_]() {
        drive(context, machine, *run_flags, Drive{});
    });

    // ---- resume ------------------------------------------------------------------
    auto resume_flags = std::make_shared<TaskFlags>();
    CLI::App* resume = cmd->add_subcommand(
        "resume",
        "Continue a stopped task from its ledger -- halted, cancelled, stalled, failed, or "
        "killed mid-round -- in the folder it was started in");
    resume->add_option("task", resume_flags->id, "The task (default: the newest)")
        ->type_name(kTaskValue);
    resume->add_flag("--no-recall", resume_flags->no_recall,
                     "Recall no earlier chats in this task");
    resume->add_flag("--force", resume_flags->force,
                     "Run the suite even when what it takes is over this machine's memory");
    resume->add_flag("-v,--verbose", resume_flags->verbose, "Print progress notes");
    resume->add_flag("--no-color", resume_flags->no_color, "Disable ANSI colour output");
    resume->add_flag("--raw", resume_flags->raw,
                     "Show answers' Markdown as written instead of rendering it on the terminal");
    resume->callback([&context, resume_flags, machine = machine_]() {
        const fs::path root = harness::tasks_dir();
        const tasks::Task task = resolve_task(root, resume_flags->id, /*prefer_running=*/false);
        drive(context, machine, *resume_flags, Drive{.existing = task});
    });

    // ---- status ------------------------------------------------------------------
    auto status_flags = std::make_shared<TaskFlags>();
    CLI::App* status = cmd->add_subcommand(
        "status",
        "A task's plan, rounds, checks and conversation (default: the running one, "
        "else the newest)");
    status->add_option("task", status_flags->id, "The task")->type_name(kTaskValue);
    status->callback([status_flags]() {
        const fs::path root = harness::tasks_dir();
        print_status(resolve_task(root, status_flags->id, /*prefer_running=*/true), root);
    });

    // ---- list --------------------------------------------------------------------
    auto list_flags = std::make_shared<TaskFlags>();
    CLI::App* list = cmd->add_subcommand("list", "Tasks and their outcomes, newest first");
    list->add_flag("--all", list_flags->all,
                   "Every task, not only the newest " + std::to_string(kListLimit));
    list->callback([list_flags]() {
        const fs::path root = harness::tasks_dir();
        std::vector<std::string> problems;
        const std::vector<tasks::Task> all = tasks::list_tasks(root, &problems);
        for (const std::string& problem : problems) {
            std::cerr << "[task] skipped: " << problem << "\n";
        }
        if (all.empty()) {
            std::cout << "no tasks yet -- 'apogee task run \"<goal>\"' starts one\n";
            return;
        }
        const std::size_t shown = list_flags->all ? all.size() : std::min(all.size(), kListLimit);
        for (std::size_t index = 0; index < shown; ++index) {
            const tasks::Task& task = all[index];
            std::cout << task.id << "  " << task.status << "  " << tasks::rounds_used(task) << "/"
                      << task.rounds_budget << " rounds  " << one_line(task.goal, 60) << "\n";
        }
        if (shown < all.size()) {
            std::cout << "... " << all.size() - shown << " more -- --all lists them\n";
        }
    });

    // ---- halt / cancel -------------------------------------------------------------
    auto halt_flags = std::make_shared<TaskFlags>();
    CLI::App* halt = cmd->add_subcommand(
        "halt",
        "Stop a task when its round ends; 'task resume' continues it (default: the "
        "running one)");
    halt->add_option("task", halt_flags->id, "The task")->type_name(kTaskValue);
    halt->callback([halt_flags]() { stop_task(*halt_flags, tasks::Request::Halt); });

    auto cancel_flags = std::make_shared<TaskFlags>();
    CLI::App* cancel = cmd->add_subcommand(
        "cancel",
        "Stop a task now, ending its turn through the loop's cancellation; 'task "
        "resume' may still continue it (default: the running one)");
    cancel->add_option("task", cancel_flags->id, "The task")->type_name(kTaskValue);
    cancel->callback([cancel_flags]() { stop_task(*cancel_flags, tasks::Request::Cancel); });
}

std::string task_holds_chat(const logger::Session& session) {
    if (session.task.empty()) {
        return {};
    }
    std::string error;
    const std::optional<tasks::Task> task =
        tasks::load_task(harness::tasks_dir(), session.task, error);
    if (!task.has_value() || !tasks::live(*task)) {
        return {};
    }
    return "chat " + session.chat_id + " is the conversation of task " + task->id + ", which is " +
           task->status + " -- 'apogee task cancel " + task->id + "' stops it first";
}

}  // namespace apogee::commands
