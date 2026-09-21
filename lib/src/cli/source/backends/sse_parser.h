#pragma once

#include <functional>
#include <string>
#include <string_view>

/// A Server-Sent Events frame parser.
///
/// Shared by every streaming cloud backend — Anthropic today, OpenAI and Gemini
/// when they land. It is a separate component precisely because the bug it
/// prevents is the same for all three and is invisible until it is not:
///
/// **An SSE event does not arrive in one read.** The transport hands over
/// arbitrary byte chunks, and a chunk boundary lands mid-frame, mid-line, and
/// mid-UTF-8-sequence whenever the network feels like it. A parser that assumes
/// "one read == one event" works perfectly on a fast local connection and drops
/// tokens on a slow one, which is the worst possible failure distribution: it
/// passes every test written on a developer's machine.
///
/// So this parser is a byte-fed state machine. Feed it whatever arrives; it
/// emits an event only when it has seen the blank line that terminates one.
///
/// Wire format (https://html.spec.whatwg.org/multipage/server-sent-events.html):
///
///     event: content_block_delta
///     data: {"type":"content_block_delta", ...}
///     <blank line>
///
/// `data:` may repeat, in which case the values are joined with newlines.
/// Lines beginning with `:` are comments (used as keep-alive pings).
namespace apogee::backends {

struct SseEvent {
    /// The `event:` field, empty when absent. Anthropic sets it; the payload
    /// also carries a `type`, and the two agree.
    std::string name;
    /// The `data:` field(s), joined with newlines when repeated.
    std::string data;
    /// The `id:` field, empty when absent.
    std::string id;
};

/// Called for each complete event. Return false to stop parsing.
using SseEventSink = std::function<bool(const SseEvent&)>;

class SseParser {
public:
    explicit SseParser(SseEventSink sink);

    /// Feeds bytes. Emits every event the buffer now completes.
    /// Returns false once the sink has asked to stop.
    bool feed(std::string_view bytes);

    /// Flushes a trailing event that was never terminated by a blank line.
    ///
    /// A well-behaved server ends the stream with one, but a connection that
    /// drops mid-stream does not, and the final event is often the one
    /// carrying the stop reason and token usage. Discarding it silently loses
    /// exactly the accounting the caller needs most.
    bool finish();

    /// Whether the sink asked to stop.
    [[nodiscard]] bool stopped() const noexcept {
        return stopped_;
    }

private:
    /// Consumes one complete line (terminator already stripped).
    bool handle_line(std::string_view line);
    bool dispatch();

    SseEventSink sink_;
    /// Bytes not yet forming a complete line. THE field that makes split
    /// chunks work.
    std::string carry_;
    SseEvent current_;
    bool has_field_ = false;
    bool stopped_ = false;
};

}  // namespace apogee::backends
