#pragma once

/// Backends -- one LLMProvider implementation per model source.
///
/// Reserved by the project skeleton. Filled in order by the `anthropic-backend`
/// item (direct Messages API + SSE streaming), then `openai-google-backends`,
/// then `llamacpp-backend` (in-process local inference), and later the
/// subscription-plan vendor-CLI backends.
///
/// Every backend here translates only between its vendor's wire format and the
/// harness IR. Behavior that is the same across vendors -- the agent loop,
/// context monitoring, session persistence -- belongs above this package, not
/// duplicated inside each one.
namespace apogee::backends {}
