#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "agent/tool.h"

/// The filesystem toolset: read, write, edit, delete, list, find and search
/// contents -- inside a root.
///
/// The root has to be an ancestor by **path components**, after symlinks are
/// followed on both sides. A string-prefix comparison is not enough: a root of
/// `/home/user` would admit `/home/userX`.
namespace apogee::tools {

inline constexpr std::size_t kDefaultReadLimit = 64 * 1024;

/// What `grep_files` returns at most: matches, and bytes of them (25d).
/// Past either it says how many more there were.
inline constexpr std::size_t kGrepMaxMatches = 100;
inline constexpr std::size_t kGrepMaxBytes = 16 * 1024;
/// How much of one line a match shows, and how much of it is searched.
inline constexpr std::size_t kGrepLineShown = 240;
inline constexpr std::size_t kGrepLineSearchLimit = 4 * 1024;

/// Resolves `path` -- absolute, `~`-prefixed, or relative to `root` -- to a
/// canonical path inside `root`, or nullopt with `error` naming the root.
/// A path that does not exist yet resolves through its longest existing
/// prefix, so a file about to be written is judged by where it will land.
[[nodiscard]] std::optional<std::filesystem::path> resolve_in_root(
    const std::filesystem::path& root, std::string_view path, std::string& error);

/// Whether `name` matches a shell-style pattern (`*`, `?`, `[abc]`).
[[nodiscard]] bool wildcard_match(std::string_view pattern, std::string_view name);

/// Registers the seven filesystem tools, sandboxed to `root`. `write_file`,
/// `edit_file` and `delete_file` declare `writes`; the rest never prompt.
void register_fs_tools(agent::ToolRegistry& registry, const std::filesystem::path& root,
                       std::size_t read_limit = kDefaultReadLimit);

}  // namespace apogee::tools
