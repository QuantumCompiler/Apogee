#include "cli/bootstrap_cmd.h"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "cli/helpers.h"
#include "cli/tui_child.h"
#include "machine/json_reporter.h"
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

}  // namespace

std::vector<BootstrapStep> bootstrap_plan(bool apple_silicon, bool nvidia_smi) {
    std::vector<BootstrapStep> out;
    // The table's own sets, in its order: a set added there joins here.
    for (const std::string_view name : training::requirement_set_names()) {
        const std::optional<training::RequirementSet> set =
            training::requirement_set_from_string(name);
        BootstrapStep step{.name = "the '" + std::string{name} + "' set"};
        if (std::string unfit = training::set_unfit_reason(*set, apple_silicon, nvidia_smi);
            !unfit.empty()) {
            step.status = "skipped";
            step.reason = std::move(unfit);
        } else {
            step.words = {"train", "setup", "--with", std::string{name}};
        }
        out.push_back(std::move(step));
    }
    out.push_back({.name = "the providers", .words = {"providers", "scan", "--register"}});
    out.push_back({.name = "the model store", .words = {"config", "scan", "--register"}});
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
    result.steps = bootstrap_plan(seams.apple_silicon, seams.nvidia_smi);
    const std::size_t count = result.steps.size();
    say("bootstrap: the runtimes this machine can run, then its providers and its model store, "
        "registered");
    std::size_t done = 0;
    for (std::size_t index = 0; index < count; ++index) {
        BootstrapStep& step = result.steps[index];
        const std::string head =
            "[" + std::to_string(index + 1) + "/" + std::to_string(count) + "] " + step.name;
        if (step.words.empty()) {
            say(head + " -- skipped: " + step.reason);
            continue;
        }
        say(head + " -- " + command_line(step.words));
        std::vector<std::string> lines;
        const int code = seams.run ? seams.run(step.words,
                                               [&](std::string line) {
                                                   say("  " + line);
                                                   lines.push_back(std::move(line));
                                               })
                                   : -1;
        if (code == 0) {
            step.status = "done";
            ++done;
            continue;
        }
        // Never fatal: said, and the rest still run.
        const std::string last = last_said(lines);
        step.status = "skipped";
        step.reason = code < 0 ? (last.empty() ? std::string{"it could not start"} : last)
                               : "exit " + std::to_string(code) +
                                     (last.empty() ? std::string{} : ": " + last);
        say("  skipped -- " + step.reason);
    }
    say("done: " + std::to_string(done) + " of " + std::to_string(count) + " steps, " +
        std::to_string(count - done) + " skipped -- " + std::string{kRerun});
    return result;
}

nlohmann::json bootstrap_document(const BootstrapResult& result) {
    nlohmann::json steps = nlohmann::json::array();
    for (const BootstrapStep& step : result.steps) {
        steps.push_back({{"name", step.name},
                         {"command", step.words.empty() ? nlohmann::json(nullptr)
                                                        : nlohmann::json(command_line(step.words))},
                         {"status", step.status},
                         {"reason", step.reason.empty() ? nlohmann::json(nullptr)
                                                        : nlohmann::json(step.reason)}});
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
        const training::HostShape host = training::detect_host();
        const std::filesystem::path binary = platform::executable_path();
        BootstrapSeams seams;
        seams.apple_silicon = host.apple_silicon;
        seams.nvidia_smi = host.nvidia_smi;
        seams.run = [&context, &binary](const std::vector<std::string>& words,
                                        const std::function<void(std::string)>& line) {
            return run_child_lines(self_command(context, binary, words), line);
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
