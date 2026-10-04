#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "backends/cli_event.h"

/// Wire JSON → typed events, for `codex exec --json`.
///
/// Mapped from the table verified against `codex-cli` 0.153.4. The shape is
/// pleasantly simple compared with Claude's, because this CLI emits **whole
/// items rather than deltas**:
///
/// ```
/// {"type":"thread.started","thread_id":"…"}
/// {"type":"turn.started"}
/// {"type":"item.completed","item":{"id":"item_0","type":"agent_message","text":"…"}}
/// {"type":"turn.completed","usage":{…}}
/// ```
///
/// **There are no delta events at all.** A twelve-line answer arrives in one
/// `item.completed`. That is a real limitation of this backend and it is
/// recorded rather than disguised — the family template is a shape to aim at,
/// not a promise to fake.
///
/// The two survival rules from the family hold here as everywhere: an unknown
/// `type` is dropped rather than fatal, so a CLI upgrade is not an outage; and
/// a malformed line costs one event, not the turn.
namespace apogee::backends::codex_cli {

/// Parses one JSONL line, or nullopt for framing, an unknown type, or bytes
/// that do not parse.
[[nodiscard]] std::optional<CliEvent> parse_line(std::string_view line);

/// Parses a whole stream, dropping lines that carry nothing.
[[nodiscard]] std::vector<CliEvent> parse_stream(std::string_view bytes);

/// A short label for an event, so a test can assert on a sequence readably.
[[nodiscard]] std::string describe(const CliEvent& event);

}  // namespace apogee::backends::codex_cli
