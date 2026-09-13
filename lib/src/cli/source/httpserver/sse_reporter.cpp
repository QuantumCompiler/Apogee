#include "httpserver/sse_reporter.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace apogee::httpserver {
namespace {

harness::StatusEvent simple_event(harness::StatusEvent::Type type,
                                  harness::StatusEvent::Phase phase, std::string name = {}) {
    harness::StatusEvent event;
    event.type = type;
    event.phase = phase;
    event.name = std::move(name);
    return event;
}

}  // namespace

std::string tool_name_from_status(std::string_view detail) {
    constexpr std::string_view tag = "[tool] ";
    if (detail.starts_with(tag)) {
        detail.remove_prefix(tag.size());
    }
    while (!detail.empty() && detail.front() == ' ') {
        detail.remove_prefix(1);
    }
    const std::size_t space = detail.find(' ');
    return std::string{space == std::string_view::npos ? detail : detail.substr(0, space)};
}

SseReporter::SseReporter(SseWriter& writer, StreamIdentity identity, Options options)
    : writer_{&writer}, identity_{std::move(identity)}, options_{options} {}

void SseReporter::emit_meta(const harness::StatusEvent& event) {
    if (!options_.emit_events) {
        return;
    }
    (void)writer_->send(meta_chunk(identity_, event));
}

void SseReporter::model_ready_if_pending() {
    if (!options_.model_loading_pending) {
        return;
    }
    options_.model_loading_pending = false;
    emit_meta(simple_event(harness::StatusEvent::Type::ModelReady,
                           harness::StatusEvent::Phase::Done, identity_.model));
}

void SseReporter::on_thinking() {
    if (in_tool_call_) {
        // The loop reports "back to rest" after a tool completes by calling
        // on_thinking; that is the tool_call/done edge. The loop's own
        // on_thinking at the top of the next iteration arrives separately and
        // becomes thinking/start, so the frame order a client sees is
        // thinking → tool_call/start → tool_call/done → thinking → …
        in_tool_call_ = false;
        emit_meta(simple_event(harness::StatusEvent::Type::ToolCall,
                               harness::StatusEvent::Phase::Done, last_tool_));
        return;
    }
    emit_meta(
        simple_event(harness::StatusEvent::Type::Thinking, harness::StatusEvent::Phase::Start));
}

void SseReporter::on_thinking_token(std::string_view /*chunk*/) {
    // Reasoning is display metadata and never part of a served response. A
    // client that wants the model's working live would get it as its own
    // meta-frame type; this is the seam for that, recorded rather than built,
    // because a status indicator -- not a transcript of the reasoning -- is
    // what the frame vocabulary exists to drive.
}

void SseReporter::on_tool_status(std::string_view detail) {
    model_ready_if_pending();
    last_tool_ = tool_name_from_status(detail);
    tools_used_.push_back(last_tool_);
    in_tool_call_ = true;
    emit_meta(simple_event(harness::StatusEvent::Type::ToolCall, harness::StatusEvent::Phase::Start,
                           last_tool_));
}

void SseReporter::on_clear_status() {
    model_ready_if_pending();
    if (in_tool_call_) {
        in_tool_call_ = false;
        emit_meta(simple_event(harness::StatusEvent::Type::ToolCall,
                               harness::StatusEvent::Phase::Done, last_tool_));
    }
}

void SseReporter::on_answer_start() {
    model_ready_if_pending();
    answer_started_ = std::chrono::steady_clock::now();
}

void SseReporter::on_answer_token(std::string_view chunk) {
    if (chunk.empty()) {
        // An empty chunk is not the end of anything. The reasoning and markup
        // filters legitimately reduce a chunk that was entirely framing to
        // nothing, and a server that read that as end-of-stream would truncate
        // the answer the moment the model started thinking. Skip it; the
        // stream ends when the loop returns, never on an empty piece.
        return;
    }
    emitted_ = true;
    answer_ += chunk;
    (void)writer_->send(
        chat_chunk(identity_, nlohmann::json{{"content", std::string{chunk}}}, std::nullopt));
}

void SseReporter::on_answer_end() {
    // The finishing chunk and `[DONE]` are the handler's: it also knows the
    // session id and the tools that ran, which ride the final frame.
}

std::chrono::steady_clock::duration SseReporter::answer_elapsed() const {
    if (answer_started_ == std::chrono::steady_clock::time_point{}) {
        return std::chrono::steady_clock::duration::zero();
    }
    return std::chrono::steady_clock::now() - answer_started_;
}

}  // namespace apogee::httpserver
