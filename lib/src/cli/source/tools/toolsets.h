#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "agent/tool.h"
#include "harness/config.h"
#include "harness/harness.h"
#include "tools/git.h"

/// The native toolsets, registered as one set.
///
/// Every surface that builds tools calls `register_native_toolsets` once;
/// nothing else decides which tools exist. Enablement is `tools.disabled` in
/// the config; safety is the permission gate, which every destructive tool
/// here declares itself into with `writes`.
namespace apogee::tools {

struct ToolsetOptions {
    /// The filesystem sandbox. Empty means the user's home directory.
    std::filesystem::path fs_root;
    /// Where the shell runs and where the git walk starts. Empty means the
    /// process's working directory.
    std::filesystem::path working_directory;
    /// Empty means the layout's `notes/`.
    std::filesystem::path notes_dir;
    std::chrono::milliseconds shell_timeout{30000};
    std::chrono::milliseconds git_timeout{30000};
    std::size_t read_limit = 64 * 1024;
    /// The review defaults for `git_diff`, set by flags.
    ReviewDefaults review;
    /// For the RAG tools' embedder. Null means lexical-only searches.
    const harness::Harness* harness = nullptr;
    const harness::Config* config = nullptr;
    /// Toolsets switched off, by name.
    std::vector<std::string> disabled;
};

/// The toolset names `tools.disabled` accepts: fs, shell, git, notes, rag.
[[nodiscard]] std::span<const std::string_view> toolset_names() noexcept;

/// The tools that declare `writes` -- the `permissions:` keys a config may
/// carry, and what the shipped template lists.
[[nodiscard]] std::span<const std::string_view> destructive_tool_names() noexcept;

/// The names of the tools a set of options would register, in registry order,
/// without registering them -- what `check` and a listing use.
[[nodiscard]] std::vector<std::string> native_tool_names(const ToolsetOptions& options);

void register_native_toolsets(agent::ToolRegistry& registry, const ToolsetOptions& options);

}  // namespace apogee::tools
