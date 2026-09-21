#pragma once

/// Provider implementations -- one per model source.
///
/// Everything here implements `harness::LLMProvider` and translates that
/// interface to a vendor's dialect. The dependency runs ONE WAY: backends
/// include harness, never the reverse. `harness::ModelBehavior` exists as plain
/// data precisely so the agent loop can ask about a model family without the
/// harness reaching back into this package -- a Core constraint, checked by
/// `harness.layering` in the test suite.
///
/// Landed: `mock.h` (MockProvider, MockEmbeddingProvider) -- no network, no
/// model, so every downstream item is testable offline.
///
/// Still to come, each with its own backlog item: `anthropic` (Messages API +
/// SSE), `openai` and `google`, `llamacpp` (in-process), then the vendor-CLI
/// family after v0.1.0.
namespace apogee::backends {}
