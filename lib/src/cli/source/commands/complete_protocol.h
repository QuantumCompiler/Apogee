#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "commands/command.h"
#include "harness/config.h"

/// The hidden `apogee __complete` verb — the shell-completion protocol.
///
/// **Why a verb rather than static completion files.** The interesting things
/// to complete are not fixed: backend names come from the user's config, and a
/// completion file generated at build time cannot know them. Ommi's completions
/// worked this way (cobra's protocol) for exactly that reason, and the small
/// per-shell stubs Apogee installs do nothing but call back into the binary —
/// so `apogee complete -m <TAB>` offers the backends this user actually has,
/// and keeps working after they add one.
///
/// **Output contract.** One candidate per line on stdout, nothing else. Errors
/// are silent and produce no candidates: a completion handler that prints a
/// diagnostic corrupts the user's command line, and a broken config should make
/// tab-completion unhelpful, never disruptive.
namespace apogee::commands {

/// What is being completed, derived from the words before the cursor.
struct CompletionRequest {
    /// Words after the program name, up to but excluding the one being typed.
    std::vector<std::string> words;

    /// The partial word under the cursor. May be empty.
    std::string current;
};

/// What the parser knows about one subcommand: its name and its flags.
///
/// Gathered from the live `CLI::App` tree rather than restated here. A hand-kept
/// flag list is the same drift the layout contract exists to prevent, one level
/// down: a flag added to a command would silently stop completing, and nobody
/// files a bug about tab-completion being slightly less helpful.
struct CommandSpec {
    std::string name;
    /// Every accepted spelling, `-m` and `--model` alike, dashes included.
    std::vector<std::string> flags;
};

/// Candidates for `request`, given `config` and what the parser knows.
///
/// Pure: takes the config and the command specs rather than reading either, so
/// the whole protocol is testable without a config file or a parser.
[[nodiscard]] std::vector<std::string> completion_candidates(
    const CompletionRequest& request, const harness::Config& config,
    const std::vector<CommandSpec>& commands);

/// Reads the command tree out of a parser.
[[nodiscard]] std::vector<CommandSpec> specs_from_app(const CLI::App& app);

/// Filters `candidates` to those starting with `prefix`.
[[nodiscard]] std::vector<std::string> filter_prefix(const std::vector<std::string>& candidates,
                                                     std::string_view prefix);

class CompleteProtocolCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
