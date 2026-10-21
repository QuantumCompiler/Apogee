#include "cli/bootstrap_cmd.h"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

#include "cli/helpers.h"
#include "cli/tui_child.h"
#include "contracts/config.h"
#include "contracts/paths.h"
#include "machine/json_reporter.h"
#include "operations/code_exec_server.h"
#include "platform/child_process.h"
#include "platform/ffmpeg.h"
#include "platform/platform.h"
#include "training/python_env.h"
#include "training/trainer.h"

namespace apogee::commands {
namespace {

/// The command line a step runs, as a person would type it.
[[nodiscard]] std::string command_line(const std::vector<std::string>& words) {
    std::string out = "apogee";
    for (const std::string& word : words) {
        out += " " + word;
    }
    return out;
}

/// What a step runs, as typed: `apogee …`, or another program's commands.
[[nodiscard]] std::string step_line(const BootstrapStep& step) {
    return step.external.empty() ? command_line(step.words) : remedy_line(step.external);
}

/// The last line with anything on it, trimmed.
[[nodiscard]] std::string last_said(const std::vector<std::string>& lines) {
    for (auto line = lines.rbegin(); line != lines.rend(); ++line) {
        const std::size_t start = line->find_first_not_of(" \t\r");
        if (start != std::string::npos) {
            const std::size_t end = line->find_last_not_of(" \t\r");
            return line->substr(start, end - start + 1);
        }
    }
    return {};
}

constexpr std::string_view kRerun = "'apogee bootstrap' runs it again any time";

/// The plan's facts, read once before the first step: the host's shape;
/// whether the code-execution server is registered, from the config; and,
/// where the captions step could run, whether ffmpeg has the subtitles
/// filter -- by `check`'s own probe (2026-10-21).
[[nodiscard]] BootstrapHost detect_bootstrap_host(const RootContext& context) {
    BootstrapHost host;
    const training::HostShape shape = training::detect_host();
    host.apple_silicon = shape.apple_silicon;
    host.nvidia_smi = shape.nvidia_smi;
    host.macos = platform::host_target().starts_with("macos");
    host.brew = !platform::find_on_path("brew").empty();
    if (host.macos && host.brew && !platform::ffmpeg_path().empty()) {
        std::string error;
        if (const std::optional<std::string> filters =
                platform::ffmpeg_listing("-filters", {}, error)) {
            host.ffmpeg_subtitles = platform::filter_listed(*filters, "subtitles");
        }
    }
    try {
        const std::filesystem::path path = harness::resolve_config_path(context.config_path);
        std::error_code code;
        if (std::filesystem::exists(path, code)) {
            const harness::Config config = harness::load_config(path);
            const operations::CodeExecRegistration registration =
                operations::code_exec_registration(harness::apogee_home());
            host.code_exec_registered_as =
                operations::registered_code_exec_server(config, registration.script);
            if (host.code_exec_registered_as.empty() &&
                config.mcp_servers.contains(std::string{operations::kCodeExecServerName})) {
                host.code_exec_registered_as = operations::kCodeExecServerName;
                host.code_exec_name_taken = true;
            }
        }
    } catch (const harness::ConfigError&) {
        // A config that does not parse is check's to report; the step runs
        // and says the create's own refusal.
    }
    return host;
}

}  // namespace

std::vector<BootstrapStep> bootstrap_plan(const BootstrapHost& host) {
    std::vector<BootstrapStep> out;
    // The table's own sets, in its order: a set added there joins here.
    for (const std::string_view name : training::requirement_set_names()) {
        const std::optional<training::RequirementSet> set =
            training::requirement_set_from_string(name);
        BootstrapStep step{.name = "the '" + std::string{name} + "' set"};
        if (std::string unfit =
                training::set_unfit_reason(*set, host.apple_silicon, host.nvidia_smi);
            !unfit.empty()) {
            step.status = "skipped";
            step.reason = std::move(unfit);
        } else {
            step.words = {"train", "setup", "--with", std::string{name}};
        }
        out.push_back(std::move(step));
    }
    // The code-execution server (47b), by its preset (47c), after the
    // runtimes so it runs under the environment they made.
    BootstrapStep code{.name = "the code-execution server"};
    if (host.code_exec_name_taken) {
        code.status = "skipped";
        code.reason = "an MCP server named '" + host.code_exec_registered_as +
                      "' runs something else -- 'apogee mcp create <name> --from code-exec' "
                      "registers it under another";
    } else if (!host.code_exec_registered_as.empty()) {
        code.status = "skipped";
        code.reason = "registered already, as '" + host.code_exec_registered_as + "'";
    } else {
        code.words = {"mcp", "create", std::string{operations::kCodeExecServerName}, "--from",
                      "code-exec"};
    }
    out.push_back(std::move(code));
    out.push_back({.name = "the providers", .words = {"providers", "scan", "--register"}});
    out.push_back({.name = "the model store", .words = {"config", "scan", "--register"}});
    // An ffmpeg that burns captions (52e's ability): check's remedy run,
    // where it is the remedy -- macOS with Homebrew -- and needed.
    BootstrapStep ffmpeg{.name = "ffmpeg with captions"};
    ffmpeg.status = "skipped";
    if (!host.macos) {
        ffmpeg.reason = "Homebrew's ffmpeg-full is macOS's -- 'apogee check' names this host's way";
    } else if (!host.brew) {
        ffmpeg.reason = "Homebrew is not on PATH -- 'apogee check' names the commands";
    } else if (!host.ffmpeg_subtitles.has_value()) {
        ffmpeg.reason = "no ffmpeg answered on PATH -- 'apogee check' names the one to install";
    } else if (*host.ffmpeg_subtitles) {
        ffmpeg.reason = "this ffmpeg burns captions already (its subtitles filter is there)";
    } else {
        ffmpeg.status.clear();
        ffmpeg.external = macos_captions_remedy();
    }
    out.push_back(std::move(ffmpeg));
    return out;
}

BootstrapResult run_bootstrap(const BootstrapSeams& seams, const std::string& minimal_by) {
    const auto say = [&seams](const std::string& line) {
        if (seams.say) {
            seams.say(line);
        }
    };
    BootstrapResult result;
    if (!minimal_by.empty()) {
        result.minimal = true;
        result.minimal_by = minimal_by;
        say("bootstrap skipped: a minimal install (" + minimal_by +
            ") -- 'apogee bootstrap' installs the runtimes and registers what this machine "
            "has, any time");
        return result;
    }
    result.steps = bootstrap_plan(seams.host);
    const std::size_t count = result.steps.size();
    say("bootstrap: the runtimes this machine can run, then its providers and its model store, "
        "registered");
    std::size_t done = 0;
    for (std::size_t index = 0; index < count; ++index) {
        BootstrapStep& step = result.steps[index];
        const std::string head =
            "[" + std::to_string(index + 1) + "/" + std::to_string(count) + "] " + step.name;
        if (!step.runs()) {
            say(head + " -- skipped: " + step.reason);
            continue;
        }
        say(head + " -- " + step_line(step));
        std::vector<std::string> lines;
        const auto hand_on = [&](std::string line) {
            say("  " + line);
            lines.push_back(std::move(line));
        };
        int code = 0;
        std::string failed;
        if (step.external.empty()) {
            code = seams.run ? seams.run(step.words, hand_on) : -1;
        } else {
            // Another program's commands, in order: one that must succeed
            // ends the step when it fails; one that may fail does not.
            for (const RemedyCommand& command : step.external) {
                lines.clear();
                const int ended = seams.run_program ? seams.run_program(command.argv, hand_on) : -1;
                if (ended != 0 && !command.may_fail) {
                    code = ended;
                    failed = remedy_line({command}) + ": ";
                    break;
                }
            }
        }
        if (code == 0) {
            step.status = "done";
            ++done;
            continue;
        }
        // Never fatal: said, and the rest still run.
        const std::string last = last_said(lines);
        step.status = "skipped";
        step.reason = failed + (code < 0 ? (last.empty() ? std::string{"it could not start"} : last)
                                         : "exit " + std::to_string(code) +
                                               (last.empty() ? std::string{} : ": " + last));
        say("  skipped -- " + step.reason);
    }
    say("done: " + std::to_string(done) + " of " + std::to_string(count) + " steps, " +
        std::to_string(count - done) + " skipped -- " + std::string{kRerun});
    return result;
}

nlohmann::json bootstrap_document(const BootstrapResult& result) {
    nlohmann::json steps = nlohmann::json::array();
    for (const BootstrapStep& step : result.steps) {
        steps.push_back(
            {{"name", step.name},
             {"command", step.runs() ? nlohmann::json(step_line(step)) : nlohmann::json(nullptr)},
             {"status", step.status},
             {"reason",
              step.reason.empty() ? nlohmann::json(nullptr) : nlohmann::json(step.reason)}});
    }
    return {{"object", "apogee.bootstrap"},
            {"minimal", result.minimal},
            {"minimal_by", result.minimal_by.empty() ? nlohmann::json(nullptr)
                                                     : nlohmann::json(result.minimal_by)},
            {"steps", steps}};
}

bool minimal_install_env() {
    const char* value = std::getenv("APOGEE_MINIMAL_INSTALL");
    if (value == nullptr) {
        return false;
    }
    const std::string_view text{value};
    return !text.empty() && text != "0" && text != "false";
}

std::string_view BootstrapCommand::name() const noexcept {
    return "bootstrap";
}

std::string_view BootstrapCommand::summary() const noexcept {
    return "Install the runtimes this machine can run and register its providers and models -- "
           "what every install runs after check --fix";
}

void BootstrapCommand::bind(CLI::App& root, const RootContext& context) {
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    auto minimal = std::make_shared<bool>(false);
    cmd->add_flag("--minimal", *minimal,
                  "Skip it whole, said (as APOGEE_MINIMAL_INSTALL does) -- for CI and air-gapped "
                  "machines");
    auto format = std::make_shared<ReadFormat>(ReadFormat::Text);
    add_read_format(cmd, format);
    cmd->callback([&context, minimal, format]() {
        const bool json = *format == ReadFormat::Json;
        std::string minimal_by;
        if (*minimal) {
            minimal_by = "--minimal";
        } else if (minimal_install_env()) {
            minimal_by = "APOGEE_MINIMAL_INSTALL";
        }
        const std::filesystem::path binary = platform::executable_path();
        BootstrapSeams seams;
        if (minimal_by.empty()) {
            seams.host = detect_bootstrap_host(context);
        }
        seams.run = [&context, &binary](const std::vector<std::string>& words,
                                        const std::function<void(std::string)>& line) {
            return run_child_lines(self_command(context, binary, words), line);
        };
        seams.run_program = [](const std::vector<std::string>& argv,
                               const std::function<void(std::string)>& line) {
            platform::ChildCommand command;
            command.program = argv.front();
            command.arguments.assign(argv.begin() + 1, argv.end());
            return run_child_lines(command, line);
        };
        // The narration is the output; under JSON, the document is, and the
        // narration goes to stderr.
        std::ostream& narration = json ? std::cerr : std::cout;
        seams.say = [&narration](const std::string& line) {
            narration << line << "\n" << std::flush;
        };
        const BootstrapResult result = run_bootstrap(seams, minimal_by);
        if (json) {
            write_document(std::cout, bootstrap_document(result));
        }
    });
}

}  // namespace apogee::commands
