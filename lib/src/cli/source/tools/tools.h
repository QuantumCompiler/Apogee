#pragma once

/// Tools -- the native in-process toolsets.
///
/// Filled by the `native-toolsets` item (Milestone V): the filesystem, shell,
/// git, notes and RAG-query tools, each registered into the agent loop's own
/// `ToolRegistry` and each destructive one declared into the permission gate.
/// What Ommi ran as five Python MCP servers, in `apogee_core`.
///
/// The package includes `agent/`, `agentloop/`, `embedstore/`, `platform/`
/// and `harness/`, and never `backends/` or `commands/`; `harness.layering`
/// holds it to that.
///
/// This umbrella header is what `tests/packages_test.cpp` includes; it names
/// the package's own headers so a broken include path fails the day it breaks.

#include "tools/args.h"
#include "tools/fs.h"
#include "tools/git.h"
#include "tools/notes.h"
#include "tools/process.h"
#include "tools/rag_query.h"
#include "tools/shell.h"
#include "tools/toolsets.h"
