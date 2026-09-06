#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "backends/cli_event.h"

/// Wire JSON → typed events, for the `claude` CLI's `stream-json` output.
///
/// The mapping table is the one verified against CLI 2.1.233 in this item's
/// design notes. Two rules shape it, and both are about surviving a CLI
/// upgrade rather than about today's schema:
///
/// **Unknown types are dropped, never fatal.** Most observed `type` values are
/// *framing* — `message_start`, `content_block_stop`, and friends carry no
/// content. A `switch` that assumes it has seen every case turns the next CLI
/// release into an outage; an allowlist that logs and drops turns it into a
/// no-op.
///
/// **A malformed line is dropped, never fatal.** The stream is a pipe from
/// another program. A banner, a warning, or a half-written object during a
/// crash must cost one event, not the turn.
namespace apogee::backends::claude_cli {

/// Parses one JSONL line into an event, or nullopt for a line that carries no
/// content (framing, an unknown type, or unparseable bytes).
[[nodiscard]] std::optional<CliEvent> parse_line(std::string_view line);

/// Parses a whole stream, dropping the lines that carry nothing.
///
/// The fixture-replay suite's entry point: feeding the same bytes at different
/// chunk sizes must produce an identical vector.
[[nodiscard]] std::vector<CliEvent> parse_stream(std::string_view bytes);

/// A short label for an event, so a test can assert on a sequence readably and
/// a failure diff names what actually differed.
[[nodiscard]] std::string describe(const CliEvent& event);

/// Renders one IR message as the JSONL a `--input-format stream-json` child
/// accepts on stdin.
///
/// One object per line, and the caller flushes after each: stdin staying open
/// is what lets a single process serve many turns.
[[nodiscard]] std::string user_message_line(std::string_view text);

}  // namespace apogee::backends::claude_cli
