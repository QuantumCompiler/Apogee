#pragma once

#include <filesystem>

#include "cli/command.h"
#include "tui/list_view.h"

/// The shell's tooling views (37g): Agents, MCP and Auth. Their reads are the
/// commands' documents -- each the control plane's body for the same read --
/// drawn in process; every act is the command itself, run as a child of the
/// shell's own binary (`cli/tui_child`), its answer its own words.
///
/// - Agents: `agents list --output-format json`'s rows; Enter the agent's
///   definition as its document states it; `x` deletes the entry, asked --
///   its prompt and schema files kept, as `agents delete` keeps them with
///   nobody at a terminal to ask.
/// - MCP: `mcp list --output-format json`'s rows, each server connected to
///   as the command connects (bounded, its stderr kept off the screen);
///   Enter the server's entry; `t` the input row on `<server> `, the tool and
///   its JSON arguments typed, run as `mcp test`; `e`/`d` enable and disable
///   through `mcp enable|disable`, asked nothing (a reversible edit whose
///   answer names it).
/// - Auth: the stored credentials as `auth list --output-format json` states
///   them -- metadata only, a type with no key field -- under which rung
///   answers for each backend. A key is stored at a real prompt (`auth add`)
///   and cleared through the exec line (37h); the shell takes no secret.
namespace apogee::commands {

[[nodiscard]] tui::ListOptions agents_view_options(const RootContext& context,
                                                   std::filesystem::path binary);
[[nodiscard]] tui::ListOptions mcp_view_options(const RootContext& context,
                                                std::filesystem::path binary);
[[nodiscard]] tui::ListOptions auth_view_options(const RootContext& context);

}  // namespace apogee::commands
