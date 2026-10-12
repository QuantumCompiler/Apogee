#pragma once

#include <string>
#include <string_view>
#include <vector>

/// A typed line split into a command's words (37f, the splitter 37h's exec
/// line runs and completes through): POSIX-style quoting with no expansion --
/// whitespace separates words; single quotes keep everything to the next
/// one; double quotes keep everything but `\"`, `\\`, `\$` and `` \` ``,
/// which they unescape; a backslash outside quotes takes the next character
/// as it is; `''` and `""` are an empty word. Nothing is globbed, expanded or
/// substituted: what is typed is what the command is given. One function,
/// so what completion saw is what runs.
namespace apogee::commands {

struct LineTokens {
    std::vector<std::string> words;
    /// The line ends past its last word -- a new, empty word is being typed
    /// (what completion asks about).
    bool trailing_space = false;
    /// Why the line cannot be split -- a quote left open, a backslash at the
    /// end -- or empty.
    std::string error;
};

[[nodiscard]] LineTokens line_tokens(std::string_view line);

}  // namespace apogee::commands
