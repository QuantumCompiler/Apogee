#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "agent/tool.h"
#include "contracts/config.h"
#include "harness/harness.h"
#include "platform/child_process.h"
#include "tools/git.h"

/// The native toolsets, registered as one set.
///
/// Every surface that builds tools calls `register_native_toolsets` once;
/// nothing else decides which tools exist. Enablement is `tools.disabled` in
/// the config; safety is the permission gate, which every destructive tool
/// here declares itself into with `writes`.
namespace apogee::tools {

struct ToolsetOptions {
    /// The filesystem sandbox. Empty means the folder Apogee was started in.
    std::filesystem::path fs_root{};
    /// Where the shell runs and where the git walk starts. Empty means the
    /// process's working directory.
    std::filesystem::path working_directory{};
    /// Empty means the layout's `notes/`.
    std::filesystem::path notes_dir{};
    std::chrono::milliseconds shell_timeout{30000};
    std::chrono::milliseconds git_timeout{30000};
    std::size_t read_limit = 64 * 1024;
    /// The review defaults for `git_diff` and `git_log`, set by flags.
    ReviewDefaults review{};
    /// A live review the git tools read at call time instead; see
    /// `GitOptions::live_review`.
    std::shared_ptr<const ReviewDefaults> live_review{};
    /// For the RAG tools' embedder. Null means lexical-only searches.
    const harness::Harness* harness = nullptr;
    /// The RAG tools' collections and the graph tools' graphs (27l) -- must
    /// outlive the registry. Null reads as an empty config.
    const harness::Config* config = nullptr;
    /// Toolsets switched off, by name.
    std::vector<std::string> disabled;
    /// Whether this build can start other programs (57k). Without it no
    /// `run_command`, no git tools, and no shell in the environment note: a
    /// model is never offered a tool that can only fail (25e's rule), and no
    /// one is asked to approve a command that cannot run. The `shell` and
    /// `git` names stay valid toolset names -- the registry lacks them as it
    /// lacks `web_search` without an instance. The platform's answer by
    /// default; a test sets it to see either side on any host.
    bool child_processes = platform::supports_child_processes();
};

/// Where the filesystem tools are sandboxed, and whether the config said so.
struct FsRoot {
    std::filesystem::path path;
    /// `tools.fs_root` set it; otherwise it is the launch folder.
    bool from_config = false;
};

/// `configured` (`tools.fs_root`, already `${ENV}`-expanded) when set, else
/// the folder Apogee was started in. The one answer the toolsets and `check`
/// share.
[[nodiscard]] FsRoot effective_fs_root(const std::filesystem::path& configured);

/// The toolset names `tools.disabled` accepts: fs, shell, git, notes, rag,
/// graph.
[[nodiscard]] std::span<const std::string_view> toolset_names() noexcept;

/// The word of `harness::suite_toolset_names()` a registered tool falls
/// under (27d) -- its native toolset, `web` or `mcp` -- or empty for one none
/// of them names.
[[nodiscard]] std::string toolset_of(std::string_view tool);

/// Why the native tool `tool` is never offered by this build, or empty: a
/// `shell` or `git` tool where no program can be started (57k) -- what a
/// refusal of `--allow`, `--deny`, `/allow` or a task's grant naming it says,
/// rather than that the name is no tool. `child_processes` is the platform's
/// answer by default; a test sets it to see either side on any host.
[[nodiscard]] std::string why_not_offered(
    std::string_view tool, bool child_processes = platform::supports_child_processes());

/// The tools that declare `writes` -- the `permissions:` keys a config may
/// carry, and what the shipped template lists.
[[nodiscard]] std::span<const std::string_view> destructive_tool_names() noexcept;

/// The names of the tools a set of options would register, in registry order,
/// without registering them -- what `check` and a listing use.
[[nodiscard]] std::vector<std::string> native_tool_names(const ToolsetOptions& options);

void register_native_toolsets(agent::ToolRegistry& registry, const ToolsetOptions& options);

}  // namespace apogee::tools
