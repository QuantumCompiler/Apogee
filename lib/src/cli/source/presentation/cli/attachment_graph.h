#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "agent/tool.h"
#include "contracts/cancellation.h"
#include "graph/code_build.h"
#include "graph/navigate.h"
#include "logger/session.h"

/// The attachment code graph (27n): a folder of code attached to a chat is
/// parsed into the chat's own index -- `attachments/<chat id>.db`, an
/// ordinary embedstore database, so the code layer's tables are already
/// there -- by 27k's build over the files the attach already found, with no
/// model, no key and no embedder. `ChatAttachments`' worker runs it after
/// the folder's chunks are indexed; this is the pass and its one line.
///
/// **The graph is the chat's, and exactly its attachments'.** Each graphed
/// folder is one source member of the index's code graph, labelled by its
/// folder's name (made unique among the chat's attachments) -- the label
/// `graph build --source` gives the same tree, so the same files build the
/// same graph byte for byte. The members link to one another (a call in one
/// folder resolving into another), and any member no attachment owns -- a
/// detached folder, a build a crash cut short -- is forgotten before the
/// next build, so nothing in the graph outlives the attachment it came from.
namespace apogee::commands {

/// One folder attachment's graph pass, as the worker is handed it.
struct AttachmentGraphJob {
    /// The attachment, as the user named it.
    std::string name;
    /// The source member its files are recorded under.
    std::string label;
    /// The attached folder, absolute.
    std::filesystem::path root;
    /// Every file the attach found, relative to `root` -- the attach's own
    /// walk, never a second one. The build leaves out, and counts, what it
    /// cannot parse.
    std::vector<std::string> files;
    /// The other attachments' members: linked from their cache as they
    /// stand. Every other member of the index is forgotten.
    std::set<std::string> keep;
    /// Build it -- a folder offering code, with the graph on. False: forget
    /// what an earlier attach under the same name built.
    bool build = true;
};

/// What a pass did.
struct AttachmentGraphOutcome {
    enum class State : std::uint8_t {
        /// Built (or updated): `record` holds its label and counts.
        Built,
        /// Cancelled between files: nothing synced, the parsed files cached.
        Cancelled,
        /// A storage error stopped it; `record.absent` says which.
        Failed,
        /// An earlier graph forgotten: none now, and nothing to say.
        Forgotten,
    };

    State state = State::Built;
    /// What the session keeps: the label and the notice's parentheses when
    /// built, the absence when not.
    logger::AttachmentGraph record;
    /// This attachment's part of the graph: the code nodes it mentions and
    /// the edges it states.
    std::int64_t nodes = 0;
    std::int64_t edges = 0;
    /// Files parsed this pass, and reused from the index's cache: an update
    /// re-parses only what changed.
    int parsed = 0;
    int unchanged = 0;
};

/// Builds `job`'s member into the index at `store_path` -- 27k's
/// `build_source` over the injected files, the other attachments' members
/// linked from their cache and any member no attachment owns forgotten
/// first. Cancelled between files: `Cancelled`, the files parsed so far
/// cached and nothing synced, so the graph is as it was -- the caller
/// decides whether to forget it. Never throws: a storage error is `Failed`.
[[nodiscard]] AttachmentGraphOutcome build_attachment_graph(
    const std::filesystem::path& store_path, const AttachmentGraphJob& job,
    const harness::CancellationToken& cancellation,
    const std::function<void(const graph::SourceProgress&)>& progress = {});

/// Forgets `label`'s part of the index's code graph -- with any member not in
/// `keep` -- and re-links what is kept from its cache, so the graph holds
/// exactly the kept attachments' code. Nothing to forget, nothing done.
/// Storage errors throw.
void forget_attachment_graph(const std::filesystem::path& store_path, const std::string& label,
                             const std::set<std::string>& keep);

/// What a build left out, by kind, for the notice: an unsupported file by its
/// extension (`.md`, `no extension`), one it could not use by why (`binary`,
/// `too large`, `unreadable`, `unparseable`), a vendored directory by its path
/// (`third_party/`), each counted.
[[nodiscard]] std::map<std::string, std::int64_t> skipped_kinds(
    const graph::SourceBuildResult& result);

/// The one notice line, repeated by `/attachments`:
/// `graph: 412 nodes, 1820 edges (supported: cpp 30, python 2; skipped: .md 3)`
/// -- or, absent, `graph: not built -- cancelled; attach it again to build it`.
[[nodiscard]] std::string attachment_graph_line(const logger::AttachmentGraph& graph,
                                                std::int64_t nodes, std::int64_t edges);

/// How many kinds the notice names before folding the rest into a count.
inline constexpr std::size_t kGraphLineKinds = 8;

// ---- The graph at work on turns (27o) -----------------------------------------
//
// A graph nobody can reach is worthless: an attachment turn expands through
// it, and the `graph` toolset -- 27l's four read-only tools, one traversal
// core -- is offered scoped to it, so a model walks the attached code rather
// than inventing paths. The scope is the chat's own state, never a `graphs:`
// entry: `resolve_turn_graph` stays about collections and named graphs.

/// What the chat's code graph is called wherever it is named: the section an
/// attachment turn injects (`[Knowledge graph: attachments]`) and every
/// payload the scoped tools return.
inline constexpr std::string_view kAttachmentGraphName = "attachments";

/// One graphed folder of a chat.
struct GraphedFolder {
    /// The attachment as the user named it: its root, as its excerpts' labels
    /// and its map card name it.
    std::string name;
    /// The source member its files are recorded under in the index.
    std::string label;

    bool operator==(const GraphedFolder&) const = default;
};

/// The chat's code graph as the scoped `graph` toolset reads it: the index,
/// and the folders whose code it holds. Equal scopes offer the same tools.
struct AttachmentGraphScope {
    std::filesystem::path store;
    std::vector<GraphedFolder> folders;

    bool operator==(const AttachmentGraphScope&) const = default;
};

/// The graph the scoped tools read: the chat's index under
/// `kAttachmentGraphName`, with the graph knobs' defaults -- a store the chat
/// owns, made directly as `graph::GraphTarget` allows.
[[nodiscard]] graph::GraphTarget attachment_graph_target(const AttachmentGraphScope& scope);

/// What the scoped tools' descriptions say they read: the attached folders by
/// their roots, each file named relative to its folder -- so 26g ranks the
/// tools against a question about that code, and a model cites a file where
/// it is.
[[nodiscard]] std::string attachment_graph_note(const AttachmentGraphScope& scope);

/// `registry` as a chat with `scope` offers it: its `graph` toolset read
/// through the chat's code graph (`tools::with_graph_scope` over
/// `attachment_graph_target`, each description ending with
/// `attachment_graph_note`), everything else as it was.
[[nodiscard]] agent::ToolRegistry attachment_graph_tools(const agent::ToolRegistry& registry,
                                                         const AttachmentGraphScope& scope);

}  // namespace apogee::commands
