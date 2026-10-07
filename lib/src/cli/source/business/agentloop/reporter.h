#pragma once

#include <string_view>

#include "agentloop/side_call.h"

/// Everything the loop wants to say, said through here.
///
/// **This interface is the reason the loop can be shared.** The single most
/// load-bearing sequencing rule is that the loop must be extracted behind an
/// observer BEFORE surfaces multiply, not after: it is what keeps chat,
/// complete, analyze, and serve consistent, and what makes deleting an entire
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

    /// The reasoning reached the turn's thinking budget and was ended there
    /// (26i). Fires while the reasoning is still being shown, so the line
    /// that summarises it can say so; at most once a model call.
    virtual void on_thinking_budget_reached() {}

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

    /// A line from the provider the user should read, and keep: neither
    /// progress nor an error (`harness::StatusEvent::Type::Notice`). A local
    /// model answering without the tools it was given says so here, rather
    /// than quietly.
    virtual void on_notice(std::string_view) {}

    /// A progress note from the provider that is not worth keeping in the
    /// ordinary run -- what a local model's cache reused of the prompt, say.
    /// The terminal prints it under `--verbose`; everything else drops it.
    virtual void on_progress(std::string_view) {}

    /// What a turn was handed from earlier conversations (26l): how many
    /// past chats, and how many recorded decisions. Said before the turn
    /// runs, so a user can see why the model knows something; never on
    /// `serve`, which never recalls.
    /// A model call besides the chat model's own (26n) -- when it starts,
    /// and again when it is over, with what it took when known. The terminal
    /// draws it inside the thinking block; machine mode says it as a
    /// `tool_status`; everything else ignores it. Never persisted.
    virtual void on_side_call(const SideCall& call) {
        (void)call;
    }

    virtual void on_recall(int chats, int decisions) {
        (void)chats;
        (void)decisions;
    }

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
