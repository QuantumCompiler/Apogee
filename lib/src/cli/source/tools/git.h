#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "agent/tool.h"

/// The git toolset: status, log, diff, show -- each a `git` child.
///
/// Ported from Ommi's `ommi-mcp-git`. Every argument that reaches `git`
/// travels in an argument list, never through a shell, so quoting is not a
/// risk; **option injection** is, and the ref allow-list -- a token that
/// starts with a letter or digit -- is what closes it.
///
/// `git_diff` carries the deterministic review form the analyze item will
/// pass parameters into: `base...head` (the merge-base diff, only what the
/// head adds), with the refs resolved against a remote and fetched on demand
/// when they are absent locally. The defaults for that form come from
/// `ReviewDefaults`, set by flags, never inferred from the model's text.
namespace apogee::tools {

/// How a review diff resolves when the model passes no `head`/`base`.
struct ReviewDefaults {
    /// The branch under review; empty means no review context.
    std::string head;
    /// The base; empty means the repository's default branch.
    std::string base;
    std::string remote = "origin";
    /// `auto` (fetch only when a ref is absent locally), `always`, or
    /// `never`.
    std::string fetch = "auto";

    [[nodiscard]] bool active() const noexcept {
        return !head.empty() || !base.empty();
    }
};

struct GitOptions {
    std::chrono::milliseconds timeout{30000};
    /// Where the walk for a `.git` directory starts when no `repo` is given.
    std::filesystem::path working_directory;
    ReviewDefaults review;
    /// When set, read at every call instead of `review`, so a surface that
    /// changes the review mid-session (`chat`'s `/branch`) re-points the
    /// tools without rebuilding the registry -- and without re-dialling
    /// every MCP server that lives in it. Still a plain in-process value:
    /// no environment side channel.
    std::shared_ptr<const ReviewDefaults> live_review;
};

/// Whether `ref` is a safe git ref token: letters, digits, `.`, `_`, `/`,
/// `-`, not starting with `-`.
[[nodiscard]] bool valid_ref(std::string_view ref) noexcept;

/// The repository for `hint`: `hint` itself when given, else the nearest
/// ancestor of `start` holding `.git`, else `start`.
[[nodiscard]] std::filesystem::path find_repo(std::string_view hint,
                                              const std::filesystem::path& start);

void register_git_tools(agent::ToolRegistry& registry, const GitOptions& options);

}  // namespace apogee::tools
