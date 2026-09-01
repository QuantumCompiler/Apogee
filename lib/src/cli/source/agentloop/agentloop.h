#pragma once

/// The agentic loop -- Apogee's I/O-agnostic heart.
///
/// | Header | Contents |
/// |---|---|
/// | `loop.h` | `run()`, `Options`, `RunResult` -- the model→tool→model cycle. |
/// | `reporter.h` | The observer every surface adapts. |
/// | `content.h` | Token estimation, compaction, transient splicing. |
/// | `question.h` | The `ask_user` tool: schema, validation, encoding. |
///
/// Tools themselves live one package over, in `agent/` -- the loop needs a
/// registry, but a registry needs no loop.
namespace apogee::agentloop {}
