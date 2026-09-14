#pragma once

#include "agent/tool.h"
#include "harness/config.h"
#include "harness/harness.h"

/// The RAG-query toolset: the model searching an ingested collection itself.
///
/// The role Ommi's `ommi-mcp-embed` server played, in-process: the chunk
/// store is opened directly and the embedder is the harness's own, resolved
/// by capability -- no `OMMI_EMBED_EXEC` handle, no second transport.
///
/// **One retriever per turn, reported honestly.** `search_documents` decides
/// how to search through the same `resolve_turn_retriever` every surface
/// uses, from the tool's `retriever` argument, the collection's pin and the
/// store's facts, and every hit names the retriever that scored it. An
/// explicit `vector` that cannot run is an error result naming lexical,
/// never a silent fallback -- the rule the ⚠ section states, kept here too.
namespace apogee::tools {

/// Registers `search_documents`, `list_collections` and `collection_info`.
/// `harness`/`config` may be null: then no embedder is available and every
/// search is lexical, which the results say.
void register_rag_tools(agent::ToolRegistry& registry, const harness::Harness* harness,
                        const harness::Config* config);

/// Whether `name` can name a collection: letters, digits, `.`, `_`, `-`.
[[nodiscard]] bool valid_collection_name(std::string_view name) noexcept;

}  // namespace apogee::tools
