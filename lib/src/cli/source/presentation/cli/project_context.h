#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// Execute's project opening (41b): a session opened inside a project
/// arrives knowing it -- its context file read into every request, and its
/// folder attached through the one attach driver.
///
/// **A project** is the folder the session opened in when it is a git
/// repository (a `.git` there) or holds a context file. Nothing above it is
/// probed: the detection is the folder itself, cheap and local, and a
/// subfolder of a repository opens as any other folder does.
///
/// **The context file** is `AGENTS.md`, else `CLAUDE.md`, first found wins --
/// read, never written: nothing in Apogee writes either. Its text is mended
/// to UTF-8 where it is read, and capped at `kProjectContextWords`, the head
/// kept and the cut said, never silent. It rides every request as transient
/// system context, as the environment note does, and is never saved into
/// the conversation.
namespace apogee::commands {

/// The context files a project opening reads, in the order looked for.
inline constexpr std::string_view kProjectContextFiles[] = {"AGENTS.md", "CLAUDE.md"};

/// The most of a context file a request carries: a few thousand words, the
/// head kept past it.
inline constexpr std::size_t kProjectContextWords = 4000;

/// A context file as a session carries it.
struct ProjectContext {
    /// The file read, absolute.
    std::filesystem::path file;
    /// Its text, mended to UTF-8 and cut to the cap.
    std::string text;
    /// Words carried, and the words the file holds.
    std::size_t words = 0;
    std::size_t total_words = 0;

    [[nodiscard]] bool truncated() const noexcept {
        return words < total_words;
    }
};

/// What opening a session in `dir` finds.
struct ProjectOpening {
    /// The folder is a project: a git repository, or a context file there.
    bool project = false;
    /// Its context file, when it holds one that could be read.
    std::optional<ProjectContext> context;
    /// A context file that is there and could not be read, said rather than
    /// skipped in silence.
    std::string unreadable;
};

/// The opening for a session started in `dir`.
[[nodiscard]] ProjectOpening read_project_opening(const std::filesystem::path& dir);

/// The system message `context` rides as: where it came from, then its
/// text, then -- when it was cut -- what was left out.
[[nodiscard]] std::string project_context_note(const ProjectContext& context);

/// The banner's line for `context`: `project context: AGENTS.md, 1,234 words,
/// in every request`, or the cut said with the counts.
[[nodiscard]] std::string project_context_line(const ProjectContext& context);

/// What a project opening attaches (41b), given the launch's `--attach`
/// values: in a project with none given, the working folder (`.`); an empty
/// value (`--attach ""`) skips that, and is never attached itself; any
/// other value given replaces it. Outside a project, what was given, less
/// the empty values.
[[nodiscard]] std::vector<std::string> opening_attaches(const std::vector<std::string>& given,
                                                        bool project);

}  // namespace apogee::commands
