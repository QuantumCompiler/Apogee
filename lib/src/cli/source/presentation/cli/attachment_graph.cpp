#include "cli/attachment_graph.h"

#include <algorithm>
#include <exception>
#include <string_view>
#include <utility>

#include "embedstore/store.h"
#include "operations/graph_sources.h"
#include "tools/graph_nav.h"

namespace apogee::commands {
namespace {

/// Forgets every member of `store`'s code graph that `keep` does not name,
/// and `drop` whatever it says, then re-links the rest from their cache --
/// only when something was forgotten. The relink parses nothing: every kept
/// member's facts are cached, and a build handed no member reads no file.
void forget_members(embedstore::Store& store, const std::set<std::string>& keep,
                    const std::string& drop) {
    bool forgot = false;
    for (const std::string& member : store.code_members()) {
        if (member == drop || !keep.contains(member)) {
            (void)store.remove_code_member(member);
            forgot = true;
        }
    }
    if (forgot) {
        graph::SourceBuildOptions relink;
        relink.prune_other_members = false;
        (void)graph::build_source(store, {}, relink);
    }
}

/// How 27k's build words a file no vendored grammar claims.
constexpr std::string_view kUnsupported = "unsupported language (";

/// What a skipped file's reason, as 27k's build words it, is counted as.
[[nodiscard]] std::string kind_of(std::string_view reason) {
    if (reason.starts_with(kUnsupported) && reason.ends_with(")")) {
        return std::string{
            reason.substr(kUnsupported.size(), reason.size() - kUnsupported.size() - 1)};
    }
    if (reason.starts_with("unreadable")) {
        return "unreadable";
    }
    if (reason.starts_with("too large")) {
        return "too large";
    }
    if (reason.starts_with("did not parse")) {
        return "unparseable";
    }
    return std::string{reason};
}

/// `cpp 30, python 2`: the largest first, ties by name, `kGraphLineKinds`
/// named and the rest counted; `none` for nothing.
[[nodiscard]] std::string kinds_text(const std::map<std::string, std::int64_t>& kinds) {
    if (kinds.empty()) {
        return "none";
    }
    std::vector<std::pair<std::string, std::int64_t>> sorted{kinds.begin(), kinds.end()};
    std::ranges::stable_sort(sorted,
                             [](const auto& a, const auto& b) { return a.second > b.second; });
    std::string out;
    for (std::size_t at = 0; at < sorted.size(); ++at) {
        if (at == kGraphLineKinds) {
            out += ", and " + std::to_string(sorted.size() - at) + " more";
            break;
        }
        out +=
            (out.empty() ? "" : ", ") + sorted[at].first + " " + std::to_string(sorted[at].second);
    }
    return out;
}

[[nodiscard]] std::string counted(std::int64_t count, std::string_view noun) {
    return std::to_string(count) + " " + std::string{noun} + (count == 1 ? "" : "s");
}

}  // namespace

AttachmentGraphOutcome build_attachment_graph(
    const std::filesystem::path& store_path, const AttachmentGraphJob& job,
    const harness::CancellationToken& cancellation,
    const std::function<void(const graph::SourceProgress&)>& progress) {
    AttachmentGraphOutcome out;
    try {
        embedstore::Store store{store_path};
        // A member no attachment owns -- a folder detached, a build a crash
        // cut short -- never links into this one.
        std::set<std::string> owned = job.keep;
        owned.insert(job.label);
        forget_members(store, owned, {});

        graph::SourceBuildOptions options;
        // The chat's other folders stand: linked from their cache, never
        // read again and never forgotten by this one's build.
        options.prune_other_members = false;
        options.cancellation = cancellation;
        options.on_progress = progress;
        const graph::SourceBuildResult result =
            graph::build_source(store, {source_member(job.label, job.root, job.files)}, options);
        out.parsed = result.files_parsed;
        out.unchanged = result.files_unchanged;
        if (result.cancelled) {
            out.state = AttachmentGraphOutcome::State::Cancelled;
            out.record.absent = "cancelled";
            return out;
        }
        out.record.label = job.label;
        for (const auto& [language, count] : result.files_by_language) {
            out.record.supported[language] = count;
        }
        out.record.skipped = skipped_kinds(result);
        const embedstore::CodeMemberCounts counts = store.code_member_counts(job.label);
        out.nodes = counts.nodes;
        out.edges = counts.edges;
    } catch (const std::exception& e) {
        out.state = AttachmentGraphOutcome::State::Failed;
        out.record = logger::AttachmentGraph{
            .label = {}, .supported = {}, .skipped = {}, .absent = e.what()};
    }
    return out;
}

void forget_attachment_graph(const std::filesystem::path& store_path, const std::string& label,
                             const std::set<std::string>& keep) {
    embedstore::Store store{store_path};
    forget_members(store, keep, label);
}

std::map<std::string, std::int64_t> skipped_kinds(const graph::SourceBuildResult& result) {
    std::map<std::string, std::int64_t> out;
    for (const graph::SkippedFile& file : result.skipped) {
        ++out[kind_of(file.reason)];
    }
    for (const auto& [directory, count] : result.excluded) {
        out[directory] += count;
    }
    return out;
}

std::string attachment_graph_line(const logger::AttachmentGraph& graph, std::int64_t nodes,
                                  std::int64_t edges) {
    if (!graph.absent.empty()) {
        return "graph: not built -- " + graph.absent + "; attach it again to build it";
    }
    return "graph: " + counted(nodes, "node") + ", " + counted(edges, "edge") +
           " (supported: " + kinds_text(graph.supported) +
           "; skipped: " + kinds_text(graph.skipped) + ")";
}

graph::GraphTarget attachment_graph_target(const AttachmentGraphScope& scope) {
    // No member databases: a code graph's provenance is its lines, read from
    // the store itself; and the knobs are `GraphTarget`'s defaults, the graph
    // block's own.
    graph::GraphTarget target;
    target.name = std::string{kAttachmentGraphName};
    target.store_path = scope.store;
    return target;
}

std::string attachment_graph_note(const AttachmentGraphScope& scope) {
    const bool one = scope.folders.size() == 1;
    std::string folders;
    for (std::size_t index = 0; index < scope.folders.size(); ++index) {
        const GraphedFolder& folder = scope.folders[index];
        if (index > 0) {
            folders += index + 1 == scope.folders.size() ? " and " : ", ";
        }
        folders += folder.name + " (member '" + folder.label + "')";
    }
    return std::string{" Reads the code graph of the "} + (one ? "folder" : "folders") +
           " attached to this chat, " + folders +
           ": where its functions and classes are defined and implemented, what calls what, and "
           "how one reaches another. A file is named relative to its " +
           (one ? "folder" : "member's folder") + ".";
}

agent::ToolRegistry attachment_graph_tools(const agent::ToolRegistry& registry,
                                           const AttachmentGraphScope& scope) {
    return tools::with_graph_scope(
        registry, tools::GraphToolsOptions{.config = nullptr,
                                           .scope = attachment_graph_target(scope),
                                           .scope_note = attachment_graph_note(scope)});
}

}  // namespace apogee::commands
