#pragma once

#include <string>
#include <vector>

#include "knowledge/record.h"

/// Records prepared for sharing: a re-importable JSON array, or a Markdown
/// report grouped by status for circulating to people. Neither ever includes
/// a raw conversation.
namespace apogee::knowledge {

/// A copy of `records` for sharing. With `strip_names`, each record has its
/// attribution stripped AND its `raw_ref` cleared: the archive path is
/// machine-local, leaks the local username, and is useless to a recipient --
/// while `source`, `downstream_link` and `supersedes` stay. Names off, chain
/// on. Without it the records come back unchanged: a faithful export.
[[nodiscard]] std::vector<Record> export_records(std::vector<Record> records, bool strip_names);

/// A human-readable report grouped by status -- shipped, rejected,
/// superseded, then any other -- one heading per record and a bullet per
/// field that is set. An empty attribution simply omits its line.
[[nodiscard]] std::string render_markdown(const std::vector<Record>& records);

}  // namespace apogee::knowledge
