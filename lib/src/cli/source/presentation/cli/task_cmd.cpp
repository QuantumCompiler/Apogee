#include "cli/task_cmd.h"

#include <CLI/CLI.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
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
#include "contracts/utf8.h"
#include "harness/harness.h"
#include "harness/roles.h"
#include "logger/operational.h"
#include "logger/session.h"
#include "machine/json_reporter.h"
#include "modelstore/footprint.h"
#include "operations/suites.h"
#include "platform/platform.h"
#include "tasks/ledger.h"
#include "tasks/policy.h"
#include "tasks/runner.h"
#include "tasks/task.h"
#include "tasks/unattended.h"
#include "tasks/view.h"
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
    /// `run` and `resume`: the terminal's rendering, or machine mode's event
    /// stream with the task's lifecycle in it (27j).
    OutputFormat output_format = OutputFormat::Text;
    /// `status` and `list`: the human view, or one JSON document (27j).
    ReadFormat read_format = ReadFormat::Text;
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
[[nodiscard]] std::string status_words(const tasks::TaskView& view) {
    if (view.process.has_value()) {
        return view.status + " (now, process " + std::to_string(*view.process) + ")";
    }
    if (view.interrupted) {
        return view.status + " (interrupted -- its process is gone; 'apogee task resume " +
               view.id + "' continues it)";
    }
    return view.status;
}

[[nodiscard]] std::string round_row(const tasks::TaskView& view, const tasks::TurnView& turn) {
    std::ostringstream out;
    out << "  "
        << (turn.kind == tasks::kPlanRound ? std::string{"plan"} : std::to_string(turn.round))
        << "  " << turn.kind << "  "
        << (turn.outcome == "in_flight" ? std::string{"in flight"} : turn.outcome);
    if (turn.outcome == tasks::kCompleted && turn.kind != tasks::kPlanRound) {
        out << "  " << turn.checks_passed << "/" << view.checks.size() << " checks, "
            << tasks::report_words(tasks::self_report_from_string(turn.self_report));
    }
    if (turn.adopted) {
        out << "  (taken from the conversation after a restart)";
    }
    out << "  tools: " << turn.tools << " ran";
    if (!turn.denied.empty()) {
        out << ", " << turn.denied.size() << " denied";
    }
    out << "  tokens: " << turn.tokens << (turn.tokens_estimated ? " (estimated)" : "");
    return out.str();
}

/// A turn as the status's sections name it.
[[nodiscard]] std::string round_name(const tasks::TurnView& turn) {
    return turn.kind == tasks::kPlanRound ? std::string{"plan"}
                                          : "round " + std::to_string(turn.round);
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
/// handed -- the view's, and the declared answer, which the view never
/// carries, from the ledger: a person at this machine reads it here, and no
/// served view or event does.
void print_policy(const tasks::TaskView& view, const tasks::Task& task) {
    if (!view.policy.has_value()) {
        return;
    }
    const tasks::PolicyView& policy = *view.policy;
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
              << (policy.on_question == tasks::to_string(tasks::OnQuestion::Answer)
                      ? "answer \"" + task.policy.answer +
                            "\" -- every question gets this declared answer"
                      : std::string{"fail -- a question nobody answers ends the task"})
              << "\n";
}

/// One section of `task status`: `heading`, then a row per record across
/// the turns -- nothing at all when there is none.
template <typename Record, typename Row>
void print_section(const tasks::TaskView& view, std::string_view heading,
                   std::vector<Record> tasks::TurnView::* records, const Row& row) {
    bool any = false;
    for (const tasks::TurnView& turn : view.turns) {
        for (const Record& record : turn.*records) {
            if (!any) {
                std::cout << heading << ":\n";
                any = true;
            }
            std::cout << "  " << round_name(turn) << ": " << row(record) << "\n";
        }
    }
}

[[nodiscard]] std::string on_target(const std::string& tool, const std::string& target) {
    return target.empty() ? tool : tool + " on " + target;
}

/// Every gated call let through, every refusal, every question answered.
void print_uses(const tasks::TaskView& view, const tasks::Task& task) {
    print_section(view, "allowed", &tasks::TurnView::allowed, [](const tasks::UseView& use) {
        return on_target(use.tool, use.target) + " -- " + authority_words(use.by);
    });
    print_section(view, "denied", &tasks::TurnView::denied, [](const tasks::UseView& use) {
        const std::string why = refusal_words(use.by);
        return on_target(use.tool, use.target) + (why.empty() ? std::string{} : " -- " + why);
    });
    // The view says who answered each question and never what; the answers
    // are the ledger's, in the view's own order -- every round's, in turn.
    std::vector<std::string> answers;
    for (const tasks::Round& round : task.rounds) {
        for (const tasks::Answered& question : round.answered) {
            answers.push_back(question.answer);
        }
    }
    std::size_t next = 0;
    print_section(view, "answered", &tasks::TurnView::answered,
                  [&answers, &next](const tasks::QuestionView& question) {
                      const std::string answer = next < answers.size() ? answers[next] : "";
                      ++next;
                      return question.question + " -- \"" + answer + "\", " +
                             (question.by == tasks::kByDeclared ? "the declared answer"
                                                                : "the person at the terminal");
                  });
}

/// `task status`, for a person: the task's view -- what every surface shows
/// -- and the declared answer beside it, which only this one does.
void print_status(const tasks::Task& task, const tasks::TaskView& view) {
    std::cout << "task " << view.id << "  " << status_words(view) << "\n"
              << "goal:          " << view.goal << "\n"
              << "conversation:  " << view.conversation << "\n"
              << "folder:        " << view.folder << "\n"
              << "tools:         " << (view.tools ? "on" : "off") << "\n";
    print_policy(view, task);
    std::cout << "rounds:        " << view.rounds_used << " of " << view.rounds_budget << " used\n"
              << "started:       " << view.created_at << "\n"
              << "updated:       " << view.updated_at << "\n";
    if (!view.reason.empty()) {
        std::cout << "reason:        " << view.reason << "\n";
    }
    // Each check's state as the newest completed round left it.
    std::cout << "checks:\n";
    for (const tasks::CheckView& check : view.checks) {
        std::cout << "  [" << (check.passed ? "x" : " ") << "] " << check.description
                  << (check.ran ? " -- " + check.detail : std::string{" -- not run yet"}) << "\n";
    }
    std::cout << "  ["
              << (view.self_report == tasks::to_string(tasks::SelfReport::Done) ? "x" : " ")
              << "] the model reports the task done\n";
    std::cout << "plan:" << (view.plan.empty() ? " (not recorded yet)\n" : "\n");
    if (!view.plan.empty()) {
        std::istringstream lines{view.plan};
        std::string line;
        while (std::getline(lines, line)) {
            std::cout << "  " << line << "\n";
        }
    }
    if (!view.turns.empty()) {
        std::cout << "turns:\n";
        for (const tasks::TurnView& turn : view.turns) {
            std::cout << round_row(view, turn) << "\n";
        }
    }
    print_uses(view, task);
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
    // Machine mode (27j): stdout carries the event stream and nothing else --
    // the turns' events and the task's lifecycle around them -- and nobody is
    // asked anything: the run reads no input, so there is no one to answer.
    const bool machine_mode = flags.output_format == OutputFormat::StreamJson;
    const bool decorate = !machine_mode && platform::is_terminal(platform::StandardStream::Out);
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
    // One reading of the clock names a new task and dates it: `list_tasks`
    // orders a second's tasks by their ids.
    const std::chrono::system_clock::time_point minted = std::chrono::system_clock::now();
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
        task_id = tasks::new_task_id(root, minted);
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
        if (const std::string refused = task_resume_refusal(task, here); !refused.empty()) {
            fail_user(refused);
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
        // The goal and the text a round must contain are kept as text: the
        // ledger is a strict JSON dump, and a round's answer is UTF-8 (the
        // Harness's), so a check against the bytes as typed could never pass.
        // A --require-file path stays as given -- it names a file on disk.
        task.goal = harness::valid_utf8(flags.goal);
        for (const std::string& text : flags.require) {
            task.checks.push_back(tasks::Check{.kind = tasks::CheckKind::Require,
                                               .value = harness::valid_utf8(text)});
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
        session.title = sanitize_title("task: " + task.goal);
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
    // A model loading says so on the spinner -- never into machine mode's
    // stream, as `chat`'s.
    if (!machine_mode) {
        harness.listen_for_loads(
            [&reporter](std::string_view backend, const harness::StatusEvent& event) {
                reporter.on_model_load(backend, event);
            });
    }

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
    // In machine mode nobody is: stdin is not the person's, and a prompt on
    // the terminal would be an input channel the protocol does not have.
    const PersonPrompt person =
        machine_mode ? PersonPrompt{}
                     : PersonPrompt{[&reporter, &style,
                                     &config_path](std::shared_ptr<SessionApprovals> approvals) {
                           return terminal_confirm_fn(reporter.status(), style, config_path,
                                                      std::move(approvals));
                       }};
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
        } else if (agentloop::AskFn there = machine_mode
                                                ? agentloop::AskFn{}
                                                : terminal_ask_fn(reporter.status(), style);
                   there) {
            ask = recorder.person(std::move(there));
        } else {
            ask = tasks::fail_on_question();
        }
    }

    if (!what.existing.has_value()) {
        // The session exists from the start, naming its task: a resume that
        // later finds it gone knows it was deleted, not never made.
        logger::save(session);
        task.created_at = tasks::timestamp(minted);
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
    // The task's events and the turns' events on stdout, in machine mode; a
    // person's rendering otherwise.
    const std::unique_ptr<JsonReporter> machine_reporter =
        machine_mode ? std::make_unique<JsonReporter>(std::cout) : nullptr;
    agentloop::Reporter& turn_reporter =
        machine_mode ? static_cast<agentloop::Reporter&>(*machine_reporter) : reporter;
    const auto notice = [&reporter, &style, machine_mode](const std::string& message) {
        if (machine_mode) {
            // stdout carries only protocol events, so a diagnostic goes to
            // stderr -- machine mode's rule, as `chat`'s.
            std::cerr << "apogee: " << message << "\n";
            return;
        }
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
                              selection.get(), member_calls.get(), ask, gate, turn_reporter, notice,
                              rag, {}, nullptr, &recall, request.cancellation);
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
        if (machine_mode) {
            // The turn ends as a machine-mode turn ends: its `result`, or the
            // machine-readable half of why it did not finish.
            if (result.completed) {
                harness::ChatResponse response;
                response.message = session.messages.empty() ? harness::ChatMessage::assistant("")
                                                            : session.messages.back();
                response.model = session.backend;
                machine_reporter->emit_result(response);
            } else if (!result.question.empty()) {
                machine_reporter->emit_error(
                    "the model asked a question and no one is present to answer it: " +
                    result.question);
            } else if (!result.error.empty()) {
                machine_reporter->emit_error(result.error);
            } else {
                machine_reporter->emit_error("cancelled");
            }
        }
        if (!result.completed) {
            session = before;
            logger::save(session);
        }
        if (session.compactions != saved_compactions) {
            saved_compactions = session.compactions;
            harness.save_conversation(session.backend, session.chat_id,
                                      progress_sink(turn_reporter));
        }
        if (decorate) {
            reporter.status().print_line("");
        }
        return result;
    };

    if (machine_mode) {
        // Once, first -- the protocol's rule -- then the task's lifecycle.
        // A task reads nothing from its driver, so it accepts no line and
        // nobody is asked over the protocol (28d).
        machine_reporter->begin_session(
            session.backend, MachineCapabilities{.accepts = {}, .tools = task.tools, .ask = false});
    }

    // One task event per transition the ledger holds, and one per grant used:
    // machine mode's view of the run.
    std::function<void(const tasks::Task&, std::size_t)> on_transition;
    std::function<void(const tasks::Task&, int, const tasks::Permit&)> on_grant;
    if (machine_mode) {
        on_transition = [&machine_reporter, &root](const tasks::Task& written, std::size_t index) {
            machine_reporter->emit_task_transition(written, index, tasks::lock_holder(root));
        };
        on_grant = [&machine_reporter](const tasks::Task& running, int round,
                                       const tasks::Permit& permit) {
            machine_reporter->emit_task_grant(running, round, permit);
        };
    }

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
                          .poll = std::chrono::milliseconds{200},
                          .on_transition = on_transition,
                          .on_grant = on_grant});

    // A clean end of the session, as `chat`'s clean exit: its summary for
    // recall and the model's saved state -- not after a cancel, which asked
    // for no more model calls.
    if (outcome.task.status != tasks::kCancelled) {
        if (const std::string said = recall.finish({}); !said.empty()) {
            reporter.status().print_line(style.dim("[memory] " + said));
        }
        harness.save_conversation(session.backend, session.chat_id, progress_sink(turn_reporter));
    }
    lock->release();

    if (!outcome.error.empty()) {
        if (machine_mode) {
            machine_reporter->emit_error(outcome.error);
        }
        fail_user(outcome.error);
    }
    const tasks::Task& ended = outcome.task;
    // The outcome, for a person; in machine mode `task_finished` says it on
    // stdout, and this goes to stderr with the other diagnostics.
    std::ostream& told = machine_mode ? std::cerr : std::cout;
    told << "task " << ended.id << " " << ended.status << " -- " << ended.reason << "\n"
         << "  conversation: " << ended.session_id << "\n";
    if (ended.status != tasks::kDone) {
        told << "  'apogee task status " << ended.id << "' shows each round";
        if (tasks::resume_refusal(ended).empty()) {
            told << "; 'apogee task resume " << ended.id << "' continues it";
        }
        told << "\n";
    }
    if (const int code = exit_code_for(ended, provider_failed); code != kSuccess) {
        throw CLI::RuntimeError(code);
    }
}

/// Why `task <verb>` refuses `task`, which `task_stoppable` turned down.
std::string stop_refusal(const tasks::Task& task, const std::string& verb) {
    if (tasks::finished(task)) {
        return "task " + task.id + " is " + task.status + " -- there is nothing to " + verb;
    }
    return "task " + task.id + " is " + task.status + " and not running -- there is nothing to " +
           verb;
}

/// `task halt` and `task cancel`: asked of the running process, or -- for a
/// task no process runs -- written to its ledger under the lock.
void stop_task(const TaskFlags& flags, tasks::Request request) {
    const fs::path root = harness::tasks_dir();
    const std::string verb{tasks::to_string(request)};
    tasks::Task task = resolve_task(root, flags.id, /*prefer_running=*/true);
    if (!task_stoppable(task, request)) {
        fail_user(stop_refusal(task, verb));
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
    // Read again under the lock: what it holds now is what is stopped.
    if (!task_stoppable(task, request)) {
        fail_user(stop_refusal(task, verb));
    }
    if (request == tasks::Request::Halt) {
        if (task.status == tasks::kHalted) {
            std::cout << "task " << task.id << " is already halted\n";
            return;
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

/// `--output-format` on `run` and `resume`: the terminal's rendering, or
/// machine mode's event stream (27j).
void add_run_format(CLI::App* command, const std::shared_ptr<TaskFlags>& flags) {
    command
        ->add_option_function<std::string>(
            "--output-format",
            [flags](const std::string& value) {
                const std::optional<OutputFormat> parsed = output_format_from_string(value);
                if (!parsed.has_value()) {
                    throw CLI::ValidationError("--output-format",
                                               "expected 'text' or 'stream-json'");
                }
                flags->output_format = *parsed;
            },
            "Output format: text (default) or stream-json -- the turns' events and the task's "
            "lifecycle, one JSON object per line, for a machine driver")
        ->type_name(words_value(format_names()));
}

/// `--output-format` on `status` and `list`: the human view, or one JSON
/// document of the same facts (27j, the read convention).
void add_read_format(CLI::App* command, const std::shared_ptr<TaskFlags>& flags) {
    command
        ->add_option_function<std::string>(
            "--output-format",
            [flags](const std::string& value) {
                const std::optional<ReadFormat> parsed = read_format_from_string(value);
                if (!parsed.has_value()) {
                    throw CLI::ValidationError("--output-format", "expected 'text' or 'json'");
                }
                flags->read_format = *parsed;
            },
            "Output format: text (default) or json -- one JSON document of the same facts")
        ->type_name(words_value(read_format_names()));
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
        ->type_name(kModelSuiteOrOffValue);
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
    add_run_format(run, run_flags);
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
        ->type_name(kResumableTaskValue);
    resume->add_flag("--no-recall", resume_flags->no_recall,
                     "Recall no earlier chats in this task");
    resume->add_flag("--force", resume_flags->force,
                     "Run the suite even when what it takes is over this machine's memory");
    resume->add_flag("-v,--verbose", resume_flags->verbose, "Print progress notes");
    resume->add_flag("--no-color", resume_flags->no_color, "Disable ANSI colour output");
    resume->add_flag("--raw", resume_flags->raw,
                     "Show answers' Markdown as written instead of rendering it on the terminal");
    add_run_format(resume, resume_flags);
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
    add_read_format(status, status_flags);
    status->callback([status_flags]() {
        const fs::path root = harness::tasks_dir();
        const tasks::Task task = resolve_task(root, status_flags->id, /*prefer_running=*/true);
        // One view, rendered for a person or as the document a host reads --
        // the body `GET /v1/admin/tasks/{id}` serves.
        const tasks::TaskView view = tasks::make_task_view(task, tasks::lock_holder(root));
        if (status_flags->read_format == ReadFormat::Json) {
            write_document(std::cout, tasks::to_json(view));
            return;
        }
        print_status(task, view);
    });

    // ---- list --------------------------------------------------------------------
    auto list_flags = std::make_shared<TaskFlags>();
    CLI::App* list = cmd->add_subcommand("list", "Tasks and their outcomes, newest first");
    list->add_flag("--all", list_flags->all,
                   "Every task, not only the newest " + std::to_string(tasks::kListLimit));
    add_read_format(list, list_flags);
    list->callback([list_flags]() {
        const fs::path root = harness::tasks_dir();
        std::vector<std::string> problems;
        const std::vector<tasks::Task> all = tasks::list_tasks(root, &problems);
        for (const std::string& problem : problems) {
            std::cerr << "[task] skipped: " << problem << "\n";
        }
        // One listing, for a person or as the document a host reads -- the
        // body `GET /v1/admin/tasks` serves.
        const tasks::TaskListView listed = tasks::make_task_list(all, list_flags->all);
        if (list_flags->read_format == ReadFormat::Json) {
            write_document(std::cout, tasks::to_json(listed));
            return;
        }
        if (listed.total == 0) {
            std::cout << "no tasks yet -- 'apogee task run \"<goal>\"' starts one\n";
            return;
        }
        for (const tasks::TaskSummary& row : listed.shown) {
            std::cout << row.id << "  " << row.status << "  " << row.rounds_used << "/"
                      << row.rounds_budget << " rounds  " << one_line(row.goal, 60) << "\n";
        }
        if (listed.shown.size() < listed.total) {
            std::cout << "... " << listed.total - listed.shown.size()
                      << " more -- --all lists them\n";
        }
    });

    // ---- halt / cancel -------------------------------------------------------------
    auto halt_flags = std::make_shared<TaskFlags>();
    CLI::App* halt = cmd->add_subcommand(
        "halt",
        "Stop a task when its round ends; 'task resume' continues it (default: the "
        "running one)");
    halt->add_option("task", halt_flags->id, "The task")->type_name(kHaltableTaskValue);
    halt->callback([halt_flags]() { stop_task(*halt_flags, tasks::Request::Halt); });

    auto cancel_flags = std::make_shared<TaskFlags>();
    CLI::App* cancel = cmd->add_subcommand(
        "cancel",
        "Stop a task now, ending its turn through the loop's cancellation; 'task "
        "resume' may still continue it (default: the running one)");
    cancel->add_option("task", cancel_flags->id, "The task")->type_name(kCancellableTaskValue);
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

bool task_stoppable(const tasks::Task& task, tasks::Request request) noexcept {
    return request == tasks::Request::Halt ? tasks::live(task) : !tasks::finished(task);
}

std::string task_resume_refusal(const tasks::Task& task, const fs::path& here) {
    if (std::string refused = tasks::resume_refusal(task); !refused.empty()) {
        return refused;
    }
    // A task's tools work where it was started; resumed anywhere else, its
    // files would be written in one place and checked in another.
    if (real_folder(task.working_directory) != real_folder(here)) {
        return "task " + task.id + " was started in " + task.working_directory +
               " and resumes only there -- cd there first";
    }
    return {};
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
