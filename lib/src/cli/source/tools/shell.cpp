#include "tools/shell.h"

#include <system_error>

#include "platform/child_process.h"
#include "tools/args.h"
#include "tools/process.h"

namespace apogee::tools {
namespace {

std::string rstrip(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' ||
                             text.back() == '\t')) {
        text.pop_back();
    }
    return text;
}

platform::ChildCommand shell_command(std::string_view command, const std::filesystem::path& cwd) {
    platform::ChildCommand child;
#ifdef _WIN32
    child.program = "cmd";
    child.arguments = {"/C", "cd /d \"" + cwd.string() + "\" && " + std::string{command}};
#else
    // The directory and the command travel as positional parameters, never
    // spliced into the script: nothing in either can break out of its quotes.
    // The positionals are cleared before the command runs, so a command that
    // reads `$1` sees what a shell would normally give it -- nothing.
    child.program = "/bin/sh";
    child.arguments = {"-c",
                       "cd \"$1\" && __apogee_command=$2 && set -- && eval \"$__apogee_command\"",
                       "apogee-shell", cwd.string(), std::string{command}};
#endif
    return child;
}

}  // namespace

ShellResult run_shell(std::string_view command, const std::filesystem::path& cwd,
                      std::chrono::milliseconds timeout) {
    ShellResult result;
    const ProcessOutcome outcome = run_to_completion(shell_command(command, cwd), timeout);
    if (!outcome.start_error.empty()) {
        result.failed_to_start = true;
        result.rendered = outcome.start_error;
        return result;
    }
    if (outcome.timed_out) {
        result.timed_out = true;
        result.rendered =
            "[timed out after " +
            std::to_string(std::chrono::duration_cast<std::chrono::seconds>(timeout).count()) +
            "s]";
        return result;
    }
    std::string rendered;
    if (!outcome.out.empty()) {
        rendered += rstrip(outcome.out);
    }
    if (!outcome.err.empty()) {
        rendered += (rendered.empty() ? "" : "\n");
        rendered += "[stderr]\n" + rstrip(outcome.err);
    }
    rendered += (rendered.empty() ? "" : "\n");
    rendered += "[exit " + std::to_string(outcome.exit_code.value_or(-1)) + "]";
    result.rendered = std::move(rendered);
    return result;
}

void register_shell_tool(agent::ToolRegistry& registry, const std::filesystem::path& default_cwd,
                         std::chrono::milliseconds timeout) {
    agent::Tool tool;
    tool.name = "run_command";
    tool.description =
        "Run a shell command and return its output. Commands run through the system shell "
        "with a " +
        std::to_string(std::chrono::duration_cast<std::chrono::seconds>(timeout).count()) +
        "-second timeout. Default working directory: " + default_cwd.string() +
        ". This operation requires the user's permission.";
    tool.parameters_schema =
        R"({"type":"object","properties":{"command":{"type":"string","description":"The command line to run"},"cwd":{"type":"string","description":"Working directory; defaults to the current one"}},"required":["command"]})";
    tool.writes = true;
    tool.describe_target = [](std::string_view arguments) {
        agent::ToolOutcome unused;
        const std::optional<Arguments> args = parse_arguments(arguments, "", unused);
        if (!args.has_value()) {
            return std::string{};
        }
        std::string command = args->string("command");
        if (command.size() > 80) {
            command = command.substr(0, 77) + "...";
        }
        return command;
    };
    tool.run = [default_cwd, timeout](std::string_view arguments) -> agent::ToolOutcome {
        agent::ToolOutcome failure;
        const std::optional<Arguments> args =
            parse_arguments(arguments, R"({"command": "ls -la"})", failure);
        if (!args.has_value()) {
            return failure;
        }
        const std::string command = args->string("command");
        if (command.empty()) {
            return error("command is required");
        }
        std::filesystem::path cwd = default_cwd;
        if (const std::string given = args->string("cwd"); !given.empty()) {
            cwd = std::filesystem::path{given};
        }
        std::error_code code;
        if (!std::filesystem::is_directory(cwd, code)) {
            return error("cwd " + cwd.string() + " is not a directory");
        }
        const ShellResult result = run_shell(command, cwd, timeout);
        if (result.failed_to_start) {
            return error(result.rendered);
        }
        return ok(result.rendered);
    };
    registry.add(tool);
}

}  // namespace apogee::tools
