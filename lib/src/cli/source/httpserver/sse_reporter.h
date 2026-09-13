#pragma once

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

#include "agentloop/reporter.h"
#include "harness/types.h"
#include "httpserver/sse_writer.h"

/// The `agentloop::Reporter` → SSE adapter: the third surface over one seam.
///
/// `CliReporter` turns loop events into escape codes, `JsonReporter` into one
/// JSON object per line, and this into OpenAI streaming frames. Answer tokens
/// become `chat.completion.chunk` content deltas; progress becomes
/// **meta-frames** -- the same chunk shape with an empty `delta.content` and a
/// `meta` object -- and only when the client opted in with `apogee_events`.
/// A stock OpenAI client never sees a frame it cannot render.
///
/// The meta vocabulary is `harness::StatusEvent`'s, the same one the terminal
/// status line renders: `thinking`, `tool_call`, `model_loading`/`model_ready`,
/// `rag_search`/`rag_result`, `token_count`, `context_warning`. Nothing is
/// invented here; a surface that invented its own progress taxonomy would be
/// the second vocabulary the Reporter seam exists to prevent.
namespace apogee::httpserver {

class SseReporter final : public agentloop::Reporter {
public:
    struct Options {
        /// Whether meta-frames are emitted at all -- the request's
        /// `apogee_events`. Content chunks are emitted regardless.
        bool emit_events = false;

        /// Set when the backend reported its model as not yet loaded before
        /// the turn began. The first sign the model has answered -- a tool
        /// call, the status clearing, the answer starting -- then emits
        /// `model_ready`, once.
        bool model_loading_pending = false;
    };

    SseReporter(SseWriter& writer, StreamIdentity identity, Options options);
    ~SseReporter() override = default;

    SseReporter(const SseReporter&) = delete;
    SseReporter& operator=(const SseReporter&) = delete;
    SseReporter(SseReporter&&) = delete;
    SseReporter& operator=(SseReporter&&) = delete;

    void on_thinking() override;
    void on_thinking_token(std::string_view chunk) override;
    void on_tool_status(std::string_view detail) override;
    void on_clear_status() override;
    void on_answer_start() override;
    void on_answer_token(std::string_view chunk) override;
    void on_answer_end() override;

    /// Writes `event` as a meta-frame -- when events are on. The handler
    /// emits its own frames (retrieval, context) through here so the opt-in
    /// is honoured in exactly one place.
    void emit_meta(const harness::StatusEvent& event);

    /// Tool names in the order they ran.
    [[nodiscard]] const std::vector<std::string>& tools_used() const noexcept {
        return tools_used_;
    }

    /// Whether any answer text was streamed.
    [[nodiscard]] bool emitted_answer() const noexcept {
        return emitted_;
    }

    /// Every answer delta, concatenated -- what the client received.
    [[nodiscard]] const std::string& answer() const noexcept {
        return answer_;
    }

    /// Time since the answer started, or zero when it has not.
    [[nodiscard]] std::chrono::steady_clock::duration answer_elapsed() const;

private:
    void model_ready_if_pending();

    SseWriter* writer_;
    StreamIdentity identity_;
    Options options_;

    bool in_tool_call_ = false;
    std::string last_tool_;
    std::vector<std::string> tools_used_;

    bool emitted_ = false;
    std::string answer_;
    std::chrono::steady_clock::time_point answer_started_;
};

/// The tool name inside a status line: `[tool] fetch_url https://…` →
/// `fetch_url`. The tag is the loop's, the first word is the name, and the rest
/// is the argument summary the terminal shows. Exposed so the rule is testable.
[[nodiscard]] std::string tool_name_from_status(std::string_view detail);

}  // namespace apogee::httpserver
