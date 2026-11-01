#pragma once

#include <iosfwd>
#include <span>
#include <string_view>
#include <vector>

#include "cli/command.h"
#include "mcp/serve_stdio.h"

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
/// `train_versions` and `check`, each answering with the document its
/// command prints under `--output-format json`, byte for byte, computed by
/// the very functions the command prints from. A writing tool is withheld,
/// and nothing here calls a model.
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

/// The status reads' names, in the order they are listed.
[[nodiscard]] std::span<const std::string_view> status_read_names() noexcept;

/// The status reads for `context`'s install: each reads the config, the
/// store and the training records afresh at its call, as its command does at
/// its run -- so a host asking after a promotion sees the promotion. A
/// read that cannot answer (a config that will not load, a backend with no
/// history) answers an error result with the command's own reason.
[[nodiscard]] std::vector<mcp::ServedRead> status_reads(const RootContext& context);

/// What `apogee mcp serve` serves for `context`, read from `in` and answered
/// on `out` until `in` ends: the native read-only tools, then the status
/// reads. Returns `serve_stdio`'s status, 0 at a clean end.
int serve_mcp(const RootContext& context, std::istream& in, std::ostream& out);

}  // namespace apogee::commands
