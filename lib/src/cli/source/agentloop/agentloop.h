#pragma once

/// Agent loop -- the shared model -> tool -> model cycle.
///
/// Reserved by the project skeleton; filled by the `agentloop-core` backlog
/// item with the loop itself, the Reporter seam that lets each surface render
/// progress its own way, and the ask_user tool.
///
/// There is exactly ONE loop, and every surface drives it (SPEC.md ->
/// Principles: "one source of truth per concern"). A second loop written for a
/// second surface is how parity bugs are born.
namespace apogee::agentloop {}
