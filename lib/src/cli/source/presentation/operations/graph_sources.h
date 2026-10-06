#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "agentloop/attachments.h"
#include "contracts/config.h"
#include "embedstore/store.h"
#include "graph/code_build.h"

/// A named graph's source trees (27k), opened the same way by every surface
/// that builds them: each tree walked with the one walk 26d set
/// (`agentloop::find_attachment_files` -- `git ls-files` inside a
/// repository, hidden entries left out), its files handed to
/// `graph::build_source` with a reader rooted at the tree. The CLI's
/// `graph build`/`graph update` and the admin plane's build share this, so a
/// tree is walked and labelled identically wherever it is built from.
namespace apogee::commands {

/// A source tree as a `graphs:` entry records it: absolute (a config is read
/// from wherever apogee runs, where a relative path would mean something
/// else), normalized, no trailing separator, `/`-separated.
[[nodiscard]] std::string absolute_source_path(std::string_view path);

/// A source tree's label in the graph -- what its mentions, sites and state
/// rows are recorded under: the tree's directory name.
[[nodiscard]] std::string source_member_label(std::string_view source);

/// The files a walk found under `root`, as a source build takes them:
/// relative to `root`, `/`-separated. A file outside `root` is left out.
[[nodiscard]] std::vector<std::string> source_files_under(
    const std::vector<agentloop::FoundFile>& found, const std::filesystem::path& root);

/// A source member over `files` (relative to `root`), read from the disk --
/// the one reader `graph build --source`, the admin build and a chat's
/// attached folder (27n) all hand `graph::build_source`, so the same files
/// in are the same bytes read.
[[nodiscard]] graph::SourceMember source_member(std::string label,
                                                const std::filesystem::path& root,
                                                std::vector<std::string> files);

/// Whether `files` (relative to their root) offer anything a vendored
/// grammar parses outside a vendored directory -- the test a chat's folder
/// attachment takes before its graph is built (27n): one that offers none
/// builds nothing and says nothing. By extension, against 27k's roster, the
/// way the build selects -- so this is true exactly when the build would use
/// a file, short of one it cannot read or parse.
[[nodiscard]] bool offers_code(const std::vector<std::string>& files);

/// The source members of a `graphs:` entry, walked. A tree that is not a
/// directory any longer is named in `missing` and contributes nothing (its
/// rows are forgotten by the build, which owns the membership).
struct GraphSources {
    std::vector<graph::SourceMember> members;
    std::vector<std::string> missing;
    /// Trees that are directories but offer no file -- inside a git
    /// repository that ignores them, only what git tracks or does not ignore
    /// is read. Built (as members with nothing), and said.
    std::vector<std::string> empty;
};

[[nodiscard]] GraphSources open_graph_sources(const harness::NamedGraphConfig& named);

/// Builds (or updates) the code layer of a named graph's database from its
/// `sources:`, with its `languages:` -- no model, no harness. `force`
/// re-parses everything; `dry_run` stores nothing. The graph's membership is
/// its entry's: a tree removed from `sources:`, or gone from the disk, is
/// forgotten -- and named in `missing` when it is the latter.
struct GraphSourceBuild {
    std::vector<std::string> missing;
    std::vector<std::string> empty;
    graph::SourceBuildResult result;
};

[[nodiscard]] GraphSourceBuild build_graph_sources(embedstore::Store& store,
                                                   const harness::NamedGraphConfig& named,
                                                   graph::SourceBuildOptions options);

}  // namespace apogee::commands
