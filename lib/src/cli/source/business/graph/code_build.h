#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/cancellation.h"
#include "embedstore/graph_code.h"
#include "embedstore/store.h"
#include "graph/code_resolve.h"

/// The code graph's build (27k): a source tree parsed with tree-sitter into
/// the same `kg_*` tables a prose build fills -- a named graph's database or
/// any other store (a chat's attachment store, 27n) -- with **zero model
/// calls by construction**: this build takes no generation closure and no
/// embedder, and nothing it includes can reach one (`harness.layering`).
///
/// ## One entry, over an injected file list
///
/// A build is handed its files -- each source member's list of paths
/// relative to its root, and a function that reads one -- never a directory
/// to walk itself. `graph build --source` walks with the one walk 26d set
/// (`git ls-files` inside a repository, hidden entries left out) and hands
/// its result here; an attachment hands the files its attach already found.
/// Same files in, same graph out, whoever called.
///
/// ## Incremental by content hash
///
/// Every parsed file's `kg_state` row holds its content hash, the extractor
/// that read it (the grammar pin and `kCodeExtractorVersion`) and the facts
/// it yielded. A file whose hash and extractor still match is not parsed
/// again: its cached facts are used. The whole tree is then re-linked from
/// the facts -- linking is cheap and global, since a renamed function in one
/// file changes what another file's call resolves to -- and the code layer
/// is synced whole, so an update after touching one file parses exactly that
/// file and stores exactly what a fresh build would.
///
/// ## Honest about what it could not read
///
/// A file no vendored grammar claims, one the `--lang` filter leaves out, a
/// binary or oversized file, one that cannot be read: each is **skipped and
/// named**, never silently, and never a reason to fail the build. A file
/// whose parse recovered from an error is used -- tree-sitter's recovery
/// keeps the rest of the file -- and named as partial. Vendored and build
/// directories nested in a tree (`third_party/`, `node_modules/`, ...) are
/// left out whole, counted per directory.
namespace apogee::graph {

/// Reads one file of a member by its relative path; nullopt with `error` set
/// when it cannot.
using ReadSourceFn =
    std::function<std::optional<std::string>(std::string_view path, std::string& error)>;

/// A source tree a graph is built over: its label, its files (relative to
/// its root, `/`-separated), and how to read one.
struct SourceMember {
    /// What every mention, site and state row from it is recorded under.
    /// Never empty: `''` is a collection's own prose label.
    std::string label;
    std::vector<std::string> files;
    ReadSourceFn read;
};

/// Files larger than this are skipped as too large: a generated table or a
/// bundled blob, not code anyone navigates.
inline constexpr std::size_t kMaxSourceFileBytes = std::size_t{2} * 1024 * 1024;

/// A directory, anywhere in a tree, whose contents are someone else's or a
/// build's -- left out of a source build whole: `third_party`, `vendor`,
/// `node_modules`, ... The first such component of `path`, or empty.
[[nodiscard]] std::string vendored_directory(std::string_view path);

/// A heartbeat per file.
struct SourceProgress {
    std::string member;
    std::string file;
    /// 1-based, over the member's supported files.
    int index = 0;
    int count = 0;
    /// Whether the file is being parsed (false: its cached facts are reused).
    bool parsing = false;
};

struct SourceBuildOptions {
    /// `--lang`: only these languages are parsed, by roster name or alias
    /// (`python`, `py`); empty means every vendored grammar. A file of
    /// another language is skipped by name, and a name no grammar answers
    /// to selects nothing.
    std::vector<std::string> languages;
    /// Re-parse every file, cached facts or not.
    bool force = false;
    /// Parse and link, store nothing: no state row, no sync.
    bool dry_run = false;
    /// Forget every source member of the store not among this build's -- a
    /// graph's membership converges on its config.
    bool prune_other_members = true;
    std::function<void(const SourceProgress&)> on_progress;
    harness::CancellationToken cancellation;
};

/// A file the build did not use, and why.
struct SkippedFile {
    std::string member;
    std::string file;
    std::string reason;
};

struct SourceBuildResult {
    bool dry_run = false;
    /// Cancellation was requested: the files parsed so far keep their
    /// cached facts, and the next build re-links from them.
    bool cancelled = false;
    /// Files offered, before anything was left out.
    int files_offered = 0;
    /// Files in a supported language this build used.
    int files_used = 0;
    /// Parsed this build -- and which, `member/path`, in order.
    int files_parsed = 0;
    std::vector<std::string> parsed;
    /// Used from their cached facts.
    int files_unchanged = 0;
    /// Forgotten: gone from the tree, now skipped, or of a member no longer
    /// in the graph.
    int files_removed = 0;
    /// Used, by language (roster name).
    std::map<std::string, int> files_by_language;
    /// Skipped, each named, sorted by (member, file).
    std::vector<SkippedFile> skipped;
    /// Parsed with an error recovered from (`member/path`): their facts are
    /// partial.
    std::vector<std::string> partial;
    /// Vendored or build directories left out, with how many files each held.
    std::map<std::string, int> excluded;
    ResolveCounts counts;
    /// The code layer after the build (in a dry run: as it would be).
    int nodes = 0;
    int edges = 0;
    embedstore::CodeSyncResult sync;
};

/// Builds (or updates) the code layer of `store` from `members`. Storage
/// errors throw; a file that cannot be used never does -- it is skipped and
/// named. Throws `std::invalid_argument` on a member with an empty label or
/// two members with one label.
[[nodiscard]] SourceBuildResult build_source(embedstore::Store& store,
                                             const std::vector<SourceMember>& members,
                                             const SourceBuildOptions& options);

/// The same files' facts, linked, with nothing stored and nothing cached --
/// what a build of exactly these files yields, for a test or a preview.
[[nodiscard]] CodeGraph parse_and_resolve(const std::vector<SourceMember>& members,
                                          const std::vector<std::string>& languages);

}  // namespace apogee::graph
