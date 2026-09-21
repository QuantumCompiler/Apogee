#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "agent/tool.h"

/// The filesystem toolset: read, write, delete, list, search -- inside a root.
///
/// Ported from Ommi's `ommi-mcp-fs`, with one correction. Ommi's sandbox was
/// `realpath(path).startswith(ROOT)`, a string comparison: a root of
/// `/home/user` admitted `/home/userX`. Here the root has to be an ancestor by
/// **path components**, after symlinks are followed on both sides.
namespace apogee::tools {

inline constexpr std::size_t kDefaultReadLimit = 64 * 1024;

/// Resolves `path` -- absolute, `~`-prefixed, or relative to `root` -- to a
/// canonical path inside `root`, or nullopt with `error` naming the root.
/// A path that does not exist yet resolves through its longest existing
/// prefix, so a file about to be written is judged by where it will land.
[[nodiscard]] std::optional<std::filesystem::path> resolve_in_root(
    const std::filesystem::path& root, std::string_view path, std::string& error);

/// Whether `name` matches a shell-style pattern (`*`, `?`, `[abc]`).
[[nodiscard]] bool wildcard_match(std::string_view pattern, std::string_view name);

/// Registers the five filesystem tools, sandboxed to `root`. `write_file`
/// and `delete_file` declare `writes`; the rest never prompt.
void register_fs_tools(agent::ToolRegistry& registry, const std::filesystem::path& root,
                       std::size_t read_limit = kDefaultReadLimit);

}  // namespace apogee::tools
