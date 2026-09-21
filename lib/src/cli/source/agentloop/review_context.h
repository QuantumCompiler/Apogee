#pragma once

#include <string>
#include <string_view>

/// The arbitrary-branch review context: which refs a git-using run reviews.
///
/// **Ref selection is deterministic from flags; free text only focuses
/// attention.** The context reaches the git toolset as plain parameters --
/// the defaults `git_diff` and `git_log` fall back to when the model passes
/// none -- and the model as a one-line system note. The model cannot change
/// which diff is reviewed, and a small local model never has to parse a
/// branch name out of a sentence. Ommi carried the same refs through
/// `OMMI_GIT_REVIEW_*` environment variables its git server read at call
/// time; Apogee's tools are in-process, so the side channel is gone and the
/// refs are just arguments.
namespace apogee::agentloop {

struct ReviewContext {
    /// The branch under review; empty means the current branch.
    std::string head;
    /// The base; empty means the repository's default branch.
    std::string base;
    std::string remote = "origin";
    /// `auto` (fetch only when a ref is absent locally), `always`, `never`.
    std::string fetch = "auto";

    [[nodiscard]] bool active() const noexcept {
        return !head.empty() || !base.empty();
    }
};

[[nodiscard]] bool valid_fetch_mode(std::string_view mode) noexcept;

/// Interprets a `/branch` argument against the current context: `off` (or
/// `clear`, `none`) switches the review off; `base..head` or `base...head`
/// sets both; a bare `head` sets the head and keeps the base.
[[nodiscard]] ReviewContext parse_branch_arg(std::string_view arg, const ReviewContext& current);

/// The system-prompt note, or empty when no review is active: "review X
/// compared to Y WITHOUT checking it out", so the model scopes its git
/// inspection to that comparison.
[[nodiscard]] std::string review_note(const ReviewContext& context);

/// One line for a status line or a banner: "feature-x vs main (remote origin)".
[[nodiscard]] std::string review_summary(const ReviewContext& context);

/// `prompt` and `note` joined with a blank line; either may be empty.
[[nodiscard]] std::string compose_system_prompt(std::string_view prompt, std::string_view note);

}  // namespace apogee::agentloop
