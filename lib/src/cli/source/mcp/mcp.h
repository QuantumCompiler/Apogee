#pragma once

/// MCP -- Model Context Protocol client and tool plumbing.
///
/// Reserved by the project skeleton; filled by the `mcp-client-tools-agents`
/// backlog item with the MCP client, the in-process native toolsets, and the
/// analyze/agents runner.
///
/// Tools reach the model through the agent loop's tool interface, not through
/// a per-surface path -- so an MCP tool works identically from `chat`,
/// `complete`, and `serve` without any of them knowing MCP exists.
namespace apogee::mcp {}
