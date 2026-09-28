#include "tools/shell.h"

#include <algorithm>
#include <cstdint>
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
    child.program = std::string{shell_program()};
#ifdef _WIN32
    child.arguments = {"/C", "cd /d \"" + cwd.string() + "\" && " + std::string{command}};
#else
    // The directory and the command travel as positional parameters, never
    // spliced into the script: nothing in either can break out of its quotes.
    // The positionals are cleared before the command runs, so a command that
    // reads `$1` sees what a shell would normally give it -- nothing.
    child.arguments = {"-c",
                       "cd \"$1\" && __apogee_command=$2 && set -- && eval \"$__apogee_command\"",
                       "apogee-shell", cwd.string(), std::string{command}};
#endif
    return child;
}

/// A byte that continues a UTF-8 sequence rather than starting one.
bool continues_character(char byte) {
    return (static_cast<unsigned char>(byte) & 0xC0U) == 0x80U;
}

/// The rendered output as it would be whole: literal text, and the gaps
/// where a stream's middle was never kept. Only the ends are ever needed,
/// and the runner keeps each stream's first and last `keep` bytes -- so
/// the first and last `keep` bytes of this are always literal.
struct Rendering {
    std::string front;  // the literal text before the first gap
    std::string back;   // the literal text after the last gap
    std::uint64_t total = 0;
    bool gapped = false;

    void text(const std::string& piece) {
        if (!gapped) {
            front += piece;
        }
        back += piece;
        total += piece.size();
    }

    void gap(std::uint64_t bytes) {
        gapped = true;
        back.clear();
        total += bytes;
    }

    void stream(const CapturedOutput& output) {
        if (output.omitted() == 0) {
            text(rstrip(output.text()));
            return;
        }
        text(output.head());
        gap(output.omitted());
        text(rstrip(output.tail()));
    }
};

/// How many bytes the character a UTF-8 lead byte starts takes; 1 for a
/// byte that is not one, so malformed output still makes progress.
std::size_t character_length(char byte) {
    const auto value = static_cast<unsigned char>(byte);
    if ((value & 0xE0U) == 0xC0U) {
        return 2;
    }
    if ((value & 0xF0U) == 0xE0U) {
        return 3;
    }
    if ((value & 0xF8U) == 0xF0U) {
        return 4;
    }
    return 1;
}

/// The first `keep` bytes of `text`, ended at the last line break in their
/// second half, or else before the character the cut would split: a seam in
/// the middle of a line is harder to read, and one in the middle of a
/// character is not text at all. `text` may itself end where a stream's
/// middle was dropped, so its own end is judged the same way.
std::string head_of(std::string_view text, std::size_t keep) {
    const std::size_t span = std::min(keep, text.size());
    const std::size_t newline = text.substr(0, span).rfind('\n');
    if (newline != std::string_view::npos && newline >= span / 2) {
        return std::string{text.substr(0, newline)};
    }
    std::size_t cut = span;
    if (cut < text.size()) {
        while (cut > 0 && continues_character(text[cut])) {
            --cut;
        }
    } else {
        // Nothing follows to judge by: drop a last character left unfinished.
        std::size_t lead = cut;
        while (lead > 0 && continues_character(text[lead - 1])) {
            --lead;
        }
        if (lead > 0 && lead - 1 + character_length(text[lead - 1]) > cut) {
            cut = lead - 1;
        }
    }
    return std::string{text.substr(0, cut)};
}

/// The last `keep` bytes of `text` -- all of it when shorter, since it may
/// begin where a stream's middle was dropped -- started after the first
/// line break in their first half, or else at the first whole character.
std::string tail_of(std::string_view text, std::size_t keep) {
    std::size_t start = text.size() > keep ? text.size() - keep : 0;
    const std::size_t span = text.size() - start;
    const std::size_t newline = text.find('\n', start);
    if (newline != std::string_view::npos && newline - start < span / 2) {
        return std::string{text.substr(newline + 1)};
    }
    while (start < text.size() && continues_character(text[start])) {
        ++start;
    }
    return std::string{text.substr(start)};
}

}  // namespace

std::string_view shell_program() noexcept {
#ifdef _WIN32
    return "cmd";
#else
    return "/bin/sh";
#endif
}

std::string render_command_output(const ProcessOutcome& outcome, std::size_t keep) {
    Rendering rendering;
    if (!outcome.out.empty()) {
        rendering.stream(outcome.out);
    }
    if (!outcome.err.empty()) {
        rendering.text(rendering.total == 0 ? "[stderr]\n" : "\n[stderr]\n");
        rendering.stream(outcome.err);
    }
    rendering.text((rendering.total == 0 ? "" : "\n") + std::string{"[exit "} +
                   std::to_string(outcome.exit_code.value_or(-1)) + "]");

    if (!rendering.gapped && rendering.total <= 2 * keep) {
        return rendering.front;
    }
    const std::string head = head_of(rendering.front, keep);
    const std::string tail = tail_of(rendering.back, keep);
    const std::uint64_t omitted = rendering.total - head.size() - tail.size();
    return head + "\n[... " + std::to_string(omitted) +
           " bytes omitted: run_command keeps the first and last " + std::to_string(keep / 1024) +
           " KB of output. To see the rest, run it again writing to a file (command > out.txt "
           "2>&1), then read that with read_file's offset and limit, or search it with "
           "grep_files ...]\n" +
           tail;
}

ShellResult run_shell(std::string_view command, const std::filesystem::path& cwd,
                      std::chrono::milliseconds timeout, std::size_t keep) {
    ShellResult result;
    const ProcessOutcome outcome =
        run_to_completion(shell_command(command, cwd), timeout, OutputLimit{keep, keep});
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
    result.rendered = render_command_output(outcome, keep);
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
        "-second timeout. Output longer than " + std::to_string(2 * kCommandOutputKeep / 1024) +
        " KB keeps only its first and last " + std::to_string(kCommandOutputKeep / 1024) +
        " KB: redirect a long output to a file and read that in ranges. Default working "
        "directory: " +
        default_cwd.string() + ". This operation requires the user's permission.";
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
