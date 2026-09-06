#pragma once

#include <cstdint>
#include <string>
#include <variant>

/// The typed events a vendor-CLI child produces.
///
/// **This union lives in `backends/`, not `harness/`, and that is deliberate.**
/// The design notes proposed it as a harness type because the harness they were
/// written against had only an untyped token channel. Apogee's does not: it
/// already has `TokenSink`, `ThinkingSink`, and `StatusSink` as separate typed
/// seams, so promoting a CLI-shaped union into `harness/` would add a second
/// vocabulary for the same thing — and would put vendor knowledge in the one
/// layer that must not have it (CLAUDE.md → *The harness never includes
/// backends*).
///
/// What survives from the notes is the rule that matters: **the rest of the
/// harness never sees a `stream_event`.** Wire JSON becomes one of these at the
/// adapter, and the provider turns these into sink calls. Nothing above
/// `backends/` learns that a subprocess was involved.
namespace apogee::backends {

/// Answer text.
struct TextDelta {
    std::string text;
};

/// Reasoning text, kept a distinct type rather than folded into TextDelta.
///
/// Merging them would force in-band markers (`<thinking>…</thinking>`) and a
/// demux filter downstream — which costs a filter that must tolerate a marker
/// split across reads, and leaves a permanent ambiguity the first time a model
/// writes a literal `<thinking>` in its answer.
struct ThinkingDelta {
    std::string text;
};

/// A progress estimate for models whose reasoning text is redacted.
///
/// Load-bearing: a redacted-thinking model emits these with **no**
/// `ThinkingDelta` payload at all. A surface that only watches for text opens
/// an empty thinking view and leaves it there.
struct ThinkingTokens {
    std::int64_t estimated = 0;
};

struct ToolUseStart {
    std::string id;
    std::string name;
};

struct ToolOutcome {
    std::string id;
    bool is_error = false;
    std::string content;
};

/// The terminal event of a turn. Carries everything the turn is accounted by.
struct TurnComplete {
    std::string session_id;
    std::string final_text;
    /// Raw JSON from `--json-schema` runs; empty when no schema was set.
    std::string structured_output;
    double cost_usd = 0.0;
    std::int64_t num_turns = 0;
    std::int64_t input_tokens = 0;
    std::int64_t output_tokens = 0;
    bool is_error = false;
    std::string error_subtype;
};

/// Something worth telling the user that is not part of the answer: session
/// init, an API retry, a rate-limit change.
struct Notice {
    std::string kind;
    std::string detail;
};

using CliEvent = std::variant<TextDelta, ThinkingDelta, ThinkingTokens, ToolUseStart, ToolOutcome,
                              TurnComplete, Notice>;

}  // namespace apogee::backends
