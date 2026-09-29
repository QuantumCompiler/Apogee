#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "harness/cancellation.h"
#include "harness/harness.h"
#include "harness/types.h"

/// A large tool result, summarised by the utility model before it reaches the
/// chat model (26b).
///
/// A local chat model reads its prompt at a few hundred tokens a second, and
/// a 40 KB file or command output costs it a minute before it writes a word --
/// most of it text the task does not need. When a utility model is set, a
/// result over the threshold is handed to it first, and the chat model reads
/// its summary, headed by what was dropped and how to get it back. With no
/// utility model set, nothing changes: the chat model is never asked to
/// summarise for itself, which would cost it the very reading this saves.
namespace apogee::agentloop {

/// Results larger than this, in bytes, are summarised (26b, default taken).
inline constexpr std::size_t kToolSummaryThreshold = 8192;

/// The request asking `backend` to summarise `content`, what `call` returned.
/// A side request. Exposed so its shape is testable.
[[nodiscard]] harness::ChatRequest summary_request(const std::string& backend,
                                                   const harness::ToolCall& call,
                                                   const std::string& content);

/// The text the chat model reads in place of `content`: the summary, headed
/// by how much was returned, who summarised it, and how to see the rest.
/// Nullopt when the summary could not be had -- the result then goes in as
/// it is, never lost.
[[nodiscard]] std::optional<std::string> summarize_tool_result(
    const harness::Harness& harness, const std::string& backend, const harness::ToolCall& call,
    const std::string& content, const harness::CancellationToken& cancellation);

}  // namespace apogee::agentloop
