#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "agentloop/attachments.h"
#include "contracts/config.h"
#include "embedstore/store.h"
#include "graph/code_build.h"
#include "platform/file_lock.h"

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

// ---- The graph build lock (57t) ---------------------------------------------
//
// A named graph's database is one file, which `graph delete` removes. A build
// in another process -- a terminal `graph build`, `embed ingest --graph`, the
// app's build job, `serve`'s -- kept writing to the unlinked file and then
// said it had built the graph, which no read could find. So every build of a
// named graph holds one lock for its whole run, and every delete of one takes
// it before it unlinks the file: a `FileLock` (55f) on `<graph db>.build.lock`,
// which the system lets go of when its holder dies, so a crashed build never
// blocks a delete. Both surfaces take it here, so they share one guard.
// Collection-backed graphs delete their rows through SQLite, which already
// orders a delete with a writer: they take no lock.

/// The lock file beside a named graph's database: `<db>.build.lock`. Never
/// removed (a `FileLock`'s rule), so it outlives a deleted graph harmlessly.
[[nodiscard]] std::filesystem::path graph_build_lock_path(const std::filesystem::path& db_path);

/// How long a build waits for a lock another run holds before it refuses: a
/// delete holds it for the instant it takes to count and unlink, and a
/// second build of the same graph is refused rather than queued.
inline constexpr std::chrono::milliseconds kGraphBuildLockWait{2000};

/// The lock a build of the named graph `name` holds for its whole run --
/// its database's folder made first -- or nullopt with `refusal` in the
/// words every surface says: another build or a delete held it past `wait`,
/// or the lock file could not be opened.
[[nodiscard]] std::optional<platform::FileLock> lock_graph_build(
    const std::filesystem::path& db_path, std::string_view name, std::string& refusal,
    std::chrono::milliseconds wait = kGraphBuildLockWait);

/// `graph delete <named>` and `DELETE /v1/admin/graph/{id}`: one delete,
/// under the build lock, refused at once while a build holds it.
struct NamedGraphDeletion {
    enum class Outcome : std::uint8_t {
        /// The database and its `-wal`/`-shm` files are gone.
        Deleted,
        /// There was no database to delete.
        Absent,
        /// A build holds the lock: nothing was touched. `message` says so.
        Building,
        /// The lock or the file could not be had: `message` says why.
        Failed,
    };
    Outcome outcome = Outcome::Absent;
    /// What the database held, read under the lock before it went.
    embedstore::GraphStats stats{};
    std::string message{};
};

[[nodiscard]] NamedGraphDeletion delete_named_graph(const std::filesystem::path& db_path,
                                                    std::string_view name);

}  // namespace apogee::commands
