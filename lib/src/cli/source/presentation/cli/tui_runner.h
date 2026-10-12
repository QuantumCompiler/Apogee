#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "cli/command.h"
#include "tui/runner_view.h"

/// The command runner (37h): what the shell's exec line completes and runs.
/// A typed line is the command minus the leading `apogee`, split once by the
/// one splitter (`cli/line_tokens`) for both paths, so what Tab saw is what
/// runs:
///
/// - completed by the binary's one completion core -- the answer `apogee
///   __complete` gives for the same words, over the same live command tree,
///   config and data directory;
/// - checked against the law's refusals (`shell_refusals`, 37a's table, one
///   source): the doors the shell already is, the `$EDITOR` verbs, and the
///   three that would pull the ground from under it, each refused naming its
///   reason;
/// - run as a captured child of the shell's own binary (`cli/tui_child`):
///   the shell's root carried, stdin closed, its output streamed with its
///   exit line, byte for byte what a script on a pipe sees.
namespace apogee::commands {

/// The words a line runs under `scope` -- the scope's words, then the line's
/// -- or why it does not run: a line that cannot be split, nothing typed, or
/// a refusal from the law's table.
struct ExecWords {
    std::vector<std::string> words;
    std::string refusal;
};

[[nodiscard]] ExecWords exec_words(const std::string& scope, const std::string& line);

/// Why the shell refuses `words` -- a refusal the law's table records, after
/// any root flags -- or empty.
[[nodiscard]] std::string exec_refusal(const std::vector<std::string>& words);

/// What Tab offers for the word under the cursor of `line` under `scope`:
/// the completion core's candidates for those words, as `__complete` gives
/// them.
[[nodiscard]] std::vector<std::string> exec_candidates(const RootContext& context,
                                                       const std::string& scope,
                                                       const std::string& line);

/// The exec line's options: completing and running as above, a run streamed
/// into `output`, the child `binary`.
[[nodiscard]] tui::ExecLineOptions exec_line_options(const RootContext& context,
                                                     std::shared_ptr<tui::Progress> output,
                                                     std::filesystem::path binary);

}  // namespace apogee::commands
