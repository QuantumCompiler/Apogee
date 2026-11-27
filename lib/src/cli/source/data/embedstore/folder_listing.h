#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

/// What a walk of a folder takes, and what it leaves out (57s).
///
/// **One listing, three readers.** An attachment folder (`agentloop/
/// attachments`), a directory ingest (`embedstore/ingest`) and the code graph's
/// vendored folders (`graph/code_build`) read the rules from here, so a file a
/// chat attachment leaves out cannot reach a collection by another walk. It
/// sits in the Data layer, below every one of them: the ingest may not reach
/// up into `agentloop/`, and the code graph's files may include `embedstore/`
/// and nothing else of the kind.
///
/// Inside a git work tree, git's own list is taken -- tracked, and untracked
/// but not ignored -- and outside one, a walk. Hidden entries are left out
/// either way, and a hidden folder is never walked into: `.git/` holds a
/// remote URL with its token, `.env` a secret, and neither is a document.
namespace apogee::embedstore {

/// Whether any component of `relative` is hidden: a name that starts with a
/// dot, other than `.` and `..`.
[[nodiscard]] bool hidden_path(const std::filesystem::path& relative);

/// Whether a folder named `name` holds vendored or built files, never a
/// tree's own: `node_modules`, `vendor`, `third_party`, `_deps`, `venv`,
/// `target`, a `cmake-build-*` and the like -- a conservative list, each a
/// near-universal convention (the code graph's, 27k).
[[nodiscard]] bool vendored_name(std::string_view name);

/// The leading folders of the '/'-separated `path` through its first vendored
/// one -- `web/node_modules` for `web/node_modules/react/index.js` -- or empty
/// when no folder on it is vendored. The last component is the file itself and
/// is never read as a folder.
[[nodiscard]] std::string vendored_directory(std::string_view path);

/// How a listing treats what an attachment walk takes as it is.
struct ListingRules {
    /// Leave out what sits under a vendored folder too, inside a repository
    /// and out: a directory ingest's rule, so `node_modules/` stays out of a
    /// collection whether git governs the folder or not. An attachment walk
    /// leaves it to git.
    bool leave_out_vendored = false;
    /// Name what was left out in `FolderListing::left_out`, asking git for
    /// what it ignores -- one more child, which an attachment does not need.
    bool name_left_out = false;
    /// A folder git ignores whole is walked, as one outside a repository is:
    /// the person named it, so it is taken -- a built `_build/html` ingested
    /// on purpose. Off, git's answer for it stands: nothing.
    bool walk_ignored_folder = false;
};

/// One folder's listing.
struct FolderListing {
    /// The files taken, relative to the folder, sorted and unique.
    std::vector<std::filesystem::path> files{};
    /// What was left out, relative to the folder in the generic form (under
    /// `name_left_out` only), sorted and unique: a hidden or vendored folder
    /// once, ending in '/', never walked into; a hidden file; what git
    /// ignores, as git names it -- a wholly ignored folder once; and the
    /// repository's own `.git` when the folder holds it.
    std::vector<std::string> left_out{};
};

/// Lists `folder` under `rules`: git's list inside a work tree, else a walk,
/// each without its hidden entries (and, under the rule, its vendored ones).
[[nodiscard]] FolderListing list_folder(const std::filesystem::path& folder,
                                        const ListingRules& rules = {});

/// Every file under `folder`, relative to it: an attachment folder's listing,
/// `list_folder` under the default rules.
[[nodiscard]] std::vector<std::filesystem::path> files_under(const std::filesystem::path& folder);

}  // namespace apogee::embedstore
