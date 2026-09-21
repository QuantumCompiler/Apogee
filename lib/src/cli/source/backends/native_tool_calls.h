#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "harness/types.h"

/// Native (control-token) tool calls: recognising a call a model emits as its
/// **own tokens** rather than through an injected prose protocol.
///
/// A model trained with tool-call tokens ignores an instruction to emit
/// `TOOL_CALL: {json}` -- the instruction is prose, the tokens are what its
/// training reinforced. Unparsed, such a call is not a no-op: nothing
/// dispatches **and** the raw markup is printed to the user as though it were
/// the answer.
///
/// ## The grammar, verified against real output
///
/// gpt-oss-20b (MXFP4), 2026-09-07, asked to read a file with one tool
/// declared. What reached the caller, verbatim:
///
/// ```
/// <|channel|>commentary to=functions.read_file
/// <|constrain|>json<|message|>{"path":"/tmp/notes.txt"}
/// ```
///
/// So: the opener, a function name under a `functions.` namespace, an optional
/// `<|constrain|>FORMAT` hint, `<|message|>`, then a JSON object. The turn ends
/// there -- `<|call|>` is an end-of-generation token, so generation stops and
/// the marker itself never reaches the text. This is the format Ommi
/// transcribed from a manifest and marked **unverified against generation**
/// because the GGUF it had would not load; it now is verified, and the
/// transcription was right.
///
/// ## One list, two consumers
///
/// `tool_call_openers()` is what the streaming display suppresses on **and**
/// what the parser accepts. A marker missing from the display leaks raw markup
/// to the user; one missing from the parser silently drops the call. Two lists
/// that must agree are two lists that will not, so `ToolCallGate` holds the
/// suppressed bytes and hands *exactly those bytes* to the parser: the display
/// and the parser cannot disagree about what a tool call is, because they are
/// looking at the same buffer.
namespace apogee::backends {

/// Every literal that begins a native tool call.
///
/// Kept in lockstep with what `parse_native_tool_calls` accepts -- deliberately
/// **not** a superset. An opener with no parser behind it would suppress text
/// that never becomes a call, and only the safety net would give it back.
[[nodiscard]] const std::vector<std::string>& tool_call_openers();

/// Parses every native tool call in `text`, in document order.
///
/// A span that opens like a call but does not parse is skipped rather than
/// abandoning the scan, so one malformed call cannot hide a valid one after it.
[[nodiscard]] std::vector<harness::ToolCall> parse_native_tool_calls(std::string_view text);

/// The streaming half: suppresses a tool call from the display, and keeps the
/// bytes it suppressed so the parser sees the same span.
///
/// Passing text through until an opener appears, then retaining everything from
/// that point, is sound because a native call **ends the turn** -- `<|call|>` is
/// end-of-generation. Nothing follows it to be swallowed.
class ToolCallGate {
public:
    ToolCallGate() = default;

    /// `enabled` false makes this a pure pass-through, for a profile that has
    /// been characterized as not emitting native calls.
    explicit ToolCallGate(bool enabled) : enabled_{enabled} {}

    /// Consumes `chunk` and returns the text safe to show now.
    [[nodiscard]] std::string write(std::string_view chunk);

    /// Ends the stream and resolves the held span.
    ///
    /// **The safety net.** If the held bytes parsed as at least one call they
    /// are dropped and `calls()` returns them. If they parsed as nothing, they
    /// are returned as text instead: no answer, no tool, and no error is the
    /// least debuggable outcome there is, and it is exactly how an unrecognised
    /// grammar variant presents.
    [[nodiscard]] std::string flush();

    /// What `flush` parsed. Empty until `flush` has run.
    [[nodiscard]] const std::vector<harness::ToolCall>& calls() const noexcept {
        return calls_;
    }

    /// Whether an opener has been seen and text is being held.
    [[nodiscard]] bool holding() const noexcept {
        return holding_;
    }

private:
    bool enabled_ = true;
    bool holding_ = false;
    std::string pending_;
    std::string held_;
    std::vector<harness::ToolCall> calls_;
};

}  // namespace apogee::backends
