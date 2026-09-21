#pragma once

#include <string_view>

/// Everything the loop wants to say, said through here.
///
/// **This interface is the reason the loop can be shared.** Ommi's single most
/// load-bearing sequencing lesson is that the loop must be extracted behind an
/// observer BEFORE surfaces multiply, not after: it is what kept chat, complete,
/// analyze, and serve consistent there, and what made deleting an entire
/// front-end a local change rather than a rewrite.
///
/// So the loop knows nothing about terminals, HTTP, or GUIs. A surface is a thin
/// adapter: the CLI writes to stdout and a status line, `serve` will turn these
/// into SSE frames, the GUI will turn them into JSONL events. A capability that
/// reaches only one surface is a bug, and that stays true structurally because
/// there is only one loop and it can only speak through this.
namespace apogee::agentloop {

class Reporter {
public:
    Reporter() = default;
    virtual ~Reporter() = default;
    Reporter(const Reporter&) = delete;
    Reporter& operator=(const Reporter&) = delete;
    Reporter(Reporter&&) = delete;
    Reporter& operator=(Reporter&&) = delete;

    /// The model is working. Called before every model call, and again after
    /// each tool call completes -- the resting state between tools.
    virtual void on_thinking() {}

    /// One chunk of the model's live reasoning.
    ///
    /// Fires between on_thinking and the answer, and can fire on runs whose
    /// answer is not streamed: thinking is progress display, not answer
    /// content. **It never appears in on_answer_token, the returned text, or
    /// history.** A surface that does not render reasoning discards it.
    virtual void on_thinking_token(std::string_view) {}

    /// Tool-call progress, as a short human-readable line. Never called with an
    /// empty string -- the return to rest is signalled by the next
    /// on_thinking.
    virtual void on_tool_status(std::string_view) {}

    /// Erase any transient status indicator. Called once immediately before the
    /// final answer, and on error paths.
    virtual void on_clear_status() {}

    /// Fires exactly once, immediately before the first token of the final
    /// answer. Not called when the turn produced no visible answer text.
    virtual void on_answer_start() {}

    /// One chunk of the final answer. Already reasoning-filtered.
    virtual void on_answer_token(std::string_view) {}

    /// Fires exactly once after the final answer, and only when
    /// on_answer_start fired.
    virtual void on_answer_end() {}
};

/// A Reporter that discards everything.
///
/// The default, and not merely a convenience: a caller that only wants the
/// returned text -- a background clerk, a quiet one-shot -- should not have to
/// implement seven no-op methods to say so.
class NullReporter final : public Reporter {};

}  // namespace apogee::agentloop
