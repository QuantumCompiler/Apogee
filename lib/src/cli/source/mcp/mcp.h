#pragma once

/// MCP -- Model Context Protocol client and tool plumbing.
///
/// Reserved by the project skeleton; filled by the `mcp-stdio-client` backlog
/// item with the stdio client, its registry, and the in-binary server. The
/// native toolsets (`tools/`) and the analyze/agents runner are their own
/// items, groomed out of the original guard document on 2026-09-13.
///
/// Tools reach the model through the agent loop's tool interface, not through
/// a per-surface path -- so an MCP tool works identically from `chat`,
/// `complete`, and `serve` without any of them knowing MCP exists.
namespace apogee::mcp {}
