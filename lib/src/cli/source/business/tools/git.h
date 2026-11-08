#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "agent/tool.h"

/// The git toolset: status, log, diff, show -- each a `git` child.
///
/// Every argument that reaches `git` travels in an argument list, never through
/// a shell, so quoting is not a risk; **option injection** is, and the ref
/// allow-list -- a token that starts with a letter or digit -- is what closes
/// it.
///
/// The tools read and never prompt, so **they are confined instead** (57o):
/// an explicit `repo` must resolve inside the file tools' root, and so must
/// what git opens there, whether the folder was named or walked to -- its
/// git directory, its common directory, its work tree and its object store's
/// alternates -- unless git reaches it only through what lies outside the
/// root (the checkout a launch in a subfolder sits in) or through a `.git`
/// file its own git directory vouches for (a linked worktree's `gitdir`, a
/// submodule's `core.worktree`). So a `.git` file, link or folder written
/// into the root cannot send git to another repository. And no git they
/// start runs a command a repository's own config names -- every call
/// passes `kGitHardening`, the repository's filter drivers are switched off
/// by name, and the commands that diff never run an external diff or a
/// textconv, nor look inside a submodule's work tree, nor diff or log inside
/// a submodule at all: one shows as the commit ids it moved between.
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
    /// The root an explicit `repo`, and what git opens, must resolve inside
    /// (57o): the file tools' effective root (`effective_fs_root`), so the
    /// git tools read no repository `read_file` could not. Empty means
    /// `working_directory`.
    std::filesystem::path fs_root;
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

/// What every git call is given ahead of its command (57o), so that nothing a
/// repository's own config names runs: no `core.fsmonitor` command, no
/// signature check through `gpg.program` (`log.showSignature`), no submodule
/// summary (`status.submoduleSummary`, a `git log` inside each moved
/// submodule), no hook (`core.hooksPath=/dev/null`: neither `.git/hooks` nor a
/// folder the repository names is looked in), and no write to the index --
/// `git status` takes no optional lock, and `git diff` against the work tree
/// does not write back what it refreshed (`diff.autoRefreshIndex=false`),
/// which it would whatever the lock option says, running `post-index-change`.
/// Filter drivers are named per repository (`filter_neutralising_options`).
[[nodiscard]] std::span<const std::string_view> git_hardening() noexcept;

/// The `-c` options that switch off each filter driver a repository's own
/// config (`local` or `worktree` scope) defines (57o): its `clean`,
/// `smudge` and `process` emptied and `required` false, so `git status` and
/// `git diff` hash a file as it is rather than running the driver.
/// `scoped_names` is `git config --show-scope --name-only --get-regexp
/// '^filter\.'`'s output. A driver whose name `-c` cannot carry (it holds a
/// `=`) sets `refusal`.
[[nodiscard]] std::vector<std::string> filter_neutralising_options(std::string_view scoped_names,
                                                                   std::string& refusal);

void register_git_tools(agent::ToolRegistry& registry, const GitOptions& options);

}  // namespace apogee::tools
