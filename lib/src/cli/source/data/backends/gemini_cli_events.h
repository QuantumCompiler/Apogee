#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "backends/cli_event.h"

/// Wire JSON → typed events, for `gemini -o stream-json`.
///
/// Mapped from the table verified against `gemini` 0.46.0 under a **Google
/// login**. This is the third vendor CLI characterized, and the spread across
/// the family is now wide enough to be the point: Claude streams tokens with
/// inline schemas, codex emits whole messages with a schema *file*, ollama
/// offers no event stream at all, and this one streams real deltas but routes
/// a single turn across several models internally.
///
/// ```jsonl
/// {"type":"init","timestamp":"…","session_id":"…","model":"auto"}
/// {"type":"message","timestamp":"…","role":"user","content":"…"}
/// {"type":"message","timestamp":"…","role":"assistant","content":"…","delta":true}
/// {"type":"tool_use","timestamp":"…","tool_name":"…","tool_id":"…","parameters":{…}}
/// {"type":"tool_result","timestamp":"…","tool_id":"…","status":"success","output":"…"}
/// {"type":"result","timestamp":"…","status":"success","stats":{…}}
/// ```
///
/// ## Three findings that shape this mapping
///
/// **Deltas are real, and they split anywhere.** A twenty-line answer arrived
/// in three assistant messages, and one boundary fell *inside* a two-digit
/// number (`…12\n1` then `3\n14…`). So deltas are concatenated verbatim with no
/// assumption about token or line boundaries — see the `streamed_deltas`
/// fixture, which exists to keep that honest.
///
/// **The user turn is echoed back.** `message` with `role":"user"` repeats the
/// prompt Apogee just sent. It is dropped: appending it would put the question
/// at the top of its own answer.
///
/// **One turn uses several models.** `init` reports `"model":"auto"` and
/// `result.stats.models` names each model that actually ran — two in every
/// recording. The adapter reports the model with the **largest output-token
/// count**, because that is the one that produced the answer the user is
/// reading; the raw set is not surfaced, since `ChatResponse::model` is a
/// single field and inventing a second vocabulary for it would leak this CLI's
/// routing into the harness.
///
/// ## How it fails (57b, read from 0.46.0's own source)
///
/// ```jsonl
/// {"type":"result","status":"error","error":{"type":"QuotaError","message":"…"},"stats":{…}}
/// {"type":"error","severity":"warning","message":"Loop detected, stopping execution"}
/// {"type":"error","severity":"error","message":"Maximum session turns exceeded"}
/// ```
///
/// **A fatal error is a result.** Every fatal error under stream-json ends in
/// a `result` whose `status` is `error` and whose `error` object says why;
/// the failure's detail is that message, its `type` only when there is no
/// message (and in `describe` either way), so a quota, a login or a turn limit
/// reads in the CLI's own words, never `reported an error: error`. A result
/// with neither leaves the detail empty, for the exit status and stderr.
///
/// **An `error` event may be a warning.** `severity: "warning"` (a loop
/// stopped, an agent's execution blocked) comes before a result that
/// succeeded: it is a `Notice` of kind `warning`, said to the person, and the
/// turn stands on its result. `severity: "error"`, or none (the older shape),
/// fails the turn.
///
/// The two family survival rules hold as everywhere: an unknown `type` is
/// dropped rather than fatal, so a CLI upgrade is not an outage; and a
/// malformed line costs one event, not the turn.
namespace apogee::backends::gemini_cli {

/// Parses one JSONL line, or nullopt for framing, an echoed user turn, an
/// unknown type, or bytes that do not parse.
[[nodiscard]] std::optional<CliEvent> parse_line(std::string_view line);

/// Parses a whole stream, dropping lines that carry nothing.
[[nodiscard]] std::vector<CliEvent> parse_stream(std::string_view bytes);

/// A short label for an event, so a test can assert on a sequence readably.
[[nodiscard]] std::string describe(const CliEvent& event);

}  // namespace apogee::backends::gemini_cli
