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
#include "contracts/assets.h"
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
#include "tasks/policy.h"
#include "tasks/runner.h"
#include "tasks/task.h"
#include "tasks/unattended.h"
#include "tools/consult.h"
#include "views/ask_prompt.h"
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
    /// `--allow`, each a tool granted for the task's life (27i).
    std::vector<std::string> allow;
    /// `--on-question`: `fail`, or `answer:<text>`; empty for the default.
    std::string on_question;
    /// `--agent`: the agent whose policy the task runs under.
    std::string agent;
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

/// A round as the status's sections name it.
[[nodiscard]] std::string round_name(const tasks::Round& round) {
    return round.kind == tasks::kPlanRound ? std::string{"plan"}
                                           : "round " + std::to_string(round.index);
}

/// On whose authority a gated call ran.
[[nodiscard]] std::string authority_words(std::string_view by) {
    if (by == tasks::kByGrant) {
        return "this task's grant";
    }
    if (by == tasks::kByPerson) {
        return "the person at the terminal";
    }
    if (by == tasks::kByConfig) {
        return "the config";
    }
    return std::string{by};
}

/// Why a gated call was refused; empty for a ledger that did not say.
[[nodiscard]] std::string refusal_words(std::string_view by) {
    if (by == tasks::kByConfig) {
        return "the config denies it";
    }
    if (by == tasks::kByNobody) {
        return "nobody present to allow it";
    }
    if (by == tasks::kByPerson) {
        return "refused at the prompt";
    }
    return {};
}

/// The task's autonomy policy, as `status` shows it (27i): what it was
/// handed, including the declared answer -- the ledger's own field.
void print_policy(const tasks::Task& task) {
    if (!task.tools) {
        return;
    }
    const tasks::AutonomyPolicy& policy = task.policy;
    if (!policy.agent.empty()) {
        std::cout << "agent:         " << policy.agent << " -- the task runs under its policy\n";
    }
    std::string grants;
    for (const std::string& tool : policy.grants) {
        grants += (grants.empty() ? "" : ", ") + tool;
    }
    std::cout << "grants:        "
              << (grants.empty() ? std::string{"none -- with nobody present, a tool that asks is "
                                               "denied"}
                                 : grants)
              << "\n";
    std::cout << "on question:   "
              << (policy.on_question == tasks::OnQuestion::Answer
                      ? "answer \"" + policy.answer +
                            "\" -- every question gets this declared answer"
                      : std::string{"fail -- a question nobody answers ends the task"})
              << "\n";
}

/// One section of `task status`: `heading`, then a row per record across
/// the rounds -- nothing at all when there is none.
template <typename Record, typename Row>
void print_section(const tasks::Task& task, std::string_view heading,
                   std::vector<Record> tasks::Round::* records, const Row& row) {
    bool any = false;
    for (const tasks::Round& round : task.rounds) {
        for (const Record& record : round.*records) {
            if (!any) {
                std::cout << heading << ":\n";
                any = true;
            }
            std::cout << "  " << round_name(round) << ": " << row(record) << "\n";
        }
    }
}

[[nodiscard]] std::string on_target(const std::string& tool, const std::string& target) {
    return target.empty() ? tool : tool + " on " + target;
}

/// Every gated call let through, every refusal, every question answered.
void print_uses(const tasks::Task& task) {
    print_section(task, "allowed", &tasks::Round::allowed, [](const tasks::Permit& permit) {
        return on_target(permit.tool, permit.target) + " -- " + authority_words(permit.by);
    });
    print_section(task, "denied", &tasks::Round::denied, [](const tasks::Denial& denial) {
        const std::string why = refusal_words(denial.by);
        return on_target(denial.tool, denial.target) + (why.empty() ? std::string{} : " -- " + why);
    });
    print_section(task, "answered", &tasks::Round::answered, [](const tasks::Answered& question) {
        return question.question + " -- \"" + question.answer + "\", " +
               (question.by == tasks::kByDeclared ? "the declared answer"
                                                  : "the person at the terminal");
    });
}

void print_status(const tasks::Task& task, const fs::path& root) {
    std::cout << "task " << task.id << "  " << status_words(task, root) << "\n"
              << "goal:          " << task.goal << "\n"
              << "conversation:  " << task.session_id << "\n"
              << "folder:        " << task.working_directory << "\n"
              << "tools:         " << (task.tools ? "on" : "off") << "\n";
    print_policy(task);
    std::cout << "rounds:        " << tasks::rounds_used(task) << " of " << task.rounds_budget
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
    print_uses(task);
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

/// A new task's autonomy policy from its flags (27i), each refused before
/// anything is made: a grant, a declared answer or an agent needs tools to
/// mean anything, and a typo must never quietly grant nothing.
[[nodiscard]] tasks::AutonomyPolicy policy_from_flags(const TaskFlags& flags) {
    tasks::AutonomyPolicy policy;
    if (!flags.on_question.empty()) {
        if (const std::string refused = tasks::parse_on_question(flags.on_question, policy);
            !refused.empty()) {
            fail_user(refused);
        }
    }
    policy.grants = tasks::normalized_grants(flags.allow);
    policy.agent = flags.agent;
    if (!flags.tools) {
        if (!policy.grants.empty()) {
            fail_user("--allow needs --tools: without tools, nothing is asked");
        }
        if (policy.on_question == tasks::OnQuestion::Answer) {
            fail_user(
                "--on-question answer: needs --tools: without tools the model cannot ask a "
                "question");
        }
        if (!policy.agent.empty()) {
            fail_user(
                "--agent needs --tools: an agent's policy is over the tools a task's turns "
                "may call");
        }
    }
    return policy;
}

/// The agent `policy` names, as the config holds it now; nullopt for none.
/// An agent that asks no questions has no use for a declared answer, and
/// one that is gone cannot be run under. `resuming` names the task a resume
/// refuses.
[[nodiscard]] std::optional<harness::AgentConfig> resolve_task_agent(
    const harness::Config& config, const tasks::AutonomyPolicy& policy,
    const std::string& resuming) {
    if (policy.agent.empty()) {
        return std::nullopt;
    }
    std::optional<harness::AgentConfig> agent = harness::resolve_agent(config, policy.agent);
    if (!agent.has_value()) {
        std::string names;
        for (const harness::NamedAgent& known : harness::all_agents(config)) {
            names += (names.empty() ? "" : ", ") + known.name;
        }
        const std::string available = names.empty() ? std::string{} : " (available: " + names + ")";
        fail_user(resuming.empty()
                      ? "--agent: no agent named '" + policy.agent + "'" + available
                      : "task " + resuming + " cannot resume: it runs under the agent '" +
                            policy.agent + "', which the config no longer has" + available);
    }
    if (policy.on_question == tasks::OnQuestion::Answer && !agent->questions) {
        fail_user((resuming.empty() ? std::string{} : "task " + resuming + " cannot resume: ") +
                  "--on-question answer: the agent '" + policy.agent +
                  "' asks no questions (its questions: false), so a declared answer would "
                  "never be used");
    }
    return agent;
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
    tasks::AutonomyPolicy policy;
    if (what.existing.has_value()) {
        task_id = what.existing->id;
        policy = what.existing->policy;
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
        policy = policy_from_flags(flags);
        task_id = tasks::new_task_id(root);
    }
    // The agent whose policy the task runs under, read as the config has it
    // now -- on a resume too: a task never runs under a policy the config no
    // longer holds.
    const std::optional<harness::AgentConfig> agent_entry =
        resolve_task_agent(config, policy, what.existing.has_value() ? task_id : std::string{});

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
        task.policy = policy;
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

    // --- the tools, and what the task may do with them (27i) --------------------
    // Composed before anything is written: a grant wider than the config or
    // the agent allows is refused with no task made, and a resume is held to
    // the config and the agent as they are now.
    const tasks::TurnRecorder recorder;
    agent::ToolRegistry available;
    const auto mcp_registry = std::make_shared<mcp::Registry>();
    const auto member_calls = std::make_shared<agentloop::MemberCalls>(harness);
    if (task.tools) {
        // Under an agent, the servers it names and none else, and none at all
        // when it calls no tools (Milestone X). Its policy narrows the
        // registry in `compose_task_gate`, where the ceiling can name it.
        std::optional<std::vector<std::string>> servers;
        if (agent_entry.has_value()) {
            servers = agent_entry->tools == harness::AgentToolPolicy::None
                          ? std::vector<std::string>{}
                          : agent_entry->mcp;
        }
        available = pin_toolset(
            make_built_in_tools(BuiltInToolOptions{
                .config = &config,
                .harness = &harness,
                .mcp_servers = servers,
                .mcp = mcp_registry,
                .mcp_status = mcp_status_line(reporter.status()),
                .mcp_server_log = flags.verbose ? mcp::StderrTail::Sink{[](std::string_view bytes) {
                    std::cerr << bytes << std::flush;
                }}
                                                : mcp::StderrTail::Sink{}}),
            config, session.backend);
    }
    // The person at the terminal, when there is one: policy adds to the human
    // path, never replaces it -- anything not granted is asked about, as a
    // chat asks. With nobody there the prompt is null and `ask` denies.
    const PersonPrompt person = [&reporter, &style,
                                 &config_path](std::shared_ptr<SessionApprovals> approvals) {
        return terminal_confirm_fn(reporter.status(), style, config_path, std::move(approvals));
    };
    TaskGate composed = compose_task_gate(
        config, available, task.policy, agent_entry.has_value() ? &*agent_entry : nullptr, person);
    if (!composed.refusal.empty()) {
        fail_user(what.existing.has_value()
                      ? "task " + task.id + " cannot resume: " + composed.refusal +
                            " (its grants are held to the config and its agent as they are now)"
                      : composed.refusal);
    }
    agent::ToolRegistry registry = std::move(composed.tools);
    agent::ToolRegistry observed;
    std::unique_ptr<agentloop::ToolSelection> selection;
    if (task.tools) {
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
    // Watched, never widened by the watching: every call the gate lets through
    // recorded with its authority, every refusal with its reason.
    const tasks::WatchedGate watched = recorder.watch(composed.gate.permission, composed.standing,
                                                      task.policy.grants, composed.gate.confirm);
    const ToolGate gate{.permission = watched.permission, .confirm = watched.confirm};
    // `ask_user` is offered where a chat offers it -- with tools -- unless the
    // agent asks no questions. The declared answer serves every question;
    // with none declared, the person at the terminal answers, and with nobody
    // there a question ends the task, naming it.
    agentloop::AskFn ask;
    if (task.tools && (!agent_entry.has_value() || agent_entry->questions)) {
        if (task.policy.on_question == tasks::OnQuestion::Answer) {
            ask = recorder.declared_answer(task.policy.answer);
        } else if (agentloop::AskFn there = terminal_ask_fn(reporter.status(), style); there) {
            ask = recorder.person(std::move(there));
        } else {
            ask = tasks::fail_on_question();
        }
    }

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
        result.allowed = recorder.take_allowed();
        result.answered = recorder.take_answered();
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
        "the budget is spent. With nobody at the terminal a tool that would ask is denied "
        "unless granted (--allow), and a question ends the task unless an answer is declared "
        "(--on-question); at a terminal, the person there is asked");
    run->add_option("goal", run_flags->goal, "What the task is to achieve")->required();
    run->add_option("--require", run_flags->require,
                    "Text the answer must contain for the task to be done (repeatable)")
        ->expected(1)
        ->allow_extra_args(false)
        ->multi_option_policy(CLI::MultiOptionPolicy::TakeAll);
    run->add_option("--require-file", run_flags->require_files,
                    "A file that must exist, not empty, for the task to be done (repeatable)")
        ->type_name(kPathValue)
        ->expected(1)
        ->allow_extra_args(false)
        ->multi_option_policy(CLI::MultiOptionPolicy::TakeAll);
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
    run->add_option("--allow", run_flags->allow,
                    "Grant a tool that writes for this task's life, so it runs without asking -- "
                    "each tool named, every use recorded, never wider than the config or the "
                    "agent allows (repeatable, with --tools)")
        ->type_name(kToolValue)
        ->expected(1)
        ->allow_extra_args(false)
        ->multi_option_policy(CLI::MultiOptionPolicy::TakeAll);
    run->add_option("--on-question", run_flags->on_question,
                    "What a question gets with nobody at the terminal: fail (the default) ends "
                    "the task, answer:\"<text>\" gives every question that one answer -- "
                    "kept in the task's ledger and shown by 'task status', so never a "
                    "credential (with --tools)")
        ->type_name(words_value(tasks::on_question_forms()));
    run->add_option("--agent", run_flags->agent,
                    "Run under this agent's policy: only the tools it allows, and its MCP "
                    "servers (with --tools)")
        ->type_name(kAgentValue);
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

TaskGate compose_task_gate(const harness::Config& config, const agent::ToolRegistry& available,
                           const tasks::AutonomyPolicy& policy, const harness::AgentConfig* agent,
                           const PersonPrompt& person) {
    TaskGate out;
    // The agent's policy is the one filter over the registry (Milestone X):
    // what it leaves out is not offered, so it cannot be granted either.
    out.tools = agent != nullptr ? apply_tool_policy(available, agent->tools) : available;
    out.refusal = tasks::grant_refusal(
        policy.grants,
        tasks::GrantScope{.available = &available,
                          .offered = &out.tools,
                          .permissions = &config.permissions,
                          .agent = policy.agent,
                          .agent_tools = agent != nullptr
                                             ? std::string{harness::to_string(agent->tools)}
                                             : std::string{}});
    if (!out.refusal.empty()) {
        return out;
    }
    // The grants are the `session` answer, given at launch: chat's `--allow`
    // through chat's own seeding and chat's own checker (26o), so the order
    // stays the gate's -- the config's no, then its yes, then the grant.
    const auto approvals = std::make_shared<SessionApprovals>();
    if (std::string refused = seed_approvals(*approvals, PermissionPresets{.allow = policy.grants},
                                             gated_tools(out.tools));
        !refused.empty()) {
        out.refusal = std::move(refused);
        return out;
    }
    out.gate.permission = make_permission_checker(config, approvals);
    out.standing = make_permission_checker(config, nullptr);
    out.gate.confirm = person ? person(approvals) : agent::ConfirmFn{};
    return out;
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
