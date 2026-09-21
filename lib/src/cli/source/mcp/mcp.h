#pragma once

/// MCP -- the Model Context Protocol client for stdio servers, and the
/// in-binary server.
///
/// Filled by the `mcp-stdio-client` item (Milestone W): JSON-RPC over
/// newline-delimited JSON on a child's pipes, the `initialize` handshake,
/// tools cached once and registered into the loop's registry as
/// `mcp__<server>__<tool>`, calls demultiplexed by a read loop whose error
/// path releases every waiter -- and `serve_stdio`, the other side of the
/// same four methods, so Apogee can host a server as a hidden subcommand.
///
/// Tools reach the model through the agent loop's tool interface, not through
/// a per-surface path -- so an MCP tool works identically from `chat`,
/// `complete`, and `serve` without any of them knowing MCP exists.
///
/// The package includes `agent/`, `platform/`, `harness/`, `events/` and the
/// JSONL framer, never a backend provider or `commands/`; `harness.layering`
/// holds it to that.
///
/// This umbrella header is what `tests/packages_test.cpp` includes; it names
/// the package's own headers so a broken include path fails the day it breaks.

#include "mcp/client.h"
#include "mcp/registry.h"
#include "mcp/serve_stdio.h"
#include "mcp/transport.h"
#include "mcp/types.h"
