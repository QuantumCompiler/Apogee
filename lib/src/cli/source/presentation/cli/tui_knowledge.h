#pragma once

#include <memory>
#include <vector>

#include "cli/command.h"
#include "tui/list_view.h"

/// The shell's knowledge views (37c): Knowledge, Collections and Graph, each
/// a `tui::ListView` drawing the read its command's JSON document is made of
/// and calling the core its command calls -- never a second implementation of
/// either (ADR 0010).
///
/// - Knowledge: `knowledge list`'s records (`read_knowledge_records`); Enter
///   the record as `knowledge info` prints it, `/` a question answered as
///   `knowledge query` answers it -- the retriever that ran, each score on its
///   scale -- and `x` deletes the record after an ask.
/// - Collections: `embed list`'s rows (`collection_rows`); Enter `embed
///   info`, `/` `embed query` over the selected collection, `x` deletes the
///   whole collection after an ask.
/// - Graph: every graph `graph stats` reads -- the named graphs as
///   configured, then each collection holding a graph of its own -- with its
///   counts as `graph stats --output-format json` states them; Enter the
///   stats, `/` a node's card as `graph explain` prints it.
///
/// Reads and asks only: the long-running verbs (ingest, build, update,
/// reindex, dedupe) and the writers (capture, link, export) run as commands.
namespace apogee::commands {

[[nodiscard]] tui::ListOptions knowledge_view_options(const RootContext& context);
[[nodiscard]] tui::ListOptions collections_view_options(const RootContext& context);
[[nodiscard]] tui::ListOptions graph_view_options(const RootContext& context);

/// The three, in their order on the shell.
[[nodiscard]] std::vector<std::unique_ptr<tui::ListView>> make_knowledge_views(
    tui::Pump& pump, tui::Theme theme, const RootContext& context);

}  // namespace apogee::commands
