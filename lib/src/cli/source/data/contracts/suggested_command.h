#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// A command Apogee prints for a person to copy (CLI-M48): a `run:` line
/// after a refusal, a `check` row's doors, a hint after a success. The
/// project promises each runs as printed, so it is rendered here, once,
/// never joined by hand: a name the CLI made -- a collection `-x` or `a;b`,
/// a symphony `-q`, a path under a home `it's home` -- reaches the verb it
/// names, in sh, bash, zsh, dash, the TUI's exec line (`line_tokens`) and
/// the Mac app's Console, which splits by the same table.
///
/// **Order.** A command keeps the order its site wrote whenever nothing
/// needs it changed, so a plain name prints today's bytes. Only for an
/// operand that starts with `-`, or an option with no limit on its values
/// sitting directly before an operand, are the options moved first and a
/// `--` added -- the way CLI11 2.4.2 reads them: options before `--`; a
/// `--` directly after such an option is eaten; an option's value parses
/// whole, so it never needs one.
///
/// **Words.** A word stays bare when only safe characters make it up;
/// otherwise it takes 53c's double quotes where every reader reads them back
/// whole, and single quotes everywhere else (`shell_word`).
///
/// **Program.** A command begins with the words of the build that printed
/// it and the root flag that chose its root (CLI-M49, `program_words()` in
/// `contracts/paths.h`): `apogee-dev` on the dev channel, `apogee --dev`
/// for a release binary run with `--dev`, so a pasted command acts on the
/// install that printed it. A release build with no flag prints `apogee`.
///
/// Text in, text out: beside the standard library it reads only
/// `contracts/paths.h`, the floor's own, so every layer's printing site
/// calls it directly.
namespace apogee::harness {

/// Which shell a person pastes into: the POSIX shells, or PowerShell --
/// the Windows build's, where `install.ps1` and `apogee.ps1` put a person.
/// The rule takes it as a parameter so either can be tested on any host.
enum class CommandShell : std::uint8_t { Posix, PowerShell };

/// The shell this build's people paste into: PowerShell on Windows, the
/// POSIX shells everywhere else.
[[nodiscard]] CommandShell native_command_shell() noexcept;

/// `word` as one word of a command a person copies, read back exactly by
/// sh, bash, zsh, dash and `line_tokens`:
/// - bare when it is non-empty, every character is in
///   `A-Za-z0-9_./:@%+=,-`, and it does not start with `=` (zsh's command
///   path) -- under PowerShell also no `,` (its array operator), no
///   leading `@` (its splat), and never `--%` (its stop-parsing token);
/// - `""` for the empty word;
/// - double quotes when it holds no `$`, backtick, `"`, `!` (an interactive
///   bash's and zsh's history) or control character, no doubled backslash
///   and no trailing backslash -- 53c's spelling, kept for every word it
///   quoted correctly;
/// - otherwise single quotes, a `'` written `'\''` (POSIX) or `''`
///   (PowerShell). PowerShell reads the typographic quotes as its own --
///   `“ ” „` as `"`, `‘ ’ ‚ ‛` as `'` -- so under it a word holding one is
///   single-quoted, each single one doubled as `'` is.
/// A dash-led word is bare: the order rule gives it its `--`.
[[nodiscard]] std::string shell_word(std::string_view word, CommandShell shell);
[[nodiscard]] std::string shell_word(std::string_view word);

/// One part of a command after its verbs: an operand, or an option with or
/// without a value. A placeholder (`<an existing .gguf>`, `"…"`, `...`)
/// prints verbatim -- it is filled by the person, never a word.
struct CommandPart {
    /// An option, else an operand.
    bool is_option = false;
    /// The option's flag (`--model-path`); empty for an operand.
    std::string flag{};
    /// The word: an operand's, or an option's value; none for a flag alone.
    std::optional<std::string> value{};
    /// The value printed verbatim, never quoted.
    bool placeholder = false;
    /// An option with no limit on its values (CLI11's vector options:
    /// `graph build --source`), which reads every word after it up to the
    /// next option, and eats a `--` that follows it directly.
    bool greedy = false;
};

/// A command: its program, the literal words that name the verb (`graph`,
/// `build`; `700` for chmod), then its parts in the order the site wrote.
struct SuggestedCommand {
    /// A program the command names itself (`chmod`), printed as written;
    /// empty for Apogee's own, which prints as `printed_program` says.
    std::string program{};
    std::vector<std::string> verbs{};
    std::vector<CommandPart> parts{};
};

/// An operand: one word, quoted as `shell_word` says.
[[nodiscard]] CommandPart operand(std::string_view word);

/// An operand the person fills in, printed verbatim (`<path>`).
[[nodiscard]] CommandPart placeholder(std::string_view text);

/// An option: `flag` alone, or with `value` as one word.
[[nodiscard]] CommandPart option(std::string_view flag, std::optional<std::string> value = {},
                                 bool greedy = false);

/// An option whose value the person fills in, printed verbatim
/// (`--model-path <an existing .gguf>`).
[[nodiscard]] CommandPart option_placeholder(std::string_view flag, std::string_view text,
                                             bool greedy = false);

/// The words an Apogee command a person copies starts with: this build's
/// name, then the root flag in force (`program_words()`), each through
/// `shell_word` -- `apogee`, `apogee-dev`, `apogee --dev`,
/// `apogee --custom "/a b/config/config.json"`. What `render` prints before
/// the verbs, and what a command whose every word is literal begins with.
[[nodiscard]] std::string printed_program(CommandShell shell);
[[nodiscard]] std::string printed_program();

/// `command` as one line a person copies, by the order and word rules.
[[nodiscard]] std::string render(const SuggestedCommand& command, CommandShell shell);
[[nodiscard]] std::string render(const SuggestedCommand& command);

/// A command line a person typed, said back as one line a person copies
/// (CLI-M61): `printed_program`, then `words` -- what `line_tokens` read from
/// the line, the full-screen shell's exec line or a view's typed ask -- each
/// through `shell_word`, in the person's own order. The order rule never
/// moves them: the person's own `--` already says which word is an operand.
[[nodiscard]] std::string render_typed(const std::vector<std::string>& words, CommandShell shell);
[[nodiscard]] std::string render_typed(const std::vector<std::string>& words);

/// Several commands rendered and joined by `joiner` (`" && "`).
[[nodiscard]] std::string render_all(const std::vector<SuggestedCommand>& commands,
                                     std::string_view joiner);

}  // namespace apogee::harness
