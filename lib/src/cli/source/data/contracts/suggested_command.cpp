#include "contracts/suggested_command.h"

#include <algorithm>
#include <array>
#include <span>
#include <utility>

#include "contracts/paths.h"

namespace apogee::harness {
namespace {

/// The characters no reader treats specially anywhere in a word.
constexpr std::string_view kBare =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_./:@%+=,-";

/// The typographic quotes PowerShell reads as its own (UTF-8): `“ ” „` end
/// a double-quoted string, `‘ ’ ‚ ‛` a single-quoted one.
constexpr std::array<std::string_view, 3> kPowerShellDoubleQuotes{"\xE2\x80\x9C", "\xE2\x80\x9D",
                                                                  "\xE2\x80\x9E"};
constexpr std::array<std::string_view, 4> kPowerShellSingleQuotes{"\xE2\x80\x98", "\xE2\x80\x99",
                                                                  "\xE2\x80\x9A", "\xE2\x80\x9B"};

[[nodiscard]] bool holds_any(std::string_view word, std::span<const std::string_view> marks) {
    return std::ranges::any_of(
        marks, [word](std::string_view mark) { return word.find(mark) != std::string_view::npos; });
}

[[nodiscard]] bool bare(std::string_view word, CommandShell shell) {
    if (word.empty() || word.find_first_not_of(kBare) != std::string_view::npos ||
        word.front() == '=') {
        return false;
    }
    return shell != CommandShell::PowerShell ||
           (word.find(',') == std::string_view::npos && word.front() != '@' && word != "--%");
}

/// Whether double quotes read `word` back whole in every reader: nothing a
/// double-quoted string expands or ends on, no `!` an interactive shell
/// expands there, no control character, and no backslash that would escape
/// the next one or the closing quote.
[[nodiscard]] bool double_quotable(std::string_view word) {
    const bool control = std::ranges::any_of(word, [](const char c) {
        const auto byte = static_cast<unsigned char>(c);
        return byte < 0x20 || byte == 0x7f;
    });
    return !control && word.find_first_of("$`\"!") == std::string_view::npos &&
           word.find("\\\\") == std::string_view::npos && !word.ends_with('\\');
}

[[nodiscard]] bool dash_led(const CommandPart& part) {
    return !part.is_option && !part.placeholder && part.value.has_value() &&
           part.value->starts_with('-');
}

[[nodiscard]] std::string spelled(const CommandPart& part, CommandShell shell) {
    if (!part.value.has_value()) {
        return {};
    }
    return part.placeholder ? *part.value : shell_word(*part.value, shell);
}

void append(std::string& line, const CommandPart& part, CommandShell shell) {
    if (part.is_option) {
        line += " " + part.flag;
        if (part.value.has_value()) {
            line += " " + spelled(part, shell);
        }
        return;
    }
    line += " " + spelled(part, shell);
}

}  // namespace

CommandShell native_command_shell() noexcept {
#if defined(_WIN32)
    return CommandShell::PowerShell;
#else
    return CommandShell::Posix;
#endif
}

std::string shell_word(std::string_view word, CommandShell shell) {
    if (word.empty()) {
        return "\"\"";
    }
    if (bare(word, shell)) {
        return std::string{word};
    }
    const bool powershell = shell == CommandShell::PowerShell;
    if (double_quotable(word) && !(powershell && (holds_any(word, kPowerShellDoubleQuotes) ||
                                                  holds_any(word, kPowerShellSingleQuotes)))) {
        return "\"" + std::string{word} + "\"";
    }
    std::string out = "'";
    for (std::size_t at = 0; at < word.size(); ++at) {
        if (word[at] == '\'') {
            out += powershell ? "''" : "'\\''";
            continue;
        }
        // PowerShell reads a typographic single quote as `'`: doubled too.
        if (powershell) {
            const auto mark = std::ranges::find_if(
                kPowerShellSingleQuotes,
                [&](std::string_view each) { return word.substr(at).starts_with(each); });
            if (mark != kPowerShellSingleQuotes.end()) {
                out += std::string{*mark} + std::string{*mark};
                at += mark->size() - 1;
                continue;
            }
        }
        out += word[at];
    }
    return out + "'";
}

std::string shell_word(std::string_view word) {
    return shell_word(word, native_command_shell());
}

CommandPart operand(std::string_view word) {
    return CommandPart{.value = std::string{word}};
}

CommandPart placeholder(std::string_view text) {
    return CommandPart{.value = std::string{text}, .placeholder = true};
}

CommandPart option(std::string_view flag, std::optional<std::string> value, bool greedy) {
    return CommandPart{
        .is_option = true, .flag = std::string{flag}, .value = std::move(value), .greedy = greedy};
}

CommandPart option_placeholder(std::string_view flag, std::string_view text, bool greedy) {
    return CommandPart{.is_option = true,
                       .flag = std::string{flag},
                       .value = std::string{text},
                       .placeholder = true,
                       .greedy = greedy};
}

std::string printed_program(CommandShell shell) {
    std::string out;
    for (const std::string& word : program_words()) {
        out += (out.empty() ? "" : " ") + shell_word(word, shell);
    }
    return out;
}

std::string printed_program() {
    return printed_program(native_command_shell());
}

std::string render(const SuggestedCommand& command, CommandShell shell) {
    std::string line = command.program.empty() ? printed_program(shell) : command.program;
    for (const std::string& verb : command.verbs) {
        line += " " + verb;
    }
    const std::vector<CommandPart>& parts = command.parts;
    const bool dashed = std::ranges::any_of(parts, dash_led);
    bool eaten = false;
    for (std::size_t at = 0; at + 1 < parts.size(); ++at) {
        eaten = eaten || (parts[at].is_option && parts[at].greedy && !parts[at + 1].is_option);
    }
    if (!dashed && !eaten) {
        for (const CommandPart& part : parts) {
            append(line, part, shell);
        }
        return line;
    }
    // The options first, in their order; a `--` CLI11 eats after a greedy
    // last one; a `--` before a dash-led operand; then the operands.
    bool last_greedy = false;
    for (const CommandPart& part : parts) {
        if (part.is_option) {
            append(line, part, shell);
            last_greedy = part.greedy;
        }
    }
    if (last_greedy) {
        line += " --";
    }
    if (dashed) {
        line += " --";
    }
    for (const CommandPart& part : parts) {
        if (!part.is_option) {
            append(line, part, shell);
        }
    }
    return line;
}

std::string render(const SuggestedCommand& command) {
    return render(command, native_command_shell());
}

std::string render_typed(const std::vector<std::string>& words, CommandShell shell) {
    std::string line = printed_program(shell);
    for (const std::string& word : words) {
        line += " " + shell_word(word, shell);
    }
    return line;
}

std::string render_typed(const std::vector<std::string>& words) {
    return render_typed(words, native_command_shell());
}

std::string render_all(const std::vector<SuggestedCommand>& commands, std::string_view joiner) {
    std::string out;
    for (const SuggestedCommand& command : commands) {
        out += (out.empty() ? "" : std::string{joiner}) + render(command);
    }
    return out;
}

}  // namespace apogee::harness
