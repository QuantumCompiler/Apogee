#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>


/// The exec line (37h): the whole command language from inside the shell,
/// through one input. `:` opens it on any view that is not typing, scoped to
/// the view's group (`View::group`: `:pull …` in Models runs `models pull
/// …`); `!` opens it unscoped. What is typed is the command minus the
/// leading `apogee`; Tab offers what the binary's one completion core offers
/// for the word under the cursor; Enter runs it -- or says why the shell
/// refuses it -- its output streamed through the progress seam with its exit
/// line; Ctrl-C stops the running command and nothing else; Esc closes the
/// line, a run going on behind it, and `:` shows it again.
///
/// `tui/` paints and keys it; what completes a line and what runs it are the
/// composition root's (`cli/tui_runner`), handed in here.
namespace apogee::tui {

class Progress;

struct ExecLineOptions {
    /// The candidates for the word under the cursor of `line`, typed under
    /// `scope` (the words the view's group prefixes; empty for none).
    std::function<std::vector<std::string>(const std::string& scope, const std::string& line)>
        complete;
    /// Runs `line` under `scope`, its output into `output`: what to say -- that
    /// it runs, or why it does not.
    std::function<std::string(const std::string& scope, const std::string& line)> run;
    /// Where a run's output is streamed and from which it is drawn.
    std::shared_ptr<Progress> output;
};

}  // namespace apogee::tui
