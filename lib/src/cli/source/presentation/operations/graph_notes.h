#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "operations/graph_members.h"

/// The graph notes' words, in one place (CLI-M58, for CLI-M59): said by the
/// CLI on stderr in the house voice -- `only_code_built` on stdout, as the
/// outcome -- and carried as they are in an admin job's `warnings`. One
/// sentence each, with no prefix and no tag: `apogee graph: ` and `[graph] `
/// belong to a rendering, never to the words.
namespace apogee::commands {

/// `entity vectors skipped for "<graph>" -- <why>; entity search will be
/// full-text only.` -- `why` is `graph::EntityEmbedder::note`.
[[nodiscard]] std::string entity_vectors_skipped(std::string_view graph, std::string_view why);

/// `summary vectors skipped for "<graph>" -- <why>; summaries will be
/// full-text searchable only.`
[[nodiscard]] std::string summary_vectors_skipped(std::string_view graph, std::string_view why);

/// `member collection "<collection>" has no database -- it contributes
/// nothing this build.`
[[nodiscard]] std::string member_without_database(std::string_view collection);

/// `no member collection of "<graph>" has any data yet -- only its code was
/// built.` -- a mixed graph's outcome when its members hold nothing.
[[nodiscard]] std::string only_code_built(std::string_view graph);

/// One `member_without_database` line per member whose store is null, in the
/// members' map order, which is the order the CLI's member loop says them in.
[[nodiscard]] std::vector<std::string> members_without_database(const GraphMembers& members);

}  // namespace apogee::commands
