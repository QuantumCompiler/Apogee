#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "backends/factory.h"
#include "cli/command.h"
#include "contracts/config.h"
#include "harness/harness.h"
#include "mcp/serve_stdio.h"
#include "operations/suites.h"

/// `apogee mcp serve` (54a): Apogee's MCP server on this process's own stdin
/// and stdout, for another MCP host -- a desktop assistant, an editor, the
/// Apogee mod. The hidden `__mcp-tools` is its alias, kept for hosts
/// registered before the verb was public.
///
/// **Two kinds of tool, both read-only.** The native toolsets' read-only
/// tools (files, git, notes, document search, graph navigation), built from
/// the toolsets directly and never the MCP registry -- a config listing this
/// very command as a server would otherwise spawn itself without end -- and
/// the **status reads**: `models_list`, `models_status`, `train_status`,
/// `train_versions` and `check` (54a), then `list_graphs`, `graph_stats`,
/// `graph_report`, `knowledge_list`, `knowledge_info` and `knowledge_query`
/// (58b), each answering with the document its command prints under
/// `--output-format json`, byte for byte -- `graph_report` with its Markdown
/// -- computed by the very functions the command prints from. A writing tool
/// is withheld.
/// `search_documents` searches as chat's tool does, through the one
/// retriever resolver with the collection's embedder (54b).
///
/// **And the calls the config lists** (54b): `complete`, served only where
/// a backend entry's `mcp_serve:` lists it and its provider says it is local
/// and unmetered -- absent from `tools/list` otherwise, never refused at
/// call. Each call is the one-shot `apogee complete` runs, bounded, and
/// narrated on stderr, the stream a host captures.
///
/// **Pipes, never a port.** stdout carries JSON-RPC and nothing else.
///
/// **Additive.** Tools are only ever added, never renamed or removed: a host
/// registered against an older Apogee keeps every tool it was offered.
namespace apogee::commands {

inline constexpr std::string_view kModelsListRead = "models_list";
inline constexpr std::string_view kModelsStatusRead = "models_status";
inline constexpr std::string_view kTrainStatusRead = "train_status";
inline constexpr std::string_view kTrainVersionsRead = "train_versions";
inline constexpr std::string_view kCheckRead = "check";
// 58b: the graphs and the knowledge records, after 54a's five.
inline constexpr std::string_view kListGraphsRead = "list_graphs";
inline constexpr std::string_view kGraphStatsRead = "graph_stats";
inline constexpr std::string_view kGraphReportRead = "graph_report";
inline constexpr std::string_view kKnowledgeListRead = "knowledge_list";
inline constexpr std::string_view kKnowledgeInfoRead = "knowledge_info";
inline constexpr std::string_view kKnowledgeQueryRead = "knowledge_query";

/// The status reads' names, in the order they are listed.
[[nodiscard]] std::span<const std::string_view> status_read_names() noexcept;

/// Where a served read that may embed takes its providers from (58b): asked
/// only when the read's one decision might need an embedder, so a lexical
/// read builds none. `serve_mcp` hands the server's own (`ServedModels`),
/// built once for its life; with none, the read builds the command's own at
/// its call, as `apogee knowledge query` does.
using ServedProviders = std::function<const harness::Harness&()>;

/// The status reads for `context`'s install: each reads the config, the
/// store and the training records afresh at its call, as its command does at
/// its run -- so a host asking after a promotion sees the promotion, and
/// after a build the graph. A read that cannot answer (a config that will
/// not load, a backend with no history, a store that is not one) answers an
/// error result with the command's own reason. Only `knowledge_query` may
/// make a model call -- its question's embedding, through `providers` -- and
/// never a rerank judge's.
[[nodiscard]] std::vector<mcp::ServedRead> status_reads(const RootContext& context,
                                                        ServedProviders providers = {});

/// One backend the served `complete` may name (54b): the entry's name as
/// written, and the most tokens a call on it generates
/// (`harness::served_max_tokens`).
struct ServedBackend {
    std::string name{};
    std::int64_t max_tokens = 0;
};

/// The backends `config` lists `complete` under, by name (`backend_names`), each
/// asked through `metered` whether its provider bills a call: the ones that
/// say local and unmetered are served, and for each that is not,
/// `left_out` holds the rule's words (`harness::mcp_serve_ineligible`) -- a
/// hand-edited listing the editor never wrote, said on the server's stderr.
struct CallableBackends {
    std::vector<ServedBackend> backends{};
    std::vector<std::string> left_out{};
};

[[nodiscard]] CallableBackends complete_backends(const harness::Config& config,
                                                 const MeteredProbe& metered);

/// The served `complete`'s description (54b), the contract a host's model
/// reads: what one call is, which backends it may name and each one's
/// bound, that the spend is this machine's, and why it is offered.
[[nodiscard]] std::string complete_description(const std::vector<ServedBackend>& backends);

/// The providers a server's model calls run on (54b): one Harness over the
/// config the server started with, every served tool holding it, its
/// providers built at the first call that needs them and kept for the
/// server's life -- so a server whose host never searches or completes
/// builds none, and a local model loads once, not per call.
class ServedModels {
public:
    ServedModels(const harness::Config& config, std::filesystem::path config_path);

    /// The Harness, its providers built or not: what a tool holds.
    [[nodiscard]] const harness::Harness& harness() const noexcept {
        return harness_;
    }

    /// Builds the providers the first time it is asked, as `apogee complete`
    /// builds them; what building them found.
    const backends::BuildResult& ready();

private:
    harness::Harness harness_;
    std::filesystem::path config_path_;
    std::optional<backends::BuildResult> built_;
};

/// The served `complete` (54b) on `backends` through `models`, narrated on
/// `narration`. A call takes `prompt`, `backend` -- one of `backends` -- and
/// optionally `max_tokens`, fewer than the backend's bound, and runs
/// `one_shot` with the utility model off: no tools, no retrieval, no call
/// but the named backend's. At the call, the config is read again and the
/// backend must still list `complete`, and the built provider is asked again
/// whether it bills. A refusal is an error result in the CLI's words.
[[nodiscard]] mcp::ServedCall served_complete(const RootContext& context, ServedModels& models,
                                              std::vector<ServedBackend> backends,
                                              std::ostream& narration);

/// The most the introduction may run to (58a), in bytes -- never fewer than
/// its characters -- and how many names each of its lists shows before the
/// cut.
inline constexpr std::size_t kInstructionsBound = 2000;
inline constexpr std::size_t kInventoryNames = 8;

/// The config the server started with, as the introduction says it (58a):
/// loaded, no file at all, or a file that will not load.
enum class ServedSetup : std::uint8_t { Ready, Absent, Unreadable };

/// One collection the introduction names: as `list_collections` names it,
/// its chunk count, and the model its vectors are from as the store records
/// it (`collection_info`'s fact) -- empty for none, `mixed` for several.
struct IntroducedCollection {
    std::string name{};
    std::int64_t chunks = 0;
    std::string embedder{};
    bool mixed = false;
    /// The store could not be opened.
    bool unreadable = false;
};

/// One graph, as `graph list` names it: its kind and size, never its
/// sources.
struct IntroducedGraph {
    std::string name{};
    std::string kind{};
    bool built = false;
    bool unreadable = false;
    std::int64_t nodes = 0;
    std::int64_t edges = 0;
};

/// The facts `apogee mcp serve` introduces itself with (58a), read once at its
/// start -- the state the reads answer from, nothing else: no key, no path.
struct ServerInventory {
    ServedSetup setup = ServedSetup::Ready;
    std::vector<IntroducedCollection> collections{};
    std::vector<IntroducedGraph> graphs{};
    std::size_t records = 0;
    std::size_t shipped_records = 0;
    /// The backends the served `complete` may name (`complete_backends`).
    std::vector<ServedBackend> backends{};
    /// The names `tools/list` lists: a when-to-use line names only these.
    std::vector<std::string> served{};
};

/// `context`'s setup as the server starts: with the config `setup` says
/// loaded, each collection `list_collections` lists, `graph list`'s rows,
/// the knowledge records' count; `backends` and the names of `tools` as
/// given. A store that cannot be read is said so, never fatal.
[[nodiscard]] ServerInventory read_server_inventory(const RootContext& context, ServedSetup setup,
                                                    std::vector<ServedBackend> backends,
                                                    const std::vector<mcp::ServedTool>& tools);

/// What `initialize` tells a host (58a), MCP's `instructions`: one paragraph
/// on what Apogee is and its two kinds of tool, then the inventory, then
/// which tool answers what -- each line naming only tools `inventory` says
/// are served. Facts only, said as names and counts: a source path is never
/// said, an embedder named by an absolute path is named by its file, and a
/// name that is not UTF-8 is mended. Each list shows at most
/// `kInventoryNames` names, then `… and N more: <the tool that lists them
/// whole>`, fewer while the text would pass `kInstructionsBound`, which it
/// never does. With no config it says Apogee is not set up and names
/// `check`; with one that will not load, that it does not load.
[[nodiscard]] std::string server_instructions(const ServerInventory& inventory);

/// What `apogee mcp serve` serves for `context`, read from `in` and answered
/// on `out` until `in` ends: the native read-only tools, then the status
/// reads, then the calls the config lists, narrated on `err` -- introduced
/// by `server_instructions` over the setup as it starts (58a). Returns
/// `serve_stdio`'s status, 0 at a clean end.
int serve_mcp(const RootContext& context, std::istream& in, std::ostream& out, std::ostream& err);

}  // namespace apogee::commands
